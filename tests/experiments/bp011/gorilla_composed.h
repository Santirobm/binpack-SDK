/**
 * @file gorilla_composed.h
 * @brief Implementacion rigurosa de Gorilla F64 (Pelkonen et al. 2015) para tuplas compuestas.
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef GORILLA_COMPOSED_H
#define GORILLA_COMPOSED_H

#include "bp011_types.h"
#include "timeseries_bitstream_safe.h"

#if defined(_MSC_VER) && !defined(__clang__)
#include <intrin.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Rutinas Portables CLZ y CTZ de 64 Bits                                   */
/* ========================================================================= */
static inline uint8_t bp011_clz64(uint64_t val) {
    if (val == 0) return 64;
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanReverse64(&idx, val);
    return (uint8_t)(63 - idx);
#elif defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_clzll(val);
#else
    uint8_t count = 0;
    while ((val & (1ULL << 63)) == 0) {
        count++;
        val <<= 1;
    }
    return count;
#endif
}

static inline uint8_t bp011_ctz64(uint64_t val) {
    if (val == 0) return 64;
#if defined(_MSC_VER) && !defined(__clang__)
    unsigned long idx;
    _BitScanForward64(&idx, val);
    return (uint8_t)idx;
#elif defined(__GNUC__) || defined(__clang__)
    return (uint8_t)__builtin_ctzll(val);
#else
    uint8_t count = 0;
    while ((val & 1ULL) == 0) {
        count++;
        val >>= 1;
    }
    return count;
#endif
}

static inline uint64_t bp011_double_to_u64(double d) {
    uint64_t u;
    memcpy(&u, &d, sizeof(u));
    return u;
}

static inline double bp011_u64_to_double(uint64_t u) {
    double d;
    memcpy(&d, &u, sizeof(d));
    return d;
}

/* ========================================================================= */
/* Estructura de Estado de Gorilla F64                                       */
/* ========================================================================= */
typedef struct {
    uint64_t prev_val;
    uint8_t  prev_lz_eff;
    uint8_t  prev_tz;
    uint8_t  prev_len;
} gorilla_f64_state_t;

static inline void gorilla_f64_init(gorilla_f64_state_t *st) {
    if (!st) return;
    st->prev_val = 0;
    st->prev_lz_eff = 64;
    st->prev_tz = 64;
    st->prev_len = 0;
}

/* ========================================================================= */
/* Codificacion de Primer Valor (64 bits literales)                         */
/* ========================================================================= */
static inline bp011_status_t gorilla_f64_encode_first(gorilla_f64_state_t *st, uint64_t val, bp011_bitwriter_t *w) {
    if (!st || !w) return BP011_ERR_NULL_POINTER;
    bp011_status_t ret = bp011_bitwriter_write_bits(w, val, 64);
    if (ret != BP011_OK) return ret;

    st->prev_val = val;
    st->prev_lz_eff = 64;
    st->prev_tz = 64;
    st->prev_len = 0;
    return BP011_OK;
}

/* ========================================================================= */
/* Codificacion de Muestra Subsecuente                                      */
/* ========================================================================= */
static inline bp011_status_t gorilla_f64_encode_sample(gorilla_f64_state_t *st, uint64_t val, bp011_bitwriter_t *w) {
    if (!st || !w) return BP011_ERR_NULL_POINTER;
    uint64_t xor_val = val ^ st->prev_val;

    if (xor_val == 0) {
        /* Bit 0: valor identico al precedente */
        return bp011_bitwriter_write_bit(w, 0);
    }

    /* Bit 1: valor modificado */
    bp011_status_t ret = bp011_bitwriter_write_bit(w, 1);
    if (ret != BP011_OK) return ret;

    uint8_t lz = bp011_clz64(xor_val);
    uint8_t tz = bp011_ctz64(xor_val);
    uint8_t lz_eff = (lz > 31) ? 31 : lz;
    uint8_t len = (uint8_t)(64 - lz_eff - tz);

    if (lz_eff >= st->prev_lz_eff && tz >= st->prev_tz && st->prev_len > 0) {
        /* Control 0: reutilizacion de ventana previa */
        ret = bp011_bitwriter_write_bit(w, 0);
        if (ret != BP011_OK) return ret;

        /* Emite bits dentro de la ventana previa */
        uint64_t meaningful = (xor_val >> st->prev_tz) & bp011_make_mask64(st->prev_len);
        ret = bp011_bitwriter_write_bits(w, meaningful, st->prev_len);
        if (ret != BP011_OK) return ret;
    } else {
        /* Control 1: nueva ventana */
        ret = bp011_bitwriter_write_bit(w, 1);
        if (ret != BP011_OK) return ret;

        /* lz_eff en 5 bits (0 a 31) */
        ret = bp011_bitwriter_write_bits(w, (uint64_t)lz_eff, 5);
        if (ret != BP011_OK) return ret;

        /* len - 1 en 6 bits (0 a 63) */
        ret = bp011_bitwriter_write_bits(w, (uint64_t)(len - 1), 6);
        if (ret != BP011_OK) return ret;

        /* Bits significativos de longitud len */
        uint64_t meaningful = (xor_val >> tz) & bp011_make_mask64(len);
        ret = bp011_bitwriter_write_bits(w, meaningful, len);
        if (ret != BP011_OK) return ret;

        st->prev_lz_eff = lz_eff;
        st->prev_tz = tz;
        st->prev_len = len;
    }

    st->prev_val = val;
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion de Primer Valor                                            */
/* ========================================================================= */
static inline bp011_status_t gorilla_f64_decode_first(gorilla_f64_state_t *st, bp011_bitreader_t *r, uint64_t *out_val) {
    if (!st || !r || !out_val) return BP011_ERR_NULL_POINTER;
    uint64_t val = 0;
    bp011_status_t ret = bp011_bitreader_read_bits(r, 64, &val);
    if (ret != BP011_OK) return ret;

    st->prev_val = val;
    st->prev_lz_eff = 64;
    st->prev_tz = 64;
    st->prev_len = 0;
    *out_val = val;
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion de Muestra Subsecuente                                     */
/* ========================================================================= */
static inline bp011_status_t gorilla_f64_decode_sample(gorilla_f64_state_t *st, bp011_bitreader_t *r, uint64_t *out_val) {
    if (!st || !r || !out_val) return BP011_ERR_NULL_POINTER;

    uint8_t bit = 0;
    bp011_status_t ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* XOR es 0 */
        *out_val = st->prev_val;
        return BP011_OK;
    }

    /* Bit 1: XOR != 0. Leer control de ventana */
    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    uint64_t xor_val = 0;
    if (bit == 0) {
        /* Reutiliza ventana previa */
        if (st->prev_len == 0) return BP011_ERR_DATA_MISMATCH;
        uint64_t meaningful = 0;
        ret = bp011_bitreader_read_bits(r, st->prev_len, &meaningful);
        if (ret != BP011_OK) return ret;
        xor_val = meaningful << st->prev_tz;
    } else {
        /* Nueva ventana */
        uint64_t lz_eff_raw = 0;
        ret = bp011_bitreader_read_bits(r, 5, &lz_eff_raw);
        if (ret != BP011_OK) return ret;
        uint8_t lz_eff = (uint8_t)lz_eff_raw;

        uint64_t len_raw = 0;
        ret = bp011_bitreader_read_bits(r, 6, &len_raw);
        if (ret != BP011_OK) return ret;
        uint8_t len = (uint8_t)(len_raw + 1);

        if (lz_eff + len > 64) return BP011_ERR_DATA_MISMATCH;
        uint8_t tz = (uint8_t)(64 - lz_eff - len);

        uint64_t meaningful = 0;
        ret = bp011_bitreader_read_bits(r, len, &meaningful);
        if (ret != BP011_OK) return ret;

        xor_val = meaningful << tz;

        st->prev_lz_eff = lz_eff;
        st->prev_tz = tz;
        st->prev_len = len;
    }

    uint64_t val = st->prev_val ^ xor_val;
    st->prev_val = val;
    *out_val = val;
    return BP011_OK;
}

/* ========================================================================= */
/* Codificacion de Matriz de Canales (Gorilla_Composed)                      */
/* ========================================================================= */
static inline bp011_status_t gorilla_encode_channels(
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
        gorilla_f64_state_t st;
        gorilla_f64_init(&st);

        for (uint32_t i = 0; i < num_samples; i++) {
            /* Acceso columnar: matrix[k * num_samples + i] */
            double d = matrix[k * num_samples + i];
            uint64_t u = bp011_double_to_u64(d);

            bp011_status_t s;
            if (i == 0) {
                s = gorilla_f64_encode_first(&st, u, &writer);
            } else {
                s = gorilla_f64_encode_sample(&st, u, &writer);
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
static inline bp011_status_t gorilla_decode_channels(
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
        gorilla_f64_state_t st;
        gorilla_f64_init(&st);

        for (uint32_t i = 0; i < num_samples; i++) {
            uint64_t u = 0;
            bp011_status_t s;
            if (i == 0) {
                s = gorilla_f64_decode_first(&st, &reader, &u);
            } else {
                s = gorilla_f64_decode_sample(&st, &reader, &u);
            }
            if (s != BP011_OK) return s;
            out_matrix[k * num_samples + i] = bp011_u64_to_double(u);
        }
    }

    return BP011_OK;
}

/* ========================================================================= */
/* Consulta Selectiva Secuencial de Canal en Gorilla (Sin Tabla de Saltos)   */
/* ========================================================================= */
static inline bp011_status_t gorilla_decode_single_channel_sequential(
    const uint8_t *in_buf,
    size_t in_bytes,
    uint64_t in_bits,
    uint32_t target_channel,
    uint32_t num_samples,
    uint32_t num_channels,
    double *out_channel
) {
    if (!in_buf || !out_channel) return BP011_ERR_NULL_POINTER;
    if (target_channel >= num_channels) return BP011_ERR_INVALID_ARG;

    bp011_bitreader_t reader;
    bp011_bitreader_init(&reader, in_buf, in_bytes, in_bits);

    for (uint32_t k = 0; k <= target_channel; k++) {
        gorilla_f64_state_t st;
        gorilla_f64_init(&st);

        for (uint32_t i = 0; i < num_samples; i++) {
            uint64_t u = 0;
            bp011_status_t s;
            if (i == 0) {
                s = gorilla_f64_decode_first(&st, &reader, &u);
            } else {
                s = gorilla_f64_decode_sample(&st, &reader, &u);
            }
            if (s != BP011_OK) return s;

            if (k == target_channel) {
                out_channel[i] = bp011_u64_to_double(u);
            }
        }
    }

    return BP011_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* GORILLA_COMPOSED_H */
