/**
 * @file test_patch_safety.c
 * @brief BinPack Enterprise SDK - BP-006B Binary Differential OTA Patching Safety & Hardening Suite
 *
 * Exhaustive adversarial and functional test suite for the Differential Patch Engine:
 * - B01: Null and invalid pointer parameters (with defensive clearing verification)
 * - B02: Truncated header physical bounds (< 40 bytes)
 * - B03: Invalid magic identifier verification
 * - B04: Versioning validation (Historical v1.0, current v2.x, rejection of unsupported/future)
 * - B05: Filter catalog validation (strict whitelist of supported filter IDs)
 * - B06: Empty compressed payload validation (0 payload bytes after header)
 * - B07: Huge new_sz DoS protection & zero-allocation early rejection
 * - B08: Zero new_sz rejection
 * - B09: Baseline size mismatch (old_sz mismatch)
 * - B10: Baseline CRC32 integrity mismatch (old_crc32 mismatch)
 * - B11: Trailing garbage / unconsumed payload rejection
 * - B12: Golden round-trip baseline (legitimate patches with and without filters)
 * - B13: Patch creation capacity calculation overflow guard
 * - B14: Unaligned header pointer resilience
 * - B15: Unknown flags and non-zero reserved bytes policy
 * - B16: Zstandard payload matrix:
 *        1. Two concatenated frames that together reconstruct expected new_sz
 *        2. First frame reconstructs expected new_sz followed by second frame
 *        3. Valid frame followed by trailing garbage
 *        4. Truncated single frame
 *        5. Single frame reconstructing less bytes than declared
 *        6. Single frame reconstructing more bytes than declared (oversized/bomb)
 * - B17: 64-bit integer overflow / 32-bit truncation bounds
 * - B18: Filter parameter validation per filter type
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>
#include <errno.h>

#include "binpack_sdk.h"
#include "../src/binpack_internal.h"
#include <zstd.h>

#if defined(_WIN32)
    #include <windows.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
#endif

#define TEST_PASS 0
#define TEST_FAIL 1

/* ========================================================================= */
/* Allocation Interception & Telemetry (under BINPACK_TESTING)               */
/* ========================================================================= */

#if defined(__linux__) && defined(BINPACK_TESTING)
extern void* __real_malloc(size_t size);
extern void __real_free(void *ptr);

static size_t s_test_alloc_count = 0;
static size_t s_test_last_alloc_sz = 0;
static size_t s_test_max_alloc_sz = 0;
static int s_test_fail_next_alloc = 0;
static int s_test_intercept_active = 0;

void* __wrap_malloc(size_t size) {
    if (s_test_intercept_active) {
        s_test_alloc_count++;
        s_test_last_alloc_sz = size;
        if (size > s_test_max_alloc_sz) {
            s_test_max_alloc_sz = size;
        }
        if (s_test_fail_next_alloc) {
            return NULL;
        }
        /* Safety ceiling: prevent host memory exhaustion if unhardened code requests >= 512 MiB */
        if (size > BINPACK_MAX_PATCH_SIZE) {
            return NULL;
        }
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
    s_test_fail_next_alloc = 0;
    s_test_intercept_active = 1;
}

static void test_alloc_stop_tracking(void) {
    s_test_intercept_active = 0;
}

static size_t test_alloc_get_count(void) {
    return s_test_alloc_count;
}

#else

static void test_alloc_start_tracking(void) {}
static void test_alloc_stop_tracking(void) {}
static size_t test_alloc_get_count(void) { return 0; }

#endif

/* ========================================================================= */
/* Sample Fixtures & Baseline Patch Generator                                */
/* ========================================================================= */

#define SAMPLE_OLD_SZ 4096
#define SAMPLE_NEW_SZ 8192

static uint8_t s_sample_old[SAMPLE_OLD_SZ];
static uint8_t s_sample_new[SAMPLE_NEW_SZ];
static int s_sample_initialized = 0;

static void init_sample_buffers(void) {
    if (s_sample_initialized) return;
    for (size_t i = 0; i < SAMPLE_OLD_SZ; i++) {
        s_sample_old[i] = (uint8_t)((i * 37 + 11) & 0xFF);
    }
    for (size_t i = 0; i < SAMPLE_NEW_SZ; i++) {
        if (i < 2048) {
            s_sample_new[i] = s_sample_old[i];
        } else {
            s_sample_new[i] = (uint8_t)((i * 59 + 23) & 0xFF);
        }
    }
    s_sample_initialized = 1;
}

static int generate_baseline_patch(
    binpack_filter_t filter,
    uint8_t **out_patch,
    size_t *out_patch_sz
) {
    init_sample_buffers();

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.filter = filter;
    if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        opt.transpose_stride = 8;
    }

    void *patch_buf = NULL;
    size_t patch_sz = 0;
    binpack_status_t st = binpack_create_patch(
        s_sample_old, SAMPLE_OLD_SZ,
        s_sample_new, SAMPLE_NEW_SZ,
        &patch_buf, &patch_sz,
        &opt
    );

    if (st != BINPACK_OK || !patch_buf || patch_sz < sizeof(binpack_patch_header_t)) {
        if (patch_buf) free(patch_buf);
        return 0;
    }

    *out_patch = (uint8_t*)patch_buf;
    *out_patch_sz = patch_sz;
    return 1;
}

/* ========================================================================= */
/* Subtest Implementations B01 - B18                                         */
/* ========================================================================= */

static int test_b01_null_args(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        fprintf(stderr, "  [B01] Failed to generate baseline patch\n");
        return TEST_FAIL;
    }

    void *out = (void*)0xDEADBEEF;
    size_t out_sz = 9999;
    int failed = 0;

    /* 1. NULL old_buf */
    binpack_status_t st = binpack_apply_patch(NULL, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_INVALID_PARAM) {
        fprintf(stderr, "  [B01] NULL old_buf returned %d, expected BINPACK_ERR_INVALID_PARAM\n", st);
        failed = 1;
    }
    if (out != NULL || out_sz != 0) {
        fprintf(stderr, "  [B01] NULL old_buf did not zero outputs: out=%p, sz=%zu\n", out, out_sz);
        failed = 1;
    }

    /* 2. zero old_sz */
    out = (void*)0xDEADBEEF; out_sz = 9999;
    st = binpack_apply_patch(s_sample_old, 0, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_INVALID_PARAM || out != NULL || out_sz != 0) {
        fprintf(stderr, "  [B01] zero old_sz returned %d, out=%p, sz=%zu\n", st, out, out_sz);
        failed = 1;
    }

    /* 3. NULL patch_buf */
    out = (void*)0xDEADBEEF; out_sz = 9999;
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, NULL, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_INVALID_PARAM || out != NULL || out_sz != 0) {
        fprintf(stderr, "  [B01] NULL patch_buf returned %d, out=%p, sz=%zu\n", st, out, out_sz);
        failed = 1;
    }

    /* 4. NULL new_out */
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, NULL, &out_sz);
    if (st != BINPACK_ERR_INVALID_PARAM) {
        fprintf(stderr, "  [B01] NULL new_out returned %d, expected BINPACK_ERR_INVALID_PARAM\n", st);
        failed = 1;
    }

    /* 5. NULL new_sz */
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM) {
        fprintf(stderr, "  [B01] NULL new_sz returned %d, expected BINPACK_ERR_INVALID_PARAM\n", st);
        failed = 1;
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b02_truncated_header(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    size_t truncated_sizes[] = { 0, 1, 10, 20, 39 };
    for (size_t i = 0; i < sizeof(truncated_sizes)/sizeof(truncated_sizes[0]); i++) {
        size_t tsz = truncated_sizes[i];
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, tsz, &out, &out_sz);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B02] Truncated header size %zu returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n",
                    tsz, st);
            failed = 1;
        }
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b03_invalid_magic(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    uint32_t bad_magics[] = { 0x00000000, 0x12345678, BINPACK_MAGIC_V1, BINPACK_MAGIC_V2, 0xFFFFFFFF };
    for (size_t i = 0; i < sizeof(bad_magics)/sizeof(bad_magics[0]); i++) {
        binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;
        binpack_store_le32((uint8_t*)&hdr->patch_magic, bad_magics[i]);

        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B03] Bad magic 0x%08X returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n",
                    bad_magics[i], st);
            failed = 1;
        }
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b04_versioning(void) {
    init_sample_buffers();
    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    /* Subcase 1: Independent historical v1.0 fixture without filter (raw Zstd + v1.0 header)
     * Completely independent of binpack_create_patch to verify true backwards compatibility. */
    {
        ZSTD_CCtx *v1_cctx = ZSTD_createCCtx();
        assert(v1_cctx);
        ZSTD_CCtx_refPrefix(v1_cctx, s_sample_old, SAMPLE_OLD_SZ);
        size_t cap = ZSTD_compressBound(SAMPLE_NEW_SZ) + 512;
        uint8_t *v1_payload = (uint8_t*)malloc(cap);
        assert(v1_payload);
        ZSTD_inBuffer in_v1 = { s_sample_new, SAMPLE_NEW_SZ, 0 };
        ZSTD_outBuffer out_v1 = { v1_payload, cap, 0 };
        size_t zres = ZSTD_compressStream2(v1_cctx, &out_v1, &in_v1, ZSTD_e_end);
        assert(!ZSTD_isError(zres) && zres == 0);
        ZSTD_freeCCtx(v1_cctx);

        size_t v1_patch_sz = sizeof(binpack_patch_header_t) + out_v1.pos;
        uint8_t *v1_patch = (uint8_t*)malloc(v1_patch_sz);
        assert(v1_patch);

        binpack_patch_header_t v1_hdr;
        memset(&v1_hdr, 0, sizeof(v1_hdr));
        binpack_store_le32((uint8_t*)&v1_hdr.patch_magic, BINPACK_MAGIC_PATCH);
        v1_hdr.version_major = 1;
        v1_hdr.version_minor = 0;
        binpack_store_le16((uint8_t*)&v1_hdr.flags, 0);
        binpack_store_le64((uint8_t*)&v1_hdr.old_sz, SAMPLE_OLD_SZ);
        binpack_store_le64((uint8_t*)&v1_hdr.new_sz, SAMPLE_NEW_SZ);
        binpack_store_le32((uint8_t*)&v1_hdr.old_crc32, binpack_crc32(0, s_sample_old, SAMPLE_OLD_SZ));
        binpack_store_le32((uint8_t*)&v1_hdr.new_crc32, binpack_crc32(0, s_sample_new, SAMPLE_NEW_SZ));
        v1_hdr.filter_id = (uint8_t)BINPACK_FILTER_NONE;
        binpack_store_le16((uint8_t*)&v1_hdr.filter_param, 0);
        memset(v1_hdr.reserved, 0, sizeof(v1_hdr.reserved));

        memcpy(v1_patch, &v1_hdr, sizeof(v1_hdr));
        memcpy(v1_patch + sizeof(v1_hdr), v1_payload, out_v1.pos);
        free(v1_payload);

        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, v1_patch, v1_patch_sz, &out, &out_sz);
        if (st != BINPACK_OK || out_sz != SAMPLE_NEW_SZ || memcmp(out, s_sample_new, SAMPLE_NEW_SZ) != 0) {
            fprintf(stderr, "  [B04] Historical v1.0 (no filter) fixture failed: st=%d, out_sz=%zu\n", st, out_sz);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }
        free(v1_patch);
    }

    /* Subcase 1b: Dedicated raw historical v1.0 fixture with BCJ architectural filter
     * Completely independent of binpack_create_patch. */
    {
        uint8_t old_bcj[SAMPLE_OLD_SZ];
        uint8_t new_bcj[SAMPLE_NEW_SZ];
        memcpy(old_bcj, s_sample_old, SAMPLE_OLD_SZ);
        memcpy(new_bcj, s_sample_new, SAMPLE_NEW_SZ);
        binpack_bcj_x86_filter(old_bcj, SAMPLE_OLD_SZ);
        binpack_bcj_x86_filter(new_bcj, SAMPLE_NEW_SZ);

        ZSTD_CCtx *v1_cctx = ZSTD_createCCtx();
        assert(v1_cctx);
        ZSTD_CCtx_refPrefix(v1_cctx, old_bcj, SAMPLE_OLD_SZ);
        size_t cap = ZSTD_compressBound(SAMPLE_NEW_SZ) + 512;
        uint8_t *v1_payload = (uint8_t*)malloc(cap);
        assert(v1_payload);
        ZSTD_inBuffer in_v1 = { new_bcj, SAMPLE_NEW_SZ, 0 };
        ZSTD_outBuffer out_v1 = { v1_payload, cap, 0 };
        size_t zres = ZSTD_compressStream2(v1_cctx, &out_v1, &in_v1, ZSTD_e_end);
        assert(!ZSTD_isError(zres) && zres == 0);
        ZSTD_freeCCtx(v1_cctx);

        size_t v1_patch_sz = sizeof(binpack_patch_header_t) + out_v1.pos;
        uint8_t *v1_patch = (uint8_t*)malloc(v1_patch_sz);
        assert(v1_patch);

        binpack_patch_header_t v1_hdr;
        memset(&v1_hdr, 0, sizeof(v1_hdr));
        binpack_store_le32((uint8_t*)&v1_hdr.patch_magic, BINPACK_MAGIC_PATCH);
        v1_hdr.version_major = 1;
        v1_hdr.version_minor = 0;
        binpack_store_le16((uint8_t*)&v1_hdr.flags, 0);
        binpack_store_le64((uint8_t*)&v1_hdr.old_sz, SAMPLE_OLD_SZ);
        binpack_store_le64((uint8_t*)&v1_hdr.new_sz, SAMPLE_NEW_SZ);
        binpack_store_le32((uint8_t*)&v1_hdr.old_crc32, binpack_crc32(0, s_sample_old, SAMPLE_OLD_SZ));
        binpack_store_le32((uint8_t*)&v1_hdr.new_crc32, binpack_crc32(0, s_sample_new, SAMPLE_NEW_SZ));
        v1_hdr.filter_id = (uint8_t)BINPACK_FILTER_X86_64;
        binpack_store_le16((uint8_t*)&v1_hdr.filter_param, 0);
        memset(v1_hdr.reserved, 0, sizeof(v1_hdr.reserved));

        memcpy(v1_patch, &v1_hdr, sizeof(v1_hdr));
        memcpy(v1_patch + sizeof(v1_hdr), v1_payload, out_v1.pos);
        free(v1_payload);

        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, v1_patch, v1_patch_sz, &out, &out_sz);
        if (st != BINPACK_OK || out_sz != SAMPLE_NEW_SZ || memcmp(out, s_sample_new, SAMPLE_NEW_SZ) != 0) {
            fprintf(stderr, "  [B04] Historical v1.0 BCJ fixture failed: st=%d, out_sz=%zu\n", st, out_sz);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }
        free(v1_patch);
    }

    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }
    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;

    /* Subcase 2: Enterprise format v2.x (current) MUST be accepted */
    hdr->version_major = BINPACK_SDK_VERSION_MAJOR;
    hdr->version_minor = BINPACK_SDK_VERSION_MINOR;
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_OK || out_sz != SAMPLE_NEW_SZ || memcmp(out, s_sample_new, SAMPLE_NEW_SZ) != 0) {
        fprintf(stderr, "  [B04] Current v2.x patch failed: st=%d, out_sz=%zu\n", st, out_sz);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 3: Unsupported version_major = 0 */
    hdr->version_major = 0;
    hdr->version_minor = 0;
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B04] version_major=0 returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 4: Future version_major = 3 */
    hdr->version_major = 3;
    hdr->version_minor = 0;
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B04] Future version_major=3 returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 5: Future minor for v1 (v1.1) must be rejected */
    hdr->version_major = 1;
    hdr->version_minor = 1;
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B04] v1.1 returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 6: Future minor for v2 (v2.99) must be rejected */
    hdr->version_major = 2;
    hdr->version_minor = 99;
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B04] v2.99 returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b05_invalid_filters(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;

    uint8_t bad_filters[] = { 9, 10, 42, 0x7F, 0xFF };
    for (size_t i = 0; i < sizeof(bad_filters)/sizeof(bad_filters[0]); i++) {
        hdr->filter_id = bad_filters[i];
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);

        if (st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B05] Invalid filter_id=%u returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n",
                    bad_filters[i], st);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b06_empty_payload(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    /* Truncate patch to exactly the header size: cmp_payload_sz == 0 */
    size_t header_only_sz = sizeof(binpack_patch_header_t);
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, header_only_sz, &out, &out_sz);

    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B06] Empty payload returned %d (expected early BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b07_huge_new_sz_dos(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;

    /* Set new_sz to 600 MiB (exceeds 512 MiB budget) */
    binpack_store_le64((uint8_t*)&hdr->new_sz, 600ULL * 1024ULL * 1024ULL);

    test_alloc_start_tracking();
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    test_alloc_stop_tracking();

    size_t alloc_count = test_alloc_get_count();

    if (alloc_count > 0) {
        fprintf(stderr, "  [B07] Huge new_sz attempted %zu allocations before bounds check!\n", alloc_count);
        failed = 1;
    }

    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B07] Huge new_sz returned %d (expected early BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }

    if (out) { free(out); out = NULL; }
    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b08_zero_new_sz(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;
    binpack_store_le64((uint8_t*)&hdr->new_sz, 0);

    test_alloc_start_tracking();
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    test_alloc_stop_tracking();

    if (test_alloc_get_count() > 0) {
        fprintf(stderr, "  [B08] new_sz=0 attempted %zu allocations!\n", test_alloc_get_count());
        failed = 1;
    }

    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B08] new_sz=0 returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }

    if (out) { free(out); out = NULL; }
    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b09_old_sz_mismatch(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ + 1, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_PATCH_MISMATCH) {
        fprintf(stderr, "  [B09] old_sz mismatch returned %d, expected BINPACK_ERR_PATCH_MISMATCH (-10)\n", st);
        failed = 1;
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b10_old_crc_mismatch(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    uint8_t corrupted_old[SAMPLE_OLD_SZ];
    memcpy(corrupted_old, s_sample_old, SAMPLE_OLD_SZ);
    corrupted_old[0] ^= 0xFF;

    binpack_status_t st = binpack_apply_patch(corrupted_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_PATCH_MISMATCH) {
        fprintf(stderr, "  [B10] old_crc mismatch returned %d, expected BINPACK_ERR_PATCH_MISMATCH (-10)\n", st);
        failed = 1;
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b11_trailing_garbage(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    size_t garbage_sz = 16;
    size_t extended_sz = patch_sz + garbage_sz;
    uint8_t *extended_patch = (uint8_t*)malloc(extended_sz);
    assert(extended_patch);
    memcpy(extended_patch, patch, patch_sz);
    memset(extended_patch + patch_sz, 0xAA, garbage_sz);

    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, extended_patch, extended_sz, &out, &out_sz);

    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B11] Trailing garbage returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    free(extended_patch);
    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b12_roundtrip_golden(void) {
    init_sample_buffers();

    binpack_filter_t test_filters[] = {
        BINPACK_FILTER_NONE,
        BINPACK_FILTER_X86_64,
        BINPACK_FILTER_AARCH64,
        BINPACK_FILTER_TEXTURE_BCN
    };

    int failed = 0;
    for (size_t i = 0; i < sizeof(test_filters)/sizeof(test_filters[0]); i++) {
        binpack_filter_t f = test_filters[i];
        uint8_t *patch = NULL;
        size_t patch_sz = 0;
        if (!generate_baseline_patch(f, &patch, &patch_sz)) {
            fprintf(stderr, "  [B12] Failed to generate baseline patch for filter %d\n", f);
            failed = 1;
            continue;
        }

        void *out = NULL;
        size_t out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);

        if (st != BINPACK_OK) {
            fprintf(stderr, "  [B12] Golden roundtrip failed for filter %d with status %d\n", f, st);
            failed = 1;
        } else if (out_sz != SAMPLE_NEW_SZ) {
            fprintf(stderr, "  [B12] Output size mismatch: got %zu, expected %zu\n", out_sz, (size_t)SAMPLE_NEW_SZ);
            failed = 1;
        } else if (memcmp(out, s_sample_new, SAMPLE_NEW_SZ) != 0) {
            fprintf(stderr, "  [B12] Reconstructed content mismatch for filter %d\n", f);
            failed = 1;
        }

        if (out) free(out);
        free(patch);
    }

    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b13_create_capacity_overflow(void) {
    init_sample_buffers();

    void *patch_out = (void*)0xDEADBEEF;
    size_t patch_sz = 9999;
    int failed = 0;

    /* Subcase 1: zero old_sz */
    binpack_status_t st = binpack_create_patch(s_sample_old, 0, s_sample_new, SAMPLE_NEW_SZ, &patch_out, &patch_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM || patch_out != NULL || patch_sz != 0) {
        fprintf(stderr, "  [B13] create_patch with old_sz=0 returned %d, out=%p, sz=%zu\n", st, patch_out, patch_sz);
        failed = 1;
    }

    /* Subcase 2: zero new_sz */
    patch_out = (void*)0xDEADBEEF; patch_sz = 9999;
    st = binpack_create_patch(s_sample_old, SAMPLE_OLD_SZ, s_sample_new, 0, &patch_out, &patch_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM || patch_out != NULL || patch_sz != 0) {
        fprintf(stderr, "  [B13] create_patch with new_sz=0 returned %d, out=%p, sz=%zu\n", st, patch_out, patch_sz);
        failed = 1;
    }

    /* Subcase 3: check capacity bound guard on new_sz (> 512 MiB limit) - must reject cleanly before reading buffer */
    patch_out = (void*)0xDEADBEEF; patch_sz = 9999;
    size_t huge_new_sz = 600ULL * 1024ULL * 1024ULL;
    st = binpack_create_patch(s_sample_old, SAMPLE_OLD_SZ, s_sample_new, huge_new_sz, &patch_out, &patch_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM || patch_out != NULL || patch_sz != 0) {
        fprintf(stderr, "  [B13] create_patch with huge new_sz returned %d, out=%p, sz=%zu\n", st, patch_out, patch_sz);
        failed = 1;
    }

    /* Subcase 4: check old_sz > 512 MiB limit */
    patch_out = (void*)0xDEADBEEF; patch_sz = 9999;
    size_t huge_old_sz = 600ULL * 1024ULL * 1024ULL;
    st = binpack_create_patch(s_sample_old, huge_old_sz, s_sample_new, SAMPLE_NEW_SZ, &patch_out, &patch_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM || patch_out != NULL || patch_sz != 0) {
        fprintf(stderr, "  [B13] create_patch with huge old_sz returned %d, out=%p, sz=%zu\n", st, patch_out, patch_sz);
        failed = 1;
    }

    /* Subcase 5: check arithmetic overflow value near SIZE_MAX */
    patch_out = (void*)0xDEADBEEF; patch_sz = 9999;
    size_t overflow_sz = (size_t)-1 - 100;
    st = binpack_create_patch(s_sample_old, SAMPLE_OLD_SZ, s_sample_new, overflow_sz, &patch_out, &patch_sz, NULL);
    if (st != BINPACK_ERR_INVALID_PARAM || patch_out != NULL || patch_sz != 0) {
        fprintf(stderr, "  [B13] create_patch with overflow new_sz returned %d, out=%p, sz=%zu\n", st, patch_out, patch_sz);
        failed = 1;
    }

    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b14_unaligned_header(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    uint8_t *unaligned_buf = (uint8_t*)malloc(patch_sz + 16);
    assert(unaligned_buf);

    int failed = 0;
    int offsets[] = { 1, 3 };
    for (int i = 0; i < 2; i++) {
        uint8_t *target_ptr = unaligned_buf + offsets[i];
        memcpy(target_ptr, patch, patch_sz);

        void *out = NULL;
        size_t out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, target_ptr, patch_sz, &out, &out_sz);
        if (st != BINPACK_OK) {
            fprintf(stderr, "  [B14] Unaligned pointer offset +%d failed with status %d\n", offsets[i], st);
            failed = 1;
        } else if (memcmp(out, s_sample_new, SAMPLE_NEW_SZ) != 0) {
            fprintf(stderr, "  [B14] Unaligned output mismatch for offset +%d\n", offsets[i]);
            failed = 1;
        }
        if (out) free(out);
    }

    free(unaligned_buf);
    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b15_flags_and_reserved(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;

    /* Subcase 1: Unknown flags = 0x8000 */
    binpack_store_le16((uint8_t*)&hdr->flags, 0x8000);
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B15] Unknown flags returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 2: Non-zero reserved bytes */
    binpack_store_le16((uint8_t*)&hdr->flags, 0);
    hdr->reserved[0] = 0xAA;
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B15] Non-zero reserved returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

/**
 * @brief B16: Exhaustive Zstandard Payload Matrix
 *
 * Verifies strict single-frame contract:
 * 1. Two concatenated frames that together reconstruct expected new_sz -> REJECTED
 * 2. First frame reconstructs expected new_sz followed by second frame -> REJECTED
 * 3. Valid frame followed by trailing garbage -> REJECTED
 * 4. Truncated single frame -> REJECTED
 * 5. Single frame reconstructing less bytes than declared -> REJECTED
 * 6. Single frame reconstructing more bytes than declared (oversized/bomb) -> REJECTED
 */
static int test_b16_zstd_payload_matrix(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    /* --------------------------------------------------------------------- */
    /* Case 1: Two concatenated frames that TOGETHER reconstruct new_sz      */
    /* --------------------------------------------------------------------- */
    {
        /* Split new payload into Part A (first 4096 bytes) and Part B (next 4096 bytes) */
        size_t part_a_sz = SAMPLE_NEW_SZ / 2;
        size_t part_b_sz = SAMPLE_NEW_SZ - part_a_sz;

        /* Compress Part A with dictionary (old_buf) */
        ZSTD_CCtx *cctx_a = ZSTD_createCCtx();
        assert(cctx_a);
        ZSTD_CCtx_refPrefix(cctx_a, s_sample_old, SAMPLE_OLD_SZ);
        size_t cap_a = ZSTD_compressBound(part_a_sz) + 512;
        uint8_t *frame_a = (uint8_t*)malloc(cap_a);
        assert(frame_a);
        ZSTD_inBuffer in_a = { s_sample_new, part_a_sz, 0 };
        ZSTD_outBuffer out_a = { frame_a, cap_a, 0 };
        size_t zres_a = ZSTD_compressStream2(cctx_a, &out_a, &in_a, ZSTD_e_end);
        assert(!ZSTD_isError(zres_a) && zres_a == 0);
        size_t frame_a_sz = out_a.pos;
        ZSTD_freeCCtx(cctx_a);

        /* Compress Part B standalone */
        size_t cap_b = ZSTD_compressBound(part_b_sz) + 512;
        uint8_t *frame_b = (uint8_t*)malloc(cap_b);
        assert(frame_b);
        size_t frame_b_sz = ZSTD_compress(frame_b, cap_b, s_sample_new + part_a_sz, part_b_sz, 1);
        assert(!ZSTD_isError(frame_b_sz) && frame_b_sz > 0);

        /* Construct patch with frame_a + frame_b */
        size_t two_frame_patch_sz = sizeof(binpack_patch_header_t) + frame_a_sz + frame_b_sz;
        uint8_t *two_frame_patch = (uint8_t*)malloc(two_frame_patch_sz);
        assert(two_frame_patch);

        binpack_patch_header_t hdr;
        memset(&hdr, 0, sizeof(hdr));
        binpack_store_le32((uint8_t*)&hdr.patch_magic, BINPACK_MAGIC_PATCH);
        hdr.version_major = BINPACK_SDK_VERSION_MAJOR;
        hdr.version_minor = BINPACK_SDK_VERSION_MINOR;
        binpack_store_le64((uint8_t*)&hdr.old_sz, SAMPLE_OLD_SZ);
        binpack_store_le64((uint8_t*)&hdr.new_sz, SAMPLE_NEW_SZ);
        binpack_store_le32((uint8_t*)&hdr.old_crc32, binpack_crc32(0, s_sample_old, SAMPLE_OLD_SZ));
        binpack_store_le32((uint8_t*)&hdr.new_crc32, binpack_crc32(0, s_sample_new, SAMPLE_NEW_SZ));
        hdr.filter_id = (uint8_t)BINPACK_FILTER_NONE;

        memcpy(two_frame_patch, &hdr, sizeof(hdr));
        memcpy(two_frame_patch + sizeof(hdr), frame_a, frame_a_sz);
        memcpy(two_frame_patch + sizeof(hdr) + frame_a_sz, frame_b, frame_b_sz);

        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, two_frame_patch, two_frame_patch_sz, &out, &out_sz);
        /* MUST REJECT: first frame finishes at 4096 bytes; single-frame stops; size mismatch! */
        if (st == BINPACK_OK) {
            fprintf(stderr, "  [B16] Two concatenated frames reconstructing new_sz accepted with BINPACK_OK!\n");
            failed = 1;
        } else if (st != BINPACK_ERR_DECOMPRESSION_FAILED && st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B16] Two concatenated frames returned unexpected status %d\n", st);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }

        free(two_frame_patch);
        free(frame_a);
        free(frame_b);
    }

    /* --------------------------------------------------------------------- */
    /* Case 2: First frame reconstructs new_sz, followed by second frame     */
    /* --------------------------------------------------------------------- */
    {
        size_t cap_extra = ZSTD_compressBound(128) + 128;
        uint8_t *extra_frame = (uint8_t*)malloc(cap_extra);
        assert(extra_frame);
        uint8_t dummy[128] = { 42 };
        size_t extra_sz = ZSTD_compress(extra_frame, cap_extra, dummy, sizeof(dummy), 1);
        assert(!ZSTD_isError(extra_sz));

        size_t concat_sz = patch_sz + extra_sz;
        uint8_t *concat_patch = (uint8_t*)malloc(concat_sz);
        assert(concat_patch);
        memcpy(concat_patch, patch, patch_sz);
        memcpy(concat_patch + patch_sz, extra_frame, extra_sz);

        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, concat_patch, concat_sz, &out, &out_sz);
        /* MUST REJECT: first frame completes new_sz, but unconsumed bytes remain! */
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B16] Frame followed by 2nd frame returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }

        free(concat_patch);
        free(extra_frame);
    }

    /* --------------------------------------------------------------------- */
    /* Case 3: Valid frame followed by trailing garbage                      */
    /* --------------------------------------------------------------------- */
    {
        size_t junk_sz = 24;
        size_t junk_patch_sz = patch_sz + junk_sz;
        uint8_t *junk_patch = (uint8_t*)malloc(junk_patch_sz);
        assert(junk_patch);
        memcpy(junk_patch, patch, patch_sz);
        memset(junk_patch + patch_sz, 0xEF, junk_sz);

        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, junk_patch, junk_patch_sz, &out, &out_sz);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B16] Frame followed by garbage returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }
        free(junk_patch);
    }

    /* --------------------------------------------------------------------- */
    /* Case 4: Truncated single frame                                        */
    /* --------------------------------------------------------------------- */
    {
        if (patch_sz > sizeof(binpack_patch_header_t) + 8) {
            size_t trunc_sz = patch_sz - 8;
            out = NULL; out_sz = 0;
            binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, trunc_sz, &out, &out_sz);
            if (st == BINPACK_OK) {
                fprintf(stderr, "  [B16] Truncated Zstd payload returned BINPACK_OK!\n");
                failed = 1;
            } else if (st != BINPACK_ERR_DECOMPRESSION_FAILED && st != BINPACK_ERR_CORRUPT_DATA) {
                fprintf(stderr, "  [B16] Truncated payload returned unexpected status %d\n", st);
                failed = 1;
            }
            if (out) { free(out); out = NULL; }
        }
    }

    /* --------------------------------------------------------------------- */
    /* Case 5: Single frame reconstructing less bytes than declared          */
    /* --------------------------------------------------------------------- */
    {
        binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;
        binpack_store_le64((uint8_t*)&hdr->new_sz, SAMPLE_NEW_SZ + 1024);
        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
        if (st == BINPACK_OK) {
            fprintf(stderr, "  [B16] Undersized stream returned BINPACK_OK!\n");
            failed = 1;
        } else if (st != BINPACK_ERR_DECOMPRESSION_FAILED) {
            fprintf(stderr, "  [B16] Undersized stream returned %d (expected BINPACK_ERR_DECOMPRESSION_FAILED -4)\n", st);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }
        /* Restore header */
        binpack_store_le64((uint8_t*)&hdr->new_sz, SAMPLE_NEW_SZ);
    }

    /* --------------------------------------------------------------------- */
    /* Case 6: Single frame reconstructing more bytes than declared (bomb)   */
    /* --------------------------------------------------------------------- */
    {
        binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;
        binpack_store_le64((uint8_t*)&hdr->new_sz, SAMPLE_NEW_SZ / 2); /* Declare half size */
        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
        if (st == BINPACK_OK) {
            fprintf(stderr, "  [B16] Oversized stream returned BINPACK_OK!\n");
            failed = 1;
        } else if (st != BINPACK_ERR_DECOMPRESSION_FAILED && st != BINPACK_ERR_CORRUPT_DATA) {
            fprintf(stderr, "  [B16] Oversized stream returned %d\n", st);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }
    }

    /* --------------------------------------------------------------------- */
    /* Case 7: Frame requiring epilogue consumption after output is full     */
    /* (Forces ZSTD_c_checksumFlag=1 to append a 4-byte CRC epilogue)        */
    /* --------------------------------------------------------------------- */
    {
        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        assert(cctx);
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
        ZSTD_CCtx_refPrefix(cctx, s_sample_old, SAMPLE_OLD_SZ);

        size_t cap = ZSTD_compressBound(SAMPLE_NEW_SZ) + 512;
        uint8_t *frame = (uint8_t*)malloc(cap);
        assert(frame);

        ZSTD_inBuffer in = { s_sample_new, SAMPLE_NEW_SZ, 0 };
        ZSTD_outBuffer out_b = { frame, cap, 0 };
        size_t zres = ZSTD_compressStream2(cctx, &out_b, &in, ZSTD_e_end);
        assert(!ZSTD_isError(zres) && zres == 0);
        size_t frame_sz = out_b.pos;
        ZSTD_freeCCtx(cctx);

        size_t chk_patch_sz = sizeof(binpack_patch_header_t) + frame_sz;
        uint8_t *chk_patch = (uint8_t*)malloc(chk_patch_sz);
        assert(chk_patch);

        binpack_patch_header_t chk_hdr;
        memset(&chk_hdr, 0, sizeof(chk_hdr));
        binpack_store_le32((uint8_t*)&chk_hdr.patch_magic, BINPACK_MAGIC_PATCH);
        chk_hdr.version_major = BINPACK_SDK_VERSION_MAJOR;
        chk_hdr.version_minor = BINPACK_SDK_VERSION_MINOR;
        binpack_store_le64((uint8_t*)&chk_hdr.old_sz, SAMPLE_OLD_SZ);
        binpack_store_le64((uint8_t*)&chk_hdr.new_sz, SAMPLE_NEW_SZ);
        binpack_store_le32((uint8_t*)&chk_hdr.old_crc32, binpack_crc32(0, s_sample_old, SAMPLE_OLD_SZ));
        binpack_store_le32((uint8_t*)&chk_hdr.new_crc32, binpack_crc32(0, s_sample_new, SAMPLE_NEW_SZ));
        chk_hdr.filter_id = (uint8_t)BINPACK_FILTER_NONE;

        memcpy(chk_patch, &chk_hdr, sizeof(chk_hdr));
        memcpy(chk_patch + sizeof(chk_hdr), frame, frame_sz);

        out = NULL; out_sz = 0;
        binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, chk_patch, chk_patch_sz, &out, &out_sz);
        if (st != BINPACK_OK || out_sz != SAMPLE_NEW_SZ || memcmp(out, s_sample_new, SAMPLE_NEW_SZ) != 0) {
            fprintf(stderr, "  [B16] Frame with checksum epilogue failed: st=%d, out_sz=%zu\n", st, out_sz);
            failed = 1;
        }
        if (out) { free(out); out = NULL; }

        free(chk_patch);
        free(frame);
    }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b17_old_new_sz_overflow_32bit(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;

    /* Subcase 1: hdr->old_sz has high 32 bits set (> 4 GiB) */
    binpack_store_le64((uint8_t*)&hdr->old_sz, 0x100000000ULL + SAMPLE_OLD_SZ);
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_PATCH_MISMATCH && st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B17] 64-bit old_sz mismatch returned %d\n", st);
        failed = 1;
    }

    /* Subcase 2: hdr->new_sz has high 32 bits set (> 4 GiB) */
    binpack_store_le64((uint8_t*)&hdr->old_sz, SAMPLE_OLD_SZ);
    binpack_store_le64((uint8_t*)&hdr->new_sz, 0x100000000ULL + SAMPLE_NEW_SZ);

    test_alloc_start_tracking();
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    test_alloc_stop_tracking();

    if (test_alloc_get_count() > 0) {
        fprintf(stderr, "  [B17] High 32-bit new_sz attempted %zu allocations before bounds check!\n", test_alloc_get_count());
        failed = 1;
    }
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B17] High 32-bit new_sz returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }

    if (out) { free(out); out = NULL; }
    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

static int test_b18_filter_params_validation(void) {
    init_sample_buffers();
    uint8_t *patch = NULL;
    size_t patch_sz = 0;
    if (!generate_baseline_patch(BINPACK_FILTER_NONE, &patch, &patch_sz)) {
        return TEST_FAIL;
    }

    void *out = NULL;
    size_t out_sz = 0;
    int failed = 0;

    binpack_patch_header_t *hdr = (binpack_patch_header_t*)patch;

    /* Subcase 1: BINPACK_FILTER_NONE with non-zero filter_param */
    hdr->filter_id = (uint8_t)BINPACK_FILTER_NONE;
    binpack_store_le16((uint8_t*)&hdr->filter_param, 99);
    binpack_status_t st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B18] FILTER_NONE with non-zero filter_param returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 2: BINPACK_FILTER_TEXTURE_BCN with invalid filter_param (e.g. 12, not 8 or 16) */
    hdr->filter_id = (uint8_t)BINPACK_FILTER_TEXTURE_BCN;
    binpack_store_le16((uint8_t*)&hdr->filter_param, 12);
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B18] TEXTURE_BCN with invalid stride returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    /* Subcase 3: Architectural filter (x86_64) with non-zero filter_param */
    hdr->filter_id = (uint8_t)BINPACK_FILTER_X86_64;
    binpack_store_le16((uint8_t*)&hdr->filter_param, 42);
    st = binpack_apply_patch(s_sample_old, SAMPLE_OLD_SZ, patch, patch_sz, &out, &out_sz);
    if (st != BINPACK_ERR_CORRUPT_DATA) {
        fprintf(stderr, "  [B18] x86_64 filter with non-zero filter_param returned %d (expected BINPACK_ERR_CORRUPT_DATA -5)\n", st);
        failed = 1;
    }
    if (out) { free(out); out = NULL; }

    free(patch);
    return failed ? TEST_FAIL : TEST_PASS;
}

/* ========================================================================= */
/* Test Runner with Process Isolation                                        */
/* ========================================================================= */

typedef struct {
    const char *name;
    const char *desc;
    int (*fn)(void);
} test_case_t;

static test_case_t s_tests[] = {
    { "B01", "Null and invalid pointer parameters", test_b01_null_args },
    { "B02", "Truncated header physical bounds (< 40 bytes)", test_b02_truncated_header },
    { "B03", "Invalid magic identifier verification", test_b03_invalid_magic },
    { "B04", "Versioning validation (Major/Minor policies)", test_b04_versioning },
    { "B05", "Filter catalog validation (unsupported/invalid filter IDs)", test_b05_invalid_filters },
    { "B06", "Empty compressed payload validation (0 payload bytes after header)", test_b06_empty_payload },
    { "B07", "Huge new_sz DoS protection & zero-allocation early rejection", test_b07_huge_new_sz_dos },
    { "B08", "Zero new_sz rejection", test_b08_zero_new_sz },
    { "B09", "Baseline size mismatch (old_sz mismatch)", test_b09_old_sz_mismatch },
    { "B10", "Baseline CRC32 integrity mismatch (old_crc32 mismatch)", test_b10_old_crc_mismatch },
    { "B11", "Trailing garbage / unconsumed payload rejection", test_b11_trailing_garbage },
    { "B12", "Golden round-trip baseline (legitimate patches with and without filters)", test_b12_roundtrip_golden },
    { "B13", "Patch creation capacity calculation overflow guard", test_b13_create_capacity_overflow },
    { "B14", "Unaligned header pointer resilience", test_b14_unaligned_header },
    { "B15", "Unknown flags and non-zero reserved bytes policy", test_b15_flags_and_reserved },
    { "B16", "Zstandard payload matrix (concatenated frames, trailing, truncated, undersized, bomb)", test_b16_zstd_payload_matrix },
    { "B17", "64-bit integer overflow / 32-bit truncation bounds", test_b17_old_new_sz_overflow_32bit },
    { "B18", "Filter parameter validation per filter type", test_b18_filter_params_validation },
};

static int run_isolated(test_case_t *tc) {
#if !defined(_WIN32)
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) {
        return tc->fn();
    }
    if (pid == 0) {
        int res = tc->fn();
        fflush(stdout);
        fflush(stderr);
        _exit(res == TEST_PASS ? 0 : 1);
    }
    int status = 0;
    waitpid(pid, &status, 0);
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
    printf("BP-006B Test Suite: Differential OTA Patching Safety & Hardening (v1.2)\n");
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
    printf("Resultados BP-006B: %d/%d superadas, %d fallidas\n",
           pass_count, total_tests, fail_count);
    printf("======================================================================\n");

    return (fail_count > 0) ? 1 : 0;
}
