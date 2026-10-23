/**
 * @file binpack_core.c
 * @brief BinPack Enterprise SDK - Architectural Transforms & Preconditioners
 */

#include "binpack_internal.h"

/* ========================================================================= */
/* CRC32 Engine (IEEE 802.3 standard polynomial 0xEDB88320)                 */
/* ========================================================================= */

static uint32_t s_crc32_table[256];
static int s_crc32_initialized = 0;

static void binpack_init_crc32_table(void) {
    if (s_crc32_initialized) return;
    for (uint32_t i = 0; i < 256; i++) {
        uint32_t c = i;
        for (int j = 0; j < 8; j++) {
            c = (c & 1) ? (0xEDB88320L ^ (c >> 1)) : (c >> 1);
        }
        s_crc32_table[i] = c;
    }
    s_crc32_initialized = 1;
}

uint32_t binpack_crc32(uint32_t crc, const uint8_t *buf, size_t len) {
    if (!s_crc32_initialized) binpack_init_crc32_table();
    crc = crc ^ 0xFFFFFFFF;
    for (size_t i = 0; i < len; i++) {
        crc = s_crc32_table[(crc ^ buf[i]) & 0xFF] ^ (crc >> 8);
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
/* BCJ x86 / x64 Branch Address Normalization (SWAR Accelerated)             */
/* ========================================================================= */

void binpack_bcj_x86_filter(uint8_t *data, size_t size) {
    if (!data || size < 5) return;
    size_t i = 0;
    size_t limit = size - 5;
    while (i <= limit) {
        /* Fast 64-bit skip if no 0xE8 or 0xE9 in the next 8 bytes */
        if (i + 12 <= size) {
            uint64_t w;
            memcpy(&w, data + i, sizeof(w));
            uint64_t v_e8 = w ^ 0xE8E8E8E8E8E8E8E8ULL;
            uint64_t v_e9 = w ^ 0xE9E9E9E9E9E9E9E9ULL;
            uint64_t m_e8 = (v_e8 - 0x0101010101010101ULL) & ~v_e8 & 0x8080808080808080ULL;
            uint64_t m_e9 = (v_e9 - 0x0101010101010101ULL) & ~v_e9 & 0x8080808080808080ULL;
            if ((m_e8 | m_e9) == 0) {
                i += 8;
                continue;
            }
        }
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9) {
            uint32_t src;
            memcpy(&src, data + i + 1, sizeof(src));
            uint32_t dest = src + (uint32_t)(i + 5);
            memcpy(data + i + 1, &dest, sizeof(dest));
            i += 5;
        } else {
            i++;
        }
    }
}

void binpack_bcj_x86_unfilter(uint8_t *data, size_t size) {
    if (!data || size < 5) return;
    size_t i = 0;
    size_t limit = size - 5;
    while (i <= limit) {
        if (i + 12 <= size) {
            uint64_t w;
            memcpy(&w, data + i, sizeof(w));
            uint64_t v_e8 = w ^ 0xE8E8E8E8E8E8E8E8ULL;
            uint64_t v_e9 = w ^ 0xE9E9E9E9E9E9E9E9ULL;
            uint64_t m_e8 = (v_e8 - 0x0101010101010101ULL) & ~v_e8 & 0x8080808080808080ULL;
            uint64_t m_e9 = (v_e9 - 0x0101010101010101ULL) & ~v_e9 & 0x8080808080808080ULL;
            if ((m_e8 | m_e9) == 0) {
                i += 8;
                continue;
            }
        }
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9) {
            uint32_t src;
            memcpy(&src, data + i + 1, sizeof(src));
            uint32_t dest = src - (uint32_t)(i + 5);
            memcpy(data + i + 1, &dest, sizeof(dest));
            i += 5;
        } else {
            i++;
        }
    }
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
        if ((instr >> 26) == 0x25) {
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
        if ((instr >> 26) == 0x25) {
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
/* Cascaded Stride-N Transpose + Delta Prefilter                             */
/* ========================================================================= */

int binpack_transpose_delta_filter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 4 || stride < 2 || stride > 64) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;
    size_t remainder = size % stride;

    /* 1. Transpose: group byte 0s of all elements, then byte 1s, etc. */
    for (size_t s = 0; s < stride; s++) {
        size_t dst_base = s * num_elements;
        for (size_t i = 0; i < num_elements; i++) {
            temp[dst_base + i] = data[i * stride + s];
        }
    }
    if (remainder > 0) {
        memcpy(temp + (stride * num_elements), data + (stride * num_elements), remainder);
    }

    /* 2. Cascaded Delta-1 on highest significance bytes and exponents */
    /* For stride 4 (float32): byte 3 (exponent) and byte 2 have delta applied */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        uint8_t prev = 0;
        for (size_t i = 0; i < num_elements; i++) {
            uint8_t cur = temp[base + i];
            temp[base + i] = cur - prev;
            prev = cur;
        }
    }

    memcpy(data, temp, size);
    free(temp);
    return 0;
}

int binpack_transpose_delta_unfilter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 4 || stride < 2 || stride > 64) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;
    size_t remainder = size % stride;

    memcpy(temp, data, size);

    /* 1. Undo Delta-1 */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        uint8_t acc = 0;
        for (size_t i = 0; i < num_elements; i++) {
            acc = acc + temp[base + i];
            temp[base + i] = acc;
        }
    }

    /* 2. Undo Transpose */
    for (size_t s = 0; s < stride; s++) {
        size_t src_base = s * num_elements;
        for (size_t i = 0; i < num_elements; i++) {
            data[i * stride + s] = temp[src_base + i];
        }
    }
    if (remainder > 0) {
        memcpy(data + (stride * num_elements), temp + (stride * num_elements), remainder);
    }

    free(temp);
    return 0;
}

/* ========================================================================= */
/* Bitshuffle: Bit-Level Matrix Transposition (SWAR Accelerated)             */
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

int binpack_bitshuffle_filter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 8 || stride < 1 || stride > 64) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;
    size_t remainder = size % stride;

    /* 1. Byte-level channel segregation */
    for (size_t s = 0; s < stride; s++) {
        size_t dst_base = s * num_elements;
        for (size_t i = 0; i < num_elements; i++) {
            temp[dst_base + i] = data[i * stride + s];
        }
    }
    if (remainder > 0) {
        memcpy(temp + (stride * num_elements), data + (stride * num_elements), remainder);
    }

    /* 2. Bit transposition within 8-byte blocks of each channel */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        size_t full_blocks = num_elements / 8;
        for (size_t b = 0; b < full_blocks; b++) {
            uint64_t val;
            memcpy(&val, temp + base + (b * 8), sizeof(val));
            val = bp_transpose_8x8_bits(val);
            memcpy(temp + base + (b * 8), &val, sizeof(val));
        }
    }

    /* 3. First-order Delta difference across high-significance bitstreams */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        uint8_t prev = 0;
        for (size_t i = 0; i < num_elements; i++) {
            uint8_t cur = temp[base + i];
            temp[base + i] = cur - prev;
            prev = cur;
        }
    }

    memcpy(data, temp, size);
    free(temp);
    return 0;
}

int binpack_bitshuffle_unfilter(uint8_t *data, size_t size, uint16_t stride) {
    if (!data || size < 8 || stride < 1 || stride > 64) return -1;
    uint8_t *temp = (uint8_t*)malloc(size);
    if (!temp) return -1;

    size_t num_elements = size / stride;
    size_t remainder = size % stride;

    memcpy(temp, data, size);

    /* 1. Undo Delta difference */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        uint8_t acc = 0;
        for (size_t i = 0; i < num_elements; i++) {
            acc = acc + temp[base + i];
            temp[base + i] = acc;
        }
    }

    /* 2. Undo Bit transposition (transpose matrix again: (M^T)^T = M) */
    for (size_t s = 0; s < stride; s++) {
        size_t base = s * num_elements;
        size_t full_blocks = num_elements / 8;
        for (size_t b = 0; b < full_blocks; b++) {
            uint64_t val;
            memcpy(&val, temp + base + (b * 8), sizeof(val));
            val = bp_transpose_8x8_bits(val);
            memcpy(temp + base + (b * 8), &val, sizeof(val));
        }
    }

    /* 3. Undo Byte-level channel segregation */
    for (size_t s = 0; s < stride; s++) {
        size_t src_base = s * num_elements;
        for (size_t i = 0; i < num_elements; i++) {
            data[i * stride + s] = temp[src_base + i];
        }
    }
    if (remainder > 0) {
        memcpy(data + (stride * num_elements), temp + (stride * num_elements), remainder);
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
        /* BC1 (DXT1): 4 bytes endpoints (RGB565 x2) + 4 bytes indices */
        uint8_t *endpoints = temp;
        uint8_t *indices   = temp + (num_blocks * 4);

        for (size_t i = 0; i < num_blocks; i++) {
            memcpy(endpoints + (i * 4), data + (i * 8), 4);
            memcpy(indices + (i * 4), data + (i * 8) + 4, 4);
        }

        /* Delta-4 across endpoints */
        for (size_t i = num_blocks - 1; i > 0; i--) {
            uint32_t cur, prev;
            memcpy(&cur, endpoints + (i * 4), 4);
            memcpy(&prev, endpoints + ((i - 1) * 4), 4);
            uint32_t diff = cur - prev;
            memcpy(endpoints + (i * 4), &diff, 4);
        }
    } else {
        /* BC3 / BC7 (16 bytes): 8 bytes alpha/mode + 4 bytes endpoints + 4 bytes indices */
        uint8_t *alpha     = temp;
        uint8_t *endpoints = temp + (num_blocks * 8);
        uint8_t *indices   = temp + (num_blocks * 12);

        for (size_t i = 0; i < num_blocks; i++) {
            memcpy(alpha + (i * 8), data + (i * 16), 8);
            memcpy(endpoints + (i * 4), data + (i * 16) + 8, 4);
            memcpy(indices + (i * 4), data + (i * 16) + 12, 4);
        }

        /* Delta-4 across endpoints */
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

        /* Undo Delta-4 across endpoints */
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

        /* Undo Delta-4 across endpoints */
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

    /* Check DirectDraw Surface (DDS) or GPU texture header */
    if (data[0] == 'D' && data[1] == 'D' && data[2] == 'S' && data[3] == ' ') {
        *out_param = 8; /* Default BC1 */
        return BINPACK_FILTER_TEXTURE_BCN;
    }

    /* Check PE executable */
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

    /* Check ELF */
    if (data[0] == 0x7F && data[1] == 'E' && data[2] == 'L' && data[3] == 'F' && size >= 20) {
        uint16_t machine = *(const uint16_t*)&data[18];
        if (machine == 183) return BINPACK_FILTER_AARCH64; /* EM_AARCH64 */
        if (machine == 40) return BINPACK_FILTER_ARM32;    /* EM_ARM */
        return BINPACK_FILTER_X86_64;                      /* EM_386 / EM_X86_64 */
    }

    /* Check numerical sensor / float telemetry autocorrelation -> Bitshuffle */
    if (size >= 1024) {
        int diff_stride4_matches = 0;
        int diff_stride2_matches = 0;
        size_t test_len = size > 4096 ? 4096 : size;
        for (size_t i = 4; i < test_len; i++) {
            if (abs((int)data[i] - (int)data[i - 4]) < 4) diff_stride4_matches++;
            if (abs((int)data[i] - (int)data[i - 2]) < 4) diff_stride2_matches++;
        }
        if (diff_stride4_matches > (int)(test_len * 0.45)) {
            *out_param = 4;
            return BINPACK_FILTER_BITSHUFFLE;
        }
        if (diff_stride2_matches > (int)(test_len * 0.50)) {
            *out_param = 2;
            return BINPACK_FILTER_BITSHUFFLE;
        }
    }

    /* Check x86 jump frequency */
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
