/**
 * @file binpack_core_avx2.c
 * @brief BinPack Enterprise SDK - Vectorized AVX2 Kernels
 *
 * Isolated translation unit compiled strictly with AVX2 instruction set enabled.
 * Only dispatched at runtime when both CPU and OS support are verified.
 */

#include "binpack_internal.h"

#if (defined(__x86_64__) || defined(_M_X64))

#include <immintrin.h>

/* ========================================================================= */
/* BCJ AArch64 (ARM64) Branch Normalization - AVX2 Fast-Path                 */
/* ========================================================================= */

void bp_bcj_aarch64_filter_avx2(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        __m256i instrs = _mm256_loadu_si256((const __m256i*)(data + i));
        __m256i ops = _mm256_srli_epi32(instrs, 26);
        __m256i m_bl = _mm256_cmpeq_epi32(ops, _mm256_set1_epi32(0x25));
        __m256i m_b  = _mm256_cmpeq_epi32(ops, _mm256_set1_epi32(0x05));
        __m256i m_any = _mm256_or_si256(m_bl, m_b);
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(m_any));
        if (mask == 0) continue;

        for (size_t k = 0; k < 8; k++) {
            if ((mask >> k) & 1) {
                size_t pos = i + k * 4;
                uint32_t instr = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8) |
                                 ((uint32_t)data[pos+2] << 16) | ((uint32_t)data[pos+3] << 24);
                uint32_t src = instr & 0x03FFFFFF;
                uint32_t dest = src + (uint32_t)(pos >> 2);
                instr = (instr & 0xFC000000) | (dest & 0x03FFFFFF);
                data[pos]   = (uint8_t)(instr);
                data[pos+1] = (uint8_t)(instr >> 8);
                data[pos+2] = (uint8_t)(instr >> 16);
                data[pos+3] = (uint8_t)(instr >> 24);
            }
        }
    }
    for (; i + 4 <= size; i += 4) {
        uint32_t instr = (uint32_t)data[i] | ((uint32_t)data[i+1] << 8) |
                         ((uint32_t)data[i+2] << 16) | ((uint32_t)data[i+3] << 24);
        uint32_t op = instr >> 26;
        if (op == 0x25 || op == 0x05) { /* BL (0x25) and B (0x05) */
            uint32_t src = instr & 0x03FFFFFF;
            uint32_t dest = src + (uint32_t)(i >> 2);
            instr = (instr & 0xFC000000) | (dest & 0x03FFFFFF);
            data[i]   = (uint8_t)(instr);
            data[i+1] = (uint8_t)(instr >> 8);
            data[i+2] = (uint8_t)(instr >> 16);
            data[i+3] = (uint8_t)(instr >> 24);
        }
    }
}

void bp_bcj_aarch64_unfilter_avx2(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        __m256i instrs = _mm256_loadu_si256((const __m256i*)(data + i));
        __m256i ops = _mm256_srli_epi32(instrs, 26);
        __m256i m_bl = _mm256_cmpeq_epi32(ops, _mm256_set1_epi32(0x25));
        __m256i m_b  = _mm256_cmpeq_epi32(ops, _mm256_set1_epi32(0x05));
        __m256i m_any = _mm256_or_si256(m_bl, m_b);
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(m_any));
        if (mask == 0) continue;

        for (size_t k = 0; k < 8; k++) {
            if ((mask >> k) & 1) {
                size_t pos = i + k * 4;
                uint32_t instr = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8) |
                                 ((uint32_t)data[pos+2] << 16) | ((uint32_t)data[pos+3] << 24);
                uint32_t src = instr & 0x03FFFFFF;
                uint32_t dest = src - (uint32_t)(pos >> 2);
                instr = (instr & 0xFC000000) | (dest & 0x03FFFFFF);
                data[pos]   = (uint8_t)(instr);
                data[pos+1] = (uint8_t)(instr >> 8);
                data[pos+2] = (uint8_t)(instr >> 16);
                data[pos+3] = (uint8_t)(instr >> 24);
            }
        }
    }
    for (; i + 4 <= size; i += 4) {
        uint32_t instr = (uint32_t)data[i] | ((uint32_t)data[i+1] << 8) |
                         ((uint32_t)data[i+2] << 16) | ((uint32_t)data[i+3] << 24);
        uint32_t op = instr >> 26;
        if (op == 0x25 || op == 0x05) { /* BL (0x25) and B (0x05) */
            uint32_t src = instr & 0x03FFFFFF;
            uint32_t dest = src - (uint32_t)(i >> 2);
            instr = (instr & 0xFC000000) | (dest & 0x03FFFFFF);
            data[i]   = (uint8_t)(instr);
            data[i+1] = (uint8_t)(instr >> 8);
            data[i+2] = (uint8_t)(instr >> 16);
            data[i+3] = (uint8_t)(instr >> 24);
        }
    }
}

/* ========================================================================= */
/* BCJ RISC-V (RV32I / RV64I JAL) Branch Normalization - AVX2 Fast-Path      */
/* ========================================================================= */

void bp_bcj_riscv_filter_avx2(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        __m256i instrs = _mm256_loadu_si256((const __m256i*)(data + i));
        __m256i ops = _mm256_and_si256(instrs, _mm256_set1_epi32(0x7F));
        __m256i m_jal = _mm256_cmpeq_epi32(ops, _mm256_set1_epi32(0x6F));
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(m_jal));
        if (mask == 0) continue;

        for (size_t k = 0; k < 8; k++) {
            if ((mask >> k) & 1) {
                size_t pos = i + k * 4;
                uint32_t instr = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8) |
                                 ((uint32_t)data[pos+2] << 16) | ((uint32_t)data[pos+3] << 24);
                uint32_t offset20 = (((instr >> 31) & 1) << 19) |
                                    (((instr >> 12) & 0xFF) << 11) |
                                    (((instr >> 20) & 1) << 10) |
                                    ((instr >> 21) & 0x3FF);
                uint32_t dest = (offset20 + (uint32_t)(pos >> 1)) & 0xFFFFF;
                instr = (instr & 0x00000FFF) |
                        (((dest >> 19) & 1) << 31) |
                        ((dest & 0x3FF) << 21) |
                        (((dest >> 10) & 1) << 20) |
                        (((dest >> 11) & 0xFF) << 12);
                data[pos]   = (uint8_t)(instr);
                data[pos+1] = (uint8_t)(instr >> 8);
                data[pos+2] = (uint8_t)(instr >> 16);
                data[pos+3] = (uint8_t)(instr >> 24);
            }
        }
    }
    for (; i + 4 <= size; i += 4) {
        uint32_t instr = (uint32_t)data[i] | ((uint32_t)data[i+1] << 8) |
                         ((uint32_t)data[i+2] << 16) | ((uint32_t)data[i+3] << 24);
        if ((instr & 0x7F) == 0x6F) { /* JAL instruction */
            uint32_t offset20 = (((instr >> 31) & 1) << 19) |
                                (((instr >> 12) & 0xFF) << 11) |
                                (((instr >> 20) & 1) << 10) |
                                ((instr >> 21) & 0x3FF);
            uint32_t dest = (offset20 + (uint32_t)(i >> 1)) & 0xFFFFF;
            instr = (instr & 0x00000FFF) |
                    (((dest >> 19) & 1) << 31) |
                    ((dest & 0x3FF) << 21) |
                    (((dest >> 10) & 1) << 20) |
                    (((dest >> 11) & 0xFF) << 12);
            data[i]   = (uint8_t)(instr);
            data[i+1] = (uint8_t)(instr >> 8);
            data[i+2] = (uint8_t)(instr >> 16);
            data[i+3] = (uint8_t)(instr >> 24);
        }
    }
}

void bp_bcj_riscv_unfilter_avx2(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    size_t i = 0;
    for (; i + 32 <= size; i += 32) {
        __m256i instrs = _mm256_loadu_si256((const __m256i*)(data + i));
        __m256i ops = _mm256_and_si256(instrs, _mm256_set1_epi32(0x7F));
        __m256i m_jal = _mm256_cmpeq_epi32(ops, _mm256_set1_epi32(0x6F));
        int mask = _mm256_movemask_ps(_mm256_castsi256_ps(m_jal));
        if (mask == 0) continue;

        for (size_t k = 0; k < 8; k++) {
            if ((mask >> k) & 1) {
                size_t pos = i + k * 4;
                uint32_t instr = (uint32_t)data[pos] | ((uint32_t)data[pos+1] << 8) |
                                 ((uint32_t)data[pos+2] << 16) | ((uint32_t)data[pos+3] << 24);
                uint32_t dest = (((instr >> 31) & 1) << 19) |
                                (((instr >> 12) & 0xFF) << 11) |
                                (((instr >> 20) & 1) << 10) |
                                ((instr >> 21) & 0x3FF);
                uint32_t offset20 = (dest - (uint32_t)(pos >> 1)) & 0xFFFFF;
                instr = (instr & 0x00000FFF) |
                        (((offset20 >> 19) & 1) << 31) |
                        ((offset20 & 0x3FF) << 21) |
                        (((offset20 >> 10) & 1) << 20) |
                        (((offset20 >> 11) & 0xFF) << 12);
                data[pos]   = (uint8_t)(instr);
                data[pos+1] = (uint8_t)(instr >> 8);
                data[pos+2] = (uint8_t)(instr >> 16);
                data[pos+3] = (uint8_t)(instr >> 24);
            }
        }
    }
    for (; i + 4 <= size; i += 4) {
        uint32_t instr = (uint32_t)data[i] | ((uint32_t)data[i+1] << 8) |
                         ((uint32_t)data[i+2] << 16) | ((uint32_t)data[i+3] << 24);
        if ((instr & 0x7F) == 0x6F) { /* JAL instruction */
            uint32_t dest = (((instr >> 31) & 1) << 19) |
                            (((instr >> 12) & 0xFF) << 11) |
                            (((instr >> 20) & 1) << 10) |
                            ((instr >> 21) & 0x3FF);
            uint32_t offset20 = (dest - (uint32_t)(i >> 1)) & 0xFFFFF;
            instr = (instr & 0x00000FFF) |
                    (((offset20 >> 19) & 1) << 31) |
                    ((offset20 & 0x3FF) << 21) |
                    (((offset20 >> 10) & 1) << 20) |
                    (((offset20 >> 11) & 0xFF) << 12);
            data[i]   = (uint8_t)(instr);
            data[i+1] = (uint8_t)(instr >> 8);
            data[i+2] = (uint8_t)(instr >> 16);
            data[i+3] = (uint8_t)(instr >> 24);
        }
    }
}

/* ========================================================================= */
/* Bitshuffle Bit-Level Transposition - AVX2 Kernels                         */
/* ========================================================================= */

static inline void bp_transpose_32x8_bits_avx2(uint8_t *data) {
    __m256i x = _mm256_loadu_si256((const __m256i*)data);
    __m256i m1 = _mm256_set1_epi64x(0x00AA00AA00AA00AAULL);
    __m256i m2 = _mm256_set1_epi64x(0x0000CCCC0000CCCCULL);
    __m256i m3 = _mm256_set1_epi64x(0x00000000F0F0F0F0ULL);

    __m256i t = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, 7)), m1);
    x = _mm256_xor_si256(_mm256_xor_si256(x, t), _mm256_slli_epi64(t, 7));

    t = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, 14)), m2);
    x = _mm256_xor_si256(_mm256_xor_si256(x, t), _mm256_slli_epi64(t, 14));

    t = _mm256_and_si256(_mm256_xor_si256(x, _mm256_srli_epi64(x, 28)), m3);
    x = _mm256_xor_si256(_mm256_xor_si256(x, t), _mm256_slli_epi64(t, 28));

    _mm256_storeu_si256((__m256i*)data, x);
}

int bp_bitshuffle_filter_avx2(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 8 || stride < 1 || stride > 256) return -1;
    int must_free = 0;
    uint8_t *temp = bp_acquire_core_scratch(size, &must_free);
    if (!temp) return -1;

    size_t num_elements = size / stride;

    if (stride > 1) {
        bp_deinterleave_channels(data, temp, num_elements, stride);
    } else {
        memcpy(temp, data, size);
    }

    /* Fused L1-hot Bit-Transpose + Delta pass writing directly back to data */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        size_t full_blocks = num_elements / 8;
        size_t b = 0;
        uint8_t prev = 0;

        for (; b + 4 <= full_blocks; b += 4) {
            size_t off = base + (b * 8);
            bp_transpose_32x8_bits_avx2(temp + off);
            for (size_t k = 0; k < 32; k++) {
                uint8_t cur = temp[off + k];
                data[off + k] = cur - prev;
                prev = cur;
            }
        }

        for (; b < full_blocks; b++) {
            size_t off = base + (b * 8);
            uint64_t val;
            memcpy(&val, temp + off, sizeof(val));
            val = bp_transpose_8x8_bits(val);
            memcpy(temp + off, &val, sizeof(val));
            for (size_t k = 0; k < 8; k++) {
                uint8_t cur = temp[off + k];
                data[off + k] = cur - prev;
                prev = cur;
            }
        }

        for (size_t i = full_blocks * 8; i < num_elements; i++) {
            uint8_t cur = temp[base + i];
            data[base + i] = cur - prev;
            prev = cur;
        }
    }

    bp_release_core_scratch(temp, must_free);
    return 0;
}

int bp_bitshuffle_unfilter_avx2(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 8 || stride < 1 || stride > 256) return -1;
    int must_free = 0;
    uint8_t *temp = bp_acquire_core_scratch(size, &must_free);
    if (!temp) return -1;

    size_t num_elements = size / stride;

    /* Fused Single-Pass Delta-Accumulate + AVX2 Bit-Transpose from data -> temp */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        size_t full_blocks = num_elements / 8;
        size_t b = 0;
        uint8_t acc = 0;

        for (; b + 4 <= full_blocks; b += 4) {
            size_t off = base + (b * 8);
            for (size_t k = 0; k < 32; k++) {
                acc = (uint8_t)(acc + data[off + k]);
                temp[off + k] = acc;
            }
            bp_transpose_32x8_bits_avx2(temp + off);
        }

        for (; b < full_blocks; b++) {
            size_t off = base + (b * 8);
            for (size_t k = 0; k < 8; k++) {
                acc = (uint8_t)(acc + data[off + k]);
                temp[off + k] = acc;
            }
            uint64_t val;
            memcpy(&val, temp + off, sizeof(val));
            val = bp_transpose_8x8_bits(val);
            memcpy(temp + off, &val, sizeof(val));
        }

        for (size_t i = full_blocks * 8; i < num_elements; i++) {
            acc = (uint8_t)(acc + data[base + i]);
            temp[base + i] = acc;
        }
    }

    /* Restore original channel interleaved structure directly back into caller buffer */
    if (stride > 1) {
        bp_interleave_channels(temp, data, num_elements, stride);
    } else {
        memcpy(data, temp, size);
    }

    bp_release_core_scratch(temp, must_free);
    return 0;
}

#else

/* Portable stubs for non-x86 compilation targets (e.g. ARM64, RISC-V) */
void bp_bcj_aarch64_filter_avx2(uint8_t *data, size_t size) {
    bp_bcj_aarch64_filter_scalar(data, size);
}
void bp_bcj_aarch64_unfilter_avx2(uint8_t *data, size_t size) {
    bp_bcj_aarch64_unfilter_scalar(data, size);
}
void bp_bcj_riscv_filter_avx2(uint8_t *data, size_t size) {
    bp_bcj_riscv_filter_scalar(data, size);
}
void bp_bcj_riscv_unfilter_avx2(uint8_t *data, size_t size) {
    bp_bcj_riscv_unfilter_scalar(data, size);
}
int bp_bitshuffle_filter_avx2(uint8_t *data, size_t size, uint16_t stride) {
    return bp_bitshuffle_filter_scalar(data, size, stride);
}
int bp_bitshuffle_unfilter_avx2(uint8_t *data, size_t size, uint16_t stride) {
    return bp_bitshuffle_unfilter_scalar(data, size, stride);
}

#endif
