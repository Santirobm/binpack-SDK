/**
 * @file test_concurrency.c
 * @brief BP-005 Concurrency & Multithread Stress Suite for BinPack Enterprise SDK
 *
 * Implements rigorous multithreaded validation across 6 blocks:
 *   - Grupo A: Cold-start simultaneous initialization (CRC32 & CPU Dispatch via child processes)
 *   - Grupo B: Parallel compression with independent contexts (identical, heterogeneous, streaming)
 *   - Grupo C: Parallel decompression with independent handles (archive files & streaming)
 *   - Grupo D: Mixed concurrent compression/decompression & indirect TLS isolation
 *   - Grupo E: Soak test (10,000 ops) tracking memory convergence with persistent worker threads
 *   - Grupo F: Shared archive handle safe usage contract via external mutex serialization
 */

#define _CRT_SECURE_NO_WARNINGS
#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdatomic.h>
#include <errno.h>

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
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <time.h>
#endif

#include "../include/binpack_sdk.h"
#include "../src/binpack_internal.h"

#define COLD_INIT_THREADS 32
#define WORKER_COUNT_16   16
#define TEST_PASS 0
#define TEST_FAIL 1

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;
static char g_self_executable_path[1024];

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

#if defined(_WIN32)
#define THREAD_YIELD() SwitchToThread()
typedef CRITICAL_SECTION mutex_t;
static int mutex_init(mutex_t *m) {
    InitializeCriticalSection(m);
    return 1; /* Win32 InitializeCriticalSection always succeeds or raises in extreme OOM */
}
#define MUTEX_LOCK(m)    EnterCriticalSection(m)
#define MUTEX_UNLOCK(m)  LeaveCriticalSection(m)
#define MUTEX_DESTROY(m) DeleteCriticalSection(m)

typedef HANDLE thread_handle_t;
typedef unsigned (__stdcall *thread_fn_t)(void *);
#define THREAD_FUNC_DECL(fn) unsigned __stdcall fn(void *arg)
#define THREAD_FUNC_RETURN return 0

#else

#define THREAD_YIELD() sched_yield()
typedef pthread_mutex_t mutex_t;
static int mutex_init(mutex_t *m) {
    return (pthread_mutex_init(m, NULL) == 0) ? 1 : 0;
}
#define MUTEX_LOCK(m)    pthread_mutex_lock(m)
#define MUTEX_UNLOCK(m)  pthread_mutex_unlock(m)
#define MUTEX_DESTROY(m) pthread_mutex_destroy(m)

typedef pthread_t thread_handle_t;
typedef void *(*thread_fn_t)(void *);
#define THREAD_FUNC_DECL(fn) void *fn(void *arg)
#define THREAD_FUNC_RETURN return NULL

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

/* ========================================================================= */
/* C-02: One-shot Atomic Memory Barrier with Coordinator Timeout & Abort    */
/* ========================================================================= */

/**
 * @brief One-shot Atomic Memory Barrier.
 *
 * Designed for single-phase coordination. The coordinator waits with a bounded
 * millisecond timeout for all worker threads to reach ready_count. If any thread
 * stalls or crashes, the coordinator triggers barrier_abort(), setting abort_flag
 * and releasing the gate so waiting workers terminate cleanly without deadlocks.
 */
typedef struct {
    atomic_int ready_count;
    atomic_int release_gate;
    atomic_int abort_flag;
    int target_threads;
} thread_barrier_t;

static void barrier_init(thread_barrier_t *b, int target_threads) {
    atomic_init(&b->ready_count, 0);
    atomic_init(&b->release_gate, 0);
    atomic_init(&b->abort_flag, 0);
    b->target_threads = target_threads;
}

static int barrier_wait(thread_barrier_t *b) {
    atomic_fetch_add_explicit(&b->ready_count, 1, memory_order_acq_rel);
    while (atomic_load_explicit(&b->release_gate, memory_order_acquire) == 0) {
        if (atomic_load_explicit(&b->abort_flag, memory_order_acquire)) {
            return -1; /* Aborted by coordinator */
        }
        THREAD_YIELD();
    }
    if (atomic_load_explicit(&b->abort_flag, memory_order_acquire)) {
        return -1;
    }
    return 0;
}

static void barrier_release(thread_barrier_t *b) {
    atomic_store_explicit(&b->release_gate, 1, memory_order_release);
}

static void barrier_abort(thread_barrier_t *b) {
    atomic_store_explicit(&b->abort_flag, 1, memory_order_release);
    atomic_store_explicit(&b->release_gate, 1, memory_order_release);
}

static int barrier_wait_coordinator(thread_barrier_t *b, uint32_t timeout_ms) {
    uint64_t start = get_time_ms();
    while (1) {
        int current_ready = atomic_load_explicit(&b->ready_count, memory_order_acquire);
        if (current_ready > b->target_threads) {
            fprintf(stderr, "[ERROR] Barrier invariant violated: ready_count (%d) > target_threads (%d)\n",
                    current_ready, b->target_threads);
            barrier_abort(b);
            return 0;
        }
        if (current_ready == b->target_threads) {
            break;
        }
        if (get_time_ms() - start > timeout_ms) {
            fprintf(stderr, "[ERROR] Barrier timeout (%u ms) waiting for workers (ready: %d/%d)\n",
                    timeout_ms, current_ready, b->target_threads);
            barrier_abort(b);
            return 0;
        }
        THREAD_YIELD();
    }
    barrier_release(b);
    return 1;
}

/* ========================================================================= */
/* C-01: Thread Management with Strict Return Code Checking                  */
/* ========================================================================= */

typedef struct {
    thread_handle_t handle;
    int thread_id;
    int status;
    void *user_ctx;
} worker_slot_t;

#if defined(_WIN32)
static int start_worker_thread(thread_handle_t *out_handle, thread_fn_t fn, void *arg) {
    uintptr_t th = _beginthreadex(NULL, 0, fn, arg, 0, NULL);
    if (th == 0) return 0;
    *out_handle = (HANDLE)th;
    return 1;
}

static int wait_worker_thread(thread_handle_t handle, uint32_t timeout_ms) {
    DWORD res = WaitForSingleObject(handle, timeout_ms);
    if (res == WAIT_OBJECT_0) {
        CloseHandle(handle);
        return 1;
    }
    if (res == WAIT_TIMEOUT) {
        fprintf(stderr, "[ERROR] Worker thread timed out after %u ms!\n", timeout_ms);
    } else {
        fprintf(stderr, "[ERROR] WaitForSingleObject failed with code 0x%08lX (Win32 err: %lu)\n",
                res, GetLastError());
    }
    /* Deliberately DO NOT close handle if thread is still running */
    return 0;
}
#elif defined(__linux__)
static int start_worker_thread(thread_handle_t *out_handle, thread_fn_t fn, void *arg) {
    if (pthread_create(out_handle, NULL, fn, arg) != 0) return 0;
    return 1;
}

/**
 * @brief POSIX thread join with bounded timeout using GNU extension pthread_timedjoin_np.
 *
 * NOTE ON POSIX SPECIFICATION:
 * pthread_timedjoin_np() requires _GNU_SOURCE (defined at the top of this file before headers).
 * Under glibc, pthread_timedjoin_np() strictly mandates an absolute timeout measured against
 * CLOCK_REALTIME (system wall clock for futex bitset matching). While CLOCK_REALTIME is subject
 * to NTP adjustments, glibc does not support CLOCK_MONOTONIC for this interface.
 * If ETIMEDOUT occurs, the test harness enforces strict fail-safe resource retention: the
 * environment structure is permanently retained in heap, eliminating any use-after-free risk.
 */
static int wait_worker_thread(thread_handle_t handle, uint32_t timeout_ms) {
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    uint64_t nsec = (uint64_t)ts.tv_nsec + (uint64_t)(timeout_ms % 1000) * 1000000ULL;
    ts.tv_sec += (time_t)(timeout_ms / 1000) + (time_t)(nsec / 1000000000ULL);
    ts.tv_nsec = (long)(nsec % 1000000000ULL);

    int res = pthread_timedjoin_np(handle, NULL, &ts);
    if (res == 0) {
        return 1;
    }
    if (res == ETIMEDOUT) {
        fprintf(stderr, "[ERROR] Worker thread timed out after %u ms (pthread_timedjoin_np)\n", timeout_ms);
    } else {
        fprintf(stderr, "[ERROR] pthread_timedjoin_np failed with error %d\n", res);
    }
    return 0;
}
#else
static int start_worker_thread(thread_handle_t *out_handle, thread_fn_t fn, void *arg) {
    if (pthread_create(out_handle, NULL, fn, arg) != 0) return 0;
    return 1;
}

static int wait_worker_thread(thread_handle_t handle, uint32_t timeout_ms) {
    (void)timeout_ms;
    return (pthread_join(handle, NULL) == 0) ? 1 : 0;
}
#endif

/* Cross-platform child process runner for cold-start isolation with bounded timeout */
static int run_cold_subtest(const char *self_path, const char *subtest_arg) {
#if defined(_WIN32)
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", self_path, subtest_arg);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return -1;
    }
    DWORD wait_res = WaitForSingleObject(pi.hProcess, 30000);
    if (wait_res != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        DWORD kill_res = WaitForSingleObject(pi.hProcess, 5000);
        if (kill_res == WAIT_OBJECT_0) {
            CloseHandle(pi.hProcess);
            CloseHandle(pi.hThread);
        } else {
            fprintf(stderr, "[ERROR] TerminateProcess failed to confirm exit within 5000 ms (code: 0x%08lX)\n", kill_res);
            /* Deliberately DO NOT close handles if exit is unconfirmed */
        }
        return -2;
    }
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execl(self_path, self_path, subtest_arg, (char*)NULL);
        _exit(127);
    }
    /* Bounded wait with timeout (30 seconds) */
    uint64_t start_ms = get_time_ms();
    int status = 0;
    while (1) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            return -3;
        }
        if (r < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (get_time_ms() - start_ms > 30000) {
            /* Child timed out: attempt to terminate it with SIGKILL */
            int kret = kill(pid, SIGKILL);
            if (kret == -1 && errno != ESRCH) {
                fprintf(stderr, "[ERROR] kill(SIGKILL) failed with fatal errno %d (process %d cleanup unconfirmed, may remain alive)\n",
                        errno, (int)pid);
                return -4; /* Unconfirmed cleanup: termination signal could not be delivered, process may remain alive */
            }

            /* Bounded harvest wait: poll for up to 5000 ms to confirm death without hanging */
            uint64_t kill_start = get_time_ms();
            int harvested = 0;
            while (get_time_ms() - kill_start < 5000) {
                pid_t wr = waitpid(pid, &status, WNOHANG);
                if (wr == pid) {
                    harvested = 1;
                    break;
                }
                if (wr < 0) {
                    if (errno == EINTR) continue;
                    if (errno == ECHILD) {
                        /* ECHILD: process is no longer waitable by caller (e.g. reaped by alternate handler/init) */
                        harvested = 1;
                        break;
                    }
                    fprintf(stderr, "[ERROR] waitpid non-transient error %d harvesting child %d (cleanup unconfirmed)\n", errno, (int)pid);
                    return -5; /* Unconfirmed cleanup: waitpid failed with non-transient error, state unconfirmed */
                }
                usleep(10000); /* 10 ms sleep between harvest polls */
            }

            if (!harvested) {
                fprintf(stderr, "[ERROR] Child process %d did not terminate within 5000 ms after SIGKILL (cleanup unconfirmed)\n", (int)pid);
                return -6; /* Unconfirmed cleanup: child process did not confirm termination within bounded timeout */
            }

            return -2; /* Child timed out and was confirmed killed and cleanly harvested */
        }
        usleep(10000); /* 10 ms polling */
    }
#endif
}

/* Helper to generate a multi-chunk archive file using the official SDK API */
static int create_test_archive_file(const char *raw_path, const char *bpk_path, size_t total_bytes, uint32_t chunk_size) {
    FILE *f = fopen(raw_path, "wb");
    if (!f) return 0;
    uint8_t buf[4096];
    size_t written = 0;
    while (written < total_bytes) {
        size_t to_write = (total_bytes - written < sizeof(buf)) ? (total_bytes - written) : sizeof(buf);
        for (size_t i = 0; i < to_write; i++) {
            buf[i] = (uint8_t)(((written + i) * 31 + 7) & 0xFF);
        }
        if (fwrite(buf, 1, to_write, f) != to_write) {
            fclose(f);
            remove(raw_path);
            return 0;
        }
        written += to_write;
    }
    if (fclose(f) != 0) {
        remove(raw_path);
        return 0;
    }

    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.chunk_size = chunk_size;

    binpack_status_t st = binpack_compress_file(raw_path, bpk_path, &opts, NULL, NULL);
    remove(raw_path);
    return (st == BINPACK_OK) ? 1 : 0;
}

/* ========================================================================= */
/* GRUPO A: Inicialización Fría Simultánea (Thundering Herd)                 */
/* ========================================================================= */

static thread_barrier_t g_cold_barrier;

static const char *s_cold_crc_payload = "BinPack Cold-Start Concurrency Validation Payload IEEE 802.3 Test";
static const uint32_t s_cold_crc_golden = 0xA85FC1BDu;

typedef struct {
    worker_slot_t slots[COLD_INIT_THREADS];
} test_a_env_t;

static THREAD_FUNC_DECL(worker_a1_cold_crc32) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    if (barrier_wait(&g_cold_barrier) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }
    uint32_t computed = binpack_crc32(0, (const uint8_t*)s_cold_crc_payload, strlen(s_cold_crc_payload));
    slot->status = (computed == s_cold_crc_golden) ? 1 : 0;
    THREAD_FUNC_RETURN;
}

static int execute_subtest_a1(void) {
    test_a_env_t *env = (test_a_env_t*)calloc(1, sizeof(test_a_env_t));
    if (!env) return 1;

    barrier_init(&g_cold_barrier, COLD_INIT_THREADS);

    int spawned = 0;
    for (int i = 0; i < COLD_INIT_THREADS; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        if (!start_worker_thread(&env->slots[i].handle, worker_a1_cold_crc32, &env->slots[i])) {
            barrier_abort(&g_cold_barrier);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return 1;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_cold_barrier, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return 1;
    }

    int all_ok = 1;
    int all_joined = 1;
    for (int i = 0; i < COLD_INIT_THREADS; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) all_ok = 0;
        } else {
            all_joined = 0;
            all_ok = 0;
        }
    }

    if (all_joined) free(env);
    return (all_ok && all_joined) ? 0 : 1;
}

static THREAD_FUNC_DECL(worker_a2_cold_dispatch) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    if (barrier_wait(&g_cold_barrier) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }
    uint8_t buffer[64];
    for (int i = 0; i < 64; i++) buffer[i] = (uint8_t)(i + slot->thread_id);

    uint8_t enc[64];
    memcpy(enc, buffer, 64);
    binpack_bcj_aarch64_filter(enc, 64);

    uint8_t dec[64];
    memcpy(dec, enc, 64);
    binpack_bcj_aarch64_unfilter(dec, 64);

    slot->status = (memcmp(buffer, dec, 64) == 0) ? 1 : 0;
    THREAD_FUNC_RETURN;
}

static int execute_subtest_a2(void) {
    test_a_env_t *env = (test_a_env_t*)calloc(1, sizeof(test_a_env_t));
    if (!env) return 1;

    barrier_init(&g_cold_barrier, COLD_INIT_THREADS);

    int spawned = 0;
    for (int i = 0; i < COLD_INIT_THREADS; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        if (!start_worker_thread(&env->slots[i].handle, worker_a2_cold_dispatch, &env->slots[i])) {
            barrier_abort(&g_cold_barrier);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return 1;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_cold_barrier, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return 1;
    }

    int all_ok = 1;
    int all_joined = 1;
    for (int i = 0; i < COLD_INIT_THREADS; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) all_ok = 0;
        } else {
            all_joined = 0;
            all_ok = 0;
        }
    }

    if (all_joined) free(env);
    return (all_ok && all_joined) ? 0 : 1;
}

static int test_A1_crc32_cold_start_concurrency(void) {
    int rc = run_cold_subtest(g_self_executable_path, "--subtest-a1");
    return (rc == 0) ? TEST_PASS : TEST_FAIL;
}

static int test_A2_cpu_dispatch_cold_start_concurrency(void) {
    int rc = run_cold_subtest(g_self_executable_path, "--subtest-a2");
    return (rc == 0) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* GRUPO B: Compresión Paralela (Contextos Independientes)                   */
/* ========================================================================= */

static thread_barrier_t g_barrier_16;

typedef struct {
    const uint8_t *src_buf;
    size_t src_sz;
    const uint8_t *golden_comp;
    size_t golden_comp_sz;
    uint8_t out_comp[2048];
    size_t out_comp_sz;
} b1_thread_ctx_t;

typedef struct {
    uint8_t raw_buf[1024];
    uint8_t golden_comp[2048];
    size_t golden_comp_sz;
    b1_thread_ctx_t thread_ctx[WORKER_COUNT_16];
    worker_slot_t slots[WORKER_COUNT_16];
} test_b1_env_t;

static THREAD_FUNC_DECL(worker_b1_parallel_compress) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    b1_thread_ctx_t *ctx = (b1_thread_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    size_t out_sz = 0;
    binpack_status_t st = binpack_compress_into(
        ctx->src_buf, ctx->src_sz,
        ctx->out_comp, sizeof(ctx->out_comp),
        &out_sz, NULL
    );

    if (st != BINPACK_OK || out_sz != ctx->golden_comp_sz) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    if (memcmp(ctx->out_comp, ctx->golden_comp, out_sz) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    ctx->out_comp_sz = out_sz;
    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_B1_identical_data_parallel_compression(void) {
    test_b1_env_t *env = (test_b1_env_t*)calloc(1, sizeof(test_b1_env_t));
    if (!env) return TEST_FAIL;

    const size_t raw_sz = sizeof(env->raw_buf);
    for (size_t i = 0; i < raw_sz; i++) {
        env->raw_buf[i] = (uint8_t)((i * 37 + 11) & 0xFF);
    }

    binpack_status_t st = binpack_compress_into(
        env->raw_buf, raw_sz,
        env->golden_comp, sizeof(env->golden_comp),
        &env->golden_comp_sz, NULL
    );
    if (st != BINPACK_OK) {
        free(env);
        return TEST_FAIL;
    }

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->thread_ctx[i];
        env->thread_ctx[i].src_buf = env->raw_buf;
        env->thread_ctx[i].src_sz = raw_sz;
        env->thread_ctx[i].golden_comp = env->golden_comp;
        env->thread_ctx[i].golden_comp_sz = env->golden_comp_sz;
        env->thread_ctx[i].out_comp_sz = 0;

        if (!start_worker_thread(&env->slots[i].handle, worker_b1_parallel_compress, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined && ok) {
        for (int i = 1; i < WORKER_COUNT_16; i++) {
            if (env->thread_ctx[i].out_comp_sz != env->thread_ctx[0].out_comp_sz ||
                memcmp(env->thread_ctx[i].out_comp, env->thread_ctx[0].out_comp, env->thread_ctx[0].out_comp_sz) != 0) {
                ok = 0;
                break;
            }
        }
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in B1! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

typedef struct {
    uint32_t seed;
    int workload_type;
} b2_thread_ctx_t;

typedef struct {
    b2_thread_ctx_t thread_ctx[WORKER_COUNT_16];
    worker_slot_t slots[WORKER_COUNT_16];
} test_b2_env_t;

static void generate_workload_data(uint8_t *buf, size_t sz, int type, uint32_t seed) {
    switch (type) {
        case 0: /* ASCII Text */
            for (size_t i = 0; i < sz; i++) {
                seed = seed * 1664525u + 1013904223u;
                buf[i] = (uint8_t)(32 + (seed % 95));
            }
            break;
        case 1: /* Binary pseudo-random */
            for (size_t i = 0; i < sz; i++) {
                seed = seed * 1103515245u + 12345u;
                buf[i] = (uint8_t)((seed >> 16) & 0xFF);
            }
            break;
        case 2: /* Float tabular sequence */
            for (size_t i = 0; i < sz / 4; i++) {
                float val = (float)(i * 0.125f + (seed & 0xFF));
                memcpy(buf + i * 4, &val, 4);
            }
            break;
        case 3: /* Highly repetitive */
            for (size_t i = 0; i < sz; i++) {
                buf[i] = (uint8_t)((i % 16) ^ (seed & 0x0F));
            }
            break;
    }
}

static THREAD_FUNC_DECL(worker_b2_heterogeneous_compress) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    b2_thread_ctx_t *ctx = (b2_thread_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    const size_t raw_sz = 2048;
    uint8_t raw[2048];
    generate_workload_data(raw, raw_sz, ctx->workload_type, ctx->seed);

    uint8_t comp[4096];
    size_t comp_sz = 0;
    binpack_status_t st = binpack_compress_into(raw, raw_sz, comp, sizeof(comp), &comp_sz, NULL);
    if (st != BINPACK_OK) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    uint8_t decomp[2048];
    size_t decomp_sz = 0;
    st = binpack_decompress_into(comp, comp_sz, decomp, sizeof(decomp), &decomp_sz, 1);
    if (st != BINPACK_OK || decomp_sz != raw_sz) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    if (memcmp(raw, decomp, raw_sz) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_B2_heterogeneous_parallel_compression(void) {
    test_b2_env_t *env = (test_b2_env_t*)calloc(1, sizeof(test_b2_env_t));
    if (!env) return TEST_FAIL;

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->thread_ctx[i];
        env->thread_ctx[i].seed = 0xABCD0000u + (uint32_t)i * 7919u;
        env->thread_ctx[i].workload_type = i % 4;

        if (!start_worker_thread(&env->slots[i].handle, worker_b2_heterogeneous_compress, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in B2! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

typedef struct {
    const uint8_t *in_data;
    size_t in_size;
    size_t in_pos;
    uint8_t *out_data;
    size_t out_cap;
    size_t out_size;
} mem_stream_state_t;

static size_t BINPACK_CALL mem_read_cb(void *user_data, void *buffer, size_t size) {
    mem_stream_state_t *st = (mem_stream_state_t*)user_data;
    if (st->in_pos >= st->in_size) return 0;
    size_t avail = st->in_size - st->in_pos;
    size_t to_read = (size < avail) ? size : avail;
    memcpy(buffer, st->in_data + st->in_pos, to_read);
    st->in_pos += to_read;
    return to_read;
}

static size_t BINPACK_CALL mem_write_cb(void *user_data, const void *buffer, size_t size) {
    mem_stream_state_t *st = (mem_stream_state_t*)user_data;
    if (st->out_size + size > st->out_cap) return 0;
    memcpy(st->out_data + st->out_size, buffer, size);
    st->out_size += size;
    return size;
}

typedef struct {
    uint32_t seed;
} b3_thread_ctx_t;

typedef struct {
    b3_thread_ctx_t thread_ctx[WORKER_COUNT_16];
    worker_slot_t slots[WORKER_COUNT_16];
} test_b3_env_t;

static THREAD_FUNC_DECL(worker_b3_parallel_streaming) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    b3_thread_ctx_t *ctx = (b3_thread_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    const size_t raw_sz = 16384;
    uint8_t raw[16384];
    uint32_t seed = ctx->seed;
    for (size_t i = 0; i < raw_sz; i++) {
        seed = seed * 1664525u + 1013904223u;
        raw[i] = (uint8_t)(seed >> 24);
    }

    uint8_t stream_out[65536];
    mem_stream_state_t enc_state = {
        .in_data = raw,
        .in_size = raw_sz,
        .in_pos = 0,
        .out_data = stream_out,
        .out_cap = sizeof(stream_out),
        .out_size = 0
    };

    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.chunk_size = 64 * 1024;

    binpack_status_t st = binpack_compress_stream(mem_read_cb, mem_write_cb, &enc_state, raw_sz, &opts, NULL);
    if (st != BINPACK_OK || enc_state.out_size == 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    uint8_t decomp[16384];
    mem_stream_state_t dec_state = {
        .in_data = stream_out,
        .in_size = enc_state.out_size,
        .in_pos = 0,
        .out_data = decomp,
        .out_cap = sizeof(decomp),
        .out_size = 0
    };

    st = binpack_decompress_stream(mem_read_cb, mem_write_cb, &dec_state, NULL);
    if (st != BINPACK_OK || dec_state.out_size != raw_sz) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    if (memcmp(raw, decomp, raw_sz) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_B3_parallel_streaming_compression(void) {
    test_b3_env_t *env = (test_b3_env_t*)calloc(1, sizeof(test_b3_env_t));
    if (!env) return TEST_FAIL;

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->thread_ctx[i];
        env->thread_ctx[i].seed = 0x54320000u + (uint32_t)i * 65537u;

        if (!start_worker_thread(&env->slots[i].handle, worker_b3_parallel_streaming, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in B3! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* GRUPO C: Descompresión Paralela (Handles Independientes)                  */
/* ========================================================================= */

typedef struct {
    const char *archive_path;
    uint32_t chunk_count;
    uint64_t total_uncompressed;
    uint32_t ref_crc;
} c1_test_ctx_t;

typedef struct {
    char arch_path[64];
    char raw_path[64];
    c1_test_ctx_t ctx;
    worker_slot_t slots[WORKER_COUNT_16];
} test_c1_env_t;

static THREAD_FUNC_DECL(worker_c1_parallel_archive) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    c1_test_ctx_t *ctx = (c1_test_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    binpack_archive_t *arch = binpack_archive_open(ctx->archive_path);
    if (!arch) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    uint64_t total_uncomp = binpack_archive_get_total_uncompressed_size(arch);

    if (count != ctx->chunk_count || total_uncomp != ctx->total_uncompressed) {
        binpack_archive_close(arch);
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    uint8_t dec_buf[65536];
    uint32_t running_crc = 0;

    for (uint32_t c = 0; c < count; c++) {
        binpack_chunk_info_t cinfo;
        binpack_status_t st = binpack_archive_get_chunk_info(arch, c, &cinfo);
        if (st != BINPACK_OK || cinfo.uncompressed_sz > sizeof(dec_buf)) {
            binpack_archive_close(arch);
            slot->status = 0;
            THREAD_FUNC_RETURN;
        }

        size_t actual_out = 0;
        st = binpack_archive_decompress_chunk(arch, c, dec_buf, sizeof(dec_buf), &actual_out);
        if (st != BINPACK_OK || actual_out != cinfo.uncompressed_sz) {
            binpack_archive_close(arch);
            slot->status = 0;
            THREAD_FUNC_RETURN;
        }

        running_crc = binpack_crc32(running_crc, dec_buf, actual_out);
    }

    binpack_archive_close(arch);

    slot->status = (running_crc == ctx->ref_crc) ? 1 : 0;
    THREAD_FUNC_RETURN;
}

static int test_C1_parallel_archive_handles(void) {
    test_c1_env_t *env = (test_c1_env_t*)calloc(1, sizeof(test_c1_env_t));
    if (!env) return TEST_FAIL;

    strncpy(env->arch_path, "tmp_concurrency_c1.bpa", sizeof(env->arch_path) - 1);
    strncpy(env->raw_path, "tmp_concurrency_c1.raw", sizeof(env->raw_path) - 1);

    const size_t total_sz = 256 * 1024;
    const uint32_t chunk_sz = 64 * 1024;
    if (!create_test_archive_file(env->raw_path, env->arch_path, total_sz, chunk_sz)) {
        free(env);
        return TEST_FAIL;
    }

    /* Compute golden CRC for verification */
    uint32_t golden_crc = 0;
    uint8_t chunk_buf[4096];
    size_t written = 0;
    while (written < total_sz) {
        size_t to_calc = (total_sz - written < sizeof(chunk_buf)) ? (total_sz - written) : sizeof(chunk_buf);
        for (size_t i = 0; i < to_calc; i++) {
            chunk_buf[i] = (uint8_t)(((written + i) * 31 + 7) & 0xFF);
        }
        golden_crc = binpack_crc32(golden_crc, chunk_buf, to_calc);
        written += to_calc;
    }

    env->ctx.archive_path = env->arch_path;
    env->ctx.chunk_count = (uint32_t)(total_sz / chunk_sz);
    env->ctx.total_uncompressed = total_sz;
    env->ctx.ref_crc = golden_crc;

    barrier_init(&g_barrier_16, WORKER_COUNT_16);
    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->ctx;
        if (!start_worker_thread(&env->slots[i].handle, worker_c1_parallel_archive, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) { remove(env->arch_path); free(env); }
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) { remove(env->arch_path); free(env); }
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        remove(env->arch_path);
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in C1! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

typedef struct {
    const uint8_t *stream_payload;
    size_t stream_sz;
    const uint8_t *golden_dec;
    size_t golden_dec_sz;
} c2_test_ctx_t;

typedef struct {
    uint8_t raw[65536];
    uint8_t stream_payload[131072];
    c2_test_ctx_t ctx;
    worker_slot_t slots[WORKER_COUNT_16];
} test_c2_env_t;

static THREAD_FUNC_DECL(worker_c2_parallel_stream_dec) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    c2_test_ctx_t *ctx = (c2_test_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    uint8_t out_buf[65536];
    mem_stream_state_t st = {
        .in_data = ctx->stream_payload,
        .in_size = ctx->stream_sz,
        .in_pos = 0,
        .out_data = out_buf,
        .out_cap = sizeof(out_buf),
        .out_size = 0
    };

    binpack_status_t res = binpack_decompress_stream(mem_read_cb, mem_write_cb, &st, NULL);
    if (res != BINPACK_OK || st.out_size != ctx->golden_dec_sz) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    if (memcmp(out_buf, ctx->golden_dec, ctx->golden_dec_sz) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_C2_parallel_stream_decompression(void) {
    test_c2_env_t *env = (test_c2_env_t*)calloc(1, sizeof(test_c2_env_t));
    if (!env) return TEST_FAIL;

    for (size_t i = 0; i < sizeof(env->raw); i++) env->raw[i] = (uint8_t)(i ^ 0xAA);

    mem_stream_state_t enc_st = {
        .in_data = env->raw,
        .in_size = sizeof(env->raw),
        .in_pos = 0,
        .out_data = env->stream_payload,
        .out_cap = sizeof(env->stream_payload),
        .out_size = 0
    };

    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.chunk_size = 64 * 1024;

    if (binpack_compress_stream(mem_read_cb, mem_write_cb, &enc_st, sizeof(env->raw), &opts, NULL) != BINPACK_OK) {
        free(env);
        return TEST_FAIL;
    }

    env->ctx.stream_payload = env->stream_payload;
    env->ctx.stream_sz = enc_st.out_size;
    env->ctx.golden_dec = env->raw;
    env->ctx.golden_dec_sz = sizeof(env->raw);

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->ctx;
        if (!start_worker_thread(&env->slots[i].handle, worker_c2_parallel_stream_dec, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in C2! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* GRUPO D: Operaciones Mixtas Cruzadas y Aislamiento TLS                    */
/* ========================================================================= */

typedef struct {
    int is_compressor;
    const uint8_t *ref_raw;
    size_t raw_sz;
    const uint8_t *ref_cmp;
    size_t cmp_sz;
} d1_thread_ctx_t;

typedef struct {
    uint8_t raw[2048];
    uint8_t golden_cmp[4096];
    size_t golden_sz;
    d1_thread_ctx_t thread_ctx[WORKER_COUNT_16];
    worker_slot_t slots[WORKER_COUNT_16];
} test_d1_env_t;

static THREAD_FUNC_DECL(worker_d1_mixed_operations) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    d1_thread_ctx_t *ctx = (d1_thread_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    for (int iter = 0; iter < 100; iter++) {
        if (ctx->is_compressor) {
            uint8_t out[4096];
            size_t out_sz = 0;
            binpack_status_t st = binpack_compress_into(
                ctx->ref_raw, ctx->raw_sz,
                out, sizeof(out),
                &out_sz, NULL
            );
            if (st != BINPACK_OK || out_sz != ctx->cmp_sz || memcmp(out, ctx->ref_cmp, out_sz) != 0) {
                slot->status = 0;
                THREAD_FUNC_RETURN;
            }
        } else {
            uint8_t out[2048];
            size_t out_sz = 0;
            binpack_status_t st = binpack_decompress_into(
                ctx->ref_cmp, ctx->cmp_sz,
                out, sizeof(out),
                &out_sz, 1
            );
            if (st != BINPACK_OK || out_sz != ctx->raw_sz || memcmp(out, ctx->ref_raw, out_sz) != 0) {
                slot->status = 0;
                THREAD_FUNC_RETURN;
            }
        }
    }

    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_D1_mixed_concurrent_compression_decompression(void) {
    test_d1_env_t *env = (test_d1_env_t*)calloc(1, sizeof(test_d1_env_t));
    if (!env) return TEST_FAIL;

    const size_t raw_sz = sizeof(env->raw);
    for (size_t i = 0; i < raw_sz; i++) env->raw[i] = (uint8_t)((i * 31 + 7) & 0xFF);

    if (binpack_compress_into(env->raw, raw_sz, env->golden_cmp, sizeof(env->golden_cmp), &env->golden_sz, NULL) != BINPACK_OK) {
        free(env);
        return TEST_FAIL;
    }

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->thread_ctx[i];
        env->thread_ctx[i].is_compressor = (i % 2 == 0);
        env->thread_ctx[i].ref_raw = env->raw;
        env->thread_ctx[i].raw_sz = raw_sz;
        env->thread_ctx[i].ref_cmp = env->golden_cmp;
        env->thread_ctx[i].cmp_sz = env->golden_sz;

        if (!start_worker_thread(&env->slots[i].handle, worker_d1_mixed_operations, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in D1! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

typedef struct {
    worker_slot_t slots[WORKER_COUNT_16];
} test_d2_env_t;

static THREAD_FUNC_DECL(worker_d2_tls_isolation) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    int tid = slot->thread_id;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    size_t payload_sz = 300 + (tid * 73) % 500;
    uint8_t payload[1024];
    uint8_t unique_byte = (uint8_t)(0xA0 + tid * 3);
    for (size_t i = 0; i < payload_sz; i++) {
        payload[i] = (uint8_t)(unique_byte + (i & 0x0F));
    }

    for (int iter = 0; iter < 150; iter++) {
        uint8_t cmp[2048];
        size_t cmp_sz = 0;
        binpack_status_t st = binpack_compress_into(
            payload, payload_sz,
            cmp, sizeof(cmp),
            &cmp_sz, NULL
        );
        if (st != BINPACK_OK) {
            slot->status = 0;
            THREAD_FUNC_RETURN;
        }

        uint8_t dec[1024];
        size_t dec_sz = 0;
        st = binpack_decompress_into(
            cmp, cmp_sz,
            dec, sizeof(dec),
            &dec_sz, 1
        );
        if (st != BINPACK_OK || dec_sz != payload_sz) {
            slot->status = 0;
            THREAD_FUNC_RETURN;
        }

        if (memcmp(payload, dec, payload_sz) != 0) {
            slot->status = 0;
            THREAD_FUNC_RETURN;
        }

        for (size_t k = 0; k < payload_sz; k++) {
            uint8_t expected = (uint8_t)(unique_byte + (k & 0x0F));
            if (dec[k] != expected) {
                slot->status = 0;
                THREAD_FUNC_RETURN;
            }
        }
    }

    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_D2_indirect_tls_scratchpad_isolation(void) {
    test_d2_env_t *env = (test_d2_env_t*)calloc(1, sizeof(test_d2_env_t));
    if (!env) return TEST_FAIL;

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = NULL;

        if (!start_worker_thread(&env->slots[i].handle, worker_d2_tls_isolation, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in D2! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* GRUPO E: Estrés Sostenido y Resistencia a Fugas (Soak Test)               */
/* ========================================================================= */

typedef struct {
    atomic_int round_gate;
    atomic_int ops_for_round;
    atomic_int completed_threads;
    atomic_int terminate_flag;
} soak_sync_t;

static soak_sync_t g_soak_sync;

typedef struct {
    worker_slot_t slots[WORKER_COUNT_16];
} test_e1_env_t;

static THREAD_FUNC_DECL(worker_e1_persistent_soak) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    int tid = slot->thread_id;
    int current_round = 0;

    uint8_t raw[1024];
    uint8_t cmp[2048];
    uint8_t dec[1024];

    while (1) {
        while (atomic_load_explicit(&g_soak_sync.round_gate, memory_order_acquire) == current_round) {
            if (atomic_load_explicit(&g_soak_sync.terminate_flag, memory_order_acquire)) {
                slot->status = 1;
                THREAD_FUNC_RETURN;
            }
            THREAD_YIELD();
        }
        current_round = atomic_load_explicit(&g_soak_sync.round_gate, memory_order_acquire);
        if (atomic_load_explicit(&g_soak_sync.terminate_flag, memory_order_acquire)) {
            slot->status = 1;
            THREAD_FUNC_RETURN;
        }

        int ops_to_run = atomic_load_explicit(&g_soak_sync.ops_for_round, memory_order_relaxed);

        for (int op = 0; op < ops_to_run; op++) {
            uint8_t byte_val = (uint8_t)((tid * 17) + (op & 0xFF));
            memset(raw, byte_val, sizeof(raw));

            size_t cmp_sz = 0;
            binpack_status_t st_c = binpack_compress_into(raw, sizeof(raw), cmp, sizeof(cmp), &cmp_sz, NULL);
            if (st_c != BINPACK_OK) {
                slot->status = 0;
                atomic_fetch_add_explicit(&g_soak_sync.completed_threads, 1, memory_order_acq_rel);
                THREAD_FUNC_RETURN;
            }

            size_t dec_sz = 0;
            binpack_status_t st_d = binpack_decompress_into(cmp, cmp_sz, dec, sizeof(dec), &dec_sz, 1);
            if (st_d != BINPACK_OK || dec_sz != sizeof(raw) || dec[0] != byte_val) {
                slot->status = 0;
                atomic_fetch_add_explicit(&g_soak_sync.completed_threads, 1, memory_order_acq_rel);
                THREAD_FUNC_RETURN;
            }
        }

        atomic_fetch_add_explicit(&g_soak_sync.completed_threads, 1, memory_order_acq_rel);
    }
}

static size_t get_current_process_memory(void) {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (size_t)pmc.WorkingSetSize;
    }
    return 0;
#else
    /* NOTE: On Linux/POSIX, ru_maxrss reports the maximum (peak) resident set size (in KB),
     * rather than current instantaneous working set. This metric is monotonic per process. */
    struct rusage usage;
    if (getrusage(RUSAGE_SELF, &usage) == 0) {
        return (size_t)usage.ru_maxrss * 1024;
    }
    return 0;
#endif
}

static int trigger_soak_round(int ops_per_thread, int next_round_idx, uint32_t timeout_ms) {
    atomic_store_explicit(&g_soak_sync.completed_threads, 0, memory_order_release);
    atomic_store_explicit(&g_soak_sync.ops_for_round, ops_per_thread, memory_order_release);
    atomic_store_explicit(&g_soak_sync.round_gate, next_round_idx, memory_order_release);

    uint64_t start = get_time_ms();
    while (atomic_load_explicit(&g_soak_sync.completed_threads, memory_order_acquire) != WORKER_COUNT_16) {
        if (get_time_ms() - start > timeout_ms) {
            fprintf(stderr, "[ERROR] Soak round %d timed out waiting for workers!\n", next_round_idx);
            atomic_store_explicit(&g_soak_sync.terminate_flag, 1, memory_order_release);
            return 0;
        }
        THREAD_YIELD();
    }
    return 1;
}

static int test_E1_soak_stress_10000_operations(void) {
    atomic_init(&g_soak_sync.round_gate, 0);
    atomic_init(&g_soak_sync.ops_for_round, 0);
    atomic_init(&g_soak_sync.completed_threads, 0);
    atomic_init(&g_soak_sync.terminate_flag, 0);

    test_e1_env_t *env = (test_e1_env_t*)calloc(1, sizeof(test_e1_env_t));
    if (!env) return TEST_FAIL;

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = NULL;

        if (!start_worker_thread(&env->slots[i].handle, worker_e1_persistent_soak, &env->slots[i])) {
            atomic_store_explicit(&g_soak_sync.terminate_flag, 1, memory_order_release);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) free(env);
            return TEST_FAIL;
        }
        spawned++;
    }

    /* Phase 1: Warmup round (800 ops) */
    if (!trigger_soak_round(50, 1, 10000)) {
        atomic_store_explicit(&g_soak_sync.terminate_flag, 1, memory_order_release);
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }
    size_t mem_warmup = get_current_process_memory();

    /* Phase 2: Soak Round 1 (10,000 ops) */
    if (!trigger_soak_round(625, 2, 30000)) {
        atomic_store_explicit(&g_soak_sync.terminate_flag, 1, memory_order_release);
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }
    size_t mem_round1 = get_current_process_memory();

    /* Phase 3: Soak Round 2 (10,000 ops) */
    if (!trigger_soak_round(625, 3, 30000)) {
        atomic_store_explicit(&g_soak_sync.terminate_flag, 1, memory_order_release);
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) free(env);
        return TEST_FAIL;
    }
    size_t mem_round2 = get_current_process_memory();

    /* Signal threads to terminate and join */
    atomic_store_explicit(&g_soak_sync.terminate_flag, 1, memory_order_release);
    atomic_fetch_add_explicit(&g_soak_sync.round_gate, 1, memory_order_release);

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    int64_t delta_warm_to_r1 = ((int64_t)mem_round1 - (int64_t)mem_warmup) / 1024;
    int64_t delta_r1_to_r2   = ((int64_t)mem_round2 - (int64_t)mem_round1) / 1024;

    printf("    -> Soak memory tracking: Warmup=%zu KB, Round1=%zu KB (d=%+lld KB), Round2=%zu KB (d=%+lld KB)\n",
           mem_warmup / 1024, mem_round1 / 1024, (long long)delta_warm_to_r1,
           mem_round2 / 1024, (long long)delta_r1_to_r2);

    if (delta_r1_to_r2 > 128) {
        printf("    -> WARNING: Unbounded memory growth detected between soaked rounds!\n");
        ok = 0;
    }

    if (all_joined) {
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in E1! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* GRUPO F: Documentación de Uso Seguro de Handle Compartido con Mutex       */
/* NOTA DE AUDITORÍA: binpack_archive_t NO es thread-safe internamente      */
/* (posee FILE* y ZSTD_DCtx* mutables). Esta prueba valida exclusivamente el */
/* CONTRATO DE USO SEGURO: serialización estricta mediante cerrojo externo.  */
/* El acceso concurrente sin mutex está expresamente prohibido por contrato. */
/* ========================================================================= */

typedef struct {
    binpack_archive_t *shared_arch;
    mutex_t *arch_mutex;
    uint32_t expected_chunks;
} f1_test_ctx_t;

typedef struct {
    char arch_path[64];
    char raw_path[64];
    binpack_archive_t *arch;
    mutex_t mutex;
    int mutex_initialized;
    f1_test_ctx_t ctx;
    worker_slot_t slots[WORKER_COUNT_16];
} test_f1_env_t;

static THREAD_FUNC_DECL(worker_f1_mutex_protected_archive) {
    worker_slot_t *slot = (worker_slot_t*)arg;
    f1_test_ctx_t *ctx = (f1_test_ctx_t*)slot->user_ctx;

    if (barrier_wait(&g_barrier_16) != 0) {
        slot->status = 0;
        THREAD_FUNC_RETURN;
    }

    uint8_t out_buf[65536];

    for (int iter = 0; iter < 50; iter++) {
        MUTEX_LOCK(ctx->arch_mutex);

        uint32_t chunk_idx = (uint32_t)(iter % ctx->expected_chunks);
        size_t actual_out = 0;
        binpack_status_t st = binpack_archive_decompress_chunk(
            ctx->shared_arch,
            chunk_idx,
            out_buf, sizeof(out_buf),
            &actual_out
        );

        MUTEX_UNLOCK(ctx->arch_mutex);

        if (st != BINPACK_OK || actual_out == 0) {
            slot->status = 0;
            THREAD_FUNC_RETURN;
        }
    }

    slot->status = 1;
    THREAD_FUNC_RETURN;
}

static int test_F1_shared_archive_safe_contract_with_mutex(void) {
    test_f1_env_t *env = (test_f1_env_t*)calloc(1, sizeof(test_f1_env_t));
    if (!env) return TEST_FAIL;

    strncpy(env->arch_path, "tmp_concurrency_f1.bpa", sizeof(env->arch_path) - 1);
    strncpy(env->raw_path, "tmp_concurrency_f1.raw", sizeof(env->raw_path) - 1);

    const size_t total_sz = 256 * 1024;
    const uint32_t chunk_sz = 64 * 1024;
    if (!create_test_archive_file(env->raw_path, env->arch_path, total_sz, chunk_sz)) {
        free(env);
        return TEST_FAIL;
    }

    env->arch = binpack_archive_open(env->arch_path);
    if (!env->arch) {
        remove(env->arch_path);
        free(env);
        return TEST_FAIL;
    }

    if (!mutex_init(&env->mutex)) {
        binpack_archive_close(env->arch);
        remove(env->arch_path);
        free(env);
        return TEST_FAIL;
    }
    env->mutex_initialized = 1;

    env->ctx.shared_arch = env->arch;
    env->ctx.arch_mutex = &env->mutex;
    env->ctx.expected_chunks = (uint32_t)(total_sz / chunk_sz);

    barrier_init(&g_barrier_16, WORKER_COUNT_16);

    int spawned = 0;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        env->slots[i].thread_id = i;
        env->slots[i].status = 0;
        env->slots[i].user_ctx = &env->ctx;

        if (!start_worker_thread(&env->slots[i].handle, worker_f1_mutex_protected_archive, &env->slots[i])) {
            barrier_abort(&g_barrier_16);
            int joined = 1;
            for (int j = 0; j < spawned; j++) {
                if (!wait_worker_thread(env->slots[j].handle, 10000)) joined = 0;
            }
            if (joined) {
                binpack_archive_close(env->arch);
                if (env->mutex_initialized) {
                    MUTEX_DESTROY(&env->mutex);
                    env->mutex_initialized = 0;
                }
                remove(env->arch_path);
                free(env);
            }
            return TEST_FAIL;
        }
        spawned++;
    }

    if (!barrier_wait_coordinator(&g_barrier_16, 10000)) {
        int joined = 1;
        for (int i = 0; i < spawned; i++) {
            if (!wait_worker_thread(env->slots[i].handle, 10000)) joined = 0;
        }
        if (joined) {
            binpack_archive_close(env->arch);
            if (env->mutex_initialized) {
                MUTEX_DESTROY(&env->mutex);
                env->mutex_initialized = 0;
            }
            remove(env->arch_path);
            free(env);
        }
        return TEST_FAIL;
    }

    int ok = 1;
    int all_joined = 1;
    for (int i = 0; i < WORKER_COUNT_16; i++) {
        if (wait_worker_thread(env->slots[i].handle, 30000)) {
            if (env->slots[i].status != 1) ok = 0;
        } else {
            all_joined = 0;
            ok = 0;
        }
    }

    if (all_joined) {
        binpack_archive_close(env->arch);
        if (env->mutex_initialized) {
            MUTEX_DESTROY(&env->mutex);
            env->mutex_initialized = 0;
        }
        remove(env->arch_path);
        free(env);
    } else {
        fprintf(stderr, "[CRITICAL] Thread timed out in F1! Retaining environment to prevent use-after-free.\n");
    }
    return (ok && all_joined) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* Main Test Runner & CLI Dispatcher                                         */
/* ========================================================================= */

int main(int argc, char *argv[]) {
    /* 1. Subprocess Cold-Start Dispatcher */
    if (argc > 1) {
        if (strcmp(argv[1], "--subtest-a1") == 0) {
            return execute_subtest_a1();
        }
        if (strcmp(argv[1], "--subtest-a2") == 0) {
            return execute_subtest_a2();
        }
    }

    /* Save path of current executable to invoke cold child processes */
    strncpy(g_self_executable_path, argv[0], sizeof(g_self_executable_path) - 1);
    g_self_executable_path[sizeof(g_self_executable_path) - 1] = '\0';

    printf("======================================================================\n");
    printf("BP-005 Concurrency & Multithread Stress Suite (v2.2)\n");
    printf("======================================================================\n\n");

    printf("[GRUPO A] Inicializacion Fria Simultanea (Thundering Herd):\n");
    RUN_TEST(test_A1_crc32_cold_start_concurrency);
    RUN_TEST(test_A2_cpu_dispatch_cold_start_concurrency);

    printf("\n[GRUPO B] Compresion Paralela (Contextos Independientes):\n");
    RUN_TEST(test_B1_identical_data_parallel_compression);
    RUN_TEST(test_B2_heterogeneous_parallel_compression);
    RUN_TEST(test_B3_parallel_streaming_compression);

    printf("\n[GRUPO C] Descompresion Paralela (Handles Independientes):\n");
    RUN_TEST(test_C1_parallel_archive_handles);
    RUN_TEST(test_C2_parallel_stream_decompression);

    printf("\n[GRUPO D] Operaciones Mixtas y Aislamiento TLS:\n");
    RUN_TEST(test_D1_mixed_concurrent_compression_decompression);
    RUN_TEST(test_D2_indirect_tls_scratchpad_isolation);

    printf("\n[GRUPO E] Estres Sostenido y Resistencia a Fugas (Soak Test):\n");
    RUN_TEST(test_E1_soak_stress_10000_operations);

    printf("\n[GRUPO F] Documentacion de Uso Seguro de Handle Compartido:\n");
    RUN_TEST(test_F1_shared_archive_safe_contract_with_mutex);

    printf("\n======================================================================\n");
    printf("Resultados BP-005: %d/%d pruebas ejecutadas con exito (%d fallos)\n",
           g_tests_passed, g_tests_run, g_tests_failed);
    printf("======================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
