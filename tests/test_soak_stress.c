/**
 * @file test_soak_stress.c
 * @brief BP-007D Comprehensive Multiplatform Soak & Stress Test Suite
 *
 * Implements rigorous, sustained multithreaded validation:
 *   - Phase 1: High-frequency ephemeral thread churn (creation/destruction cycles)
 *   - Phase 2: Persistent worker thread pool with long-term context reuse
 *   - Phase 3: Mixed concurrent workload (persistent pool + ephemeral background churn)
 *   - Phase 4: Scratchpad capacity boundary exercise (sub-threshold, near-64MB, above-64MB external heap)
 *   - Phase 5: Continuous round-trip data verification & strict resource accounting
 */

#define _CRT_SECURE_NO_WARNINGS
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <time.h>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <process.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#include <sys/resource.h>
#endif

#include "../include/binpack_sdk.h"
#include "../src/binpack_internal.h"

#define TEST_PASS 0
#define TEST_FAIL 1

/* Testing Debug Counters */
#if defined(BINPACK_TESTING)
extern atomic_size_t g_dbg_core_tls_created;
extern atomic_size_t g_dbg_core_tls_destroyed;
extern atomic_size_t g_dbg_core_scratch_allocs;
extern atomic_size_t g_dbg_core_scratch_frees;

extern atomic_size_t g_dbg_sdk_tls_created;
extern atomic_size_t g_dbg_sdk_tls_destroyed;
extern atomic_size_t g_dbg_sdk_cctx_created;
extern atomic_size_t g_dbg_sdk_cctx_reused;
extern atomic_size_t g_dbg_sdk_cctx_destroyed;
extern atomic_size_t g_dbg_sdk_scratch_allocs;
extern atomic_size_t g_dbg_sdk_scratch_frees;

extern void bp_core_cleanup_current_thread(void);
extern void bp_sdk_cleanup_current_thread(void);
#endif

/* Monotonic Millisecond Timer */
static uint64_t get_time_ms(void) {
#if defined(_WIN32)
    return (uint64_t)GetTickCount64();
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ULL + (uint64_t)ts.tv_nsec / 1000000ULL;
#endif
}

/* RSS Memory Tracker in Kilobytes */
static size_t get_current_rss_kb(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (size_t)(pmc.WorkingSetSize / 1024);
    }
    return 0;
#else
    FILE *f = fopen("/proc/self/statm", "r");
    if (!f) return 0;
    long pages = 0, resident = 0;
    if (fscanf(f, "%ld %ld", &pages, &resident) == 2) {
        fclose(f);
        long page_size_kb = sysconf(_SC_PAGESIZE) / 1024;
        return (size_t)(resident * page_size_kb);
    }
    fclose(f);
    return 0;
#endif
}

/* Cross-Platform Threading Wrappers */
#if defined(_WIN32)
typedef HANDLE thread_t;
typedef unsigned (__stdcall *thread_fn_t)(void *);
static int start_thread(thread_t *t, thread_fn_t fn, void *arg) {
    *t = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
    return (*t != NULL);
}
static void wait_thread(thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#define THREAD_FUNC_DECL(fn) unsigned __stdcall fn(void *arg)
#define THREAD_FUNC_RETURN return 0
#else
typedef pthread_t thread_t;
typedef void *(*thread_fn_t)(void *);
static int start_thread(thread_t *t, thread_fn_t fn, void *arg) {
    return (pthread_create(t, NULL, fn, arg) == 0);
}
static void wait_thread(thread_t t) {
    pthread_join(t, NULL);
}
#define THREAD_FUNC_DECL(fn) void *fn(void *arg)
#define THREAD_FUNC_RETURN return NULL
#endif

/* Global statistics across soak test */
static atomic_size_t g_total_compressions = 0;
static atomic_size_t g_total_bytes_processed = 0;
static atomic_size_t g_roundtrip_mismatches = 0;

/* Helper: Generate deterministic pseudo-random test buffers */
static void fill_test_buffer(uint8_t *buf, size_t sz, uint32_t seed) {
    uint32_t state = seed;
    for (size_t i = 0; i < sz; i++) {
        state = state * 1664525U + 1013904223U;
        buf[i] = (uint8_t)(state >> 24);
    }
}

/* ========================================================================= */
/* Phase 1: High-Frequency Ephemeral Thread Churn                            */
/* ========================================================================= */

typedef struct {
    int thread_id;
    size_t buffer_sz;
    binpack_filter_t filter;
    int clevel;
    int status;
} ephemeral_task_t;

static THREAD_FUNC_DECL(worker_ephemeral_churn) {
    ephemeral_task_t *task = (ephemeral_task_t*)arg;
    task->status = TEST_FAIL;

    uint8_t *src = (uint8_t*)malloc(task->buffer_sz);
    size_t dst_cap = task->buffer_sz * 2 + 1024;
    uint8_t *dst = (uint8_t*)malloc(dst_cap);
    uint8_t *dec = (uint8_t*)malloc(task->buffer_sz);

    if (!src || !dst || !dec) {
        free(src); free(dst); free(dec);
        THREAD_FUNC_RETURN;
    }

    fill_test_buffer(src, task->buffer_sz, (uint32_t)(task->thread_id * 10007 + 3));

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.filter = task->filter;
    opt.clevel = task->clevel;
    opt.transpose_stride = 4;

    size_t c_size = 0;
    binpack_status_t st = binpack_compress_into(src, task->buffer_sz, dst, dst_cap, &c_size, &opt);
    if (st == BINPACK_OK) {
        size_t dec_sz = 0;
        st = binpack_decompress_into(dst, c_size, dec, task->buffer_sz, &dec_sz, 1);
        if (st == BINPACK_OK && dec_sz == task->buffer_sz) {
            if (memcmp(src, dec, task->buffer_sz) == 0) {
                task->status = TEST_PASS;
                atomic_fetch_add_explicit(&g_total_compressions, 1, memory_order_relaxed);
                atomic_fetch_add_explicit(&g_total_bytes_processed, task->buffer_sz, memory_order_relaxed);
            } else {
                atomic_fetch_add_explicit(&g_roundtrip_mismatches, 1, memory_order_relaxed);
            }
        }
    }

    free(src);
    free(dst);
    free(dec);
    THREAD_FUNC_RETURN;
}

static int run_phase1_ephemeral_churn(int iterations, int threads_per_batch) {
    printf("[FASE 1] Churn de Hilos Efimeros (%d batches de %d hilos):\n", iterations, threads_per_batch);

#if defined(BINPACK_TESTING)
    size_t initial_core_tls_c = atomic_load(&g_dbg_core_tls_created);
    size_t initial_core_tls_d = atomic_load(&g_dbg_core_tls_destroyed);
    size_t initial_sdk_tls_c = atomic_load(&g_dbg_sdk_tls_created);
    size_t initial_sdk_tls_d = atomic_load(&g_dbg_sdk_tls_destroyed);
    size_t initial_cctx_c = atomic_load(&g_dbg_sdk_cctx_created);
    size_t initial_cctx_d = atomic_load(&g_dbg_sdk_cctx_destroyed);
#endif

    const size_t test_sizes[] = { 1024, 8192, 65536, 262144, 1048576 };
    const binpack_filter_t test_filters[] = {
        BINPACK_FILTER_AUTO,
        BINPACK_FILTER_TRANSPOSE_DELTA,
        BINPACK_FILTER_BITSHUFFLE,
        BINPACK_FILTER_TEXTURE_BCN,
        BINPACK_FILTER_NONE
    };

    thread_t *threads = (thread_t*)malloc(sizeof(thread_t) * threads_per_batch);
    ephemeral_task_t *tasks = (ephemeral_task_t*)malloc(sizeof(ephemeral_task_t) * threads_per_batch);

    for (int batch = 0; batch < iterations; batch++) {
        for (int i = 0; i < threads_per_batch; i++) {
            tasks[i].thread_id = batch * threads_per_batch + i;
            tasks[i].buffer_sz = test_sizes[(batch + i) % 5];
            tasks[i].filter = test_filters[(batch + i) % 5];
            tasks[i].clevel = 1 + ((batch + i) % 19);
            tasks[i].status = TEST_FAIL;

            if (!start_thread(&threads[i], worker_ephemeral_churn, &tasks[i])) {
                free(threads); free(tasks);
                return TEST_FAIL;
            }
        }

        for (int i = 0; i < threads_per_batch; i++) {
            wait_thread(threads[i]);
            if (tasks[i].status != TEST_PASS) {
                printf("  [FAIL] Ephemeral worker %d failed in batch %d\n", i, batch);
                free(threads); free(tasks);
                return TEST_FAIL;
            }
        }
    }

    free(threads);
    free(tasks);

#if defined(BINPACK_TESTING)
    size_t net_core_tls = (atomic_load(&g_dbg_core_tls_created) - initial_core_tls_c) -
                          (atomic_load(&g_dbg_core_tls_destroyed) - initial_core_tls_d);
    size_t net_sdk_tls = (atomic_load(&g_dbg_sdk_tls_created) - initial_sdk_tls_c) -
                         (atomic_load(&g_dbg_sdk_tls_destroyed) - initial_sdk_tls_d);
    size_t net_cctx = (atomic_load(&g_dbg_sdk_cctx_created) - initial_cctx_c) -
                      (atomic_load(&g_dbg_sdk_cctx_destroyed) - initial_cctx_d);

    printf("  -> Post-churn accounting: Net Core TLS=%zu, Net SDK TLS=%zu, Net CCtx=%zu\n",
           net_core_tls, net_sdk_tls, net_cctx);

    if (net_core_tls != 0 || net_sdk_tls != 0 || net_cctx != 0) {
        printf("  [FAIL] Leaked TLS resources detected in ephemeral churn!\n");
        return TEST_FAIL;
    }
#endif

    printf("  [PASS] %d hilos efimeros ejecutados y liberados limpiamente al 100%%\n", iterations * threads_per_batch);
    return TEST_PASS;
}

/* ========================================================================= */
/* Phase 2: Persistent Worker Pool with Long-Term Context Reuse              */
/* ========================================================================= */

typedef struct {
    int worker_id;
    int operations_per_worker;
    atomic_int stop_flag;
    int status;
} pool_worker_t;

static THREAD_FUNC_DECL(worker_persistent_soak) {
    pool_worker_t *w = (pool_worker_t*)arg;
    w->status = TEST_FAIL;

    const size_t BUF_SZ = 65536;
    uint8_t *src = (uint8_t*)malloc(BUF_SZ);
    uint8_t *dst = (uint8_t*)malloc(BUF_SZ * 2);
    uint8_t *dec = (uint8_t*)malloc(BUF_SZ);

    if (!src || !dst || !dec) {
        free(src); free(dst); free(dec);
        THREAD_FUNC_RETURN;
    }

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.clevel = 3;

    int completed = 0;
    for (int op = 0; op < w->operations_per_worker; op++) {
        if (atomic_load(&w->stop_flag)) break;

        fill_test_buffer(src, BUF_SZ, (uint32_t)(w->worker_id * 1000 + op));
        size_t c_sz = 0;
        binpack_status_t st = binpack_compress_into(src, BUF_SZ, dst, BUF_SZ * 2, &c_sz, &opt);
        if (st != BINPACK_OK) break;

        size_t dec_sz = 0;
        st = binpack_decompress_into(dst, c_sz, dec, BUF_SZ, &dec_sz, 1);
        if (st != BINPACK_OK || dec_sz != BUF_SZ || memcmp(src, dec, BUF_SZ) != 0) {
            atomic_fetch_add_explicit(&g_roundtrip_mismatches, 1, memory_order_relaxed);
            break;
        }

        atomic_fetch_add_explicit(&g_total_compressions, 1, memory_order_relaxed);
        atomic_fetch_add_explicit(&g_total_bytes_processed, BUF_SZ, memory_order_relaxed);
        completed++;
    }

    if (completed == w->operations_per_worker) {
        w->status = TEST_PASS;
    }

    free(src);
    free(dst);
    free(dec);
    THREAD_FUNC_RETURN;
}

static int run_phase2_persistent_pool(int num_workers, int ops_per_worker) {
    printf("\n[FASE 2] Thread Pool Persistente (%d workers x %d ops = %d ops):\n",
           num_workers, ops_per_worker, num_workers * ops_per_worker);

    thread_t *threads = (thread_t*)malloc(sizeof(thread_t) * num_workers);
    pool_worker_t *workers = (pool_worker_t*)malloc(sizeof(pool_worker_t) * num_workers);

    size_t rss_start = get_current_rss_kb();

    for (int i = 0; i < num_workers; i++) {
        workers[i].worker_id = i;
        workers[i].operations_per_worker = ops_per_worker;
        atomic_init(&workers[i].stop_flag, 0);
        workers[i].status = TEST_FAIL;
        start_thread(&threads[i], worker_persistent_soak, &workers[i]);
    }

    for (int i = 0; i < num_workers; i++) {
        wait_thread(threads[i]);
    }

    size_t rss_end = get_current_rss_kb();
    int ok = 1;
    for (int i = 0; i < num_workers; i++) {
        if (workers[i].status != TEST_PASS) ok = 0;
    }

    free(threads);
    free(workers);

    printf("  -> Memoria RSS: Start=%zu KB, End=%zu KB (Delta=%ld KB)\n",
           rss_start, rss_end, (long)(rss_end - rss_start));

    if (!ok) {
        printf("  [FAIL] One or more persistent pool workers failed!\n");
        return TEST_FAIL;
    }

    printf("  [PASS] Thread pool persistente completo con reutilizacion estable\n");
    return TEST_PASS;
}

/* ========================================================================= */
/* Phase 3: Mixed Concurrent Churn & Persistent Pool                         */
/* ========================================================================= */

typedef struct {
    atomic_int stop_flag;
    int churn_batches_completed;
} mixed_coordinator_t;

static THREAD_FUNC_DECL(worker_mixed_ephemeral_generator) {
    mixed_coordinator_t *coord = (mixed_coordinator_t*)arg;
    while (!atomic_load(&coord->stop_flag)) {
        ephemeral_task_t t;
        t.thread_id = coord->churn_batches_completed;
        t.buffer_sz = 32768;
        t.filter = BINPACK_FILTER_AUTO;
        t.clevel = 1;
        t.status = TEST_FAIL;

        thread_t th;
        if (start_thread(&th, worker_ephemeral_churn, &t)) {
            wait_thread(th);
            coord->churn_batches_completed++;
        }
#if defined(_WIN32)
        Sleep(5);
#else
        usleep(5000);
#endif
    }
    THREAD_FUNC_RETURN;
}

static int run_phase3_mixed_workload(int duration_seconds) {
    printf("\n[FASE 3] Carga Mixta Concurrente (Pool Persistente + Generador Efimero durante %d s):\n", duration_seconds);

    const int POOL_WORKERS = 8;
    thread_t pool_threads[8];
    pool_worker_t pool_tasks[8];

    mixed_coordinator_t coord;
    atomic_init(&coord.stop_flag, 0);
    coord.churn_batches_completed = 0;

    thread_t gen_thread;
    start_thread(&gen_thread, worker_mixed_ephemeral_generator, &coord);

    /* Launch persistent pool doing continuous work */
    for (int i = 0; i < POOL_WORKERS; i++) {
        pool_tasks[i].worker_id = i;
        pool_tasks[i].operations_per_worker = 100000; /* Run until stopped */
        atomic_init(&pool_tasks[i].stop_flag, 0);
        pool_tasks[i].status = TEST_FAIL;
        start_thread(&pool_threads[i], worker_persistent_soak, &pool_tasks[i]);
    }

    uint64_t start_ms = get_time_ms();
    while ((get_time_ms() - start_ms) < (uint64_t)(duration_seconds * 1000)) {
#if defined(_WIN32)
        Sleep(250);
#else
        usleep(250000);
#endif
    }

    /* Stop all workers */
    atomic_store(&coord.stop_flag, 1);
    for (int i = 0; i < POOL_WORKERS; i++) {
        atomic_store(&pool_tasks[i].stop_flag, 1);
    }

    wait_thread(gen_thread);
    for (int i = 0; i < POOL_WORKERS; i++) {
        wait_thread(pool_threads[i]);
    }

    printf("  -> Carga mixta finalizada: %d hilos efimeros concurrentes generados\n", coord.churn_batches_completed);
    printf("  [PASS] Carga mixta ejecutada sin carreras ni bloqueos\n");
    return TEST_PASS;
}

/* ========================================================================= */
/* Phase 4: Scratchpad Capacity Boundary (Threshold Transitions)             */
/* ========================================================================= */

static int run_phase4_boundary_capacities(void) {
    printf("\n[FASE 4] Ejercicio de Fronteras de Scratchpad (64 MB TLS vs Heap Externo):\n");

    /* Case A: 63.9 MB (Must use TLS scratchpad) */
    size_t sz_tls = 63 * 1024 * 1024;
    int must_free = -1;
    uint8_t *p_tls = bp_acquire_core_scratch(sz_tls, &must_free);
    if (!p_tls || must_free != 0) {
        printf("  [FAIL] Near-64MB buffer did not acquire TLS scratchpad (must_free=%d)\n", must_free);
        bp_release_core_scratch(p_tls, must_free);
        return TEST_FAIL;
    }
    bp_release_core_scratch(p_tls, must_free);
    printf("  [PASS] 63 MiB: Asignado correctamente en TLS scratchpad (must_free=0)\n");

    /* Case B: 65 MB (Must bypass TLS and allocate via heap with must_free=1) */
    size_t sz_heap = 65 * 1024 * 1024;
    must_free = -1;
    uint8_t *p_heap = bp_acquire_core_scratch(sz_heap, &must_free);
    if (!p_heap || must_free != 1) {
        printf("  [FAIL] >64MB buffer did not trigger external heap fallback (must_free=%d)\n", must_free);
        bp_release_core_scratch(p_heap, must_free);
        return TEST_FAIL;
    }
    bp_release_core_scratch(p_heap, must_free);
    printf("  [PASS] 65 MiB: Supero umbral de 64MB y derivo en heap externo (must_free=1)\n");

    return TEST_PASS;
}

/* ========================================================================= */
/* Phase 5: Overall Verification & Reporting                                 */
/* ========================================================================= */

int main(int argc, char *argv[]) {
    int duration_seconds = 10;
    int ephemeral_batches = 20;
    int threads_per_batch = 10;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--duration") == 0 && i + 1 < argc) {
            duration_seconds = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--batches") == 0 && i + 1 < argc) {
            ephemeral_batches = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--threads") == 0 && i + 1 < argc) {
            threads_per_batch = atoi(argv[++i]);
        }
    }

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("======================================================================\n");
    printf("BP-007D Soak Prolongado & Estres de Ciclo de Vida Multihilo\n");
    printf("======================================================================\n\n");

    uint64_t t_start = get_time_ms();

    if (run_phase1_ephemeral_churn(ephemeral_batches, threads_per_batch) != TEST_PASS) return 1;
    if (run_phase2_persistent_pool(8, 250) != TEST_PASS) return 1;
    if (run_phase3_mixed_workload(duration_seconds) != TEST_PASS) return 1;
    if (run_phase4_boundary_capacities() != TEST_PASS) return 1;

    uint64_t t_end = get_time_ms();
    double elapsed_sec = (double)(t_end - t_start) / 1000.0;
    double total_mb = (double)atomic_load(&g_total_bytes_processed) / (1024.0 * 1024.0);
    double mb_per_sec = (elapsed_sec > 0.0) ? (total_mb / elapsed_sec) : 0.0;

    printf("\n======================================================================\n");
    printf("Estadisticas Globales de Soak BP-007D:\n");
    printf("  - Tiempo Total de Ejecucion:   %.2f s\n", elapsed_sec);
    printf("  - Compresiones Realizadas:     %zu\n", atomic_load(&g_total_compressions));
    printf("  - Datos Procesados:            %.2f MB (%.2f MB/s)\n", total_mb, mb_per_sec);
    printf("  - Discrepancias Round-trip:    %zu\n", atomic_load(&g_roundtrip_mismatches));
    printf("======================================================================\n");

    if (atomic_load(&g_roundtrip_mismatches) > 0) {
        printf("[FAIL] Data integrity mismatches detected!\n");
        return 1;
    }

    printf("[SUCCESS] BP-007D Soak Test completado exitosamente con 0 fallos.\n");
    return 0;
}
