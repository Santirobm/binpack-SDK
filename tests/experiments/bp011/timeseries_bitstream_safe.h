/**
 * @file timeseries_bitstream_safe.h
 * @brief Lector y escritor seguro de flujos de bits a nivel de memoria con orden MSB-first.
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef TIMESERIES_BITSTREAM_SAFE_H
#define TIMESERIES_BITSTREAM_SAFE_H

#include "bp011_types.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Estructura del Escritor de Bits Seguro (MSB-first)                        */
/* ========================================================================= */
typedef struct {
    uint8_t *buf;            /* Buffer de salida */
    size_t   capacity_bytes; /* Capacidad maxima en bytes */
    size_t   byte_pos;       /* Cursor de bytes escritos en buf */
    uint64_t total_bits;     /* Contador total de bits utiles escritos */
    uint8_t  cur_byte;       /* Acumulador del byte actual en construccion */
    uint8_t  bits_in_cur;    /* Cantidad de bits acumulados en cur_byte (0 a 7) */
} bp011_bitwriter_t;

static inline bp011_status_t bp011_bitwriter_init(bp011_bitwriter_t *w, uint8_t *buf, size_t capacity) {
    if (!w || !buf) return BP011_ERR_NULL_POINTER;
    if (capacity == 0) return BP011_ERR_INVALID_ARG;
    w->buf = buf;
    w->capacity_bytes = capacity;
    w->byte_pos = 0;
    w->total_bits = 0;
    w->cur_byte = 0;
    w->bits_in_cur = 0;
    return BP011_OK;
}

static inline bp011_status_t bp011_bitwriter_write_bit(bp011_bitwriter_t *w, uint8_t bit) {
    if (!w || !w->buf) return BP011_ERR_NULL_POINTER;

    /* Si este bit completara el byte (bits_in_cur == 7), verifica capacidad ANTES de mutar estado (NC-D04) */
    if (w->bits_in_cur == 7) {
        if (w->byte_pos >= w->capacity_bytes) {
            return BP011_ERR_BUFFER_OVERFLOW;
        }
        uint8_t full_byte = (uint8_t)((w->cur_byte << 1) | (bit & 1U));
        w->buf[w->byte_pos++] = full_byte;
        w->cur_byte = 0;
        w->bits_in_cur = 0;
        w->total_bits++;
        return BP011_OK;
    }

    w->cur_byte = (uint8_t)((w->cur_byte << 1) | (bit & 1U));
    w->bits_in_cur++;
    w->total_bits++;
    return BP011_OK;
}

static inline bp011_status_t bp011_bitwriter_write_bits(bp011_bitwriter_t *w, uint64_t val, uint8_t num_bits) {
    if (!w || !w->buf) return BP011_ERR_NULL_POINTER;
    if (num_bits > 64) return BP011_ERR_INVALID_ARG;
    if (num_bits == 0) return BP011_OK;

    for (int i = (int)num_bits - 1; i >= 0; i--) {
        uint8_t b = (uint8_t)((val >> i) & 1ULL);
        bp011_status_t st = bp011_bitwriter_write_bit(w, b);
        if (st != BP011_OK) return st;
    }
    return BP011_OK;
}

static inline bp011_status_t bp011_bitwriter_flush(bp011_bitwriter_t *w) {
    if (!w || !w->buf) return BP011_ERR_NULL_POINTER;
    if (w->bits_in_cur > 0) {
        if (w->byte_pos >= w->capacity_bytes) {
            return BP011_ERR_BUFFER_OVERFLOW;
        }
        /* Desplaza los bits utiles hacia los bits mas significativos (MSB) */
        uint8_t pad = (uint8_t)(8 - w->bits_in_cur);
        uint8_t flushed_byte = (uint8_t)(w->cur_byte << pad);

        w->buf[w->byte_pos++] = flushed_byte;
        w->cur_byte = 0;
        w->bits_in_cur = 0;
    }
    return BP011_OK;
}

static inline size_t bp011_bitwriter_get_bytes(const bp011_bitwriter_t *w) {
    return w ? w->byte_pos : 0;
}

static inline uint64_t bp011_bitwriter_get_bits(const bp011_bitwriter_t *w) {
    return w ? w->total_bits : 0;
}

/* ========================================================================= */
/* Estructura del Lector de Bits Seguro (MSB-first)                         */
/* ========================================================================= */
typedef struct {
    const uint8_t *buf;         /* Buffer de entrada */
    size_t         total_bytes; /* Longitud del buffer en bytes */
    uint64_t       total_bits;  /* Cantidad exacta de bits utiles */
    uint64_t       bit_cursor;  /* Cursor actual de lectura en bits */
} bp011_bitreader_t;

static inline bp011_status_t bp011_bitreader_init(bp011_bitreader_t *r, const uint8_t *buf, size_t total_bytes, uint64_t total_bits) {
    if (!r || !buf) return BP011_ERR_NULL_POINTER;
    /* Comprobacion incondicional de consistencia de longitud (NC-D04) */
    if (total_bits > (uint64_t)total_bytes * 8ULL) {
        return BP011_ERR_INVALID_ARG;
    }
    r->buf = buf;
    r->total_bytes = total_bytes;
    r->total_bits = total_bits;
    r->bit_cursor = 0;
    return BP011_OK;
}

static inline bp011_status_t bp011_bitreader_read_bit(bp011_bitreader_t *r, uint8_t *out_bit) {
    if (!r || !r->buf || !out_bit) return BP011_ERR_NULL_POINTER;
    if (r->bit_cursor >= r->total_bits) return BP011_ERR_STREAM_TRUNCATED;

    size_t byte_idx = (size_t)(r->bit_cursor / 8);
    if (byte_idx >= r->total_bytes) return BP011_ERR_BUFFER_UNDERFLOW;

    uint8_t bit_idx = (uint8_t)(7 - (r->bit_cursor % 8));
    *out_bit = (uint8_t)((r->buf[byte_idx] >> bit_idx) & 1U);
    r->bit_cursor++;
    return BP011_OK;
}

static inline bp011_status_t bp011_bitreader_read_bits(bp011_bitreader_t *r, uint8_t num_bits, uint64_t *out_val) {
    if (!r || !r->buf || !out_val) return BP011_ERR_NULL_POINTER;
    if (num_bits > 64) return BP011_ERR_INVALID_ARG;
    *out_val = 0;
    if (num_bits == 0) return BP011_OK;

    /* Comprobacion por resta segura para evitar overflow en bit_cursor + num_bits (NC-D04) */
    if ((uint64_t)num_bits > r->total_bits - r->bit_cursor) {
        return BP011_ERR_STREAM_TRUNCATED;
    }

    uint64_t val = 0;
    for (uint8_t i = 0; i < num_bits; i++) {
        size_t byte_idx = (size_t)(r->bit_cursor / 8);
        if (byte_idx >= r->total_bytes) return BP011_ERR_BUFFER_UNDERFLOW;

        uint8_t bit_idx = (uint8_t)(7 - (r->bit_cursor % 8));
        uint8_t b = (uint8_t)((r->buf[byte_idx] >> bit_idx) & 1U);
        val = (val << 1) | (uint64_t)b;
        r->bit_cursor++;
    }
    *out_val = val;
    return BP011_OK;
}

static inline bool bp011_bitreader_has_bits(const bp011_bitreader_t *r, uint64_t num_bits) {
    if (!r) return false;
    if (num_bits > r->total_bits - r->bit_cursor) return false;
    return true;
}

#ifdef __cplusplus
}
#endif

#endif /* TIMESERIES_BITSTREAM_SAFE_H */
