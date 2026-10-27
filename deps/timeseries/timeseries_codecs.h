#ifndef TIMESERIES_CODECS_H
#define TIMESERIES_CODECS_H

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#if defined(_MSC_VER)
#include <intrin.h>
static inline int clz32(uint32_t x) { unsigned long r; return _BitScanReverse(&r, x) ? (31 - r) : 32; }
static inline int ctz32(uint32_t x) { unsigned long r; return _BitScanForward(&r, x) ? r : 32; }
static inline int clz64(uint64_t x) { unsigned long r; return _BitScanReverse64(&r, x) ? (63 - r) : 64; }
static inline int ctz64(uint64_t x) { unsigned long r; return _BitScanForward64(&r, x) ? r : 64; }
#else
static inline int clz32(uint32_t x) { return x ? __builtin_clz(x) : 32; }
static inline int ctz32(uint32_t x) { return x ? __builtin_ctz(x) : 32; }
static inline int clz64(uint64_t x) { return x ? __builtin_clzll(x) : 64; }
static inline int ctz64(uint64_t x) { return x ? __builtin_ctzll(x) : 64; }
#endif

/* ========================================================================= */
/* Fast BitStream Writer and Reader                                          */
/* ========================================================================= */

typedef struct {
    uint8_t *buf;
    size_t cap;
    size_t bit_pos;
} bit_writer_t;

static inline void bw_init(bit_writer_t *bw, uint8_t *buf, size_t cap) {
    bw->buf = buf;
    bw->cap = cap;
    bw->bit_pos = 0;
    if (cap > 0) buf[0] = 0;
}

static inline void bw_write_bit(bit_writer_t *bw, uint32_t bit) {
    size_t byte_idx = bw->bit_pos >> 3;
    size_t bit_idx = 7 - (bw->bit_pos & 7);
    if (bit_idx == 7) bw->buf[byte_idx] = 0;
    if (bit) bw->buf[byte_idx] |= (uint8_t)(1 << bit_idx);
    bw->bit_pos++;
}

static inline void bw_write_bits(bit_writer_t *bw, uint64_t val, int num_bits) {
    for (int i = num_bits - 1; i >= 0; i--) {
        bw_write_bit(bw, (uint32_t)((val >> i) & 1));
    }
}

static inline size_t bw_finish(bit_writer_t *bw) {
    return (bw->bit_pos + 7) >> 3;
}

typedef struct {
    const uint8_t *buf;
    size_t bit_len;
    size_t bit_pos;
} bit_reader_t;

static inline void br_init(bit_reader_t *br, const uint8_t *buf, size_t num_bytes) {
    br->buf = buf;
    br->bit_len = num_bytes * 8;
    br->bit_pos = 0;
}

static inline uint32_t br_read_bit(bit_reader_t *br) {
    size_t byte_idx = br->bit_pos >> 3;
    size_t bit_idx = 7 - (br->bit_pos & 7);
    br->bit_pos++;
    return (uint32_t)((br->buf[byte_idx] >> bit_idx) & 1);
}

static inline uint64_t br_read_bits(bit_reader_t *br, int num_bits) {
    uint64_t res = 0;
    for (int i = 0; i < num_bits; i++) {
        res = (res << 1) | br_read_bit(br);
    }
    return res;
}

/* ========================================================================= */
/* 1. Facebook Gorilla Float32 & Float64 Codec                               */
/* ========================================================================= */

static inline size_t gorilla_compress_f32(const float *src, size_t count, uint8_t *dst, size_t dst_cap) {
    if (count == 0) return 0;
    bit_writer_t bw;
    bw_init(&bw, dst, dst_cap);

    uint32_t prev_val;
    memcpy(&prev_val, &src[0], 4);
    bw_write_bits(&bw, prev_val, 32);

    int prev_lz = -1;
    int prev_tz = -1;

    for (size_t i = 1; i < count; i++) {
        uint32_t cur_val;
        memcpy(&cur_val, &src[i], 4);
        uint32_t xor_val = cur_val ^ prev_val;
        prev_val = cur_val;

        if (xor_val == 0) {
            bw_write_bit(&bw, 0);
        } else {
            bw_write_bit(&bw, 1);
            int lz = clz32(xor_val);
            int tz = ctz32(xor_val);

            if (prev_lz != -1 && lz >= prev_lz && tz >= prev_tz) {
                bw_write_bit(&bw, 0);
                int meaningful_bits = 32 - prev_lz - prev_tz;
                bw_write_bits(&bw, xor_val >> prev_tz, meaningful_bits);
            } else {
                bw_write_bit(&bw, 1);
                bw_write_bits(&bw, lz, 5);
                int meaningful_bits = 32 - lz - tz;
                bw_write_bits(&bw, meaningful_bits, 6);
                bw_write_bits(&bw, xor_val >> tz, meaningful_bits);
                prev_lz = lz;
                prev_tz = tz;
            }
        }
    }
    return bw_finish(&bw);
}

static inline void gorilla_decompress_f32(const uint8_t *src, size_t src_len, float *dst, size_t count) {
    if (count == 0) return;
    bit_reader_t br;
    br_init(&br, src, src_len);

    uint32_t prev_val = (uint32_t)br_read_bits(&br, 32);
    memcpy(&dst[0], &prev_val, 4);

    int prev_lz = -1;
    int prev_tz = -1;

    for (size_t i = 1; i < count; i++) {
        if (br_read_bit(&br) == 0) {
            memcpy(&dst[i], &prev_val, 4);
        } else {
            int meaningful_bits = 0;
            int tz = 0;
            if (br_read_bit(&br) == 0) {
                meaningful_bits = 32 - prev_lz - prev_tz;
                tz = prev_tz;
            } else {
                prev_lz = (int)br_read_bits(&br, 5);
                meaningful_bits = (int)br_read_bits(&br, 6);
                prev_tz = 32 - prev_lz - meaningful_bits;
                tz = prev_tz;
            }
            uint32_t bits = (uint32_t)br_read_bits(&br, meaningful_bits);
            uint32_t xor_val = bits << tz;
            uint32_t cur_val = prev_val ^ xor_val;
            memcpy(&dst[i], &cur_val, 4);
            prev_val = cur_val;
        }
    }
}

static inline size_t gorilla_compress_f64(const double *src, size_t count, uint8_t *dst, size_t dst_cap) {
    if (count == 0) return 0;
    bit_writer_t bw;
    bw_init(&bw, dst, dst_cap);

    uint64_t prev_val;
    memcpy(&prev_val, &src[0], 8);
    bw_write_bits(&bw, prev_val, 64);

    int prev_lz = -1;
    int prev_tz = -1;

    for (size_t i = 1; i < count; i++) {
        uint64_t cur_val;
        memcpy(&cur_val, &src[i], 8);
        uint64_t xor_val = cur_val ^ prev_val;
        prev_val = cur_val;

        if (xor_val == 0) {
            bw_write_bit(&bw, 0);
        } else {
            bw_write_bit(&bw, 1);
            int lz = clz64(xor_val);
            int tz = ctz64(xor_val);

            if (prev_lz != -1 && lz >= prev_lz && tz >= prev_tz) {
                bw_write_bit(&bw, 0);
                int meaningful_bits = 64 - prev_lz - prev_tz;
                bw_write_bits(&bw, xor_val >> prev_tz, meaningful_bits);
            } else {
                bw_write_bit(&bw, 1);
                bw_write_bits(&bw, lz, 6);
                int meaningful_bits = 64 - lz - tz;
                bw_write_bits(&bw, meaningful_bits, 6);
                bw_write_bits(&bw, xor_val >> tz, meaningful_bits);
                prev_lz = lz;
                prev_tz = tz;
            }
        }
    }
    return bw_finish(&bw);
}

static inline void gorilla_decompress_f64(const uint8_t *src, size_t src_len, double *dst, size_t count) {
    if (count == 0) return;
    bit_reader_t br;
    br_init(&br, src, src_len);

    uint64_t prev_val = (uint64_t)br_read_bits(&br, 64);
    memcpy(&dst[0], &prev_val, 8);

    int prev_lz = -1;
    int prev_tz = -1;

    for (size_t i = 1; i < count; i++) {
        if (br_read_bit(&br) == 0) {
            memcpy(&dst[i], &prev_val, 8);
        } else {
            int meaningful_bits = 0;
            int tz = 0;
            if (br_read_bit(&br) == 0) {
                meaningful_bits = 64 - prev_lz - prev_tz;
                tz = prev_tz;
            } else {
                prev_lz = (int)br_read_bits(&br, 6);
                meaningful_bits = (int)br_read_bits(&br, 6);
                if (meaningful_bits == 0) meaningful_bits = 64;
                prev_tz = 64 - prev_lz - meaningful_bits;
                tz = prev_tz;
            }
            uint64_t bits = (uint64_t)br_read_bits(&br, meaningful_bits);
            uint64_t xor_val = bits << tz;
            uint64_t cur_val = prev_val ^ xor_val;
            memcpy(&dst[i], &cur_val, 8);
            prev_val = cur_val;
        }
    }
}

/* ========================================================================= */
/* 2. Chimp (VLDB 2022) Floating Point Time-Series Codec                     */
/* ========================================================================= */

static inline size_t chimp_compress_f32(const float *src, size_t count, uint8_t *dst, size_t dst_cap) {
    if (count == 0) return 0;
    bit_writer_t bw;
    bw_init(&bw, dst, dst_cap);

    uint32_t prev_val;
    memcpy(&prev_val, &src[0], 4);
    bw_write_bits(&bw, prev_val, 32);

    int prev_lz = -1;
    int prev_tz = -1;

    for (size_t i = 1; i < count; i++) {
        uint32_t cur_val;
        memcpy(&cur_val, &src[i], 4);
        uint32_t xor_val = cur_val ^ prev_val;
        prev_val = cur_val;

        if (xor_val == 0) {
            bw_write_bits(&bw, 0, 2); /* Case 00: exact same */
        } else {
            int lz = clz32(xor_val);
            int tz = ctz32(xor_val);

            if (prev_lz != -1 && lz >= prev_lz && tz >= prev_tz) {
                bw_write_bits(&bw, 1, 2); /* Case 01: same bounds */
                int meaningful_bits = 32 - prev_lz - prev_tz;
                bw_write_bits(&bw, xor_val >> prev_tz, meaningful_bits);
            } else {
                bw_write_bits(&bw, 2, 2); /* Case 10: new bounds */
                int lz_quant = lz / 4;
                if (lz_quant > 7) lz_quant = 7;
                bw_write_bits(&bw, lz_quant, 3);
                int actual_lz = lz_quant * 4;
                int meaningful_bits = 32 - actual_lz - tz;
                bw_write_bits(&bw, meaningful_bits, 6);
                bw_write_bits(&bw, xor_val >> tz, meaningful_bits);
                prev_lz = actual_lz;
                prev_tz = tz;
            }
        }
    }
    return bw_finish(&bw);
}

static inline void chimp_decompress_f32(const uint8_t *src, size_t src_len, float *dst, size_t count) {
    if (count == 0) return;
    bit_reader_t br;
    br_init(&br, src, src_len);

    uint32_t prev_val = (uint32_t)br_read_bits(&br, 32);
    memcpy(&dst[0], &prev_val, 4);

    int prev_lz = -1;
    int prev_tz = -1;

    for (size_t i = 1; i < count; i++) {
        uint32_t case_code = (uint32_t)br_read_bits(&br, 2);
        if (case_code == 0) {
            memcpy(&dst[i], &prev_val, 4);
        } else if (case_code == 1) {
            int meaningful_bits = 32 - prev_lz - prev_tz;
            uint32_t bits = (uint32_t)br_read_bits(&br, meaningful_bits);
            uint32_t xor_val = bits << prev_tz;
            uint32_t cur_val = prev_val ^ xor_val;
            memcpy(&dst[i], &cur_val, 4);
            prev_val = cur_val;
        } else {
            int lz_quant = (int)br_read_bits(&br, 3);
            prev_lz = lz_quant * 4;
            int meaningful_bits = (int)br_read_bits(&br, 6);
            prev_tz = 32 - prev_lz - meaningful_bits;
            uint32_t bits = (uint32_t)br_read_bits(&br, meaningful_bits);
            uint32_t xor_val = bits << prev_tz;
            uint32_t cur_val = prev_val ^ xor_val;
            memcpy(&dst[i], &cur_val, 4);
            prev_val = cur_val;
        }
    }
}

/* ========================================================================= */
/* 3. Sprintz (VLDB 2018) Integer Forecasting & Bit-Packing Codec (Int16)    */
/* ========================================================================= */

static inline uint32_t zigzag_encode16(int16_t n) {
    return (uint32_t)((n << 1) ^ (n >> 15));
}

static inline int16_t zigzag_decode16(uint32_t n) {
    return (int16_t)((n >> 1) ^ (-(int16_t)(n & 1)));
}

static inline int bits_needed32(uint32_t n) {
    return n ? (32 - clz32(n)) : 0;
}

static inline size_t sprintz_compress_i16(const int16_t *src, size_t count, uint8_t *dst, size_t dst_cap) {
    if (count == 0) return 0;
    bit_writer_t bw;
    bw_init(&bw, dst, dst_cap);

    /* Write count (32 bits) */
    bw_write_bits(&bw, (uint32_t)count, 32);

    /* Process in blocks of 8 */
    int16_t prev = 0;
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        uint32_t zz[8];
        int max_bits = 0;
        for (int k = 0; k < 8; k++) {
            int16_t cur = src[i + k];
            int16_t delta = cur - prev;
            prev = cur;
            zz[k] = zigzag_encode16(delta);
            int b = bits_needed32(zz[k]);
            if (b > max_bits) max_bits = b;
        }
        if (max_bits > 16) max_bits = 16;
        /* Header: 5 bits for bit-width (0 to 16) */
        bw_write_bits(&bw, max_bits, 5);
        if (max_bits > 0) {
            for (int k = 0; k < 8; k++) {
                bw_write_bits(&bw, zz[k], max_bits);
            }
        }
    }

    /* Remainder */
    for (; i < count; i++) {
        int16_t cur = src[i];
        int16_t delta = cur - prev;
        prev = cur;
        uint32_t zz = zigzag_encode16(delta);
        int b = bits_needed32(zz);
        if (b > 16) b = 16;
        bw_write_bits(&bw, b, 5);
        if (b > 0) bw_write_bits(&bw, zz, b);
    }

    return bw_finish(&bw);
}

static inline void sprintz_decompress_i16(const uint8_t *src, size_t src_len, int16_t *dst, size_t count) {
    if (count == 0) return;
    bit_reader_t br;
    br_init(&br, src, src_len);

    uint32_t stored_count = (uint32_t)br_read_bits(&br, 32);
    int16_t prev = 0;
    size_t i = 0;
    for (; i + 8 <= count; i += 8) {
        int bit_width = (int)br_read_bits(&br, 5);
        for (int k = 0; k < 8; k++) {
            uint32_t zz = bit_width > 0 ? (uint32_t)br_read_bits(&br, bit_width) : 0;
            int16_t delta = zigzag_decode16(zz);
            int16_t cur = prev + delta;
            dst[i + k] = cur;
            prev = cur;
        }
    }
    for (; i < count; i++) {
        int b = (int)br_read_bits(&br, 5);
        uint32_t zz = b > 0 ? (uint32_t)br_read_bits(&br, b) : 0;
        int16_t delta = zigzag_decode16(zz);
        int16_t cur = prev + delta;
        dst[i] = cur;
        prev = cur;
    }
}

#endif /* TIMESERIES_CODECS_H */
