/**
 * @file binpack_eval.c
 * @brief BinPack Enterprise SDK - Micro-Trial & Zero-Regression Evaluator (Zero-Alloc Hot-Path)
 */

#include "binpack_internal.h"

#if defined(_WIN32)
    #include <windows.h>
    #include <fibersapi.h>
#else
    #include <pthread.h>
#endif

#define BP_EVAL_MAX_SAMPLE 32768

#if defined(ZSTD_COMPRESSBOUND)
    #define BP_EVAL_CMP_BOUND ZSTD_COMPRESSBOUND(BP_EVAL_MAX_SAMPLE)
#else
    #define BP_EVAL_CMP_BOUND (BP_EVAL_MAX_SAMPLE + (BP_EVAL_MAX_SAMPLE >> 8) + (((128 << 10) - BP_EVAL_MAX_SAMPLE) >> 11))
#endif

#if defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 201112L)
    _Static_assert(BP_EVAL_CMP_BOUND >= 32944, "BP_EVAL_CMP_BOUND must satisfy ZSTD_compressBound(32768)");
#elif defined(__cplusplus) && (__cplusplus >= 201103L)
    static_assert(BP_EVAL_CMP_BOUND >= 32944, "BP_EVAL_CMP_BOUND must satisfy ZSTD_compressBound(32768)");
#endif

/* ========================================================================= */
/* Thread-Local Context Lifecycle & Multiplatform Auto-Cleanup               */
/* ========================================================================= */

typedef struct bp_eval_tls_s {
    ZSTD_CCtx *cctx;
    uint8_t   sample_copy[BP_EVAL_MAX_SAMPLE];
    uint8_t   cmp_buf[BP_EVAL_CMP_BOUND];
} bp_eval_tls_t;

static void bp_eval_tls_free_contents(bp_eval_tls_t *tls) {
    if (!tls) return;
    if (tls->cctx) {
        ZSTD_freeCCtx(tls->cctx);
        tls->cctx = NULL;
    }
}

#if !defined(_WIN32)

static pthread_key_t  s_eval_tls_key;
static pthread_once_t s_eval_tls_once = PTHREAD_ONCE_INIT;
static int            s_eval_tls_key_valid = 0;

static void bp_eval_posix_destructor(void *arg) {
    bp_eval_tls_t *tls = (bp_eval_tls_t*)arg;
    if (tls) {
        bp_eval_tls_free_contents(tls);
        free(tls);
    }
}

static void bp_eval_posix_key_init(void) {
    if (pthread_key_create(&s_eval_tls_key, bp_eval_posix_destructor) == 0) {
        s_eval_tls_key_valid = 1;
    }
}

static bp_eval_tls_t* bp_get_eval_tls(void) {
    if (pthread_once(&s_eval_tls_once, bp_eval_posix_key_init) != 0 || !s_eval_tls_key_valid) {
        return NULL;
    }
    bp_eval_tls_t *tls = (bp_eval_tls_t*)pthread_getspecific(s_eval_tls_key);
    if (!tls) {
        tls = (bp_eval_tls_t*)calloc(1, sizeof(bp_eval_tls_t));
        if (!tls) return NULL;
        tls->cctx = ZSTD_createCCtx();
        if (!tls->cctx) {
            free(tls);
            return NULL;
        }
        if (pthread_setspecific(s_eval_tls_key, tls) != 0) {
            bp_eval_tls_free_contents(tls);
            free(tls);
            return NULL;
        }
    }
    return tls;
}

#else /* _WIN32 */

static DWORD s_eval_fls_idx = FLS_OUT_OF_INDEXES;
static INIT_ONCE s_eval_fls_once = INIT_ONCE_STATIC_INIT;

static void NTAPI bp_eval_fls_destructor(PVOID ptr) {
    bp_eval_tls_t *tls = (bp_eval_tls_t*)ptr;
    if (tls) {
        bp_eval_tls_free_contents(tls);
        free(tls);
    }
}

static BOOL WINAPI bp_eval_init_fls_once(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once; (void)param; (void)context;
    s_eval_fls_idx = FlsAlloc(bp_eval_fls_destructor);
    return (s_eval_fls_idx != FLS_OUT_OF_INDEXES) ? TRUE : FALSE;
}

static bp_eval_tls_t* bp_get_eval_tls(void) {
    if (!InitOnceExecuteOnce(&s_eval_fls_once, bp_eval_init_fls_once, NULL, NULL)) {
        return NULL;
    }
    if (s_eval_fls_idx == FLS_OUT_OF_INDEXES) {
        return NULL;
    }
    bp_eval_tls_t *tls = (bp_eval_tls_t*)FlsGetValue(s_eval_fls_idx);
    if (!tls) {
        tls = (bp_eval_tls_t*)calloc(1, sizeof(bp_eval_tls_t));
        if (!tls) return NULL;
        tls->cctx = ZSTD_createCCtx();
        if (!tls->cctx) {
            free(tls);
            return NULL;
        }
        if (!FlsSetValue(s_eval_fls_idx, tls)) {
            bp_eval_tls_free_contents(tls);
            free(tls);
            return NULL;
        }
    }
    return tls;
}

#endif /* _WIN32 */

#if defined(BINPACK_TESTING)
void bp_eval_cleanup_current_thread(void) {
#if !defined(_WIN32)
    if (!s_eval_tls_key_valid) return;
    bp_eval_tls_t *tls = (bp_eval_tls_t*)pthread_getspecific(s_eval_tls_key);
    if (tls) {
        if (pthread_setspecific(s_eval_tls_key, NULL) == 0) {
            bp_eval_posix_destructor(tls);
        }
    }
#else
    if (s_eval_fls_idx != FLS_OUT_OF_INDEXES) {
        bp_eval_tls_t *tls = (bp_eval_tls_t*)FlsGetValue(s_eval_fls_idx);
        if (tls) {
            if (FlsSetValue(s_eval_fls_idx, NULL)) {
                bp_eval_fls_destructor(tls);
            }
        }
    }
#endif
}
#endif /* BINPACK_TESTING */

/* ========================================================================= */
/* Public Micro-Trial & Zero-Regression Evaluator API                        */
/* ========================================================================= */

int BINPACK_CALL binpack_evaluate_filter_gain(
    const uint8_t *sample, size_t sample_sz,
    binpack_filter_t filter, uint16_t stride
) {
    /* 1. Fast boundary and filter range checks */
    if (!sample || sample_sz < 64 || filter <= BINPACK_FILTER_NONE || filter > BINPACK_FILTER_TEXTURE_BCN) {
        return 0;
    }

    /* 2. Filter-specific parameter validation */
    switch (filter) {
        case BINPACK_FILTER_TRANSPOSE_DELTA:
            if (stride < 2) stride = 4;
            if (stride > 256) return 0;
            break;
        case BINPACK_FILTER_BITSHUFFLE:
            if (stride < 1) stride = 4;
            if (stride > 256) return 0;
            break;
        case BINPACK_FILTER_TEXTURE_BCN:
            if (stride != 8 && stride != 16) stride = 8;
            break;
        case BINPACK_FILTER_X86_64:
        case BINPACK_FILTER_ARM32:
        case BINPACK_FILTER_AARCH64:
        case BINPACK_FILTER_RISCV:
            /* Stride is ignored for BCJ architecture filters */
            break;
        default:
            return 0;
    }

    /* 3. Clamp evaluation sample to 32 KB for sub-millisecond evaluation (< 25 µs) */
    size_t eval_len = sample_sz > BP_EVAL_MAX_SAMPLE ? BP_EVAL_MAX_SAMPLE : sample_sz;

    /* 4. Acquire thread-local evaluation workspace */
    bp_eval_tls_t *tls = bp_get_eval_tls();
    if (!tls || !tls->cctx) {
        return 0;
    }

    /* 5. Fast level 1 reference compression on raw sample */
    ZSTD_CCtx_reset(tls->cctx, ZSTD_reset_session_only);
    ZSTD_CCtx_setParameter(tls->cctx, ZSTD_c_compressionLevel, 1);
    size_t raw_sz = ZSTD_compressCCtx(tls->cctx, tls->cmp_buf, sizeof(tls->cmp_buf), sample, eval_len, 1);
    if (ZSTD_isError(raw_sz)) {
        return 0;
    }

    /* 6. Apply candidate filter on sample copy */
    memcpy(tls->sample_copy, sample, eval_len);
    switch (filter) {
        case BINPACK_FILTER_X86_64:
            binpack_bcj_x86_filter(tls->sample_copy, eval_len);
            break;
        case BINPACK_FILTER_ARM32:
            binpack_bcj_arm32_filter(tls->sample_copy, eval_len);
            break;
        case BINPACK_FILTER_AARCH64:
            binpack_bcj_aarch64_filter(tls->sample_copy, eval_len);
            break;
        case BINPACK_FILTER_RISCV:
            binpack_bcj_riscv_filter(tls->sample_copy, eval_len);
            break;
        case BINPACK_FILTER_TRANSPOSE_DELTA:
            binpack_transpose_delta_filter(tls->sample_copy, eval_len, stride);
            break;
        case BINPACK_FILTER_BITSHUFFLE:
            binpack_bitshuffle_filter(tls->sample_copy, eval_len, stride);
            break;
        case BINPACK_FILTER_TEXTURE_BCN:
            binpack_texture_bcn_filter(tls->sample_copy, eval_len, stride);
            break;
        default:
            return 0;
    }

    /* 7. Fast level 1 trial compression on transformed sample into the SAME reusable buffer */
    ZSTD_CCtx_reset(tls->cctx, ZSTD_reset_session_only);
    ZSTD_CCtx_setParameter(tls->cctx, ZSTD_c_compressionLevel, 1);
    size_t trans_sz = ZSTD_compressCCtx(tls->cctx, tls->cmp_buf, sizeof(tls->cmp_buf), tls->sample_copy, eval_len, 1);

    int is_beneficial = 0;
    if (!ZSTD_isError(trans_sz)) {
        /* Only accept filter if it measurably reduces compressed size (zero-regression guarantee) */
        if (trans_sz < raw_sz) {
            is_beneficial = 1;
        }
    }

    return is_beneficial;
}
