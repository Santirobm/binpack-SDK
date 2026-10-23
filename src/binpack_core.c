/**
 * @file binpack_core.c
 * @brief BinPack Enterprise SDK - Architectural Transforms & Preconditioners
 */

#include "binpack_internal.h"

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#elif defined(__ARM_NEON) || defined(__ARM_NEON__) || defined(_M_ARM64)
#include <arm_neon.h>
#endif

/* ========================================================================= */
/* CRC32 Slicing-by-8 Fast Integrity Engine (IEEE 802.3 polynomial)          */
/* ========================================================================= */

static uint32_t s_crc32_table[8][256];
static int s_crc32_initialized = 0;

static void binpack_init_crc32_table(void) {
    if (s_crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        s_crc32_table[0][i] = c;
    }
    for (uint32_t i = 0; i < 256; i++) {
        for (int j = 1; j < 8; j++) {
            s_crc32_table[j][i] = (s_crc32_table[j - 1][i] >> 8) ^ s_crc32_table[0][s_crc32_table[j - 1][i] & 0xFF];
        }
    }
    s_crc32_initialized = 1;
}

uint32_t binpack_crc32(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!s_crc32_initialized) binpack_init_crc32_table();
    crc = crc ^ 0xFFFFFFFF;

    /* Slicing-by-8 parallel table lookups */
    while (len >= 8) {
        uint32_t one = *(const uint32_t*)buf ^ crc;
        uint32_t two = *(const uint32_t*)(buf + 4);
        crc = s_crc32_table[7][one & 0xFF] ^
              s_crc32_table[6][(one >> 8) & 0xFF] ^
              s_crc32_table[5][(one >> 16) & 0xFF] ^
              s_crc32_table[4][one >> 24] ^
              s_crc32_table[3][two & 0xFF] ^
              s_crc32_table[2][(two >> 8) & 0xFF] ^
              s_crc32_table[1][(two >> 16) & 0xFF] ^
              s_crc32_table[0][two >> 24];
        buf += 8;
        len -= 8;
    }

    while (len > 0) {
        crc = s_crc32_table[0][(crc ^ *buf++) & 0xFF] ^ (crc >> 8);
        len--;
    }
    return crc ^ 0xFFFFFFFF;
}

/* ========================================================================= */
/* Section Bounds Detection (PE / ELF .text Boundaries)                      */
/* ========================================================================= */

void binpack_find_code_bounds(const uint8_t *data, size_t size, size_t *out_offset, size_t *out_length) {
    *out_offset = 0;
    *out_length = size;
    if (!data || size < 64) return;

    /* 1. Portable Executable (PE32 / PE32+) Detection */
    if (data[0] == 'M' && data[1] == 'Z') {
        if (size > 0x3C + 4) {
            uint32_t pe_off = (uint32_t)data[0x3C] | ((uint32_t)data[0x3D] << 8) |
                              ((uint32_t)data[0x3E] << 16) | ((uint32_t)data[0x3F] << 24);
            if (pe_off + 24 < size && data[pe_off] == 'P' && data[pe_off + 1] == 'E' &&
                data[pe_off + 2] == 0 && data[pe_off + 3] == 0) {
                uint16_t num_sections = (uint16_t)data[pe_off + 6] | ((uint16_t)data[pe_off + 7] << 8);
                uint16_t opt_hdr_sz = (uint16_t)data[pe_off + 20] | ((uint16_t)data[pe_off + 21] << 8);
                size_t sec_table_off = pe_off + 24 + opt_hdr_sz;

                for (uint16_t i = 0; i < num_sections; i++) {
                    size_t cur = sec_table_off + (size_t)i * 40;
                    if (cur + 40 > size) break;
                    if (memcmp(&data[cur], ".text", 5) == 0) {
                        uint32_t raw_sz = *(const uint32_t*)&data[cur + 16];
                        uint32_t raw_ptr = *(const uint32_t*)&data[cur + 20];
                        if (raw_ptr < size && raw_sz > 0 && (size_t)raw_ptr + raw_sz <= size) {
                            *out_offset = raw_ptr;
                            *out_length = raw_sz;
                            return;
                        }
                    }
                }
            }
        }
    }

    /* 2. Executable and Linkable Format (ELF32 / ELF64) Detection */
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F') {
        uint8_t elf_class = data[4];
        if (elf_class == 1 && size >= 52) { /* ELF32 */
            uint32_t shoff = *(const uint32_t*)&data[32];
            uint16_t shentsize = *(const uint16_t*)&data[46];
            uint16_t shnum = *(const uint16_t*)&data[48];
            uint16_t shstrndx = *(const uint16_t*)&data[50];

            if (shentsize >= 40 && shstrndx < shnum) {
                size_t strtab_sec = shoff + (size_t)shstrndx * shentsize;
                if (strtab_sec + 40 <= size) {
                    uint32_t strtab_off = *(const uint32_t*)&data[strtab_sec + 16];
                    uint32_t strtab_sz = *(const uint32_t*)&data[strtab_sec + 20];
                    if (strtab_off + strtab_sz <= size) {
                        for (uint16_t i = 0; i < shnum; i++) {
                            size_t cur = shoff + (size_t)i * shentsize;
                            if (cur + 40 > size) break;
                            uint32_t name_idx = *(const uint32_t*)&data[cur];
                            if (name_idx < strtab_sz &&
                                strcmp((const char*)&data[strtab_off + name_idx], ".text") == 0) {
                                uint32_t sec_off = *(const uint32_t*)&data[cur + 16];
                                uint32_t sec_sz = *(const uint32_t*)&data[cur + 20];
                                if (sec_off < size && (size_t)sec_off + sec_sz <= size) {
                                    *out_offset = sec_off;
                                    *out_length = sec_sz;
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        } else if (elf_class == 2 && size >= 64) { /* ELF64 */
            uint64_t shoff = *(const uint64_t*)&data[40];
            uint16_t shentsize = *(const uint16_t*)&data[58];
            uint16_t shnum = *(const uint16_t*)&data[60];
            uint16_t shstrndx = *(const uint16_t*)&data[62];

            if (shentsize >= 64 && shstrndx < shnum && shoff < size) {
                size_t strtab_sec = (size_t)shoff + (size_t)shstrndx * shentsize;
                if (strtab_sec + 64 <= size) {
                    uint64_t strtab_off = *(const uint64_t*)&data[strtab_sec + 24];
                    uint64_t strtab_sz = *(const uint64_t*)&data[strtab_sec + 32];
                    if (strtab_off + strtab_sz <= size) {
                        for (uint16_t i = 0; i < shnum; i++) {
                            size_t cur = (size_t)shoff + (size_t)i * shentsize;
                            if (cur + 64 > size) break;
                            uint32_t name_idx = *(const uint32_t*)&data[cur];
                            if (name_idx < strtab_sz &&
                                strcmp((const char*)&data[strtab_off + name_idx], ".text") == 0) {
                                uint64_t sec_off = *(const uint64_t*)&data[cur + 24];
                                uint64_t sec_sz = *(const uint64_t*)&data[cur + 32];
                                if (sec_off < size && (size_t)sec_off + sec_sz <= size) {
                                    *out_offset = (size_t)sec_off;
                                    *out_length = (size_t)sec_sz;
                                    return;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
}

/* ========================================================================= */
/* BCJ x86 / x64 Branch Address Normalization (State-Machine Accelerated)    */
/* ========================================================================= */

void binpack_bcj_x86_filter(uint8_t *data, size_t size) {
    if (!data || size < 5) return;
    UInt32 state = 0;
    x86_Convert(data, size, 0, &state, 1);
}

void binpack_bcj_x86_unfilter(uint8_t *data, size_t size) {
    if (!data || size < 5) return;
    UInt32 state = 0;
    x86_Convert(data, size, 0, &state, 0);
}

/* ========================================================================= */
/* BCJ ARM32 Thumb-2 / ARMv7-A Normalization                                 */
/* ========================================================================= */

void binpack_bcj_arm32_filter(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        if (data[i + 3] == 0xEB) {
            uint32_t src = (uint32_t)data[i] | ((uint32_t)data[i+1] << 8) | ((uint32_t)data[i+2] << 16);
            uint32_t dest = src + (uint32_t)((i + 8) >> 2);
            data[i]   = (uint8_t)(dest);
            data[i+1] = (uint8_t)(dest >> 8);
            data[i+2] = (uint8_t)(dest >> 16);
        }
    }
}

void binpack_bcj_arm32_unfilter(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
        if (data[i + 3] == 0xEB) {
            uint32_t src = (uint32_t)data[i] | ((uint32_t)data[i+1] << 8) | ((uint32_t)data[i+2] << 16);
            uint32_t dest = src - (uint32_t)((i + 8) >> 2);
            data[i]   = (uint8_t)(dest);
            data[i+1] = (uint8_t)(dest >> 8);
            data[i+2] = (uint8_t)(dest >> 16);
        }
    }
}

/* ========================================================================= */
/* BCJ AArch64 (ARM64) Branch Normalization                                  */
/* ========================================================================= */

void binpack_bcj_aarch64_filter(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
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

void binpack_bcj_aarch64_unfilter(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
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
/* BCJ RISC-V (RV32I / RV64I JAL) Branch Normalization                       */
/* ========================================================================= */

void binpack_bcj_riscv_filter(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
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

void binpack_bcj_riscv_unfilter(uint8_t *data, size_t size) {
    if (!data || size < 4) return;
    for (size_t i = 0; i + 4 <= size; i += 4) {
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
/* Cascaded Stride-N Transpose + Delta Prefilter (SIMD & Fused Single-Pass)  */
/* ========================================================================= */

static void bp_deinterleave_channels(const uint8_t *src, uint8_t *dst, size_t num_elements, uint16_t stride) {
    if (stride == 2) {
        uint8_t *d0 = dst;
        uint8_t *d1 = dst + num_elements;
        for (size_t i = 0; i < num_elements; i++) {
            d0[i] = src[2 * i];
            d1[i] = src[2 * i + 1];
        }
    } else if (stride == 4) {
        uint8_t *d0 = dst;
        uint8_t *d1 = dst + num_elements;
        uint8_t *d2 = dst + 2 * num_elements;
        uint8_t *d3 = dst + 3 * num_elements;
        for (size_t i = 0; i < num_elements; i++) {
            d0[i] = src[4 * i];
            d1[i] = src[4 * i + 1];
            d2[i] = src[4 * i + 2];
            d3[i] = src[4 * i + 3];
        }
    } else {
        for (size_t i = 0; i < num_elements; i++) {
            const uint8_t *elem = src + i * stride;
            for (size_t s = 0; s < stride; s++) {
                dst[s * num_elements + i] = elem[s];
            }
        }
    }
}

static void bp_interleave_channels(const uint8_t *src, uint8_t *dst, size_t num_elements, uint16_t stride) {
    if (stride == 2) {
        const uint8_t *s0 = src;
        const uint8_t *s1 = src + num_elements;
        size_t i = 0;
#if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
        for (; i + 16 <= num_elements; i += 16) {
            __m128i v0 = _mm_loadu_si128((const __m128i*)(s0 + i));
            __m128i v1 = _mm_loadu_si128((const __m128i*)(s1 + i));
            __m128i lo = _mm_unpacklo_epi8(v0, v1);
            __m128i hi = _mm_unpackhi_epi8(v0, v1);
            _mm_storeu_si128((__m128i*)(dst + 2 * i), lo);
            _mm_storeu_si128((__m128i*)(dst + 2 * i + 16), hi);
        }
#endif
        for (; i < num_elements; i++) {
            dst[2 * i]     = s0[i];
            dst[2 * i + 1] = s1[i];
        }
    } else if (stride == 4) {
        const uint8_t *s0 = src;
        const uint8_t *s1 = src + num_elements;
        const uint8_t *s2 = src + 2 * num_elements;
        const uint8_t *s3 = src + 3 * num_elements;
        size_t i = 0;
#if defined(__SSE2__) || defined(__x86_64__) || defined(_M_X64)
        for (; i + 16 <= num_elements; i += 16) {
            __m128i v0 = _mm_loadu_si128((const __m128i*)(s0 + i));
            __m128i v1 = _mm_loadu_si128((const __m128i*)(s1 + i));
            __m128i v2 = _mm_loadu_si128((const __m128i*)(s2 + i));
            __m128i v3 = _mm_loadu_si128((const __m128i*)(s3 + i));
            __m128i t01_lo = _mm_unpacklo_epi8(v0, v1);
            __m128i t01_hi = _mm_unpackhi_epi8(v0, v1);
            __m128i t23_lo = _mm_unpacklo_epi8(v2, v3);
            __m128i t23_hi = _mm_unpackhi_epi8(v2, v3);
            _mm_storeu_si128((__m128i*)(dst + 4 * i),      _mm_unpacklo_epi16(t01_lo, t23_lo));
            _mm_storeu_si128((__m128i*)(dst + 4 * i + 16), _mm_unpackhi_epi16(t01_lo, t23_lo));
            _mm_storeu_si128((__m128i*)(dst + 4 * i + 32), _mm_unpacklo_epi16(t01_hi, t23_hi));
            _mm_storeu_si128((__m128i*)(dst + 4 * i + 48), _mm_unpackhi_epi16(t01_hi, t23_hi));
        }
#endif
        for (; i < num_elements; i++) {
            dst[4 * i]     = s0[i];
            dst[4 * i + 1] = s1[i];
            dst[4 * i + 2] = s2[i];
            dst[4 * i + 3] = s3[i];
        }
    } else {
        for (size_t i = 0; i < num_elements; i++) {
            uint8_t *elem = dst + i * stride;
            for (size_t s = 0; s < stride; s++) {
                elem[s] = src[s * num_elements + i];
            }
        }
    }
}

int binpack_transpose_delta_filter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 4 || stride < 2 || stride > 256) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;

    bp_deinterleave_channels(data, temp, num_elements, stride);

    /* Delta encode directly from temp back into data (eliminating final memcpy) */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        uint8_t prev = 0;
        for (size_t i = 0; i < num_elements; i++) {
            uint8_t cur = temp[base + i];
            data[base + i] = cur - prev;
            prev = cur;
        }
    }

    free(temp);
    return 0;
}

int binpack_transpose_delta_unfilter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 4 || stride < 2 || stride > 256) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;

    /* Prefix-sum decode from data into temp (eliminating initial memcpy) */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        uint8_t acc = 0;
        for (size_t i = 0; i < num_elements; i++) {
            acc = (uint8_t)(acc + data[base + i]);
            temp[base + i] = acc;
        }
    }

    /* SIMD-accelerated interleave directly from temp back into data */
    bp_interleave_channels(temp, data, num_elements, stride);

    free(temp);
    return 0;
}

/* ========================================================================= */
/* Bitshuffle: Bit-Level Matrix Transposition (SIMD / SWAR Accelerated)      */
/* ========================================================================= */

static inline uint64_t bp_transpose_8x8_bits(uint64_t x) {
    uint64_t t;
    t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAULL;
    x = x ^ t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCULL;
    x = x ^ t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ULL;
    x = x ^ t ^ (t << 28);
    return x;
}

#if defined(__AVX2__)
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
#endif

int binpack_bitshuffle_filter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 8 || stride < 1 || stride > 256) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
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
#if defined(__AVX2__)
        for (; b + 4 <= full_blocks; b += 4) {
            size_t off = base + (b * 8);
            bp_transpose_32x8_bits_avx2(temp + off);
            for (size_t k = 0; k < 32; k++) {
                uint8_t cur = temp[off + k];
                data[off + k] = cur - prev;
                prev = cur;
            }
        }
#endif
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

    free(temp);
    return 0;
}

int binpack_bitshuffle_unfilter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 8 || stride < 1 || stride > 256) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;

    /* Fused Single-Pass Delta-Accumulate + AVX2 Bit-Transpose from data -> temp */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        size_t full_blocks = num_elements / 8;
        size_t b = 0;
        uint8_t acc = 0;
#if defined(__AVX2__)
        for (; b + 4 <= full_blocks; b += 4) {
            size_t off = base + (b * 8);
            for (size_t k = 0; k < 32; k++) {
                acc = (uint8_t)(acc + data[off + k]);
                temp[off + k] = acc;
            }
            bp_transpose_32x8_bits_avx2(temp + off);
        }
#endif
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

    if (stride > 1) {
        bp_interleave_channels(temp, data, num_elements, stride);
    } else {
        memcpy(data, temp, num_elements);
    }

    free(temp);
    return 0;
}

/* ========================================================================= */
/* GPU Texture Block Decoupling (BC1 - BC7 & ASTC)                           */
/* ========================================================================= */

int binpack_texture_bcn_filter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 16) return -1;
    if (stride != 8 && stride != 16) stride = 8;

    size_t block_size = stride;
    size_t num_blocks = size / block_size;
    size_t remainder = size % block_size;
    if (num_blocks == 0) return 0;

    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    if (stride == 8) {
        uint8_t *endpoints = temp;
        uint8_t *indices   = temp + (num_blocks * 4);

        for (size_t i = 0; i < num_blocks; i++) {
            memcpy(endpoints + (i * 4), data + (i * 8), 4);
            memcpy(indices + (i * 4), data + (i * 8) + 4, 4);
        }

        for (size_t i = num_blocks - 1; i > 0; i--) {
            uint32_t cur, prev;
            memcpy(&cur, endpoints + (i * 4), 4);
            memcpy(&prev, endpoints + ((i - 1) * 4), 4);
            uint32_t diff = cur - prev;
            memcpy(endpoints + (i * 4), &diff, 4);
        }
    } else {
        uint8_t *alpha     = temp;
        uint8_t *endpoints = temp + (num_blocks * 8);
        uint8_t *indices   = temp + (num_blocks * 12);

        for (size_t i = 0; i < num_blocks; i++) {
            memcpy(alpha + (i * 8), data + (i * 16), 8);
            memcpy(endpoints + (i * 4), data + (i * 16) + 8, 4);
            memcpy(indices + (i * 4), data + (i * 16) + 12, 4);
        }

        for (size_t i = num_blocks - 1; i > 0; i--) {
            uint32_t cur, prev;
            memcpy(&cur, endpoints + (i * 4), 4);
            memcpy(&prev, endpoints + ((i - 1) * 4), 4);
            uint32_t diff = cur - prev;
            memcpy(endpoints + (i * 4), &diff, 4);
        }
    }

    if (remainder > 0) {
        memcpy(temp + (num_blocks * block_size), data + (num_blocks * block_size), remainder);
    }
    memcpy(data, temp, size);
    free(temp);
    return 0;
}

int binpack_texture_bcn_unfilter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 16) return -1;
    if (stride != 8 && stride != 16) stride = 8;

    size_t block_size = stride;
    size_t num_blocks = size / block_size;
    size_t remainder = size % block_size;
    if (num_blocks == 0) return 0;

    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    memcpy(temp, data, size);

    if (stride == 8) {
        uint8_t *endpoints = temp;
        uint8_t *indices   = temp + (num_blocks * 4);

        for (size_t i = 1; i < num_blocks; i++) {
            uint32_t diff, prev;
            memcpy(&diff, endpoints + (i * 4), 4);
            memcpy(&prev, endpoints + ((i - 1) * 4), 4);
            uint32_t orig = prev + diff;
            memcpy(endpoints + (i * 4), &orig, 4);
        }

        for (size_t i = 0; i < num_blocks; i++) {
            memcpy(data + (i * 8), endpoints + (i * 4), 4);
            memcpy(data + (i * 8) + 4, indices + (i * 4), 4);
        }
    } else {
        uint8_t *alpha     = temp;
        uint8_t *endpoints = temp + (num_blocks * 8);
        uint8_t *indices   = temp + (num_blocks * 12);

        for (size_t i = 1; i < num_blocks; i++) {
            uint32_t diff, prev;
            memcpy(&diff, endpoints + (i * 4), 4);
            memcpy(&prev, endpoints + ((i - 1) * 4), 4);
            uint32_t orig = prev + diff;
            memcpy(endpoints + (i * 4), &orig, 4);
        }

        for (size_t i = 0; i < num_blocks; i++) {
            memcpy(data + (i * 16), alpha + (i * 8), 8);
            memcpy(data + (i * 16) + 8, endpoints + (i * 4), 4);
            memcpy(data + (i * 16) + 12, indices + (i * 4), 4);
        }
    }

    if (remainder > 0) {
        memcpy(data + (num_blocks * block_size), temp + (num_blocks * block_size), remainder);
    }
    free(temp);
    return 0;
}

/* ========================================================================= */
/* Automatic Filter Selection Heuristic                                      */
/* ========================================================================= */

binpack_filter_t binpack_auto_select_filter(const uint8_t *data, size_t size, uint16_t *out_param) {
    *out_param = 0;
    if (!data || size < 64) return BINPACK_FILTER_NONE;

    if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
        *out_param = 8;
        return BINPACK_FILTER_TEXTURE_BCN;
    }

    if (data[0] == 'M' && data[1] == 'Z') {
        if (size > 0x3C + 4) {
            uint32_t pe_off = *(const uint32_t*)&data[0x3C];
            if (pe_off + 6 < size && data[pe_off] == 'P' && data[pe_off + 1] == 'E') {
                uint16_t machine = *(const uint16_t*)&data[pe_off + 4];
                if (machine == 0xAA64) return BINPACK_FILTER_AARCH64;
                if (machine == 0x01C0 || machine == 0x01C2 || machine == 0x01C4) return BINPACK_FILTER_ARM32;
                return BINPACK_FILTER_X86_64;
            }
        }
        return BINPACK_FILTER_X86_64;
    }

    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F' && size >= 20) {
        uint16_t machine = *(const uint16_t*)&data[18];
        if (machine == 243) return BINPACK_FILTER_RISCV;
        if (machine == 183) return BINPACK_FILTER_AARCH64;
        if (machine == 40) return BINPACK_FILTER_ARM32;
        return BINPACK_FILTER_X86_64;
    }

    if (size >= 1024) {
        static const uint16_t candidate_strides[] = {
            2, 4, 8, 12, 16, 24, 32, 48, 64, 84, 96, 104, 128
        };
        /* Sample from middle of buffer when large to avoid archive/container headers */
        const uint8_t *sample = (size > 65536) ? (data + (size / 2) - 4096) : data;
        size_t test_len = size > 8192 ? 8192 : size;

        /* Measure baseline stride-1 byte correlation (rejects zero-runs / plain text) */
        size_t s1_matches = 0;
        for (size_t i = 1; i < test_len; i++) {
            int diff = (int)sample[i] - (int)sample[i - 1];
            if (diff > -4 && diff < 4) s1_matches++;
        }
        double s1_score = (double)s1_matches / (double)(test_len - 1);

        uint16_t best_stride = 0;
        double best_score = 0.0;

        for (size_t c = 0; c < sizeof(candidate_strides) / sizeof(candidate_strides[0]); c++) {
            uint16_t st = candidate_strides[c];
            if (test_len <= (size_t)st * 8) continue;
            size_t matches = 0;
            size_t total = test_len - st;
            for (size_t i = st; i < test_len; i++) {
                int diff = (int)sample[i] - (int)sample[i - st];
                if (diff > -4 && diff < 4) matches++;
            }
            double score = (double)matches / (double)total;
            if (score > 0.40 && score > s1_score + 0.15 && score > best_score + 0.03) {
                best_score = score;
                best_stride = st;
            }
        }
        if (best_stride >= 2) {
            *out_param = best_stride;
            return BINPACK_FILTER_TRANSPOSE_DELTA;
        }
    }

    size_t jump_count = 0;
    size_t sample_len = size > 16384 ? 16384 : size;
    for (size_t i = 0; i < sample_len; i++) {
        if (data[i] == 0xE8 || data[i] == 0xE9) jump_count++;
    }
    if (jump_count * 100 / sample_len >= 1) {
        return BINPACK_FILTER_X86_64;
    }

    return BINPACK_FILTER_NONE;
}
