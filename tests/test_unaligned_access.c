/**
 * @file test_unaligned_access.c
 * @brief Stress test for unaligned memory accesses and strict-aliasing in binpack_crc32
 *
 * Reproducer and validation harness for BP-002:
 * Evaluates buffers across all byte offsets (0 to 7) and lengths (0 to 256)
 * across 4 data distributions (Sequential, Zeros, 0x5A, Pseudo-random)
 * using both one-shot and incremental multi-chunk CRC32 calls.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/binpack_internal.h"

#define MAX_BUFFER 256
#define MAX_OFFSET 7

/**
 * Independent byte-by-byte reference implementation (IEEE 802.3 CRC-32).
 * Reads strictly as uint8_t; completely immune to alignment or strict-aliasing issues.
 */
static uint32_t reference_crc32(uint32_t crc, const uint8_t *buf, size_t len)
{
    crc ^= 0xFFFFFFFFU;

    for (size_t i = 0; i < len; i++) {
        crc ^= buf[i];

        for (int j = 0; j < 8; j++) {
            crc = (crc & 1)
                ? ((crc >> 1) ^ 0xEDB88320U)
                : (crc >> 1);
        }
    }

    return crc ^ 0xFFFFFFFFU;
}

typedef enum {
    PATTERN_SEQUENTIAL,
    PATTERN_ZEROS,
    PATTERN_0X5A,
    PATTERN_PSEUDO_RANDOM
} test_pattern_t;

static void fill_pattern(uint8_t *buf, size_t len, size_t offset, test_pattern_t pat)
{
    switch (pat) {
        case PATTERN_SEQUENTIAL:
            for (size_t i = 0; i < len; i++) {
                buf[i] = (uint8_t)((i * 37 + offset * 13) & 0xFF);
            }
            break;
        case PATTERN_ZEROS:
            memset(buf, 0x00, len);
            break;
        case PATTERN_0X5A:
            memset(buf, 0x5A, len);
            break;
        case PATTERN_PSEUDO_RANDOM: {
            uint32_t lcg = (uint32_t)(offset * 1013904223U + 1664525U);
            for (size_t i = 0; i < len; i++) {
                lcg = lcg * 1664525U + 1013904223U;
                buf[i] = (uint8_t)(lcg >> 24);
            }
            break;
        }
    }
}

static const char* pattern_name(test_pattern_t pat)
{
    switch (pat) {
        case PATTERN_SEQUENTIAL:    return "Sequential";
        case PATTERN_ZEROS:         return "All-Zeros";
        case PATTERN_0X5A:          return "0x5A Pattern";
        case PATTERN_PSEUDO_RANDOM: return "Pseudo-Random";
    }
    return "Unknown";
}

int main(void)
{
    /* Extra padding to safely test up to MAX_OFFSET with large buffers */
    uint8_t storage[MAX_BUFFER + MAX_OFFSET + 64];
    unsigned total_tests = 0;
    unsigned failures = 0;

    printf("=========================================================\n");
    printf("BP-002: Unaligned Memory Access & Strict-Aliasing Harness\n");
    printf("Testing offsets 0..%d, lengths 0..%d across 4 patterns\n", MAX_OFFSET, MAX_BUFFER);
    printf("=========================================================\n");

    for (int pat = 0; pat < 4; pat++) {
        test_pattern_t pattern = (test_pattern_t)pat;
        unsigned pattern_failures = 0;

        for (size_t offset = 0; offset <= MAX_OFFSET; offset++) {
            uint8_t *buf = storage + offset;
            fill_pattern(buf, MAX_BUFFER, offset, pattern);

            for (size_t len = 0; len <= MAX_BUFFER; len++) {
                /* 1. Full one-shot CRC32 */
                uint32_t expected = reference_crc32(0, buf, len);
                uint32_t actual = binpack_crc32(0, buf, len);
                total_tests++;

                if (actual != expected) {
                    if (pattern_failures < 5) {
                        printf("[FAIL ONE-SHOT] pat=%s offset=%zu len=%zu expected=0x%08X actual=0x%08X\n",
                               pattern_name(pattern), offset, len, expected, actual);
                    }
                    pattern_failures++;
                    failures++;
                }

                /* 2. Incremental 2-chunk CRC32 (if len >= 2) */
                if (len >= 2) {
                    size_t split = len / 2;
                    uint32_t exp_split = reference_crc32(0, buf, split);
                    exp_split = reference_crc32(exp_split, buf + split, len - split);

                    uint32_t act_split = binpack_crc32(0, buf, split);
                    act_split = binpack_crc32(act_split, buf + split, len - split);
                    total_tests++;

                    if (act_split != exp_split || act_split != expected) {
                        if (pattern_failures < 5) {
                            printf("[FAIL INCR] pat=%s offset=%zu len=%zu (split=%zu) exp=0x%08X act=0x%08X\n",
                                   pattern_name(pattern), offset, len, split, expected, act_split);
                        }
                        pattern_failures++;
                        failures++;
                    }
                }
            }
        }

        printf("[PATTERN] %-14s: %u checks, %u failures\n",
               pattern_name(pattern),
               (MAX_OFFSET + 1) * (MAX_BUFFER + 1) * 2 - (MAX_OFFSET + 1) * 2,
               pattern_failures);
    }

    printf("=========================================================\n");
    printf("Total unaligned operations executed: %u\n", total_tests);
    printf("Total failures detected:            %u\n", failures);

    if (failures != 0) {
        printf("[FAIL] Unaligned CRC32 verification failed!\n");
        return 1;
    }

    printf("[PASS] All unaligned and incremental CRC32 tests matched reference.\n");
    return 0;
}
