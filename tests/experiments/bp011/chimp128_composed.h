/**
 * @file chimp128_composed.h
 * @brief Implementacion rigurosa de Chimp128_Composed_Variant con guarda de ventana valida (Liakos et al. 2022).
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef CHIMP128_COMPOSED_H
#define CHIMP128_COMPOSED_H

#include "bp011_types.h"
#include "timeseries_bitstream_safe.h"
#include "gorilla_composed.h" /* Para bp011_clz64, bp011_ctz64, bp011_double_to_u64 */

#ifdef __cplusplus
extern "C" {
#endif

#define CHIMP128_RING_SIZE        128U
#define CHIMP128_HASH_TABLE_SIZE  16384U /* 2^14 entradas */

/* ========================================================================= */
/* Estructura de Estado de Codificacion de Chimp128                          */
/* ========================================================================= */
typedef struct {
    uint64_t ring_buf[CHIMP128_RING_SIZE];
    uint32_t ring_sample_idx[CHIMP128_RING_SIZE];
    int32_t  hash_table[CHIMP128_HASH_TABLE_SIZE];
    uint64_t prev_val;
    uint8_t  prev_lz_quant;
    uint8_t  prev_tz;
    uint8_t  prev_len;
    bool     has_valid_window; /* Guarda: false hasta emitir la primera ventana no nula */
} chimp128_f64_state_t;

/* ========================================================================= */
/* Estructura de Estado de Decodificacion de Chimp128                        */
/* ========================================================================= */
typedef struct {
    uint64_t ring_buf[CHIMP128_RING_SIZE];
    uint64_t prev_val;
    uint8_t  prev_lz_quant;
    uint8_t  prev_tz;
    uint8_t  prev_len;
    bool     has_valid_window;
} chimp128_f64_decoder_state_t;

static inline void chimp128_f64_init(chimp128_f64_state_t *st) {
    if (!st) return;
    memset(st->ring_buf, 0, sizeof(st->ring_buf));
    memset(st->ring_sample_idx, 0, sizeof(st->ring_sample_idx));
    for (uint32_t i = 0; i < CHIMP128_HASH_TABLE_SIZE; i++) {
        st->hash_table[i] = -1;
    }
    st->prev_val = 0;
    st->prev_lz_quant = 0;
    st->prev_tz = 0;
    st->prev_len = 0;
    st->has_valid_window = false;
}

static inline void chimp128_f64_decoder_init(chimp128_f64_decoder_state_t *st) {
    if (!st) return;
    memset(st->ring_buf, 0, sizeof(st->ring_buf));
    st->prev_val = 0;
    st->prev_lz_quant = 0;
    st->prev_tz = 0;
    st->prev_len = 0;
    st->has_valid_window = false;
}

/* ========================================================================= */
/* Codificacion de Primer Valor (64 bits literales)                         */
/* ========================================================================= */
static inline bp011_status_t chimp128_f64_encode_first(chimp128_f64_state_t *st, uint64_t val, bp011_bitwriter_t *w) {
    if (!st || !w) return BP011_ERR_NULL_POINTER;
    bp011_status_t ret = bp011_bitwriter_write_bits(w, val, 64);
    if (ret != BP011_OK) return ret;

    st->prev_val = val;
    st->ring_buf[0] = val;
    st->ring_sample_idx[0] = 0;
    st->hash_table[val & 0x3FFFU] = 0;
    st->has_valid_window = false;
    return BP011_OK;
}

/* ========================================================================= */
/* Codificacion de Muestra Subsecuente con Algoritmo Chimp128                */
/* ========================================================================= */
static inline bp011_status_t chimp128_f64_encode_sample(chimp128_f64_state_t *st, uint32_t i, uint64_t val, bp011_bitwriter_t *w) {
    if (!st || !w) return BP011_ERR_NULL_POINTER;

    uint32_t key = (uint32_t)(val & 0x3FFFU);
    int32_t c = st->hash_table[key];
    bool is_valid_cand = (c >= 0) && (1 <= ((int32_t)i - c)) && (((int32_t)i - c) <= 128);

    uint64_t v_ref;
    bp011_status_t ret;

    if (is_valid_cand) {
        /* Bit 1 + 7 bits ring index */
        ret = bp011_bitwriter_write_bit(w, 1);
        if (ret != BP011_OK) return ret;
        uint32_t ring_idx = (uint32_t)c % 128U;
        ret = bp011_bitwriter_write_bits(w, (uint64_t)ring_idx, 7);
        if (ret != BP011_OK) return ret;
        v_ref = st->ring_buf[ring_idx];
    } else {
        /* Bit 0: referencia precedente */
        ret = bp011_bitwriter_write_bit(w, 0);
        if (ret != BP011_OK) return ret;
        v_ref = st->prev_val;
    }

    uint64_t xor_val = val ^ v_ref;
    if (xor_val == 0) {
        ret = bp011_bitwriter_write_bit(w, 0);
        if (ret != BP011_OK) return ret;
    } else {
        ret = bp011_bitwriter_write_bit(w, 1);
        if (ret != BP011_OK) return ret;

        uint8_t lz = bp011_clz64(xor_val);
        uint8_t tz = bp011_ctz64(xor_val);
        uint8_t lz_quant = (uint8_t)(lz / 8);
        if (lz_quant > 7) lz_quant = 7;

        bool can_reuse = st->has_valid_window && (lz_quant == st->prev_lz_quant) && (tz == st->prev_tz);
        if (can_reuse) {
            /* Control 0: reutilizacion de ventana previa */
            ret = bp011_bitwriter_write_bit(w, 0);
            if (ret != BP011_OK) return ret;

            uint64_t meaningful = (xor_val >> st->prev_tz) & bp011_make_mask64(st->prev_len);
            ret = bp011_bitwriter_write_bits(w, meaningful, st->prev_len);
            if (ret != BP011_OK) return ret;
        } else {
            /* Control 1: nueva ventana */
            ret = bp011_bitwriter_write_bit(w, 1);
            if (ret != BP011_OK) return ret;

            uint8_t len = (uint8_t)(64 - (lz_quant * 8) - tz);

            /* lz_quant en 3 bits (0 a 7) */
            ret = bp011_bitwriter_write_bits(w, (uint64_t)lz_quant, 3);
            if (ret != BP011_OK) return ret;

            /* len - 1 en 6 bits (0 a 63) */
            ret = bp011_bitwriter_write_bits(w, (uint64_t)(len - 1), 6);
            if (ret != BP011_OK) return ret;

            /* meaningful en len bits */
            uint64_t meaningful = (xor_val >> tz) & bp011_make_mask64(len);
            ret = bp011_bitwriter_write_bits(w, meaningful, len);
            if (ret != BP011_OK) return ret;

            st->prev_lz_quant = lz_quant;
            st->prev_tz = tz;
            st->prev_len = len;
            st->has_valid_window = true;
        }
    }

    uint32_t ring_pos = i % 128U;
    st->ring_buf[ring_pos] = val;
    st->ring_sample_idx[ring_pos] = i;
    st->hash_table[key] = (int32_t)i;
    st->prev_val = val;
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion de Primer Valor                                            */
/* ========================================================================= */
static inline bp011_status_t chimp128_f64_decode_first(chimp128_f64_decoder_state_t *st, bp011_bitreader_t *r, uint64_t *out_val) {
    if (!st || !r || !out_val) return BP011_ERR_NULL_POINTER;
    uint64_t val = 0;
    bp011_status_t ret = bp011_bitreader_read_bits(r, 64, &val);
    if (ret != BP011_OK) return ret;

    st->prev_val = val;
    st->ring_buf[0] = val;
    st->has_valid_window = false;
    *out_val = val;
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion de Muestra Subsecuente                                     */
/* ========================================================================= */
static inline bp011_status_t chimp128_f64_decode_sample(chimp128_f64_decoder_state_t *st, uint32_t i, bp011_bitreader_t *r, uint64_t *out_val) {
    if (!st || !r || !out_val) return BP011_ERR_NULL_POINTER;

    uint8_t bit = 0;
    bp011_status_t ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    uint64_t v_ref;
    if (bit == 1) {
        uint64_t ring_idx = 0;
        ret = bp011_bitreader_read_bits(r, 7, &ring_idx);
        if (ret != BP011_OK) return ret;
        v_ref = st->ring_buf[(uint32_t)ring_idx];
    } else {
        v_ref = st->prev_val;
    }

    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    uint64_t xor_val = 0;
    if (bit == 0) {
        xor_val = 0;
    } else {
        ret = bp011_bitreader_read_bit(r, &bit);
        if (ret != BP011_OK) return ret;

        if (bit == 0) {
            /* Reutiliza ventana previa */
            if (!st->has_valid_window) return BP011_ERR_DATA_MISMATCH;
            uint64_t meaningful = 0;
            ret = bp011_bitreader_read_bits(r, st->prev_len, &meaningful);
            if (ret != BP011_OK) return ret;
            xor_val = meaningful << st->prev_tz;
        } else {
            /* Nueva ventana */
            uint64_t lz_quant_raw = 0;
            ret = bp011_bitreader_read_bits(r, 3, &lz_quant_raw);
            if (ret != BP011_OK) return ret;
            uint8_t lz_quant = (uint8_t)lz_quant_raw;

            uint64_t len_raw = 0;
            ret = bp011_bitreader_read_bits(r, 6, &len_raw);
            if (ret != BP011_OK) return ret;
            uint8_t len = (uint8_t)(len_raw + 1);

            if ((lz_quant * 8) + len > 64) return BP011_ERR_DATA_MISMATCH;
            uint8_t tz = (uint8_t)(64 - (lz_quant * 8) - len);

            uint64_t meaningful = 0;
            ret = bp011_bitreader_read_bits(r, len, &meaningful);
            if (ret != BP011_OK) return ret;

            xor_val = meaningful << tz;

            st->prev_lz_quant = lz_quant;
            st->prev_tz = tz;
            st->prev_len = len;
            st->has_valid_window = true;
        }
    }

    uint64_t val = v_ref ^ xor_val;
    st->ring_buf[i % 128U] = val;
    st->prev_val = val;
    *out_val = val;
    return BP011_OK;
}

/* ========================================================================= */
/* Codificacion de Matriz de Canales (Chimp128_Composed_Variant)             */
/* ========================================================================= */
static inline bp011_status_t chimp128_encode_channels(
    const double *matrix,
    uint32_t num_samples,
    uint32_t num_channels,
    uint8_t *out_buf,
    size_t out_cap,
    size_t *out_bytes,
    uint64_t *out_bits
) {
    if (!matrix || !out_buf || !out_bytes || !out_bits) return BP011_ERR_NULL_POINTER;
    if (num_samples == 0 || num_channels == 0) return BP011_ERR_INVALID_ARG;

    bp011_bitwriter_t writer;
    bp011_bitwriter_init(&writer, out_buf, out_cap);

    for (uint32_t k = 0; k < num_channels; k++) {
        chimp128_f64_state_t st;
        chimp128_f64_init(&st);

        for (uint32_t i = 0; i < num_samples; i++) {
            double d = matrix[k * num_samples + i];
            uint64_t u = bp011_double_to_u64(d);

            bp011_status_t s;
            if (i == 0) {
                s = chimp128_f64_encode_first(&st, u, &writer);
            } else {
                s = chimp128_f64_encode_sample(&st, i, u, &writer);
            }
            if (s != BP011_OK) return s;
        }
    }

    bp011_status_t flush_st = bp011_bitwriter_flush(&writer);
    if (flush_st != BP011_OK) return flush_st;

    *out_bytes = bp011_bitwriter_get_bytes(&writer);
    *out_bits = bp011_bitwriter_get_bits(&writer);
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion Completa de Matriz de Canales                             */
/* ========================================================================= */
static inline bp011_status_t chimp128_decode_channels(
    const uint8_t *in_buf,
    size_t in_bytes,
    uint64_t in_bits,
    double *out_matrix,
    uint32_t num_samples,
    uint32_t num_channels
) {
    if (!in_buf || !out_matrix) return BP011_ERR_NULL_POINTER;
    if (num_samples == 0 || num_channels == 0) return BP011_ERR_INVALID_ARG;

    bp011_bitreader_t reader;
    bp011_bitreader_init(&reader, in_buf, in_bytes, in_bits);

    for (uint32_t k = 0; k < num_channels; k++) {
        chimp128_f64_decoder_state_t st;
        chimp128_f64_decoder_init(&st);

        for (uint32_t i = 0; i < num_samples; i++) {
            uint64_t u = 0;
            bp011_status_t s;
            if (i == 0) {
                s = chimp128_f64_decode_first(&st, &reader, &u);
            } else {
                s = chimp128_f64_decode_sample(&st, i, &reader, &u);
            }
            if (s != BP011_OK) return s;
            out_matrix[k * num_samples + i] = bp011_u64_to_double(u);
        }
    }

    return BP011_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* CHIMP128_COMPOSED_H */
