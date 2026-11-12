/**
 * @file timeseries_timestamp_codec.h
 * @brief Implementacion literal del codec de timestamps delta-of-delta segun Protocolo R15.10.
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef TIMESERIES_TIMESTAMP_CODEC_H
#define TIMESERIES_TIMESTAMP_CODEC_H

#include "bp011_types.h"
#include "timeseries_bitstream_safe.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Estructura de Estado del Predictor Temporal R15.10                        */
/* ========================================================================= */
typedef struct {
    int64_t prev_timestamp;
    int64_t prev_delta;
    bool    has_prev_delta;
} bp011_timestamp_codec_state_t;

static inline void bp011_timestamp_codec_init(bp011_timestamp_codec_state_t *st) {
    if (!st) return;
    st->prev_timestamp = 0;
    st->prev_delta = 0;
    st->has_prev_delta = false;
}

/* ========================================================================= */
/* Emision de Escape Absoluto (Prefijo 111111 + 64 bits literales = 70 bits)  */
/* ========================================================================= */
static inline bp011_status_t bp011_timestamp_encode_escape(bp011_timestamp_codec_state_t *st, int64_t t_i, bp011_bitwriter_t *w) {
    if (!st || !w) return BP011_ERR_NULL_POINTER;

    /* Prefijo 111111 (6 bits binarios = 0x3F) */
    bp011_status_t ret = bp011_bitwriter_write_bits(w, 0x3FU, 6);
    if (ret != BP011_OK) return ret;

    /* Carga literal de 64 bits */
    ret = bp011_bitwriter_write_bits(w, (uint64_t)t_i, 64);
    if (ret != BP011_OK) return ret;

    st->prev_timestamp = t_i;
    st->prev_delta = 0;
    st->has_prev_delta = false;
    return BP011_OK;
}

/* ========================================================================= */
/* Codificacion de Muestra Temporal R15.10                                   */
/* ========================================================================= */
static inline bp011_status_t bp011_timestamp_encode_sample(bp011_timestamp_codec_state_t *st, int64_t t_i, bp011_bitwriter_t *w) {
    if (!st || !w) return BP011_ERR_NULL_POINTER;

    if (!st->has_prev_delta) {
        /* Evaluacion de la primera diferencia simple tras escape o inicio */
        bool overflow = false;
        if (st->prev_timestamp > 0 && t_i < INT64_MIN + st->prev_timestamp) overflow = true;
        if (st->prev_timestamp < 0 && t_i > INT64_MAX + st->prev_timestamp) overflow = true;

        if (overflow) {
            return bp011_timestamp_encode_escape(st, t_i, w);
        }

        int64_t delta_init = t_i - st->prev_timestamp;

        /* Prefijo 111110 (6 bits binarios = 0x3E) */
        bp011_status_t ret = bp011_bitwriter_write_bits(w, 0x3EU, 6);
        if (ret != BP011_OK) return ret;

        /* Carga de delta inicial de 64 bits con signo */
        ret = bp011_bitwriter_write_bits(w, (uint64_t)delta_init, 64);
        if (ret != BP011_OK) return ret;

        st->prev_timestamp = t_i;
        st->prev_delta = delta_init;
        st->has_prev_delta = true;
        return BP011_OK;
    }

    /* Regimen Delta-of-Delta pleno */
    bool diff_overflow = false;
    if (st->prev_timestamp > 0 && t_i < INT64_MIN + st->prev_timestamp) diff_overflow = true;
    if (st->prev_timestamp < 0 && t_i > INT64_MAX + st->prev_timestamp) diff_overflow = true;

    if (diff_overflow) {
        return bp011_timestamp_encode_escape(st, t_i, w);
    }

    int64_t cur_delta = t_i - st->prev_timestamp;

    bool dod_overflow = false;
    if (st->prev_delta > 0 && cur_delta < INT64_MIN + st->prev_delta) dod_overflow = true;
    if (st->prev_delta < 0 && cur_delta > INT64_MAX + st->prev_delta) dod_overflow = true;

    if (dod_overflow) {
        return bp011_timestamp_encode_escape(st, t_i, w);
    }

    int64_t D = cur_delta - st->prev_delta;

    bp011_status_t ret;
    if (D == 0) {
        /* Prefijo 0 (1 bit) */
        ret = bp011_bitwriter_write_bit(w, 0);
        if (ret != BP011_OK) return ret;
    } else if (D >= -63 && D <= 64) {
        /* Prefijo 10 (2 bits) + 7 bits sesgados: D_7 = D + 63 in [0, 127] */
        ret = bp011_bitwriter_write_bits(w, 0x02U, 2);
        if (ret != BP011_OK) return ret;
        uint64_t d7 = (uint64_t)(D + 63);
        ret = bp011_bitwriter_write_bits(w, d7, 7);
        if (ret != BP011_OK) return ret;
    } else if (D >= -255 && D <= 256) {
        /* Prefijo 110 (3 bits) + 9 bits sesgados: D_9 = D + 255 in [0, 511] */
        ret = bp011_bitwriter_write_bits(w, 0x06U, 3);
        if (ret != BP011_OK) return ret;
        uint64_t d9 = (uint64_t)(D + 255);
        ret = bp011_bitwriter_write_bits(w, d9, 9);
        if (ret != BP011_OK) return ret;
    } else if (D >= -2047 && D <= 2048) {
        /* Prefijo 1110 (4 bits) + 12 bits sesgados: D_12 = D + 2047 in [0, 4095] */
        ret = bp011_bitwriter_write_bits(w, 0x0EU, 4);
        if (ret != BP011_OK) return ret;
        uint64_t d12 = (uint64_t)(D + 2047);
        ret = bp011_bitwriter_write_bits(w, d12, 12);
        if (ret != BP011_OK) return ret;
    } else if (D >= -2147483648LL && D <= 2147483647LL) {
        /* Prefijo 11110 (5 bits) + 32 bits complemento a 2 */
        ret = bp011_bitwriter_write_bits(w, 0x1EU, 5);
        if (ret != BP011_OK) return ret;
        uint32_t d32 = (uint32_t)((int32_t)D);
        ret = bp011_bitwriter_write_bits(w, (uint64_t)d32, 32);
        if (ret != BP011_OK) return ret;
    } else {
        /* Prefijo 111110 (6 bits) + 64 bits con signo */
        ret = bp011_bitwriter_write_bits(w, 0x3EU, 6);
        if (ret != BP011_OK) return ret;
        ret = bp011_bitwriter_write_bits(w, (uint64_t)D, 64);
        if (ret != BP011_OK) return ret;
    }

    st->prev_timestamp = t_i;
    st->prev_delta = cur_delta;
    st->has_prev_delta = true;
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion de Muestra Temporal R15.10                                 */
/* ========================================================================= */
static inline bp011_status_t bp011_timestamp_decode_sample(bp011_timestamp_codec_state_t *st, bp011_bitreader_t *r, int64_t *out_t_i) {
    if (!st || !r || !out_t_i) return BP011_ERR_NULL_POINTER;

    uint8_t bit = 0;
    bp011_status_t ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* Prefijo 0: D = 0 */
        if (!st->has_prev_delta) return BP011_ERR_DATA_MISMATCH;
        int64_t t_i = st->prev_timestamp + st->prev_delta;
        st->prev_timestamp = t_i;
        *out_t_i = t_i;
        return BP011_OK;
    }

    /* Bit 1 leido, leer siguiente bit */
    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* Prefijo 10: 7 bits sesgados */
        uint64_t raw = 0;
        ret = bp011_bitreader_read_bits(r, 7, &raw);
        if (ret != BP011_OK) return ret;
        int64_t D = (int64_t)raw - 63;
        int64_t cur_delta = st->prev_delta + D;
        int64_t t_i = st->prev_timestamp + cur_delta;
        st->prev_timestamp = t_i;
        st->prev_delta = cur_delta;
        st->has_prev_delta = true;
        *out_t_i = t_i;
        return BP011_OK;
    }

    /* Prefijo 11..., leer siguiente bit */
    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* Prefijo 110: 9 bits sesgados */
        uint64_t raw = 0;
        ret = bp011_bitreader_read_bits(r, 9, &raw);
        if (ret != BP011_OK) return ret;
        int64_t D = (int64_t)raw - 255;
        int64_t cur_delta = st->prev_delta + D;
        int64_t t_i = st->prev_timestamp + cur_delta;
        st->prev_timestamp = t_i;
        st->prev_delta = cur_delta;
        st->has_prev_delta = true;
        *out_t_i = t_i;
        return BP011_OK;
    }

    /* Prefijo 111..., leer siguiente bit */
    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* Prefijo 1110: 12 bits sesgados */
        uint64_t raw = 0;
        ret = bp011_bitreader_read_bits(r, 12, &raw);
        if (ret != BP011_OK) return ret;
        int64_t D = (int64_t)raw - 2047;
        int64_t cur_delta = st->prev_delta + D;
        int64_t t_i = st->prev_timestamp + cur_delta;
        st->prev_timestamp = t_i;
        st->prev_delta = cur_delta;
        st->has_prev_delta = true;
        *out_t_i = t_i;
        return BP011_OK;
    }

    /* Prefijo 1111..., leer siguiente bit */
    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* Prefijo 11110: 32 bits complemento a 2 */
        uint64_t raw = 0;
        ret = bp011_bitreader_read_bits(r, 32, &raw);
        if (ret != BP011_OK) return ret;
        int64_t D = (int64_t)(int32_t)(uint32_t)raw;
        int64_t cur_delta = st->prev_delta + D;
        int64_t t_i = st->prev_timestamp + cur_delta;
        st->prev_timestamp = t_i;
        st->prev_delta = cur_delta;
        st->has_prev_delta = true;
        *out_t_i = t_i;
        return BP011_OK;
    }

    /* Prefijo 11111..., leer siguiente bit */
    ret = bp011_bitreader_read_bit(r, &bit);
    if (ret != BP011_OK) return ret;

    if (bit == 0) {
        /* Prefijo 111110: 64 bits con signo */
        uint64_t raw = 0;
        ret = bp011_bitreader_read_bits(r, 64, &raw);
        if (ret != BP011_OK) return ret;
        int64_t val = (int64_t)raw;
        if (!st->has_prev_delta) {
            /* Delta inicial */
            int64_t t_i = st->prev_timestamp + val;
            st->prev_timestamp = t_i;
            st->prev_delta = val;
            st->has_prev_delta = true;
            *out_t_i = t_i;
        } else {
            /* Delta-of-delta de 64 bits */
            int64_t cur_delta = st->prev_delta + val;
            int64_t t_i = st->prev_timestamp + cur_delta;
            st->prev_timestamp = t_i;
            st->prev_delta = cur_delta;
            st->has_prev_delta = true;
            *out_t_i = t_i;
        }
        return BP011_OK;
    }

    /* Prefijo 111111: Escape absoluto de 64 bits */
    uint64_t raw = 0;
    ret = bp011_bitreader_read_bits(r, 64, &raw);
    if (ret != BP011_OK) return ret;
    int64_t t_i = (int64_t)raw;
    st->prev_timestamp = t_i;
    st->prev_delta = 0;
    st->has_prev_delta = false;
    *out_t_i = t_i;
    return BP011_OK;
}

/* ========================================================================= */
/* Codificacion de Stream Completo de Timestamps                             */
/* ========================================================================= */
static inline bp011_status_t bp011_timestamp_encode_stream(
    const int64_t *timestamps,
    uint32_t count,
    const bp011_series_segment_t *series_dir,
    uint32_t num_series,
    uint8_t *out_buf,
    size_t out_cap,
    size_t *out_bytes,
    uint64_t *out_bits
) {
    if (!timestamps || !out_buf || !out_bytes || !out_bits) return BP011_ERR_NULL_POINTER;
    if (count == 0) return BP011_ERR_INVALID_ARG;

    bp011_bitwriter_t writer;
    bp011_bitwriter_init(&writer, out_buf, out_cap);

    bp011_timestamp_codec_state_t st;
    bp011_timestamp_codec_init(&st);

    uint32_t next_series_idx = 0;

    for (uint32_t i = 0; i < count; i++) {
        bp011_status_t s;
        if (i == 0) {
            /* Muestra inicial: Escape absoluto obligatorio */
            s = bp011_timestamp_encode_escape(&st, timestamps[i], &writer);
            if (series_dir && num_series > 0 && next_series_idx < num_series) {
                if (series_dir[next_series_idx].start_sample_idx == 0) {
                    next_series_idx++;
                }
            }
        } else if (series_dir && next_series_idx < num_series && i == series_dir[next_series_idx].start_sample_idx) {
            /* Frontera de motor en C-MAPSS: Escape absoluto obligatorio (Seccion 3.2) */
            s = bp011_timestamp_encode_escape(&st, timestamps[i], &writer);
            next_series_idx++;
        } else {
            /* Muestra regular */
            s = bp011_timestamp_encode_sample(&st, timestamps[i], &writer);
        }
        if (s != BP011_OK) return s;
    }

    bp011_status_t flush_st = bp011_bitwriter_flush(&writer);
    if (flush_st != BP011_OK) return flush_st;

    *out_bytes = bp011_bitwriter_get_bytes(&writer);
    *out_bits = bp011_bitwriter_get_bits(&writer);
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion de Stream Completo de Timestamps                           */
/* ========================================================================= */
static inline bp011_status_t bp011_timestamp_decode_stream(
    const uint8_t *in_buf,
    size_t in_bytes,
    uint64_t in_bits,
    int64_t *out_timestamps,
    uint32_t count
) {
    if (!in_buf || !out_timestamps) return BP011_ERR_NULL_POINTER;
    if (count == 0) return BP011_ERR_INVALID_ARG;

    bp011_bitreader_t reader;
    bp011_bitreader_init(&reader, in_buf, in_bytes, in_bits);

    bp011_timestamp_codec_state_t st;
    bp011_timestamp_codec_init(&st);

    for (uint32_t i = 0; i < count; i++) {
        bp011_status_t s = bp011_timestamp_decode_sample(&st, &reader, &out_timestamps[i]);
        if (s != BP011_OK) return s;
    }

    return BP011_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* TIMESERIES_TIMESTAMP_CODEC_H */
