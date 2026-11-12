/**
 * @file binpack_timeseries_adapter.h
 * @brief Adaptador planar con transposicion de planos de bytes y empaquetado de bloques BinPack.
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef BINPACK_TIMESERIES_ADAPTER_H
#define BINPACK_TIMESERIES_ADAPTER_H

#include "bp011_types.h"
#include "gorilla_composed.h" /* Para bp011_double_to_u64 y bp011_u64_to_double */
#include "binpack_sdk.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Transposicion de Planos de Bytes (Byte-Plane Transposition)               */
/* ========================================================================= */
static inline void bp011_transpose_f64_forward(const double *src, uint8_t *dst, uint32_t count) {
    if (!src || !dst || count == 0) return;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t u = bp011_double_to_u64(src[i]);
        for (uint32_t b = 0; b < 8; b++) {
            dst[b * count + i] = (uint8_t)((u >> (b * 8)) & 0xFFU);
        }
    }
}

static inline void bp011_transpose_f64_inverse(const uint8_t *src, double *dst, uint32_t count) {
    if (!src || !dst || count == 0) return;
    for (uint32_t i = 0; i < count; i++) {
        uint64_t u = 0;
        for (uint32_t b = 0; b < 8; b++) {
            u |= ((uint64_t)src[b * count + i]) << (b * 8);
        }
        dst[i] = bp011_u64_to_double(u);
    }
}

/* ========================================================================= */
/* Compresion de Matriz de Canales con BinPack Transpuesto (M7)             */
/* ========================================================================= */
static inline bp011_status_t binpack_encode_channels(
    const double *matrix,
    uint32_t num_samples,
    uint32_t num_channels,
    uint8_t *out_buf,
    size_t out_cap,
    size_t *out_bytes,
    uint64_t *out_bits,
    bp011_channel_entry_t *out_channel_table,
    uint32_t channel_base_offset
) {
    if (!matrix || !out_buf || !out_bytes || !out_bits || !out_channel_table) {
        return BP011_ERR_NULL_POINTER;
    }
    if (num_samples == 0 || num_channels == 0) return BP011_ERR_INVALID_ARG;

    size_t raw_chan_bytes = (size_t)num_samples * sizeof(double);
    uint8_t *transposed_tmp = (uint8_t *)bp011_tracked_malloc(raw_chan_bytes);
    if (!transposed_tmp) return BP011_ERR_BUFFER_OVERFLOW;

    size_t cur_offset = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.mode = BINPACK_MODE_DEFAULT;

    for (uint32_t k = 0; k < num_channels; k++) {
        const double *chan_src = &matrix[k * num_samples];
        bp011_transpose_f64_forward(chan_src, transposed_tmp, num_samples);

        size_t max_comp_sz = binpack_compress_bound(raw_chan_bytes);
        if (cur_offset + max_comp_sz > out_cap) {
            bp011_tracked_free(transposed_tmp);
            return BP011_ERR_BUFFER_OVERFLOW;
        }

        size_t comp_sz = 0;
        binpack_status_t bp_st = binpack_compress_into(
            transposed_tmp, raw_chan_bytes,
            out_buf + cur_offset, out_cap - cur_offset,
            &comp_sz, &opts
        );

        if (bp_st != BINPACK_OK) {
            bp011_tracked_free(transposed_tmp);
            return BP011_ERR_BUFFER_OVERFLOW;
        }

        out_channel_table[k].byte_offset = channel_base_offset + (uint32_t)cur_offset;
        out_channel_table[k].byte_length = (uint32_t)comp_sz;
        out_channel_table[k].bit_length  = (uint32_t)(comp_sz * 8U);

        cur_offset += comp_sz;
    }

    bp011_tracked_free(transposed_tmp);
    *out_bytes = cur_offset;
    *out_bits = (uint64_t)cur_offset * 8ULL;
    return BP011_OK;
}

/* ========================================================================= */
/* Decodificacion Completa de Matriz de Canales BinPack                      */
/* ========================================================================= */
static inline bp011_status_t binpack_decode_channels(
    const uint8_t *in_buf,
    size_t in_bytes,
    const bp011_channel_entry_t *channel_table,
    uint32_t channel_base_offset,
    double *out_matrix,
    uint32_t num_samples,
    uint32_t num_channels
) {
    if (!in_buf || !channel_table || !out_matrix) return BP011_ERR_NULL_POINTER;
    if (num_samples == 0 || num_channels == 0) return BP011_ERR_INVALID_ARG;

    size_t raw_chan_bytes = (size_t)num_samples * sizeof(double);
    uint8_t *transposed_tmp = (uint8_t *)bp011_tracked_malloc(raw_chan_bytes);
    if (!transposed_tmp) return BP011_ERR_BUFFER_OVERFLOW;

    for (uint32_t k = 0; k < num_channels; k++) {
        uint32_t rel_offset = channel_table[k].byte_offset - channel_base_offset;
        uint32_t comp_bytes = channel_table[k].byte_length;

        if (rel_offset + comp_bytes > in_bytes) {
            bp011_tracked_free(transposed_tmp);
            return BP011_ERR_STREAM_TRUNCATED;
        }

        size_t decomp_sz = 0;
        binpack_status_t bp_st = binpack_decompress_into(
            in_buf + rel_offset, (size_t)comp_bytes,
            transposed_tmp, raw_chan_bytes,
            &decomp_sz, 1
        );

        if (bp_st != BINPACK_OK || decomp_sz != raw_chan_bytes) {
            bp011_tracked_free(transposed_tmp);
            return BP011_ERR_DATA_MISMATCH;
        }

        double *chan_dst = &out_matrix[k * num_samples];
        bp011_transpose_f64_inverse(transposed_tmp, chan_dst, num_samples);
    }

    bp011_tracked_free(transposed_tmp);
    return BP011_OK;
}

/* ========================================================================= */
/* Consulta Selectiva Directa de Canal en BinPack (O(1) Seek via Tabla M7)   */
/* ========================================================================= */
static inline bp011_status_t binpack_decode_single_channel(
    const uint8_t *in_buf,
    size_t in_bytes,
    const bp011_channel_entry_t *channel_table,
    uint32_t channel_base_offset,
    uint32_t target_channel,
    double *out_channel,
    uint32_t num_samples,
    uint32_t num_channels
) {
    if (!in_buf || !channel_table || !out_channel) return BP011_ERR_NULL_POINTER;
    if (target_channel >= num_channels || num_samples == 0) return BP011_ERR_INVALID_ARG;

    uint32_t rel_offset = channel_table[target_channel].byte_offset - channel_base_offset;
    uint32_t comp_bytes = channel_table[target_channel].byte_length;

    if (rel_offset + comp_bytes > in_bytes) return BP011_ERR_STREAM_TRUNCATED;

    size_t raw_chan_bytes = (size_t)num_samples * sizeof(double);
    uint8_t *transposed_tmp = (uint8_t *)bp011_tracked_malloc(raw_chan_bytes);
    if (!transposed_tmp) return BP011_ERR_BUFFER_OVERFLOW;

    size_t decomp_sz = 0;
    binpack_status_t bp_st = binpack_decompress_into(
        in_buf + rel_offset, (size_t)comp_bytes,
        transposed_tmp, raw_chan_bytes,
        &decomp_sz, 1
    );

    if (bp_st != BINPACK_OK || decomp_sz != raw_chan_bytes) {
        bp011_tracked_free(transposed_tmp);
        return BP011_ERR_DATA_MISMATCH;
    }

    bp011_transpose_f64_inverse(transposed_tmp, out_channel, num_samples);
    bp011_tracked_free(transposed_tmp);
    return BP011_OK;
}

/* ========================================================================= */
/* Ensamblaje Canónico de Bloque BinPack con Cabecera de 64 Bytes e Integridad*/
/* ========================================================================= */
static inline bp011_status_t bp011_package_block(
    uint8_t *file_buf,
    size_t file_cap,
    size_t *actual_file_bytes,
    uint32_t num_channels,
    uint32_t num_samples,
    const bp011_channel_entry_t *chan_table,
    const bp011_series_segment_t *series_dir,
    uint32_t num_series,
    const uint8_t *time_stream,
    size_t time_bytes,
    uint64_t time_bits,
    const uint8_t *values_stream,
    size_t values_bytes,
    uint64_t values_bits
) {
    if (!file_buf || !actual_file_bytes || !time_stream || !values_stream) {
        return BP011_ERR_NULL_POINTER;
    }

    uint32_t cursor = BP011_HEADER_SIZE_BYTES;
    bp011_block_header_t h;
    memset(&h, 0, sizeof(h));

    h.magic = BP011_BLOCK_MAGIC;
    h.version = BP011_FORMAT_VERSION;
    h.num_channels = num_channels;
    h.sample_count = num_samples;

    /* Tabla de canales (M7) */
    if (chan_table) {
        h.flags |= BP011_FLAG_HAS_CHANNEL_TABLE;
        h.channel_table_offset = cursor;
        h.channel_table_bytes = num_channels * (uint32_t)sizeof(bp011_channel_entry_t);
        if (cursor + h.channel_table_bytes > file_cap) return BP011_ERR_BUFFER_OVERFLOW;
        memcpy(file_buf + cursor, chan_table, h.channel_table_bytes);
        cursor += h.channel_table_bytes;
    }

    /* Directorio de series C-MAPSS */
    if (series_dir && num_series > 0) {
        h.flags |= BP011_FLAG_HAS_SERIES_DIR;
        h.series_dir_offset = cursor;
        h.series_dir_bytes = num_series * (uint32_t)sizeof(bp011_series_segment_t);
        if (cursor + h.series_dir_bytes > file_cap) return BP011_ERR_BUFFER_OVERFLOW;
        memcpy(file_buf + cursor, series_dir, h.series_dir_bytes);
        cursor += h.series_dir_bytes;
    }

    /* Stream de tiempo */
    h.time_stream_offset = cursor;
    h.time_stream_bytes  = (uint32_t)time_bytes;
    h.time_stream_bits   = (uint32_t)time_bits;
    if (cursor + time_bytes > file_cap) return BP011_ERR_BUFFER_OVERFLOW;
    memcpy(file_buf + cursor, time_stream, time_bytes);
    cursor += (uint32_t)time_bytes;

    /* Stream de valores */
    h.values_stream_offset = cursor;
    h.values_stream_bytes  = (uint32_t)values_bytes;
    h.values_stream_bits   = (uint32_t)values_bits;
    if (cursor + values_bytes > file_cap) return BP011_ERR_BUFFER_OVERFLOW;
    memcpy(file_buf + cursor, values_stream, values_bytes);
    cursor += (uint32_t)values_bytes;

    h.total_file_bytes = cursor;
    h.crc32 = 0; /* CRC temporal para calcular sobre el archivo */

    bp011_serialize_header(file_buf, &h);

    /* Calculo de CRC-32 IEEE 802.3 sobre el archivo completo */
    uint32_t calculated_crc = bp011_crc32(file_buf, cursor);
    h.crc32 = calculated_crc;
    bp011_serialize_header(file_buf, &h);

    *actual_file_bytes = cursor;

    /* Verificacion incondicional de validacion tras empaquetado */
    bp011_status_t val_st = bp011_validate_header(&h, file_buf, cursor);
    if (val_st != BP011_OK) return val_st;

    if (series_dir && num_series > 0) {
        bp011_status_t dir_st = bp011_verify_series_directory(&h, series_dir, num_series);
        if (dir_st != BP011_OK) return dir_st;
    }

    return BP011_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* BINPACK_TIMESERIES_ADAPTER_H */
