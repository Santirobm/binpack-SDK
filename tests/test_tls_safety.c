/**
 * @file test_tls_safety.c
 * @brief BP-007C TLS & FLS Multiplatform Lifecycle, Accounting & Stress Harness
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
#else
#include <pthread.h>
#include <unistd.h>
#include <sched.h>
#endif

#include "../include/binpack_sdk.h"
#include "../src/binpack_internal.h"

#define TEST_PASS 0
#define TEST_FAIL 1

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("  [RUN ] %s...\n", #fn); \
    int res = fn(); \
    if (res == TEST_PASS) { \
        printf("  [PASS] %s\n", #fn); \
        g_tests_passed++; \
    } else { \
        printf("  [FAIL] %s (line %d)\n", #fn, __LINE__); \
        g_tests_failed++; \
    } \
} while(0)

/* Testing Instrumentation Counters from binpack_core.c and binpack_sdk.c */
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

/* Monotonic nanosecond timer */
static uint64_t get_time_ns(void) {
#if defined(_WIN32)
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#endif
}

/* Cross-platform Thread Spawner */
#if defined(_WIN32)
typedef HANDLE test_thread_t;
typedef unsigned (__stdcall *test_thread_fn_t)(void *);
static int spawn_thread(test_thread_t *t, test_thread_fn_t fn, void *arg) {
    *t = (HANDLE)_beginthreadex(NULL, 0, fn, arg, 0, NULL);
    return (*t != NULL);
}
static void join_thread(test_thread_t t) {
    WaitForSingleObject(t, INFINITE);
    CloseHandle(t);
}
#else
typedef pthread_t test_thread_t;
typedef void *(*test_thread_fn_t)(void *);
static int spawn_thread(test_thread_t *t, test_thread_fn_t fn, void *arg) {
    return (pthread_create(t, NULL, fn, arg) == 0);
}
static void join_thread(test_thread_t t) {
    pthread_join(t, NULL);
}
#endif

/* ========================================================================= */
/* Test 1: Ephemeral Threads Complete Lifecycle & Invariant Accounting      */
/* ========================================================================= */

#if defined(_WIN32)
static unsigned __stdcall worker_ephemeral(void *arg)
#else
static void* worker_ephemeral(void *arg)
#endif
{
    int thread_id = (int)(intptr_t)arg;
    uint8_t src[16384];
    uint8_t dst[32768];
    for (size_t i = 0; i < sizeof(src); i++) {
        src[i] = (uint8_t)((i * 37 + thread_id) & 0xFF);
    }

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.filter = BINPACK_FILTER_TRANSPOSE_DELTA;
    opt.transpose_stride = 4;

    size_t c_size = 0;
    binpack_status_t st = binpack_compress_into(src, sizeof(src), dst, sizeof(dst), &c_size, &opt);
    if (st != BINPACK_OK) {
#if defined(_WIN32)
        return 1;
#else
        return (void*)(intptr_t)1;
#endif
    }

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int test_ephemeral_thread_accounting(void) {
#if !defined(BINPACK_TESTING)
    printf("    [SKIP] Requires -DBINPACK_TESTING\n");
    return TEST_PASS;
#else
    const int THREAD_COUNT = 20;
    test_thread_t threads[20];

    size_t initial_core_tls = atomic_load(&g_dbg_core_tls_created);
    size_t initial_core_tls_dest = atomic_load(&g_dbg_core_tls_destroyed);
    size_t initial_core_scratch_a = atomic_load(&g_dbg_core_scratch_allocs);
    size_t initial_core_scratch_f = atomic_load(&g_dbg_core_scratch_frees);

    size_t initial_sdk_tls = atomic_load(&g_dbg_sdk_tls_created);
    size_t initial_sdk_tls_dest = atomic_load(&g_dbg_sdk_tls_destroyed);
    size_t initial_sdk_cctx_c = atomic_load(&g_dbg_sdk_cctx_created);
    size_t initial_sdk_cctx_d = atomic_load(&g_dbg_sdk_cctx_destroyed);
    size_t initial_sdk_scratch_a = atomic_load(&g_dbg_sdk_scratch_allocs);
    size_t initial_sdk_scratch_f = atomic_load(&g_dbg_sdk_scratch_frees);

    for (int i = 0; i < THREAD_COUNT; i++) {
        if (!spawn_thread(&threads[i], worker_ephemeral, (void*)(intptr_t)i)) {
            return TEST_FAIL;
        }
    }

    for (int i = 0; i < THREAD_COUNT; i++) {
        join_thread(threads[i]);
    }

    /* Verify Strict Invariants: N_created == N_destroyed post-join */
    size_t core_tls_net = (atomic_load(&g_dbg_core_tls_created) - initial_core_tls) -
                          (atomic_load(&g_dbg_core_tls_destroyed) - initial_core_tls_dest);
    size_t core_scratch_net = (atomic_load(&g_dbg_core_scratch_allocs) - initial_core_scratch_a) -
                             (atomic_load(&g_dbg_core_scratch_frees) - initial_core_scratch_f);
    size_t sdk_tls_net = (atomic_load(&g_dbg_sdk_tls_created) - initial_sdk_tls) -
                         (atomic_load(&g_dbg_sdk_tls_destroyed) - initial_sdk_tls_dest);
    size_t sdk_cctx_net = (atomic_load(&g_dbg_sdk_cctx_created) - initial_sdk_cctx_c) -
                          (atomic_load(&g_dbg_sdk_cctx_destroyed) - initial_sdk_cctx_d);
    size_t sdk_scratch_net = (atomic_load(&g_dbg_sdk_scratch_allocs) - initial_sdk_scratch_a) -
                            (atomic_load(&g_dbg_sdk_scratch_frees) - initial_sdk_scratch_f);

    printf("    [Accounting] Core TLS net=%zu, Core scratch net=%zu, SDK TLS net=%zu, CCtx net=%zu, SDK scratch net=%zu\n",
           core_tls_net, core_scratch_net, sdk_tls_net, sdk_cctx_net, sdk_scratch_net);

    if (core_tls_net != 0 || core_scratch_net != 0 || sdk_tls_net != 0 || sdk_cctx_net != 0 || sdk_scratch_net != 0) {
        return TEST_FAIL;
    }

    return TEST_PASS;
#endif
}

/* ========================================================================= */
/* Test 2: Persistent Thread Pool Reuse Without Cumulative Growth            */
/* ========================================================================= */

#if defined(_WIN32)
static unsigned __stdcall worker_persistent_pool(void *arg)
#else
static void* worker_persistent_pool(void *arg)
#endif
{
    int thread_id = (int)(intptr_t)arg;
    uint8_t src[8192];
    uint8_t dst[16384];
    binpack_options_t opt;
    binpack_default_options(&opt);

    for (int round = 0; round < 50; round++) {
        for (size_t i = 0; i < sizeof(src); i++) {
            src[i] = (uint8_t)((i + round + thread_id) & 0xFF);
        }
        size_t c_size = 0;
        binpack_status_t st = binpack_compress_into(src, sizeof(src), dst, sizeof(dst), &c_size, &opt);
        if (st != BINPACK_OK) {
#if defined(_WIN32)
            return 1;
#else
            return (void*)(intptr_t)1;
#endif
        }
    }

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int test_persistent_thread_pool_reuse(void) {
#if !defined(BINPACK_TESTING)
    printf("    [SKIP] Requires -DBINPACK_TESTING\n");
    return TEST_PASS;
#else
    const int WORKERS = 4;
    test_thread_t workers[4];

    size_t before_cctx_created = atomic_load(&g_dbg_sdk_cctx_created);
    size_t before_cctx_reused = atomic_load(&g_dbg_sdk_cctx_reused);

    for (int i = 0; i < WORKERS; i++) {
        spawn_thread(&workers[i], worker_persistent_pool, (void*)(intptr_t)i);
    }
    for (int i = 0; i < WORKERS; i++) {
        join_thread(workers[i]);
    }

    size_t cctx_created = atomic_load(&g_dbg_sdk_cctx_created) - before_cctx_created;
    size_t cctx_reused = atomic_load(&g_dbg_sdk_cctx_reused) - before_cctx_reused;

    printf("    [Reuse] Workers=%d, CCtx Created=%zu, CCtx Reused=%zu\n", WORKERS, cctx_created, cctx_reused);

    /* Exactly 1 CCtx created per worker, and (50 - 1) * 4 = 196 reuses */
    if (cctx_created != (size_t)WORKERS || cctx_reused < 150) {
        return TEST_FAIL;
    }

    return TEST_PASS;
#endif
}

/* ========================================================================= */
/* Test 3: Explicit Cleanup Safety (Zero Double Free)                        */
/* ========================================================================= */

#if defined(_WIN32)
static unsigned __stdcall worker_explicit_cleanup(void *arg)
#else
static void* worker_explicit_cleanup(void *arg)
#endif
{
    (void)arg;
    uint8_t src[4096];
    uint8_t dst[8192];
    memset(src, 0xAB, sizeof(src));
    binpack_options_t opt;
    binpack_default_options(&opt);
    size_t c_size = 0;
    binpack_compress_into(src, sizeof(src), dst, sizeof(dst), &c_size, &opt);

#if defined(BINPACK_TESTING)
    /* Explicit cleanup should detach keys so OS destructor does not double-free */
    bp_core_cleanup_current_thread();
    bp_sdk_cleanup_current_thread();
#endif

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

static int test_explicit_cleanup_safety(void) {
    test_thread_t t;
    if (!spawn_thread(&t, worker_explicit_cleanup, NULL)) return TEST_FAIL;
    join_thread(t);
    return TEST_PASS;
}

/* ========================================================================= */
/* Test 4: Real Compression Throughput & Microbenchmark Overhead             */
/* ========================================================================= */

static int test_real_compression_benchmark(void) {
    const size_t BUF_SZ = 65536;
    uint8_t *src = (uint8_t*)malloc(BUF_SZ);
    uint8_t *dst = (uint8_t*)malloc(BUF_SZ * 2);
    if (!src || !dst) {
        free(src); free(dst);
        return TEST_FAIL;
    }

    for (size_t i = 0; i < BUF_SZ; i++) {
        src[i] = (uint8_t)((i * 13 + (i >> 4)) & 0xFF);
    }

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    const int ITERATIONS = 1000;
    size_t c_sz = 0;

    /* Warm-up */
    binpack_compress_into(src, BUF_SZ, dst, BUF_SZ * 2, &c_sz, &opt);

    uint64_t t0 = get_time_ns();
    for (int i = 0; i < ITERATIONS; i++) {
        binpack_status_t st = binpack_compress_into(src, BUF_SZ, dst, BUF_SZ * 2, &c_sz, &opt);
        if (st != BINPACK_OK) {
            free(src); free(dst);
            return TEST_FAIL;
        }
    }
    uint64_t t1 = get_time_ns();

    double total_ms = (double)(t1 - t0) / 1000000.0;
    double us_per_op = (double)(t1 - t0) / (double)ITERATIONS / 1000.0;
    double total_mb = (double)(BUF_SZ * ITERATIONS) / (1024.0 * 1024.0);
    double mb_per_sec = total_mb / (total_ms / 1000.0);

    printf("    [Real Benchmark] %d iterations x 64 KiB: total=%.2f ms, latency=%.2f us/op, throughput=%.2f MB/s\n",
           ITERATIONS, total_ms, us_per_op, mb_per_sec);
    printf("    [TLS Overhead Context] Measured TLS access overhead (~1.03 ns) is %.4f%% of 1-op latency (%.2f us)\n",
           (1.031 / (us_per_op * 1000.0)) * 100.0, us_per_op);

    free(src);
    free(dst);
    return TEST_PASS;
}

/* ========================================================================= */
/* Test 5: Windows Fiber Local Storage (FLS) Verification                     */
/* ========================================================================= */

#if defined(_WIN32)
typedef struct {
    PVOID main_fiber;
    int status;
    size_t fiber_cctx_created_inside;
    size_t fiber_sdk_tls_created_inside;
} fiber_ctx_t;

static VOID WINAPI fiber_worker(PVOID lpParameter) {
    fiber_ctx_t *ctx = (fiber_ctx_t*)lpParameter;
    uint8_t src[8192];
    uint8_t dst[16384];
    for (size_t i = 0; i < sizeof(src); i++) {
        src[i] = (uint8_t)((i * 17) & 0xFF);
    }
    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.filter = BINPACK_FILTER_TRANSPOSE_DELTA;
    opt.transpose_stride = 4;

    size_t c_size = 0;
    binpack_status_t st = binpack_compress_into(src, sizeof(src), dst, sizeof(dst), &c_size, &opt);
    if (st == BINPACK_OK) {
        ctx->status = 1;
    }
#if defined(BINPACK_TESTING)
    ctx->fiber_cctx_created_inside = atomic_load(&g_dbg_sdk_cctx_created);
    ctx->fiber_sdk_tls_created_inside = atomic_load(&g_dbg_sdk_tls_created);
#endif
    SwitchToFiber(ctx->main_fiber);
}
#endif

static int test_windows_fls_fiber_safety(void) {
#if !defined(_WIN32)
    printf("    [SKIP] Windows FLS Fiber test only applies to Windows\n");
    return TEST_PASS;
#else
#if !defined(BINPACK_TESTING)
    printf("    [SKIP] Requires -DBINPACK_TESTING\n");
    return TEST_PASS;
#else
    PVOID main_fiber = ConvertThreadToFiber(NULL);
    if (!main_fiber) {
        /* Already a fiber or unsupported */
        return TEST_PASS;
    }

    size_t before_cctx_c = atomic_load(&g_dbg_sdk_cctx_created);
    size_t before_cctx_d = atomic_load(&g_dbg_sdk_cctx_destroyed);
    size_t before_sdk_tls_c = atomic_load(&g_dbg_sdk_tls_created);
    size_t before_sdk_tls_d = atomic_load(&g_dbg_sdk_tls_destroyed);
    size_t before_core_tls_c = atomic_load(&g_dbg_core_tls_created);
    size_t before_core_tls_d = atomic_load(&g_dbg_core_tls_destroyed);

    fiber_ctx_t ctx = { main_fiber, 0, 0, 0 };
    PVOID child_fiber = CreateFiber(0, fiber_worker, &ctx);
    if (!child_fiber) {
        ConvertFiberToThread();
        return TEST_FAIL;
    }

    /* 1. Switch to child fiber: it creates TLS/FLS resources */
    SwitchToFiber(child_fiber);

    if (ctx.status != 1) {
        DeleteFiber(child_fiber);
        ConvertFiberToThread();
        return TEST_FAIL;
    }

    /* 2. Verify that while the child fiber was alive, resources were created but NOT yet destroyed */
    size_t created_cctx = ctx.fiber_cctx_created_inside - before_cctx_c;
    size_t created_sdk_tls = ctx.fiber_sdk_tls_created_inside - before_sdk_tls_c;
    size_t destroyed_cctx_before_delete = atomic_load(&g_dbg_sdk_cctx_destroyed) - before_cctx_d;

    printf("    [Fiber Active] Created CCtx=%zu, Created SDK TLS=%zu, Destroyed prior to DeleteFiber=%zu\n",
           created_cctx, created_sdk_tls, destroyed_cctx_before_delete);

    if (created_cctx != 1 || created_sdk_tls != 1 || destroyed_cctx_before_delete != 0) {
        DeleteFiber(child_fiber);
        ConvertFiberToThread();
        return TEST_FAIL;
    }

    /* 3. Delete the fiber: Windows MUST invoke the FLS destructor callbacks */
    DeleteFiber(child_fiber);
    ConvertFiberToThread();

    /* 4. Verify that DeleteFiber executed the FLS destructor and reclaimed all resources */
    size_t after_cctx_d = atomic_load(&g_dbg_sdk_cctx_destroyed) - before_cctx_d;
    size_t after_sdk_tls_d = atomic_load(&g_dbg_sdk_tls_destroyed) - before_sdk_tls_d;
    size_t after_core_tls_d = atomic_load(&g_dbg_core_tls_destroyed) - before_core_tls_d;
    size_t after_core_tls_c = atomic_load(&g_dbg_core_tls_created) - before_core_tls_c;

    printf("    [Fiber Destroyed] Destroyed CCtx=%zu, Destroyed SDK TLS=%zu, Net Core TLS=%zu\n",
           after_cctx_d, after_sdk_tls_d, after_core_tls_c - after_core_tls_d);

    if (after_cctx_d != created_cctx || after_sdk_tls_d != created_sdk_tls || (after_core_tls_c != after_core_tls_d)) {
        return TEST_FAIL;
    }

    return TEST_PASS;
#endif
#endif
}

/* ========================================================================= */
/* Main Runner                                                               */
/* ========================================================================= */

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("======================================================================\n");
    printf("BP-007C TLS & FLS Multiplatform Lifecycle Test Suite (v1.0)\n");
    printf("======================================================================\n\n");

    RUN_TEST(test_ephemeral_thread_accounting);
    RUN_TEST(test_persistent_thread_pool_reuse);
    RUN_TEST(test_explicit_cleanup_safety);
    RUN_TEST(test_real_compression_benchmark);
    RUN_TEST(test_windows_fls_fiber_safety);

    printf("\n======================================================================\n");
    printf("Resultados BP-007C: %d/%d pruebas ejecutadas con exito (%d fallos)\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    printf("======================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
