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
/* BCJ x86 / x64 Branch Address Normalization                                */
/* ========================================================================= */

void binpack_bcj_x86_filter(uint8_t *data, size_t size) {
    if (!data || size < 5) return;
    size_t i = 0;
    while (i <= size - 5) {
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9) {
            uint32_t src = (uint32_t)data[i+1] | ((uint32_t)data[i+2] << 8) |
                           ((uint32_t)data[i+3] << 16) | ((uint32_t)data[i+4] << 24);
            uint32_t dest = src + (uint32_t)(i + 5);
            data[i+1] = (uint8_t)(dest);
            data[i+2] = (uint8_t)(dest >> 8);
            data[i+3] = (uint8_t)(dest >> 16);
            data[i+4] = (uint8_t)(dest >> 24);
            i += 5;
        } else {
            i++;
        }
    }
}

void binpack_bcj_x86_unfilter(uint8_t *data, size_t size) {
    if (!data || size < 5) return;
    size_t i = 0;
    while (i <= size - 5) {
        uint8_t b = data[i];
        if (b == 0xE8 || b == 0xE9) {
            uint32_t src = (uint32_t)data[i+1] | ((uint32_t)data[i+2] << 8) |
                           ((uint32_t)data[i+3] << 16) | ((uint32_t)data[i+4] << 24);
            uint32_t dest = src - (uint32_t)(i + 5);
            data[i+1] = (uint8_t)(dest);
            data[i+2] = (uint8_t)(dest >> 8);
            data[i+3] = (uint8_t)(dest >> 16);
            data[i+4] = (uint8_t)(dest >> 24);
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
/* Automatic Filter Selection Heuristic                                      */
/* ========================================================================= */

binpack_filter_t binpack_auto_select_filter(const uint8_t *data, size_t size, uint16_t *out_param) {
    *out_param = 0;
    if (!data || size < 64) return BINPACK_FILTER_NONE;

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

    /* Check numerical sensor / float telemetry autocorrelation */
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
            return BINPACK_FILTER_TRANSPOSE_DELTA;
        }
        if (diff_stride2_matches > (int)(test_len * 0.50)) {
            *out_param = 2;
            return BINPACK_FILTER_TRANSPOSE_DELTA;
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
