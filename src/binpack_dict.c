/**
 * @file binpack_dict.c
 * @brief BinPack Enterprise SDK - Pre-trained Dictionary Engine for IoT & CAN-bus Telemetry
 */

#include "binpack_internal.h"

BINPACK_API binpack_dict_t* BINPACK_CALL binpack_train_dict(
    const void *samples,
    const size_t *sample_sizes,
    size_t sample_count,
    size_t dict_capacity
) {
    if (!samples || !sample_sizes || sample_count == 0 || dict_capacity == 0) {
        return NULL;
    }

    void *dict_buf = malloc(dict_capacity);
    if (!dict_buf) return NULL;

    size_t trained_sz = ZDICT_trainFromBuffer(
        dict_buf, dict_capacity,
        samples, sample_sizes, (unsigned)sample_count
    );

    if (ZDICT_isError(trained_sz) || trained_sz == 0) {
        free(dict_buf);
        return NULL;
    }

    binpack_dict_t *dict = binpack_create_dict(dict_buf, trained_sz, 19);
    free(dict_buf);
    return dict;
}

BINPACK_API binpack_dict_t* BINPACK_CALL binpack_create_dict(
    const void *dict_buffer,
    size_t dict_size,
    int clevel
) {
    if (!dict_buffer || dict_size == 0) return NULL;

    binpack_dict_t *dict = (binpack_dict_t*)malloc(sizeof(binpack_dict_t));
    if (!dict) return NULL;

    dict->buffer = malloc(dict_size);
    if (!dict->buffer) {
        free(dict);
        return NULL;
    }
    memcpy(dict->buffer, dict_buffer, dict_size);
    dict->size = dict_size;

    int level = (clevel > 0 && clevel <= 22) ? clevel : 19;
    dict->cdict = ZSTD_createCDict(dict->buffer, dict->size, level);
    dict->ddict = ZSTD_createDDict(dict->buffer, dict->size);

    if (!dict->cdict || !dict->ddict) {
        if (dict->cdict) ZSTD_freeCDict(dict->cdict);
        if (dict->ddict) ZSTD_freeDDict(dict->ddict);
        free(dict->buffer);
        free(dict);
        return NULL;
    }

    return dict;
}

BINPACK_API const void* BINPACK_CALL binpack_get_dict_buffer(
    const binpack_dict_t *dict,
    size_t *out_size
) {
    if (!dict) {
        if (out_size) *out_size = 0;
        return NULL;
    }
    if (out_size) *out_size = dict->size;
    return dict->buffer;
}

#if defined(_MSC_VER)
    static __declspec(thread) ZSTD_CCtx *s_dict_cctx = NULL;
    static __declspec(thread) ZSTD_DCtx *s_dict_dctx = NULL;
#else
    static __thread ZSTD_CCtx *s_dict_cctx = NULL;
    static __thread ZSTD_DCtx *s_dict_dctx = NULL;
#endif

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *compressed_sz,
    const binpack_dict_t *dict
) {
    if (!src || !dst || !compressed_sz || !dict || !dict->cdict) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    if (!s_dict_cctx) {
        s_dict_cctx = ZSTD_createCCtx();
        if (!s_dict_cctx) return BINPACK_ERR_OUT_OF_MEMORY;
    }

    size_t c_size = ZSTD_compress_usingCDict(s_dict_cctx, dst, dst_cap, src, src_sz, dict->cdict);

    if (ZSTD_isError(c_size)) {
        return BINPACK_ERR_COMPRESSION_FAILED;
    }

    *compressed_sz = c_size;
    return BINPACK_OK;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *decompressed_sz,
    const binpack_dict_t *dict
) {
    if (!src || !dst || !decompressed_sz || !dict || !dict->ddict) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    if (!s_dict_dctx) {
        s_dict_dctx = ZSTD_createDCtx();
        if (!s_dict_dctx) return BINPACK_ERR_OUT_OF_MEMORY;
    }

    size_t d_size = ZSTD_decompress_usingDDict(s_dict_dctx, dst, dst_cap, src, src_sz, dict->ddict);

    if (ZSTD_isError(d_size)) {
        return BINPACK_ERR_DECOMPRESSION_FAILED;
    }

    *decompressed_sz = d_size;
    return BINPACK_OK;
}

BINPACK_API void BINPACK_CALL binpack_free_dict(binpack_dict_t *dict) {
    if (!dict) return;
    if (dict->cdict) ZSTD_freeCDict(dict->cdict);
    if (dict->ddict) ZSTD_freeDDict(dict->ddict);
    if (dict->buffer) free(dict->buffer);
    free(dict);
}
