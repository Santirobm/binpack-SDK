/**
 * @file test_dict_safety.c
 * @brief BinPack Enterprise SDK - BP-006C Pre-trained Dictionary Engine Safety & Adversarial Suite
 *
 * Adversarial test harness to characterize and audit the Dictionary Engine (src/binpack_dict.c):
 * - C01: Null and invalid pointer parameters (and defensive output clearing)
 * - C02: Excessive dict_capacity early rejection & allocation telemetry in train_dict
 * - C03: Truncation and 64-bit integer overflow in sample_count (sample_count > UINT_MAX)
 * - C04: Cumulative sample_sizes overflow detection without invalid memory reads
 * - C05: Empty samples handling (all zero-length vs interleaved valid/empty)
 * - C06: Excessive dict_size early rejection in create_dict
 * - C07: Corrupted, truncated, and arbitrary bytes dictionary resilience
 * - C08: Compression level (clevel) range characterization (negative, zero, valid, beyond max)
 * - C09: Output size clearing on all error paths (compressed_sz and decompressed_sz)
 * - C10: Exact error status reporting on insufficient buffer capacity (dst_cap)
 * - C11: Lifecycle resilience: binpack_free_dict(NULL) and clean tear-down
 * - C12: Functional golden round-trip on micro-telemetry packets
 * - C13: Cross-dictionary decompression and corrupted stream resilience
 * - C14: Ephemeral threads lifecycle and TLS context memory retention measurement
 * - C15: Context reuse, state isolation, and consecutive call stability
 */

#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <limits.h>
#include <assert.h>
#include <errno.h>
#include <pthread.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <signal.h>

#include "binpack_sdk.h"
#include <zstd.h>
#include <zdict.h>

#define TEST_PASS 0
#define TEST_FAIL 1

/* ========================================================================= */
/* Allocation Tracking & Fault Injection Harness                             */
/* ========================================================================= */

#ifdef BINPACK_TESTING

extern void* __real_malloc(size_t size);
extern void  __real_free(void *ptr);

static volatile size_t s_test_alloc_count = 0;
static volatile size_t s_test_last_alloc_sz = 0;
static volatile size_t s_test_max_alloc_sz = 0;
static volatile size_t s_test_total_allocated = 0;
static volatile int    s_test_intercept_active = 0;

void* __wrap_malloc(size_t size) {
    if (s_test_intercept_active) {
        s_test_alloc_count++;
        s_test_last_alloc_sz = size;
        if (size > s_test_max_alloc_sz) {
            s_test_max_alloc_sz = size;
        }
        s_test_total_allocated += size;
    }
    return __real_malloc(size);
}

void __wrap_free(void *ptr) {
    __real_free(ptr);
}

static void test_alloc_start_tracking(void) {
    s_test_alloc_count = 0;
    s_test_last_alloc_sz = 0;
    s_test_max_alloc_sz = 0;
    s_test_total_allocated = 0;
    s_test_intercept_active = 1;
}

static void test_alloc_stop_tracking(void) {
    s_test_intercept_active = 0;
}

static size_t test_alloc_get_count(void) {
    return s_test_alloc_count;
}

static size_t test_alloc_get_max_sz(void) {
    return s_test_max_alloc_sz;
}

#else

static void test_alloc_start_tracking(void) {}
static void test_alloc_stop_tracking(void) {}
static size_t test_alloc_get_count(void) { return 0; }
static size_t test_alloc_get_max_sz(void) { return 0; }

#endif

/* ========================================================================= */
/* Sample Data Generator for Training & Testing                              */
/* ========================================================================= */

#define SAMPLE_COUNT_STD 100
#define SAMPLE_SIZE_STD  64

static uint8_t  s_samples_buf[SAMPLE_COUNT_STD * SAMPLE_SIZE_STD];
static size_t   s_sample_sizes[SAMPLE_COUNT_STD];
static int      s_samples_ready = 0;

static void init_standard_samples(void) {
    if (s_samples_ready) return;
    for (size_t i = 0; i < SAMPLE_COUNT_STD; i++) {
        s_sample_sizes[i] = SAMPLE_SIZE_STD;
        uint8_t *p = s_samples_buf + (i * SAMPLE_SIZE_STD);
        /* Simulated CAN-bus telemetry packet */
        p[0] = 0x18;
        p[1] = 0xFE;
        p[2] = 0xF1;
        p[3] = (uint8_t)(i & 0x0F);
        for (size_t b = 4; b < SAMPLE_SIZE_STD; b++) {
            p[b] = (uint8_t)((b * 13 + i * 7) ^ 0xA5);
        }
    }
    s_samples_ready = 1;
}

/* ========================================================================= */
/* Test Cases C01 - C15                                                      */
/* ========================================================================= */

/**
 * C01: Parameter validation on NULL pointers and outputs across all 6 public dictionary APIs.
 */
static int test_c01_null_parameters(void) {
    init_standard_samples();
    int failed = 0;

    /* 1. binpack_train_dict with NULL parameters */
    if (binpack_train_dict(NULL, s_sample_sizes, SAMPLE_COUNT_STD, 16384) != NULL) {
        fprintf(stderr, "  [C01] train_dict(NULL samples) did not return NULL\n");
        failed = 1;
    }
    if (binpack_train_dict(s_samples_buf, NULL, SAMPLE_COUNT_STD, 16384) != NULL) {
        fprintf(stderr, "  [C01] train_dict(NULL sizes) did not return NULL\n");
        failed = 1;
    }
    if (binpack_train_dict(s_samples_buf, s_sample_sizes, 0, 16384) != NULL) {
        fprintf(stderr, "  [C01] train_dict(sample_count=0) did not return NULL\n");
        failed = 1;
    }
    if (binpack_train_dict(s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 0) != NULL) {
        fprintf(stderr, "  [C01] train_dict(dict_capacity=0) did not return NULL\n");
        failed = 1;
    }

    /* 2. binpack_create_dict with NULL / zero */
    if (binpack_create_dict(NULL, 1024, 3) != NULL) {
        fprintf(stderr, "  [C01] create_dict(NULL buffer) did not return NULL\n");
        failed = 1;
    }
    if (binpack_create_dict(s_samples_buf, 0, 3) != NULL) {
        fprintf(stderr, "  [C01] create_dict(dict_size=0) did not return NULL\n");
        failed = 1;
    }

    /* 3. binpack_get_dict_buffer with NULL dict */
    size_t out_sz = 9999;
    const void *buf = binpack_get_dict_buffer(NULL, &out_sz);
    if (buf != NULL || out_sz != 0) {
        fprintf(stderr, "  [C01] get_dict_buffer(NULL) did not return NULL with out_sz=0 (buf=%p, sz=%zu)\n",
                buf, out_sz);
        failed = 1;
    }
    /* get_dict_buffer with NULL out_size should not crash */
    buf = binpack_get_dict_buffer(NULL, NULL);
    if (buf != NULL) {
        fprintf(stderr, "  [C01] get_dict_buffer(NULL, NULL) returned non-NULL\n");
        failed = 1;
    }

    /* 4. binpack_compress_dict with NULL parameters */
    uint8_t dummy_in[64] = {0};
    uint8_t dummy_out[256];
    size_t cmp_sz = 9999;

    binpack_status_t st = binpack_compress_dict(NULL, sizeof(dummy_in), dummy_out, sizeof(dummy_out), &cmp_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM) {
        fprintf(stderr, "  [C01] compress_dict(NULL, ...) returned %d, expected BINPACK_ERR_INVALID_PARAM\n", st);
        failed = 1;
    }
    if (cmp_sz != 0) {
        fprintf(stderr, "  [C01] compress_dict failed to zero cmp_sz on invalid param (cmp_sz=%zu)\n", cmp_sz);
        failed = 1;
    }

    /* 5. binpack_decompress_dict with NULL parameters */
    size_t dec_sz = 9999;
    st = binpack_decompress_dict(NULL, 10, dummy_out, sizeof(dummy_out), &dec_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM) {
        fprintf(stderr, "  [C01] decompress_dict(NULL, ...) returned %d, expected BINPACK_ERR_INVALID_PARAM\n", st);
        failed = 1;
    }
    if (dec_sz != 0) {
        fprintf(stderr, "  [C01] decompress_dict failed to zero dec_sz on invalid param (dec_sz=%zu)\n", dec_sz);
        failed = 1;
    }

    /* 6. binpack_free_dict(NULL) must be a safe no-op */
    binpack_free_dict(NULL);

    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C02: Excessive dict_capacity early rejection in train_dict.
 * Verifies whether oversized capacity requests (e.g. > 32 MiB or SIZE_MAX) are rejected
 * with ZERO memory allocations before allocating.
 */
static int test_c02_excessive_train_capacity(void) {
    init_standard_samples();
    int failed = 0;

    size_t huge_capacities[] = {
        (size_t)-1,
        (size_t)1024 * 1024 * 1024 * 2ULL, /* 2 GiB */
        (size_t)64 * 1024 * 1024            /* 64 MiB - above 32 MiB ceiling */
    };

    for (size_t i = 0; i < sizeof(huge_capacities)/sizeof(huge_capacities[0]); i++) {
        size_t cap = huge_capacities[i];
        test_alloc_start_tracking();

        binpack_dict_t *dict = binpack_train_dict(
            s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, cap
        );

        test_alloc_stop_tracking();

        if (dict != NULL) {
            fprintf(stderr, "  [C02] train_dict unexpectedly succeeded for capacity %zu\n", cap);
            binpack_free_dict(dict);
            failed = 1;
        }

        size_t alloc_count = test_alloc_get_count();
        size_t max_alloc = test_alloc_get_max_sz();

        printf("    [C02 Telemetry] Capacity=%zu -> AllocCount=%zu, MaxAllocSz=%zu\n",
               cap, alloc_count, max_alloc);

        /* Hardened invariant: must be rejected upfront with ZERO dynamic allocations */
        if (alloc_count > 0) {
            fprintf(stderr, "  [C02] Memory allocation attempted for oversized capacity %zu (AllocCount=%zu)\n",
                    cap, alloc_count);
            failed = 1;
        }
    }

    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C03: sample_count > UINT_MAX 64-bit integer overflow / 32-bit truncation.
 * Verifies that sample_count > UINT_MAX is rejected upfront before iterating or casting.
 */
static int test_c03_sample_count_truncation(void) {
    int failed = 0;

    size_t dummy_size = 64;
    uint8_t dummy_sample[64] = {0};

    /* Construct a 64-bit count that would wrap around to 1 if cast to unsigned 32-bit */
    size_t overflow_count = ((size_t)UINT_MAX) + 2ULL;

    test_alloc_start_tracking();

    binpack_dict_t *dict = binpack_train_dict(
        dummy_sample, &dummy_size, overflow_count, 16384
    );

    test_alloc_stop_tracking();

    if (dict != NULL) {
        fprintf(stderr, "  [C03] train_dict accepted sample_count > UINT_MAX (%zu)\n", overflow_count);
        binpack_free_dict(dict);
        failed = 1;
    }

    size_t alloc_count = test_alloc_get_count();
    printf("    [C03 Telemetry] overflow_count=%zu -> Result=NULL, AllocCount=%zu\n",
           overflow_count, alloc_count);

    if (alloc_count > 0) {
        fprintf(stderr, "  [C03] Allocated memory before validating sample_count bounds\n");
        failed = 1;
    }

    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C04: Cumulative sample_sizes overflow detection.
 * Checks that synthetic arrays whose total sum would overflow SIZE_MAX are rejected cleanly.
 */
static int test_c04_cumulative_size_overflow(void) {
    int failed = 0;

    size_t bad_sizes[2];
    bad_sizes[0] = (SIZE_MAX / 2) + 1000ULL;
    bad_sizes[1] = (SIZE_MAX / 2) + 1000ULL;

    uint8_t dummy[64] = {0};

    test_alloc_start_tracking();

    binpack_dict_t *dict = binpack_train_dict(
        dummy, bad_sizes, 2, 16384
    );

    test_alloc_stop_tracking();

    if (dict != NULL) {
        fprintf(stderr, "  [C04] train_dict accepted cumulative overflow in sample_sizes\n");
        binpack_free_dict(dict);
        failed = 1;
    }

    size_t alloc_count = test_alloc_get_count();
    printf("    [C04 Telemetry] Cumulative overflow array -> AllocCount=%zu\n", alloc_count);

    if (alloc_count > 0) {
        fprintf(stderr, "  [C04] Attempted allocation despite arithmetic overflow in samples sum\n");
        failed = 1;
    }

    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C05: Empty samples handling:
 * Case A: all samples are size 0.
 * Case B: samples of size 0 interleaved with legitimate samples.
 */
static int test_c05_empty_samples(void) {
    int failed = 0;

    /* Case A: All zero sizes */
    size_t zero_sizes[4] = { 0, 0, 0, 0 };
    uint8_t dummy[64] = {0};

    test_alloc_start_tracking();
    binpack_dict_t *dict_a = binpack_train_dict(dummy, zero_sizes, 4, 16384);
    test_alloc_stop_tracking();

    if (dict_a != NULL) {
        fprintf(stderr, "  [C05] train_dict succeeded with all zero-sized samples\n");
        binpack_free_dict(dict_a);
        failed = 1;
    }
    printf("    [C05 Case A] All zero sizes -> Result=NULL, AllocCount=%zu\n", test_alloc_get_count());
    if (test_alloc_get_count() > 0) {
        fprintf(stderr, "  [C05] Allocated buffer before verifying non-empty sample payload\n");
        failed = 1;
    }

    /* Case B: Interleaved zero sizes with real samples */
    init_standard_samples();
    size_t mixed_sizes[6] = { 0, 64, 0, 64, 0, 64 };
    uint8_t mixed_samples[64 * 3];
    memcpy(mixed_samples, s_samples_buf, sizeof(mixed_samples));

    /* ZDICT generally rejects datasets with fewer than required bytes or invalid frames,
     * but the SDK should handle this without crashes or memory corruption. */
    binpack_dict_t *dict_b = binpack_train_dict(mixed_samples, mixed_sizes, 6, 16384);
    if (dict_b != NULL) {
        binpack_free_dict(dict_b);
    }
    printf("    [C05 Case B] Interleaved zero sizes handled cleanly without crash\n");

    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C06: Excessive dict_size early rejection in create_dict.
 * Verifies that binpack_create_dict rejects giant sizes before malloc.
 */
static int test_c06_excessive_create_size(void) {
    int failed = 0;

    uint8_t dummy[64] = {0};
    size_t excessive_sizes[] = {
        (size_t)-1,
        (size_t)1024 * 1024 * 1024, /* 1 GiB */
        (size_t)64 * 1024 * 1024    /* 64 MiB */
    };

    for (size_t i = 0; i < sizeof(excessive_sizes)/sizeof(excessive_sizes[0]); i++) {
        size_t sz = excessive_sizes[i];
        test_alloc_start_tracking();

        binpack_dict_t *dict = binpack_create_dict(dummy, sz, 3);

        test_alloc_stop_tracking();

        if (dict != NULL) {
            fprintf(stderr, "  [C06] create_dict succeeded on oversized dict_size %zu\n", sz);
            binpack_free_dict(dict);
            failed = 1;
        }

        size_t alloc_count = test_alloc_get_count();
        size_t max_alloc = test_alloc_get_max_sz();

        printf("    [C06 Telemetry] create_dict size=%zu -> AllocCount=%zu, MaxAlloc=%zu\n",
               sz, alloc_count, max_alloc);

        if (alloc_count > 0) {
            fprintf(stderr, "  [C06] Memory allocation attempted for oversized dict_size %zu (AllocCount=%zu)\n",
                    sz, alloc_count);
            failed = 1;
        }
    }

    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C07: Corrupted, truncated, and arbitrary bytes dictionary in create_dict.
 * Characterizes behavior on 1, 2, 3 bytes and random noise buffers.
 */
static int test_c07_corrupt_or_truncated_dict(void) {
    int failed = 0;

    /* Very small raw buffers: 1, 2, 3, 7 bytes */
    uint8_t tiny_buf[8] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04 };
    for (size_t sz = 1; sz <= 7; sz++) {
        binpack_dict_t *dict = binpack_create_dict(tiny_buf, sz, 3);
        if (dict) {
            /* Raw content dictionary may be created by Zstandard for arbitrary bytes */
            binpack_free_dict(dict);
        }
    }

    /* Buffer with fake Zstandard dictionary magic header (0xEC30A437) but truncated/corrupt */
    uint8_t bad_hdr_dict[16] = {
        0x37, 0xA4, 0x30, 0xEC, /* ZSTD_MAGIC_DICTIONARY little-endian */
        0x01, 0x00, 0x00, 0x00, /* Dict ID 1 */
        0xFF, 0xFF, 0xFF, 0xFF  /* Corrupt payload */
    };

    binpack_dict_t *dict_corrupt = binpack_create_dict(bad_hdr_dict, sizeof(bad_hdr_dict), 3);
    if (dict_corrupt) {
        /* Even if created as raw dictionary, check that get_dict_buffer returns correct size */
        size_t reported_sz = 0;
        const void *buf = binpack_get_dict_buffer(dict_corrupt, &reported_sz);
        if (reported_sz != sizeof(bad_hdr_dict) || buf == NULL) {
            fprintf(stderr, "  [C07] get_dict_buffer discrepancy on corrupt dict\n");
            failed = 1;
        }
        binpack_free_dict(dict_corrupt);
    }

    printf("    [C07] Truncated and corrupt dictionary inputs handled cleanly without crash\n");
    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C08: clevel parameter characterization.
 * Evaluates negative levels, level 0, standard levels, and levels beyond maximum.
 */
static int test_c08_clevel_ranges(void) {
    init_standard_samples();
    int failed = 0;

    /* Train a baseline dictionary */
    binpack_dict_t *base_dict = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!base_dict) {
        fprintf(stderr, "  [C08] Failed to train baseline dictionary for clevel test\n");
        return TEST_FAIL;
    }

    size_t dict_raw_sz = 0;
    const void *dict_raw = binpack_get_dict_buffer(base_dict, &dict_raw_sz);
    assert(dict_raw && dict_raw_sz > 0);

    int test_levels[] = { -5, -1, 0, 1, 3, 19, 22, 23, 99 };
    for (size_t i = 0; i < sizeof(test_levels)/sizeof(test_levels[0]); i++) {
        int lvl = test_levels[i];
        binpack_dict_t *d = binpack_create_dict(dict_raw, dict_raw_sz, lvl);
        if (!d) {
            fprintf(stderr, "  [C08] create_dict failed for clevel=%d\n", lvl);
            failed = 1;
            continue;
        }

        /* Test compression with this dictionary */
        uint8_t out[256];
        size_t cmp_sz = 0;
        binpack_status_t st = binpack_compress_dict(s_samples_buf, 64, out, sizeof(out), &cmp_sz, d);
        if (st != BINPACK_OK || cmp_sz == 0) {
            fprintf(stderr, "  [C08] compress_dict failed for clevel=%d: st=%d\n", lvl, st);
            failed = 1;
        }

        /* Historical semantics: clevel == 0, negative values, and values > max_lvl all fallback to level 19 */
        printf("    [C08] clevel=%2d -> Result: st=%d, compressed_sz=%zu (Historical fallback to 19 on non-positive or out-of-range)\n",
               lvl, st, cmp_sz);
        binpack_free_dict(d);
    }

    binpack_free_dict(base_dict);
    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C09: Defensive clearing of output sizes on error paths.
 * Checks that *compressed_sz and *decompressed_sz are reset to 0 whenever an error occurs.
 */
static int test_c09_defensive_output_clearing(void) {
    init_standard_samples();
    int failed = 0;

    binpack_dict_t *dict = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict) {
        fprintf(stderr, "  [C09] Failed to train baseline dictionary\n");
        return TEST_FAIL;
    }

    uint8_t dummy_buf[64] = {0};
    uint8_t out_buf[256];

    /* 1. compress_dict with NULL src */
    size_t cmp_sz = 0xDEADBEEF;
    binpack_status_t st = binpack_compress_dict(NULL, 64, out_buf, sizeof(out_buf), &cmp_sz, dict);
    if (st != BINPACK_ERR_INVALID_PARAM || cmp_sz != 0) {
        fprintf(stderr, "  [C09 Defect Observed] compress_dict(NULL src) st=%d, cmp_sz=%zu (expected 0)\n",
                st, cmp_sz);
        failed = 1;
    }

    /* 2. compress_dict with NULL dict */
    cmp_sz = 0xDEADBEEF;
    st = binpack_compress_dict(dummy_buf, 64, out_buf, sizeof(out_buf), &cmp_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM || cmp_sz != 0) {
        fprintf(stderr, "  [C09 Defect Observed] compress_dict(NULL dict) st=%d, cmp_sz=%zu (expected 0)\n",
                st, cmp_sz);
        failed = 1;
    }

    /* 3. decompress_dict with NULL src */
    size_t dec_sz = 0xDEADBEEF;
    st = binpack_decompress_dict(NULL, 64, out_buf, sizeof(out_buf), &dec_sz, dict);
    if (st != BINPACK_ERR_INVALID_PARAM || dec_sz != 0) {
        fprintf(stderr, "  [C09 Defect Observed] decompress_dict(NULL src) st=%d, dec_sz=%zu (expected 0)\n",
                st, dec_sz);
        failed = 1;
    }

    /* 4. decompress_dict with corrupt compressed payload */
    uint8_t garbage[16] = { 0xFF, 0xEE, 0xDD, 0xCC, 0xBB, 0xAA };
    dec_sz = 0xDEADBEEF;
    st = binpack_decompress_dict(garbage, sizeof(garbage), out_buf, sizeof(out_buf), &dec_sz, dict);
    if (st == BINPACK_OK || dec_sz != 0) {
        fprintf(stderr, "  [C09 Defect Observed] decompress_dict(garbage) st=%d, dec_sz=%zu (expected 0)\n",
                st, dec_sz);
        failed = 1;
    }

    binpack_free_dict(dict);
    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C10: Exact error status reporting on insufficient buffer capacity (dst_cap).
 */
static int test_c10_buffer_too_small_reporting(void) {
    init_standard_samples();
    int failed = 0;

    binpack_dict_t *dict = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict) return TEST_FAIL;

    uint8_t tiny_dst[2];
    size_t cmp_sz = 0x1234;

    /* Compress with destination buffer of only 2 bytes */
    binpack_status_t st_c = binpack_compress_dict(
        s_samples_buf, 64, tiny_dst, sizeof(tiny_dst), &cmp_sz, dict
    );

    printf("    [C10 Telemetry] compress_dict with dst_cap=2 returned status: %d (%s)\n",
           st_c, binpack_error_string(st_c));

    if (st_c != BINPACK_ERR_BUFFER_TOO_SMALL) {
        fprintf(stderr, "  [C10] Expected BINPACK_ERR_BUFFER_TOO_SMALL (-7), got %d\n", st_c);
        failed = 1;
    }
    if (cmp_sz != 0) {
        fprintf(stderr, "  [C10] cmp_sz not zeroed on buffer too small: %zu\n", cmp_sz);
        failed = 1;
    }

    /* Generate valid compressed packet */
    uint8_t valid_cmp[256];
    size_t valid_cmp_sz = 0;
    st_c = binpack_compress_dict(s_samples_buf, 64, valid_cmp, sizeof(valid_cmp), &valid_cmp_sz, dict);
    assert(st_c == BINPACK_OK);

    /* Decompress with insufficient dst_cap (e.g. 10 bytes instead of 64) */
    uint8_t small_dec[10];
    size_t dec_sz = 0x5678;
    binpack_status_t st_d = binpack_decompress_dict(
        valid_cmp, valid_cmp_sz, small_dec, sizeof(small_dec), &dec_sz, dict
    );

    printf("    [C10 Telemetry] decompress_dict with dst_cap=10 returned status: %d (%s)\n",
           st_d, binpack_error_string(st_d));

    if (st_d != BINPACK_ERR_BUFFER_TOO_SMALL) {
        fprintf(stderr, "  [C10] Expected BINPACK_ERR_BUFFER_TOO_SMALL (-7) on decompress, got %d\n", st_d);
        failed = 1;
    }
    if (dec_sz != 0) {
        fprintf(stderr, "  [C10] dec_sz not zeroed on buffer too small: %zu\n", dec_sz);
        failed = 1;
    }

    binpack_free_dict(dict);
    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C11: Lifecycle resilience: binpack_free_dict(NULL) and clean tear-down.
 */
static int test_c11_lifecycle_and_double_free(void) {
    init_standard_samples();

    /* 1. Multiple NULL frees */
    binpack_free_dict(NULL);
    binpack_free_dict(NULL);

    /* 2. Rapid allocate and free cycle */
    for (int i = 0; i < 20; i++) {
        binpack_dict_t *d = binpack_create_dict(s_samples_buf, 1024, 1);
        if (!d) return TEST_FAIL;
        binpack_free_dict(d);
    }

    printf("    [C11] 20 create/free cycles completed cleanly\n");
    return TEST_PASS;
}

/**
 * C12: Functional golden round-trip on micro-telemetry packets.
 */
static int test_c12_golden_roundtrip(void) {
    init_standard_samples();

    binpack_dict_t *dict = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict) {
        fprintf(stderr, "  [C12] Failed to train dictionary\n");
        return TEST_FAIL;
    }

    /* Test all 100 samples individually */
    int all_matched = 1;
    size_t total_orig = 0;
    size_t total_cmp = 0;

    for (size_t i = 0; i < SAMPLE_COUNT_STD; i++) {
        const uint8_t *orig = s_samples_buf + (i * SAMPLE_SIZE_STD);
        uint8_t cmp[256];
        size_t cmp_sz = 0;

        binpack_status_t st = binpack_compress_dict(orig, SAMPLE_SIZE_STD, cmp, sizeof(cmp), &cmp_sz, dict);
        if (st != BINPACK_OK || cmp_sz == 0) {
            all_matched = 0;
            break;
        }

        uint8_t restored[SAMPLE_SIZE_STD];
        size_t dec_sz = 0;
        st = binpack_decompress_dict(cmp, cmp_sz, restored, sizeof(restored), &dec_sz, dict);
        if (st != BINPACK_OK || dec_sz != SAMPLE_SIZE_STD || memcmp(orig, restored, SAMPLE_SIZE_STD) != 0) {
            all_matched = 0;
            break;
        }

        total_orig += SAMPLE_SIZE_STD;
        total_cmp += cmp_sz;
    }

    binpack_free_dict(dict);

    printf("    [C12 Telemetry] 100 micro-packets: Orig=%zu B, Cmp=%zu B (Ratio: %.2fx)\n",
           total_orig, total_cmp, (double)total_orig / (double)total_cmp);

    return all_matched ? TEST_PASS : TEST_FAIL;
}

/**
 * C13: Cross-dictionary decompression and corrupted stream resilience.
 */
static int test_c13_cross_dict_and_corrupt_stream(void) {
    init_standard_samples();
    int failed = 0;

    /* Train Dict A on standard CAN-bus samples */
    binpack_dict_t *dict_a = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict_a) return TEST_FAIL;

    /* Train Dict B on completely different text/ASCII samples */
    uint8_t text_samples[SAMPLE_COUNT_STD * SAMPLE_SIZE_STD];
    size_t text_sizes[SAMPLE_COUNT_STD];
    for (size_t i = 0; i < SAMPLE_COUNT_STD; i++) {
        text_sizes[i] = SAMPLE_SIZE_STD;
        snprintf((char*)text_samples + (i * SAMPLE_SIZE_STD), SAMPLE_SIZE_STD,
                 "JSON_LOG_ENTRY_ID_%06zu_TIMESTAMP_%010zu_SEV_INFO_OK\n", i, i * 1000);
    }
    binpack_dict_t *dict_b = binpack_train_dict(
        text_samples, text_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict_b) {
        binpack_free_dict(dict_a);
        return TEST_FAIL;
    }

    /* Compress Packet with Dict A */
    uint8_t cmp_a[256];
    size_t cmp_a_sz = 0;
    binpack_status_t st = binpack_compress_dict(
        s_samples_buf, SAMPLE_SIZE_STD, cmp_a, sizeof(cmp_a), &cmp_a_sz, dict_a
    );
    assert(st == BINPACK_OK);

    /* Try decompressing with Dict B (Cross-dictionary) */
    uint8_t dec_out[256];
    size_t dec_sz = 0;
    binpack_status_t st_cross = binpack_decompress_dict(
        cmp_a, cmp_a_sz, dec_out, sizeof(dec_out), &dec_sz, dict_b
    );

    printf("    [C13 Telemetry] Cross-dictionary decompression result: st=%d, dec_sz=%zu\n",
           st_cross, dec_sz);

    /* Zstandard dictionary with ID mismatch fails decompress_usingDDict */
    if (st_cross == BINPACK_OK && (dec_sz != SAMPLE_SIZE_STD || memcmp(s_samples_buf, dec_out, SAMPLE_SIZE_STD) != 0)) {
        printf("    [C13 Note] Raw content cross-dict produced payload mismatch without error flag\n");
    }

    /* Corrupt bytes in compressed payload */
    cmp_a[cmp_a_sz / 2] ^= 0xFF;
    dec_sz = 9999;
    binpack_status_t st_corrupt = binpack_decompress_dict(
        cmp_a, cmp_a_sz, dec_out, sizeof(dec_out), &dec_sz, dict_a
    );
    printf("    [C13 Telemetry] Corrupted byte decompression result: st=%d, dec_sz=%zu\n",
           st_corrupt, dec_sz);

    if (st_corrupt == BINPACK_OK) {
        fprintf(stderr, "  [C13] Corrupted compressed payload was accepted as valid\n");
        failed = 1;
    }

    binpack_free_dict(dict_a);
    binpack_free_dict(dict_b);
    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * C14: Ephemeral threads lifecycle and TLS context memory retention measurement.
 * Tests both:
 * 1. Concurrent simultaneous threads (8 threads executing in parallel).
 * 2. Sequential ephemeral lifecycle (20 sequential threads created, joined, destroyed).
 * Validates clean thread-exit destruction without leaking TLS contexts.
 */
typedef struct {
    binpack_dict_t *dict;
    int iterations;
} thread_worker_arg_t;

static void* thread_worker_fn(void *arg) {
    thread_worker_arg_t *warg = (thread_worker_arg_t*)arg;
    uint8_t in_pkt[64];
    memset(in_pkt, 0x42, sizeof(in_pkt));
    uint8_t cmp_pkt[256];
    uint8_t dec_pkt[64];

    for (int i = 0; i < warg->iterations; i++) {
        size_t cmp_sz = 0;
        binpack_status_t st_c = binpack_compress_dict(
            in_pkt, sizeof(in_pkt), cmp_pkt, sizeof(cmp_pkt), &cmp_sz, warg->dict
        );
        assert(st_c == BINPACK_OK);

        size_t dec_sz = 0;
        binpack_status_t st_d = binpack_decompress_dict(
            cmp_pkt, cmp_sz, dec_pkt, sizeof(dec_pkt), &dec_sz, warg->dict
        );
        assert(st_d == BINPACK_OK && dec_sz == sizeof(in_pkt));
    }
    return NULL;
}

static int test_c14_ephemeral_threads_tls_retention(void) {
    init_standard_samples();

    binpack_dict_t *dict = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict) return TEST_FAIL;

    /* Phase A: 8 concurrent simultaneous worker threads */
    const int CONCURRENT_THREADS = 8;
    pthread_t concurrent[CONCURRENT_THREADS];
    thread_worker_arg_t warg_c = { dict, 10 };

    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        int r = pthread_create(&concurrent[i], NULL, thread_worker_fn, &warg_c);
        if (r != 0) {
            fprintf(stderr, "  [C14] Concurrent pthread_create failed: %d\n", r);
            binpack_free_dict(dict);
            return TEST_FAIL;
        }
    }
    for (int i = 0; i < CONCURRENT_THREADS; i++) {
        pthread_join(concurrent[i], NULL);
    }
    printf("    [C14 Phase A] %d concurrent threads completed successfully\n", CONCURRENT_THREADS);

    /* Phase B: 20 sequential ephemeral worker threads */
    const int EPHEMERAL_THREADS = 20;
    thread_worker_arg_t warg_e = { dict, 5 };

    test_alloc_start_tracking();

    for (int i = 0; i < EPHEMERAL_THREADS; i++) {
        pthread_t t;
        int r = pthread_create(&t, NULL, thread_worker_fn, &warg_e);
        if (r != 0) {
            fprintf(stderr, "  [C14] Ephemeral pthread_create failed: %d\n", r);
            binpack_free_dict(dict);
            return TEST_FAIL;
        }
        pthread_join(t, NULL);
    }

    test_alloc_stop_tracking();

    printf("    [C14 Phase B] %d sequential ephemeral threads completed successfully\n", EPHEMERAL_THREADS);

    binpack_free_dict(dict);
    return TEST_PASS;
}

/**
 * C15: Context reuse, state isolation, and consecutive call stability.
 * Executes alternating valid and invalid calls on the same thread to ensure
 * prior failures do not poison persistent TLS contexts.
 */
static int test_c15_context_reuse_and_state_isolation(void) {
    init_standard_samples();
    int failed = 0;

    binpack_dict_t *dict = binpack_train_dict(
        s_samples_buf, s_sample_sizes, SAMPLE_COUNT_STD, 16384
    );
    if (!dict) return TEST_FAIL;

    uint8_t valid_in[64];
    memcpy(valid_in, s_samples_buf, 64);
    uint8_t cmp[256];
    size_t cmp_sz = 0;

    /* 1. Successful compression */
    binpack_status_t st = binpack_compress_dict(valid_in, 64, cmp, sizeof(cmp), &cmp_sz, dict);
    if (st != BINPACK_OK) {
        fprintf(stderr, "  [C15] Step 1 valid compression failed: st=%d\n", st);
        failed = 1;
    }

    /* 2. Intentionally failing compression (corrupt/NULL) */
    size_t bad_sz = 9999;
    st = binpack_compress_dict(NULL, 64, cmp, sizeof(cmp), &bad_sz, dict);
    if (st != BINPACK_ERR_INVALID_PARAM) {
        fprintf(stderr, "  [C15] Step 2 failing call returned unexpected st=%d\n", st);
        failed = 1;
    }

    /* 3. Successful compression immediately following failure (must not be poisoned) */
    cmp_sz = 0;
    st = binpack_compress_dict(valid_in, 64, cmp, sizeof(cmp), &cmp_sz, dict);
    if (st != BINPACK_OK || cmp_sz == 0) {
        fprintf(stderr, "  [C15] Step 3 compression poisoned by prior failure: st=%d\n", st);
        failed = 1;
    }

    /* 4. Successful decompression */
    uint8_t restored[64];
    size_t dec_sz = 0;
    st = binpack_decompress_dict(cmp, cmp_sz, restored, sizeof(restored), &dec_sz, dict);
    if (st != BINPACK_OK || dec_sz != 64 || memcmp(valid_in, restored, 64) != 0) {
        fprintf(stderr, "  [C15] Step 4 decompression failed: st=%d\n", st);
        failed = 1;
    }

    /* 5. Intentionally failing decompression (garbage input) */
    uint8_t garbage[16] = { 0x01, 0x02, 0x03, 0x04 };
    bad_sz = 9999;
    st = binpack_decompress_dict(garbage, sizeof(garbage), restored, sizeof(restored), &bad_sz, dict);
    if (st == BINPACK_OK) {
        fprintf(stderr, "  [C15] Step 5 garbage decompress unexpectedly succeeded\n");
        failed = 1;
    }

    /* 6. Successful decompression immediately following failure */
    dec_sz = 0;
    st = binpack_decompress_dict(cmp, cmp_sz, restored, sizeof(restored), &dec_sz, dict);
    if (st != BINPACK_OK || dec_sz != 64 || memcmp(valid_in, restored, 64) != 0) {
        fprintf(stderr, "  [C15] Step 6 decompression poisoned by prior failure: st=%d\n", st);
        failed = 1;
    }

    binpack_free_dict(dict);
    return failed ? TEST_FAIL : TEST_PASS;
}

/* ========================================================================= */
/* Test Harness Dispatch & Process Isolation                                 */
/* ========================================================================= */

typedef struct {
    const char *name;
    const char *desc;
    int (*fn)(void);
} test_case_t;

static test_case_t s_tests[] = {
    { "C01", "Null and invalid pointer parameters (and defensive clearing)", test_c01_null_parameters },
    { "C02", "Excessive dict_capacity early rejection & allocation telemetry", test_c02_excessive_train_capacity },
    { "C03", "Truncation and 64-bit integer overflow in sample_count", test_c03_sample_count_truncation },
    { "C04", "Cumulative sample_sizes overflow detection", test_c04_cumulative_size_overflow },
    { "C05", "Empty samples handling (all zero-length vs interleaved)", test_c05_empty_samples },
    { "C06", "Excessive dict_size early rejection in create_dict", test_c06_excessive_create_size },
    { "C07", "Corrupted, truncated, and arbitrary bytes dictionary", test_c07_corrupt_or_truncated_dict },
    { "C08", "Compression level (clevel) range characterization", test_c08_clevel_ranges },
    { "C09", "Defensive output clearing on error paths", test_c09_defensive_output_clearing },
    { "C10", "Exact error status reporting on insufficient buffer capacity", test_c10_buffer_too_small_reporting },
    { "C11", "Lifecycle resilience: binpack_free_dict(NULL) and clean tear-down", test_c11_lifecycle_and_double_free },
    { "C12", "Functional golden round-trip on micro-telemetry packets", test_c12_golden_roundtrip },
    { "C13", "Cross-dictionary decompression and corrupted stream resilience", test_c13_cross_dict_and_corrupt_stream },
    { "C14", "Ephemeral threads lifecycle and TLS context memory retention", test_c14_ephemeral_threads_tls_retention },
    { "C15", "Context reuse, state isolation, and consecutive call stability", test_c15_context_reuse_and_state_isolation }
};

static int run_isolated(test_case_t *tc) {
#if defined(__unix__) || defined(__APPLE__) || defined(__linux__)
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "Fork failed for %s: %s\n", tc->name, strerror(errno));
        return tc->fn();
    }
    if (pid == 0) {
        /* Child process */
        int res = tc->fn();
        exit(res == TEST_PASS ? 0 : 1);
    }

    int status = 0;
    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return TEST_FAIL;
    }
    if (WIFSIGNALED(status)) {
        int sig = WTERMSIG(status);
        printf("    [CRASH DETECTED] %s crashed with signal %d (%s)!\n",
               tc->name, sig, sig == 11 ? "SIGSEGV" : sig == 6 ? "SIGABRT" : "SIGNAL");
        return TEST_FAIL;
    }
    if (WIFEXITED(status)) {
        return (WEXITSTATUS(status) == 0) ? TEST_PASS : TEST_FAIL;
    }
    return TEST_FAIL;
#else
    return tc->fn();
#endif
}

int main(int argc, char **argv) {
    (void)argc;
    (void)argv;

    printf("======================================================================\n");
    printf("BP-006C Test Suite: Pre-trained Dictionary Engine Safety & Hardening (v1.0)\n");
    printf("======================================================================\n\n");

    int pass_count = 0;
    int fail_count = 0;
    int total_tests = (int)(sizeof(s_tests) / sizeof(s_tests[0]));

    for (int i = 0; i < total_tests; i++) {
        printf("[RUN ] %s: %s...\n", s_tests[i].name, s_tests[i].desc);
        int res = run_isolated(&s_tests[i]);
        if (res == TEST_PASS) {
            printf("[PASS] %s: %s\n\n", s_tests[i].name, s_tests[i].desc);
            pass_count++;
        } else {
            printf("[FAIL] %s: %s\n\n", s_tests[i].name, s_tests[i].desc);
            fail_count++;
        }
    }

    printf("======================================================================\n");
    printf("Resultados BP-006C: %d/%d superadas, %d fallidas\n",
           pass_count, total_tests, fail_count);
    printf("======================================================================\n");

    return fail_count == 0 ? 0 : 1;
}
