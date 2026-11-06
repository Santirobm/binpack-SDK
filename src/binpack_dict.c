/**
 * @file binpack_dict.c
 * @brief BinPack Enterprise SDK - Pre-trained Dictionary Engine for IoT & CAN-bus Telemetry
 */

#include "binpack_internal.h"
#include <limits.h>
#include <zdict.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <fibersapi.h>
#else
    #include <pthread.h>
#endif

/* ========================================================================= */
/* Thread-Local Context Lifecycle & Multiplatform Auto-Cleanup               */
/* ========================================================================= */

typedef struct bp_dict_tls_s {
    ZSTD_CCtx *cctx;
    ZSTD_DCtx *dctx;
} bp_dict_tls_t;

static void bp_dict_tls_free_contents(bp_dict_tls_t *tls) {
    if (!tls) return;
    if (tls->cctx) {
        ZSTD_freeCCtx(tls->cctx);
        tls->cctx = NULL;
    }
    if (tls->dctx) {
        ZSTD_freeDCtx(tls->dctx);
        tls->dctx = NULL;
    }
}

#if !defined(_WIN32)

static pthread_key_t  s_dict_tls_key;
static pthread_once_t s_dict_tls_once = PTHREAD_ONCE_INIT;
static int            s_dict_tls_key_valid = 0;

static void bp_dict_posix_destructor(void *arg) {
    bp_dict_tls_t *tls = (bp_dict_tls_t*)arg;
    if (tls) {
        bp_dict_tls_free_contents(tls);
        free(tls);
    }
}

static void bp_dict_posix_key_init(void) {
    if (pthread_key_create(&s_dict_tls_key, bp_dict_posix_destructor) == 0) {
        s_dict_tls_key_valid = 1;
    }
}

static bp_dict_tls_t* bp_get_dict_tls(void) {
    if (pthread_once(&s_dict_tls_once, bp_dict_posix_key_init) != 0 || !s_dict_tls_key_valid) {
        return NULL;
    }
    bp_dict_tls_t *tls = (bp_dict_tls_t*)pthread_getspecific(s_dict_tls_key);
    if (!tls) {
        tls = (bp_dict_tls_t*)calloc(1, sizeof(bp_dict_tls_t));
        if (!tls) return NULL;
        if (pthread_setspecific(s_dict_tls_key, tls) != 0) {
            free(tls);
            return NULL;
        }
    }
    return tls;
}

#else /* Windows */

static DWORD      s_dict_fls_idx = FLS_OUT_OF_INDEXES;
static INIT_ONCE  s_dict_fls_once = INIT_ONCE_STATIC_INIT;

static VOID NTAPI bp_dict_fls_destructor(PVOID lpFlsData) {
    bp_dict_tls_t *tls = (bp_dict_tls_t*)lpFlsData;
    if (tls) {
        bp_dict_tls_free_contents(tls);
        free(tls);
    }
}

static BOOL CALLBACK bp_dict_fls_init_cb(PINIT_ONCE InitOnce, PVOID Parameter, PVOID *Context) {
    (void)InitOnce; (void)Parameter; (void)Context;
    s_dict_fls_idx = FlsAlloc(bp_dict_fls_destructor);
    return (s_dict_fls_idx != FLS_OUT_OF_INDEXES) ? TRUE : FALSE;
}

static bp_dict_tls_t* bp_get_dict_tls(void) {
    if (!InitOnceExecuteOnce(&s_dict_fls_once, bp_dict_fls_init_cb, NULL, NULL)) {
        return NULL;
    }
    if (s_dict_fls_idx == FLS_OUT_OF_INDEXES) return NULL;

    bp_dict_tls_t *tls = (bp_dict_tls_t*)FlsGetValue(s_dict_fls_idx);
    if (!tls) {
        tls = (bp_dict_tls_t*)calloc(1, sizeof(bp_dict_tls_t));
        if (!tls) return NULL;
        if (!FlsSetValue(s_dict_fls_idx, tls)) {
            free(tls);
            return NULL;
        }
    }
    return tls;
}

#endif

#ifdef BINPACK_TESTING
/* Explicit test-harness helper to clear current thread's TLS context on demand.
 * Verifies that unbinding succeeds before manually invoking the destructor,
 * avoiding any possibility of double free if the unbind operation fails. */
void bp_dict_cleanup_current_thread(void) {
#if !defined(_WIN32)
    if (s_dict_tls_key_valid) {
        bp_dict_tls_t *tls = (bp_dict_tls_t*)pthread_getspecific(s_dict_tls_key);
        if (tls) {
            if (pthread_setspecific(s_dict_tls_key, NULL) == 0) {
                bp_dict_posix_destructor(tls);
            }
        }
    }
#else
    if (s_dict_fls_idx != FLS_OUT_OF_INDEXES) {
        bp_dict_tls_t *tls = (bp_dict_tls_t*)FlsGetValue(s_dict_fls_idx);
        if (tls) {
            if (FlsSetValue(s_dict_fls_idx, NULL)) {
                bp_dict_fls_destructor(tls);
            }
        }
    }
#endif
}
#endif

/* ========================================================================= */
/* Public Dictionary API Implementations                                     */
/* ========================================================================= */

BINPACK_API binpack_dict_t* BINPACK_CALL binpack_train_dict(
    const void *samples,
    const size_t *sample_sizes,
    size_t sample_count,
    size_t dict_capacity
) {
    /* 1. Base pointer and parameter validation */
    if (!samples || !sample_sizes || sample_count == 0 || dict_capacity == 0) {
        return NULL;
    }

    /* 2. 64-bit integer overflow / 32-bit truncation rejection */
    if (sample_count > UINT_MAX) {
        return NULL;
    }

    /* 3. Cumulative sum overflow validation and non-empty payload check */
    size_t total_samples_sz = 0;
    for (size_t i = 0; i < sample_count; i++) {
        if (sample_sizes[i] > SIZE_MAX - total_samples_sz) {
            return NULL;
        }
        total_samples_sz += sample_sizes[i];
    }
    if (total_samples_sz == 0) {
        return NULL;
    }

    /* 4. Enforce upper capacity limit (32 MiB budget) before any allocation */
    if (dict_capacity > BINPACK_MAX_DICT_SIZE) {
        return NULL;
    }

    /* 5. Allocate bounded dictionary training buffer */
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
    /* Upfront parameter validation and memory budget guard (32 MiB limit) */
    if (!dict_buffer || dict_size == 0 || dict_size > BINPACK_MAX_DICT_SIZE) {
        return NULL;
    }

    binpack_dict_t *dict = (binpack_dict_t*)malloc(sizeof(binpack_dict_t));
    if (!dict) return NULL;

    dict->buffer = malloc(dict_size);
    if (!dict->buffer) {
        free(dict);
        return NULL;
    }
    memcpy(dict->buffer, dict_buffer, dict_size);
    dict->size = dict_size;

    /* Historical compression level contract:
     * - clevel in [1, 22] and within [min_lvl, max_lvl]: use caller requested level
     * - all other values (0, negative, > 22, out-of-range): fall back to historical default level 19 */
    int min_lvl = ZSTD_minCLevel();
    int max_lvl = ZSTD_maxCLevel();
    int level = 19;
    if (clevel > 0 &&
        clevel <= 22 &&
        clevel >= min_lvl &&
        clevel <= max_lvl) {
        level = clevel;
    }

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

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *compressed_sz,
    const binpack_dict_t *dict
) {
    /* Defensive initialization of outputs */
    if (compressed_sz) *compressed_sz = 0;

    if (!src || !dst || !compressed_sz || !dict || !dict->cdict) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    bp_dict_tls_t *tls = bp_get_dict_tls();
    if (!tls) return BINPACK_ERR_OUT_OF_MEMORY;

    if (!tls->cctx) {
        tls->cctx = ZSTD_createCCtx();
        if (!tls->cctx) return BINPACK_ERR_OUT_OF_MEMORY;
    }

    size_t c_size = ZSTD_compress_usingCDict(tls->cctx, dst, dst_cap, src, src_sz, dict->cdict);

    if (ZSTD_isError(c_size)) {
        if (ZSTD_getErrorCode(c_size) == ZSTD_error_dstSize_tooSmall) {
            return BINPACK_ERR_BUFFER_TOO_SMALL;
        }
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
    /* Defensive initialization of outputs */
    if (decompressed_sz) *decompressed_sz = 0;

    if (!src || !dst || !decompressed_sz || !dict || !dict->ddict) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    bp_dict_tls_t *tls = bp_get_dict_tls();
    if (!tls) return BINPACK_ERR_OUT_OF_MEMORY;

    if (!tls->dctx) {
        tls->dctx = ZSTD_createDCtx();
        if (!tls->dctx) return BINPACK_ERR_OUT_OF_MEMORY;
    }

    size_t d_size = ZSTD_decompress_usingDDict(tls->dctx, dst, dst_cap, src, src_sz, dict->ddict);

    if (ZSTD_isError(d_size)) {
        if (ZSTD_getErrorCode(d_size) == ZSTD_error_dstSize_tooSmall) {
            return BINPACK_ERR_BUFFER_TOO_SMALL;
        }
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
