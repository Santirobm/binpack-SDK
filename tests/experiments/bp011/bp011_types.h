/**
 * @file bp011_types.h
 * @brief Tipos fundamentales, cabecera de 64 bytes, validaciones y asignador rastreador para BP-011.
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef BP011_TYPES_H
#define BP011_TYPES_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* Códigos de Estado y Error Unívocos                                        */
/* ========================================================================= */
typedef enum {
    BP011_OK                      =  0,
    BP011_ERR_NULL_POINTER        = -1,
    BP011_ERR_INVALID_ARG         = -2,
    BP011_ERR_BUFFER_OVERFLOW     = -3,
    BP011_ERR_STREAM_TRUNCATED    = -4,
    BP011_ERR_BUFFER_UNDERFLOW    = -5,
    BP011_ERR_CORRUPT_PADDING     = -6,
    BP011_ERR_CORRUPT_CRC         = -7,
    BP011_ERR_CORRUPT_DIRECTORY   = -8,
    BP011_ERR_INVALID_OFFSET      = -9,
    BP011_ERR_UNKNOWN_FLAGS       = -10,
    BP011_ERR_EMPTY_SERIES        = -11,
    BP011_ERR_OVERLAPPING_SEGMENT = -12,
    BP011_ERR_UNALIGNED_BUFFER    = -13,
    BP011_ERR_DATA_MISMATCH       = -14
} bp011_status_t;

/* ========================================================================= */
/* Constantes de Cabecera y Formato Binario                                  */
/* ========================================================================= */
#define BP011_BLOCK_MAGIC             0x4B504E42U /* "BNPK" en Little-Endian */
#define BP011_FORMAT_VERSION          0x0001U
#define BP011_HEADER_SIZE_BYTES       64U

#define BP011_FLAG_HAS_CHANNEL_TABLE  (1U << 0)
#define BP011_FLAG_HAS_SERIES_DIR     (1U << 1)
#define BP011_KNOWN_FLAGS             (BP011_FLAG_HAS_CHANNEL_TABLE | BP011_FLAG_HAS_SERIES_DIR)

/* ========================================================================= */
/* Estructura Canónica de Cabecera (64 Bytes)                               */
/* ========================================================================= */
typedef struct {
    uint32_t magic;                 /* 0x00: 0x4B504E42 ("BNPK") */
    uint16_t version;               /* 0x04: 0x0001 */
    uint16_t flags;                 /* 0x06: Flags de presencia */
    uint32_t num_channels;          /* 0x08: K (canales de datos) */
    uint32_t sample_count;          /* 0x0C: N (muestras temporales) */
    uint32_t channel_table_offset;  /* 0x10: Offset de tabla M7 (0 si ausente) */
    uint32_t channel_table_bytes;   /* 0x14: Bytes de tabla M7 (K * 12 si presente) */
    uint32_t series_dir_offset;     /* 0x18: Offset directorio series (0 si ausente) */
    uint32_t series_dir_bytes;      /* 0x1C: Bytes directorio (M * 32 si presente) */
    uint32_t time_stream_offset;    /* 0x20: Offset obligatorio stream tiempo */
    uint32_t time_stream_bytes;     /* 0x24: Bytes obligatorios stream tiempo */
    uint32_t time_stream_bits;      /* 0x28: Bits obligatorios stream tiempo */
    uint32_t values_stream_offset;  /* 0x2C: Offset obligatorio stream valores */
    uint32_t values_stream_bytes;   /* 0x30: Bytes obligatorios stream valores */
    uint32_t values_stream_bits;    /* 0x34: Bits obligatorios stream valores */
    uint32_t total_file_bytes;      /* 0x38: Tamaño total del bloque */
    uint32_t crc32;                 /* 0x3C: CRC-32 (IEEE 802.3) con crc32=0 */
} bp011_block_header_t;

/* ========================================================================= */
/* Estructura de Entrada en Tabla de Saltos de Canales (M7, 12 Bytes)        */
/* ========================================================================= */
typedef struct {
    uint32_t byte_offset;
    uint32_t byte_length;
    uint32_t bit_length;
} bp011_channel_entry_t;

/* ========================================================================= */
/* Estructura de Segmento en Directorio de Series C-MAPSS (32 Bytes)        */
/* ========================================================================= */
typedef struct {
    uint32_t unit_id;                  /* Identificador del motor (1 a M) */
    uint32_t start_sample_idx;         /* Muestra inicial absoluta (0 a N-1) */
    uint32_t sample_count;             /* Cantidad de ciclos (> 0) */
    uint32_t start_cycle;              /* Ciclo inicial físico (ej. 1) */
    uint32_t time_stream_byte_offset;  /* Offset de tiempo del motor */
    uint32_t time_stream_bytes;        /* Longitud en bytes de tiempo del motor */
    uint32_t values_stream_byte_offset;/* Offset de valores del motor */
    uint32_t values_stream_bytes;      /* Longitud en bytes de valores del motor */
} bp011_series_segment_t;

/* ========================================================================= */
/* Rutinas de Serialización / Deserialización Little-Endian Explícitas      */
/* ========================================================================= */
static inline void bp011_write_le16(uint8_t *dst, uint16_t val) {
    dst[0] = (uint8_t)(val & 0xFFU);
    dst[1] = (uint8_t)((val >> 8) & 0xFFU);
}

static inline void bp011_write_le32(uint8_t *dst, uint32_t val) {
    dst[0] = (uint8_t)(val & 0xFFU);
    dst[1] = (uint8_t)((val >> 8) & 0xFFU);
    dst[2] = (uint8_t)((val >> 16) & 0xFFU);
    dst[3] = (uint8_t)((val >> 24) & 0xFFU);
}

static inline void bp011_write_le64(uint8_t *dst, uint64_t val) {
    for (int i = 0; i < 8; i++) {
        dst[i] = (uint8_t)((val >> (i * 8)) & 0xFFULL);
    }
}

static inline uint16_t bp011_read_le16(const uint8_t *src) {
    return (uint16_t)src[0] | ((uint16_t)src[1] << 8);
}

static inline uint32_t bp011_read_le32(const uint8_t *src) {
    return (uint32_t)src[0] | ((uint32_t)src[1] << 8) |
           ((uint32_t)src[2] << 16) | ((uint32_t)src[3] << 24);
}

static inline uint64_t bp011_read_le64(const uint8_t *src) {
    uint64_t val = 0;
    for (int i = 0; i < 8; i++) {
        val |= ((uint64_t)src[i] << (i * 8));
    }
    return val;
}

/* ========================================================================= */
/* Serialización y Deserialización de Cabecera bp011_block_header_t          */
/* ========================================================================= */
static inline void bp011_serialize_header(uint8_t *dst, const bp011_block_header_t *h) {
    bp011_write_le32(dst + 0x00, h->magic);
    bp011_write_le16(dst + 0x04, h->version);
    bp011_write_le16(dst + 0x06, h->flags);
    bp011_write_le32(dst + 0x08, h->num_channels);
    bp011_write_le32(dst + 0x0C, h->sample_count);
    bp011_write_le32(dst + 0x10, h->channel_table_offset);
    bp011_write_le32(dst + 0x14, h->channel_table_bytes);
    bp011_write_le32(dst + 0x18, h->series_dir_offset);
    bp011_write_le32(dst + 0x1C, h->series_dir_bytes);
    bp011_write_le32(dst + 0x20, h->time_stream_offset);
    bp011_write_le32(dst + 0x24, h->time_stream_bytes);
    bp011_write_le32(dst + 0x28, h->time_stream_bits);
    bp011_write_le32(dst + 0x2C, h->values_stream_offset);
    bp011_write_le32(dst + 0x30, h->values_stream_bytes);
    bp011_write_le32(dst + 0x34, h->values_stream_bits);
    bp011_write_le32(dst + 0x38, h->total_file_bytes);
    bp011_write_le32(dst + 0x3C, h->crc32);
}

static inline void bp011_deserialize_header(bp011_block_header_t *h, const uint8_t *src) {
    h->magic                = bp011_read_le32(src + 0x00);
    h->version              = bp011_read_le16(src + 0x04);
    h->flags                = bp011_read_le16(src + 0x06);
    h->num_channels         = bp011_read_le32(src + 0x08);
    h->sample_count         = bp011_read_le32(src + 0x0C);
    h->channel_table_offset = bp011_read_le32(src + 0x10);
    h->channel_table_bytes  = bp011_read_le32(src + 0x14);
    h->series_dir_offset    = bp011_read_le32(src + 0x18);
    h->series_dir_bytes     = bp011_read_le32(src + 0x1C);
    h->time_stream_offset   = bp011_read_le32(src + 0x20);
    h->time_stream_bytes    = bp011_read_le32(src + 0x24);
    h->time_stream_bits     = bp011_read_le32(src + 0x28);
    h->values_stream_offset = bp011_read_le32(src + 0x2C);
    h->values_stream_bytes  = bp011_read_le32(src + 0x30);
    h->values_stream_bits   = bp011_read_le32(src + 0x34);
    h->total_file_bytes     = bp011_read_le32(src + 0x38);
    h->crc32                = bp011_read_le32(src + 0x3C);
}

/* ========================================================================= */
/* Máscara de 64 Bits Segura (Inmune a Comportamiento Indefinido)           */
/* ========================================================================= */
static inline uint64_t bp011_make_mask64(uint8_t len) {
    if (len == 0) return 0ULL;
    if (len >= 64) return ~0ULL;
    return (1ULL << len) - 1ULL;
}

/* ========================================================================= */
/* Cálculo Canónico de CRC-32 (IEEE 802.3, Polinomio 0xEDB88320)             */
/* ========================================================================= */
static inline uint32_t bp011_crc32(const uint8_t *buf, size_t len) {
    uint32_t crc = 0xFFFFFFFFU;
    for (size_t i = 0; i < len; i++) {
        crc ^= (uint32_t)buf[i];
        for (int b = 0; b < 8; b++) {
            if (crc & 1U) {
                crc = (crc >> 1) ^ 0xEDB88320U;
            } else {
                crc >>= 1;
            }
        }
    }
    return ~crc;
}

/* ========================================================================= */
/* Validación Incondicional de Cabecera y No Solapamiento                    */
/* ========================================================================= */
typedef struct {
    uint32_t start;
    uint32_t end;
    bool active;
} bp011_range_t;

static inline bool bp011_ranges_overlap(const bp011_range_t *a, const bp011_range_t *b) {
    if (!a->active || !b->active) return false;
    return !(a->end <= b->start || b->end <= a->start);
}

static inline bp011_status_t bp011_validate_header(const bp011_block_header_t *h, const uint8_t *file_buf, uint64_t actual_file_size) {
    if (!h || !file_buf) return BP011_ERR_NULL_POINTER;
    if (actual_file_size < BP011_HEADER_SIZE_BYTES) return BP011_ERR_BUFFER_UNDERFLOW;
    if (h->magic != BP011_BLOCK_MAGIC) return BP011_ERR_INVALID_ARG;
    if (h->version != BP011_FORMAT_VERSION) return BP011_ERR_INVALID_ARG;
    if (h->total_file_bytes > actual_file_size) return BP011_ERR_STREAM_TRUNCATED;
    if (h->total_file_bytes < BP011_HEADER_SIZE_BYTES) return BP011_ERR_BUFFER_UNDERFLOW;

    if ((h->flags & ~BP011_KNOWN_FLAGS) != 0) return BP011_ERR_UNKNOWN_FLAGS;
    if (h->sample_count == 0 || h->num_channels == 0) return BP011_ERR_INVALID_ARG;
    if (h->time_stream_bytes == 0 || h->time_stream_bits == 0) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bytes == 0 || h->values_stream_bits == 0) return BP011_ERR_INVALID_ARG;

    if (h->time_stream_bits > (uint64_t)h->time_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;
    if (h->values_stream_bits > (uint64_t)h->values_stream_bytes * 8ULL) return BP011_ERR_INVALID_ARG;

    bool has_chan = (h->flags & BP011_FLAG_HAS_CHANNEL_TABLE) != 0;
    if (!has_chan && (h->channel_table_offset != 0 || h->channel_table_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_chan && (h->channel_table_offset < BP011_HEADER_SIZE_BYTES || h->channel_table_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    bool has_series = (h->flags & BP011_FLAG_HAS_SERIES_DIR) != 0;
    if (!has_series && (h->series_dir_offset != 0 || h->series_dir_bytes != 0)) return BP011_ERR_INVALID_OFFSET;
    if (has_series && (h->series_dir_offset < BP011_HEADER_SIZE_BYTES || h->series_dir_bytes == 0)) return BP011_ERR_INVALID_OFFSET;

    #define BP011_CHECK_BOUNDS(off, bytes) do { \
        if ((off) < BP011_HEADER_SIZE_BYTES) return BP011_ERR_INVALID_OFFSET; \
        if ((off) > h->total_file_bytes) return BP011_ERR_INVALID_OFFSET; \
        if ((bytes) > h->total_file_bytes - (off)) return BP011_ERR_INVALID_OFFSET; \
    } while (0)

    if (has_chan) BP011_CHECK_BOUNDS(h->channel_table_offset, h->channel_table_bytes);
    if (has_series) BP011_CHECK_BOUNDS(h->series_dir_offset, h->series_dir_bytes);
    BP011_CHECK_BOUNDS(h->time_stream_offset, h->time_stream_bytes);
    BP011_CHECK_BOUNDS(h->values_stream_offset, h->values_stream_bytes);
    #undef BP011_CHECK_BOUNDS

    bp011_range_t ranges[5] = {
        { 0, BP011_HEADER_SIZE_BYTES, true },
        { h->channel_table_offset, h->channel_table_offset + h->channel_table_bytes, has_chan },
        { h->series_dir_offset, h->series_dir_offset + h->series_dir_bytes, has_series },
        { h->time_stream_offset, h->time_stream_offset + h->time_stream_bytes, true },
        { h->values_stream_offset, h->values_stream_offset + h->values_stream_bytes, true }
    };

    for (int i = 0; i < 5; i++) {
        for (int j = i + 1; j < 5; j++) {
            if (bp011_ranges_overlap(&ranges[i], &ranges[j])) return BP011_ERR_INVALID_OFFSET;
        }
    }

    /* Inspección segura de padding: ejecutada estrictamente tras validar límites */
    uint8_t time_pad = (uint8_t)((h->time_stream_bytes * 8ULL) - h->time_stream_bits);
    if (time_pad > 0 && time_pad < 8) {
        uint8_t last_byte = file_buf[h->time_stream_offset + h->time_stream_bytes - 1];
        if ((last_byte & ((1U << time_pad) - 1U)) != 0) return BP011_ERR_CORRUPT_PADDING;
    }

    uint8_t val_pad = (uint8_t)((h->values_stream_bytes * 8ULL) - h->values_stream_bits);
    if (val_pad > 0 && val_pad < 8) {
        uint8_t last_byte = file_buf[h->values_stream_offset + h->values_stream_bytes - 1];
        if ((last_byte & ((1U << val_pad) - 1U)) != 0) return BP011_ERR_CORRUPT_PADDING;
    }

    return BP011_OK;
}

/* ========================================================================= */
/* Validación Segura del Directorio C-MAPSS (Anti-Underflow)                */
/* ========================================================================= */
static inline bp011_status_t bp011_verify_series_directory(const bp011_block_header_t *h, const bp011_series_segment_t *dir, uint32_t num_series) {
    if (!h || !dir) return BP011_ERR_NULL_POINTER;
    if (num_series == 0) return BP011_ERR_INVALID_ARG;

    uint32_t expected_sample_cursor = 0;
    uint32_t expected_time_cursor = h->time_stream_offset;
    uint32_t expected_val_cursor = h->values_stream_offset;

    for (uint32_t m = 0; m < num_series; m++) {
        const bp011_series_segment_t *seg = &dir[m];

        if (seg->sample_count == 0) return BP011_ERR_EMPTY_SERIES;
        if (seg->time_stream_bytes == 0 || seg->values_stream_bytes == 0) return BP011_ERR_INVALID_ARG;
        if (m > 0 && seg->unit_id <= dir[m - 1].unit_id) return BP011_ERR_CORRUPT_DIRECTORY;

        if (seg->start_sample_idx > h->sample_count) return BP011_ERR_INVALID_OFFSET;
        if (seg->sample_count > h->sample_count - seg->start_sample_idx) return BP011_ERR_INVALID_ARG;

        if (seg->start_sample_idx < expected_sample_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        expected_sample_cursor = seg->start_sample_idx + seg->sample_count;

        if (seg->time_stream_byte_offset < expected_time_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->time_stream_byte_offset > h->time_stream_offset + h->time_stream_bytes) return BP011_ERR_INVALID_OFFSET;
        if (seg->time_stream_bytes > (h->time_stream_offset + h->time_stream_bytes) - seg->time_stream_byte_offset) {
            return BP011_ERR_INVALID_OFFSET;
        }
        expected_time_cursor = seg->time_stream_byte_offset + seg->time_stream_bytes;

        if (seg->values_stream_byte_offset < expected_val_cursor) return BP011_ERR_OVERLAPPING_SEGMENT;
        if (seg->values_stream_byte_offset > h->values_stream_offset + h->values_stream_bytes) return BP011_ERR_INVALID_OFFSET;
        if (seg->values_stream_bytes > (h->values_stream_offset + h->values_stream_bytes) - seg->values_stream_byte_offset) {
            return BP011_ERR_INVALID_OFFSET;
        }
        expected_val_cursor = seg->values_stream_byte_offset + seg->values_stream_bytes;
    }
    return BP011_OK;
}

/* ========================================================================= */
/* Asignador Rastreable de Memoria Dinámica (NC-04)                          */
/* ========================================================================= */
typedef struct {
    size_t current_bytes;
    size_t base_bytes;
    size_t peak_bytes;
} bp011_mem_tracker_t;

extern bp011_mem_tracker_t g_bp011_mem_tracker;

static inline void bp011_mem_tracker_init(void) {
    g_bp011_mem_tracker.current_bytes = 0;
    g_bp011_mem_tracker.base_bytes = 0;
    g_bp011_mem_tracker.peak_bytes = 0;
}

static inline void bp011_mem_tracker_reset_op(void) {
    g_bp011_mem_tracker.base_bytes = g_bp011_mem_tracker.current_bytes;
    g_bp011_mem_tracker.peak_bytes = g_bp011_mem_tracker.current_bytes;
}

static inline size_t bp011_mem_tracker_get_peak_net(void) {
    if (g_bp011_mem_tracker.peak_bytes >= g_bp011_mem_tracker.base_bytes) {
        return g_bp011_mem_tracker.peak_bytes - g_bp011_mem_tracker.base_bytes;
    }
    return 0;
}

static inline void *bp011_tracked_malloc(size_t size) {
    if (size == 0) return NULL;
    /* Almacena el tamaño antes del puntero devuelto */
    size_t total = size + sizeof(size_t);
    void *raw = malloc(total);
    if (!raw) return NULL;
    *(size_t *)raw = size;
    g_bp011_mem_tracker.current_bytes += size;
    if (g_bp011_mem_tracker.current_bytes > g_bp011_mem_tracker.peak_bytes) {
        g_bp011_mem_tracker.peak_bytes = g_bp011_mem_tracker.current_bytes;
    }
    return (void *)((uint8_t *)raw + sizeof(size_t));
}

static inline void *bp011_tracked_calloc(size_t num, size_t size) {
    size_t total_payload = num * size;
    void *ptr = bp011_tracked_malloc(total_payload);
    if (ptr) memset(ptr, 0, total_payload);
    return ptr;
}

static inline void bp011_tracked_free(void *ptr) {
    if (!ptr) return;
    void *raw = (void *)((uint8_t *)ptr - sizeof(size_t));
    size_t size = *(size_t *)raw;
    if (g_bp011_mem_tracker.current_bytes >= size) {
        g_bp011_mem_tracker.current_bytes -= size;
    } else {
        g_bp011_mem_tracker.current_bytes = 0;
    }
    free(raw);
}

static inline void *bp011_tracked_realloc(void *ptr, size_t new_size) {
    if (!ptr) return bp011_tracked_malloc(new_size);
    if (new_size == 0) {
        bp011_tracked_free(ptr);
        return NULL;
    }
    void *raw = (void *)((uint8_t *)ptr - sizeof(size_t));
    size_t old_size = *(size_t *)raw;
    void *new_ptr = bp011_tracked_malloc(new_size);
    if (!new_ptr) return NULL;
    size_t copy_size = (old_size < new_size) ? old_size : new_size;
    memcpy(new_ptr, ptr, copy_size);
    bp011_tracked_free(ptr);
    return new_ptr;
}

#ifdef __cplusplus
}
#endif

#endif /* BP011_TYPES_H */
