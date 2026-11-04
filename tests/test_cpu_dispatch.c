#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>

#include "../src/binpack_internal.h"

static uint32_t xorshift32(uint32_t *state) {
    uint32_t x = *state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    *state = x;
    return x;
}

static void fill_buffer(uint8_t *buf, size_t size, uint32_t seed) {
    uint32_t state = seed;
    for (size_t i = 0; i < size; i++) {
        buf[i] = (uint8_t)(xorshift32(&state) & 0xFF);
    }
}

static int test_bcj_aarch64_equivalence(void) {
    printf("[DISPATCH-TEST] BCJ AArch64 Scalar vs AVX2 bit-for-bit equivalence...\n");
    const size_t sizes[] = { 16, 32, 48, 64, 128, 256, 1024, 4096, 65536 };
    const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (size_t s = 0; s < num_sizes; s++) {
        size_t size = sizes[s];
        uint8_t *orig = (uint8_t *)malloc(size);
        uint8_t *buf_scalar = (uint8_t *)malloc(size);
        uint8_t *buf_avx2 = (uint8_t *)malloc(size);
        assert(orig && buf_scalar && buf_avx2);

        fill_buffer(orig, size, (uint32_t)(0x12345678 + size));
        /* Inject some ARM64 B and BL instructions */
        for (size_t i = 0; i + 4 <= size; i += 8) {
            uint32_t instr = (i % 16 == 0) ? 0x14000000 : 0x94000000;
            instr |= (uint32_t)((i / 4) & 0x03FFFFFF);
            orig[i + 0] = (uint8_t)(instr & 0xFF);
            orig[i + 1] = (uint8_t)((instr >> 8) & 0xFF);
            orig[i + 2] = (uint8_t)((instr >> 16) & 0xFF);
            orig[i + 3] = (uint8_t)((instr >> 24) & 0xFF);
        }

        /* 1. Compare Scalar Filter vs AVX2 Filter */
        memcpy(buf_scalar, orig, size);
        memcpy(buf_avx2, orig, size);
        bp_bcj_aarch64_filter_scalar(buf_scalar, size);
#if (defined(__x86_64__) || defined(_M_X64))
        if (bp_cpu_dispatch_get_active_backend() == BINPACK_BACKEND_AVX2) {
            bp_bcj_aarch64_filter_avx2(buf_avx2, size);
            if (memcmp(buf_scalar, buf_avx2, size) != 0) {
                fprintf(stderr, "FAIL: BCJ AArch64 filter mismatch at size %zu!\n", size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }
        }
#endif

        /* 2. Round-trip Scalar unfilter */
        bp_bcj_aarch64_unfilter_scalar(buf_scalar, size);
        if (memcmp(buf_scalar, orig, size) != 0) {
            fprintf(stderr, "FAIL: BCJ AArch64 scalar round-trip mismatch at size %zu!\n", size);
            free(orig); free(buf_scalar); free(buf_avx2);
            return -1;
        }

#if (defined(__x86_64__) || defined(_M_X64))
        if (bp_cpu_dispatch_get_active_backend() == BINPACK_BACKEND_AVX2) {
            /* 3. Round-trip AVX2 unfilter */
            bp_bcj_aarch64_unfilter_avx2(buf_avx2, size);
            if (memcmp(buf_avx2, orig, size) != 0) {
                fprintf(stderr, "FAIL: BCJ AArch64 AVX2 round-trip mismatch at size %zu!\n", size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }

            /* 4. Cross round-trip: Filter AVX2 -> Unfilter Scalar */
            memcpy(buf_avx2, orig, size);
            bp_bcj_aarch64_filter_avx2(buf_avx2, size);
            bp_bcj_aarch64_unfilter_scalar(buf_avx2, size);
            if (memcmp(buf_avx2, orig, size) != 0) {
                fprintf(stderr, "FAIL: BCJ AArch64 cross round-trip (AVX2 filter -> Scalar unfilter) at size %zu!\n", size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }
        }
#endif

        free(orig);
        free(buf_scalar);
        free(buf_avx2);
    }
    printf("  [PASS] BCJ AArch64 equivalence & round-trip passed for all %zu sizes.\n", num_sizes);
    return 0;
}

static int test_bcj_riscv_equivalence(void) {
    printf("[DISPATCH-TEST] BCJ RISC-V Scalar vs AVX2 bit-for-bit equivalence...\n");
    const size_t sizes[] = { 16, 32, 48, 64, 128, 256, 1024, 4096, 65536 };
    const size_t num_sizes = sizeof(sizes) / sizeof(sizes[0]);

    for (size_t s = 0; s < num_sizes; s++) {
        size_t size = sizes[s];
        uint8_t *orig = (uint8_t *)malloc(size);
        uint8_t *buf_scalar = (uint8_t *)malloc(size);
        uint8_t *buf_avx2 = (uint8_t *)malloc(size);
        assert(orig && buf_scalar && buf_avx2);

        fill_buffer(orig, size, (uint32_t)(0x87654321 + size));
        /* Inject some RISC-V JAL instructions (opcode 0x6F) */
        for (size_t i = 0; i + 4 <= size; i += 8) {
            uint32_t imm20 = (uint32_t)((i * 123) & 0x000FFFFF);
            uint32_t rd = (uint32_t)((i / 4) & 0x1F);
            uint32_t jal = 0x6F | (rd << 7) | (imm20 << 12);
            orig[i + 0] = (uint8_t)(jal & 0xFF);
            orig[i + 1] = (uint8_t)((jal >> 8) & 0xFF);
            orig[i + 2] = (uint8_t)((jal >> 16) & 0xFF);
            orig[i + 3] = (uint8_t)((jal >> 24) & 0xFF);
        }

        memcpy(buf_scalar, orig, size);
        memcpy(buf_avx2, orig, size);
        bp_bcj_riscv_filter_scalar(buf_scalar, size);

#if (defined(__x86_64__) || defined(_M_X64))
        if (bp_cpu_dispatch_get_active_backend() == BINPACK_BACKEND_AVX2) {
            bp_bcj_riscv_filter_avx2(buf_avx2, size);
            if (memcmp(buf_scalar, buf_avx2, size) != 0) {
                fprintf(stderr, "FAIL: BCJ RISC-V filter mismatch at size %zu!\n", size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }
        }
#endif

        bp_bcj_riscv_unfilter_scalar(buf_scalar, size);
        if (memcmp(buf_scalar, orig, size) != 0) {
            fprintf(stderr, "FAIL: BCJ RISC-V scalar round-trip mismatch at size %zu!\n", size);
            free(orig); free(buf_scalar); free(buf_avx2);
            return -1;
        }

#if (defined(__x86_64__) || defined(_M_X64))
        if (bp_cpu_dispatch_get_active_backend() == BINPACK_BACKEND_AVX2) {
            bp_bcj_riscv_unfilter_avx2(buf_avx2, size);
            if (memcmp(buf_avx2, orig, size) != 0) {
                fprintf(stderr, "FAIL: BCJ RISC-V AVX2 round-trip mismatch at size %zu!\n", size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }

            /* Cross round-trip: AVX2 filter -> Scalar unfilter */
            memcpy(buf_avx2, orig, size);
            bp_bcj_riscv_filter_avx2(buf_avx2, size);
            bp_bcj_riscv_unfilter_scalar(buf_avx2, size);
            if (memcmp(buf_avx2, orig, size) != 0) {
                fprintf(stderr, "FAIL: BCJ RISC-V cross round-trip (AVX2 filter -> Scalar unfilter) at size %zu!\n", size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }
        }
#endif

        free(orig);
        free(buf_scalar);
        free(buf_avx2);
    }
    printf("  [PASS] BCJ RISC-V equivalence & round-trip passed for all %zu sizes.\n", num_sizes);
    return 0;
}

static int test_bitshuffle_equivalence(void) {
    printf("[DISPATCH-TEST] Bitshuffle Scalar vs AVX2 bit-for-bit equivalence...\n");
    const uint16_t strides[] = { 1, 2, 4, 8, 16 };
    const size_t num_strides = sizeof(strides) / sizeof(strides[0]);
    /* Test both exact multiples of (32 * stride) and non-multiples to verify tail scalar loop */
    const size_t size_multipliers[] = { 15, 31, 32, 33, 64, 127, 128, 256, 1024, 8192 };
    const size_t num_multipliers = sizeof(size_multipliers) / sizeof(size_multipliers[0]);

    for (size_t st = 0; st < num_strides; st++) {
        uint16_t stride = strides[st];
        for (size_t sm = 0; sm < num_multipliers; sm++) {
            size_t size = size_multipliers[sm] * stride;
            uint8_t *orig = (uint8_t *)malloc(size);
            uint8_t *buf_scalar = (uint8_t *)malloc(size);
            uint8_t *buf_avx2 = (uint8_t *)malloc(size);
            assert(orig && buf_scalar && buf_avx2);

            fill_buffer(orig, size, (uint32_t)(0xCAFEBABE + size + stride));
            memcpy(buf_scalar, orig, size);
            memcpy(buf_avx2, orig, size);

            bp_bitshuffle_filter_scalar(buf_scalar, size, stride);

#if (defined(__x86_64__) || defined(_M_X64))
            if (bp_cpu_dispatch_get_active_backend() == BINPACK_BACKEND_AVX2) {
                bp_bitshuffle_filter_avx2(buf_avx2, size, stride);
                if (memcmp(buf_scalar, buf_avx2, size) != 0) {
                    fprintf(stderr, "FAIL: Bitshuffle filter mismatch! stride=%u, size=%zu\n", stride, size);
                    free(orig); free(buf_scalar); free(buf_avx2);
                    return -1;
                }
            }
#endif

            bp_bitshuffle_unfilter_scalar(buf_scalar, size, stride);
            if (memcmp(buf_scalar, orig, size) != 0) {
                fprintf(stderr, "FAIL: Bitshuffle scalar round-trip mismatch! stride=%u, size=%zu\n", stride, size);
                free(orig); free(buf_scalar); free(buf_avx2);
                return -1;
            }

#if (defined(__x86_64__) || defined(_M_X64))
            if (bp_cpu_dispatch_get_active_backend() == BINPACK_BACKEND_AVX2) {
                bp_bitshuffle_unfilter_avx2(buf_avx2, size, stride);
                if (memcmp(buf_avx2, orig, size) != 0) {
                    fprintf(stderr, "FAIL: Bitshuffle AVX2 round-trip mismatch! stride=%u, size=%zu\n", stride, size);
                    free(orig); free(buf_scalar); free(buf_avx2);
                    return -1;
                }

                /* Cross round-trip: AVX2 filter -> Scalar unfilter */
                memcpy(buf_avx2, orig, size);
                bp_bitshuffle_filter_avx2(buf_avx2, size, stride);
                bp_bitshuffle_unfilter_scalar(buf_avx2, size, stride);
                if (memcmp(buf_avx2, orig, size) != 0) {
                    fprintf(stderr, "FAIL: Bitshuffle cross round-trip (AVX2 filter -> Scalar unfilter)! stride=%u, size=%zu\n", stride, size);
                    free(orig); free(buf_scalar); free(buf_avx2);
                    return -1;
                }
            }
#endif

            free(orig);
            free(buf_scalar);
            free(buf_avx2);
        }
    }
    printf("  [PASS] Bitshuffle equivalence & round-trip passed across all strides (1, 2, 4, 8, 16) and sizes.\n");
    return 0;
}

static int test_dispatch_table_integrity(void) {
    printf("[DISPATCH-TEST] Checking Dispatch Table and API Wrappers...\n");
    bp_ensure_cpu_dispatch_initialized();

    int active_backend = bp_cpu_dispatch_get_active_backend();
    const char *name = (active_backend == BINPACK_BACKEND_AVX2) ? "AVX2 SIMD" : "Portable Scalar";
    printf("  Active CPU Dispatch Backend: [%d] %s\n", active_backend, name);

    assert(g_bp_dispatch.bcj_aarch64_filter != NULL);
    assert(g_bp_dispatch.bcj_aarch64_unfilter != NULL);
    assert(g_bp_dispatch.bcj_riscv_filter != NULL);
    assert(g_bp_dispatch.bcj_riscv_unfilter != NULL);
    assert(g_bp_dispatch.bitshuffle_filter != NULL);
    assert(g_bp_dispatch.bitshuffle_unfilter != NULL);

    /* Test thin wrappers invoking through g_bp_dispatch */
    uint8_t buffer[64];
    memset(buffer, 0x55, sizeof(buffer));
    binpack_bcj_aarch64_filter(buffer, sizeof(buffer));
    binpack_bcj_aarch64_unfilter(buffer, sizeof(buffer));
    for (size_t i = 0; i < sizeof(buffer); i++) {
        if (buffer[i] != 0x55) {
            fprintf(stderr, "FAIL: Wrapper roundtrip altered data!\n");
            return -1;
        }
    }

    printf("  [PASS] Dispatch table integrity and wrappers verified successfully.\n");
    return 0;
}

int main(void) {
    printf("=========================================================\n");
    printf("BP-003 Test Suite: CPU Dispatch & Bit-for-Bit Equivalence\n");
    printf("=========================================================\n");

    if (test_dispatch_table_integrity() != 0) return 1;
    if (test_bcj_aarch64_equivalence() != 0) return 1;
    if (test_bcj_riscv_equivalence() != 0) return 1;
    if (test_bitshuffle_equivalence() != 0) return 1;

    printf("=========================================================\n");
    printf("[SUCCESS] All BP-003 CPU dispatch & kernel tests PASSED!\n");
    printf("=========================================================\n");
    return 0;
}
