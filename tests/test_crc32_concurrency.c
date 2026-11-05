/**
 * @file test_crc32_concurrency.c
 * @brief Concurrency and thread-safety stress test for binpack_crc32
 *
 * Reproducer for BP-001 (data race during cold CRC32 table initialization).
 * Uses a memory barrier gate so 8 worker threads reach readiness before
 * being released simultaneously to query binpack_crc32 in a clean process.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdatomic.h>

#include "../src/binpack_internal.h"

#if defined(_WIN32)
#include <windows.h>
typedef HANDLE thread_handle_t;
#define YIELD_THREAD() SwitchToThread()
#else
#include <pthread.h>
#include <sched.h>
typedef pthread_t thread_handle_t;
#define YIELD_THREAD() sched_yield()
#endif

#define THREAD_COUNT 8
#define ITERATIONS 10000
#define BUFFER_SIZE 4096

/*
 * Ground-truth reference for 4096 bytes of 0x5A
 * verified via canonical IEEE 802.3 CRC-32 (0xEDB88320 polynomial):
 * CRC32(b'\x5a' * 4096) = 0x7CD551DD
 */
#define EXPECTED_CRC 0x7CD551DDu

static uint8_t buffer[BUFFER_SIZE];
static atomic_int ready_count = 0;
static atomic_int start_flag = 0;
static atomic_int failures = 0;

#if defined(_WIN32)
static DWORD WINAPI worker_routine(LPVOID arg) {
    (void)arg;
#else
static void* worker_routine(void* arg) {
    (void)arg;
#endif

    /* 1. Register thread presence at the starting gate */
    atomic_fetch_add_explicit(&ready_count, 1, memory_order_release);

    /* 2. Wait at gate until main thread drops start_flag */
    while (!atomic_load_explicit(&start_flag, memory_order_acquire)) {
        YIELD_THREAD();
    }

    /* 3. Concurrently invoke CRC32 on uninitialized / cold table */
    for (int i = 0; i < ITERATIONS; ++i) {
        uint32_t crc = binpack_crc32(0, buffer, sizeof(buffer));
        if (crc != EXPECTED_CRC) {
            atomic_fetch_add(&failures, 1);
        }
    }

#if defined(_WIN32)
    return 0;
#else
    return NULL;
#endif
}

int main(void) {
    memset(buffer, 0x5A, sizeof(buffer));

    printf("=========================================================\n");
    printf("BP-001 Concurrency Reproducer: Cold-start CRC32 Stress\n");
    printf("Expected reference CRC32: 0x%08X\n", EXPECTED_CRC);
    printf("Spawning %d threads (%d iterations each)...\n", THREAD_COUNT, ITERATIONS);
    printf("=========================================================\n");

    thread_handle_t threads[THREAD_COUNT];

#if defined(_WIN32)
    int spawned = 0;
    for (int i = 0; i < THREAD_COUNT; ++i) {
        threads[i] = CreateThread(NULL, 0, worker_routine, NULL, 0, NULL);
        if (threads[i] == NULL) {
            fprintf(stderr, "Fatal: CreateThread failed at worker %d (Win32 err: %lu)\n", i, GetLastError());
            /* Clean up already created threads */
            atomic_store_explicit(&start_flag, 1, memory_order_release);
            for (int j = 0; j < spawned; ++j) {
                WaitForSingleObject(threads[j], 1000);
                CloseHandle(threads[j]);
            }
            return 2;
        }
        spawned++;
    }

    /* Wait for all spawned threads to reach readiness gate */
    while (atomic_load_explicit(&ready_count, memory_order_acquire) != THREAD_COUNT) {
        YIELD_THREAD();
    }

    /* Release gate */
    atomic_store_explicit(&start_flag, 1, memory_order_release);

    DWORD wait_res = WaitForMultipleObjects(THREAD_COUNT, threads, TRUE, 30000);
    if (wait_res == WAIT_FAILED || wait_res == WAIT_TIMEOUT) {
        fprintf(stderr, "Fatal: WaitForMultipleObjects returned 0x%08lX (err: %lu)\n", wait_res, GetLastError());
        for (int i = 0; i < THREAD_COUNT; ++i) CloseHandle(threads[i]);
        return 3;
    }

    for (int i = 0; i < THREAD_COUNT; ++i) {
        CloseHandle(threads[i]);
    }
#else
    int spawned = 0;
    for (int i = 0; i < THREAD_COUNT; ++i) {
        int rc = pthread_create(&threads[i], NULL, worker_routine, NULL);
        if (rc != 0) {
            fprintf(stderr, "Fatal: pthread_create failed at worker %d (rc: %d)\n", i, rc);
            atomic_store_explicit(&start_flag, 1, memory_order_release);
            for (int j = 0; j < spawned; ++j) {
                pthread_join(threads[j], NULL);
            }
            return 2;
        }
        spawned++;
    }

    while (atomic_load_explicit(&ready_count, memory_order_acquire) != THREAD_COUNT) {
        YIELD_THREAD();
    }

    atomic_store_explicit(&start_flag, 1, memory_order_release);

    for (int i = 0; i < THREAD_COUNT; ++i) {
        int rc = pthread_join(threads[i], NULL);
        if (rc != 0) {
            fprintf(stderr, "Warning: pthread_join failed for worker %d (rc: %d)\n", i, rc);
        }
    }
#endif

    int fail_count = atomic_load(&failures);
    printf("Threads executed:      %d\n", THREAD_COUNT);
    printf("Iterations per thread: %d\n", ITERATIONS);
    printf("Total CRC operations:  %d\n", THREAD_COUNT * ITERATIONS);
    printf("Detected Failures:     %d\n", fail_count);

    if (fail_count > 0) {
        printf("[FAIL] Detected data race or corruption in CRC32 calculation!\n");
        return 1;
    }

    printf("[PASS] All threads returned exact expected CRC32 (0x%08X)\n", EXPECTED_CRC);
    return 0;
}
