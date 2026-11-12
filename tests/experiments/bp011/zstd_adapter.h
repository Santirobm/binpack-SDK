/**
 * @file zstd_adapter.h
 * @brief Adaptador Zstd (Nivel 3) instrumentado con asignador rastreador para la linea base secundaria.
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#ifndef ZSTD_ADAPTER_H
#define ZSTD_ADAPTER_H

#include "bp011_types.h"
#include "zstd.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BP011_ZSTD_DEFAULT_LEVEL 3

/* ========================================================================= */
/* Callbacks de Memoria Rastreable para Zstd (NC-D01)                        */
/* ========================================================================= */
static inline void *bp011_zstd_custom_alloc(void *opaque, size_t size) {
    (void)opaque;
    return bp011_tracked_malloc(size);
}

static inline void bp011_zstd_custom_free(void *opaque, void *address) {
    (void)opaque;
    bp011_tracked_free(address);
}

static inline ZSTD_customMem bp011_get_zstd_custom_mem(void) {
    ZSTD_customMem cm;
    cm.customAlloc = bp011_zstd_custom_alloc;
    cm.customFree  = bp011_zstd_custom_free;
    cm.opaque      = NULL;
    return cm;
}

/* ========================================================================= */
/* Compresion de Matriz de Valores con Zstd Instrumentado                     */
/* ========================================================================= */
static inline bp011_status_t zstd_encode_matrix(
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

    if (num_samples > SIZE_MAX / ((size_t)num_channels * sizeof(double))) {
        return BP011_ERR_BUFFER_OVERFLOW;
    }
    size_t raw_bytes = (size_t)num_samples * (size_t)num_channels * sizeof(double);
    size_t comp_bound = ZSTD_compressBound(raw_bytes);
    if (comp_bound > out_cap) return BP011_ERR_BUFFER_OVERFLOW;

    /* Utiliza contexto Zstd con asignador rastreador contractual (NC-D01) */
    ZSTD_customMem cm = bp011_get_zstd_custom_mem();
    ZSTD_CCtx *cctx = ZSTD_createCCtx_advanced(cm);
    if (!cctx) return BP011_ERR_BUFFER_OVERFLOW;

    size_t res = ZSTD_compressCCtx(cctx, out_buf, out_cap, matrix, raw_bytes, BP011_ZSTD_DEFAULT_LEVEL);
    ZSTD_freeCCtx(cctx);

    if (ZSTD_isError(res)) return BP011_ERR_BUFFER_OVERFLOW;

    *out_bytes = res;
    *out_bits = (uint64_t)res * 8ULL;
    return BP011_OK;
}

/* ========================================================================= */
/* Descompresion de Matriz de Valores con Zstd Instrumentado                 */
/* ========================================================================= */
static inline bp011_status_t zstd_decode_matrix(
    const uint8_t *in_buf,
    size_t in_bytes,
    double *out_matrix,
    uint32_t num_samples,
    uint32_t num_channels
) {
    if (!in_buf || !out_matrix) return BP011_ERR_NULL_POINTER;
    if (num_samples == 0 || num_channels == 0) return BP011_ERR_INVALID_ARG;

    if (num_samples > SIZE_MAX / ((size_t)num_channels * sizeof(double))) {
        return BP011_ERR_BUFFER_OVERFLOW;
    }
    size_t expected_raw_bytes = (size_t)num_samples * (size_t)num_channels * sizeof(double);

    /* Utiliza contexto Zstd con asignador rastreador contractual (NC-D01) */
    ZSTD_customMem cm = bp011_get_zstd_custom_mem();
    ZSTD_DCtx *dctx = ZSTD_createDCtx_advanced(cm);
    if (!dctx) return BP011_ERR_BUFFER_OVERFLOW;

    size_t res = ZSTD_decompressDCtx(dctx, out_matrix, expected_raw_bytes, in_buf, in_bytes);
    ZSTD_freeDCtx(dctx);

    if (ZSTD_isError(res) || res != expected_raw_bytes) {
        return BP011_ERR_DATA_MISMATCH;
    }

    return BP011_OK;
}

#ifdef __cplusplus
}
#endif

#endif /* ZSTD_ADAPTER_H */
