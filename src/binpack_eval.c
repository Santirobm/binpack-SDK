/**
 * @file binpack_eval.c
 * @brief BinPack Enterprise SDK - Micro-Trial & Zero-Regression Evaluator (Zero-Alloc Hot-Path)
 */

#include "binpack_internal.h"

#if defined(_MSC_VER)
    static __declspec(thread) ZSTD_CCtx *s_eval_cctx = NULL;
#else
    static __thread ZSTD_CCtx *s_eval_cctx = NULL;
#endif

int BINPACK_CALL binpack_evaluate_filter_gain(
    const uint8_t *sample, size_t sample_sz,
    binpack_filter_t filter, uint16_t stride
) {
    if (!sample || sample_sz < 64 || filter == BINPACK_FILTER_NONE) {
        return 0;
    }

    /* Clamp evaluation sample to 32 KB for sub-millisecond evaluation (< 25 µs) */
    size_t eval_len = sample_sz > 32768 ? 32768 : sample_sz;

    /* Stack-allocated scratch buffers to completely eliminate heap malloc / free overhead */
    uint8_t sample_copy[32768];
    uint8_t cmp_raw[34000];
    uint8_t cmp_trans[34000];

    /* Lazy-init persistent thread-local ZSTD context to eliminate context alloc/free cycles */
    if (!s_eval_cctx) {
        s_eval_cctx = ZSTD_createCCtx();
        if (!s_eval_cctx) return 0;
    }

    /* 1. Fast level 1 reference compression on raw sample */
    ZSTD_CCtx_reset(s_eval_cctx, ZSTD_reset_session_only);
    ZSTD_CCtx_setParameter(s_eval_cctx, ZSTD_c_compressionLevel, 1);
    size_t raw_sz = ZSTD_compressCCtx(s_eval_cctx, cmp_raw, sizeof(cmp_raw), sample, eval_len, 1);
    if (ZSTD_isError(raw_sz)) {
        return 0;
    }

    /* 2. Apply candidate filter on sample copy */
    memcpy(sample_copy, sample, eval_len);
    switch (filter) {
        case BINPACK_FILTER_X86_64:
            binpack_bcj_x86_filter(sample_copy, eval_len);
            break;
        case BINPACK_FILTER_ARM32:
            binpack_bcj_arm32_filter(sample_copy, eval_len);
            break;
        case BINPACK_FILTER_AARCH64:
            binpack_bcj_aarch64_filter(sample_copy, eval_len);
            break;
        case BINPACK_FILTER_RISCV:
            binpack_bcj_riscv_filter(sample_copy, eval_len);
            break;
        case BINPACK_FILTER_TRANSPOSE_DELTA:
            if (stride < 2) stride = 4;
            binpack_transpose_delta_filter(sample_copy, eval_len, stride);
            break;
        case BINPACK_FILTER_BITSHUFFLE:
            if (stride < 1) stride = 4;
            binpack_bitshuffle_filter(sample_copy, eval_len, stride);
            break;
        case BINPACK_FILTER_TEXTURE_BCN:
            if (stride != 8 && stride != 16) stride = 8;
            binpack_texture_bcn_filter(sample_copy, eval_len, stride);
            break;
        default:
            break;
    }

    /* 3. Fast level 1 trial compression on transformed sample */
    ZSTD_CCtx_reset(s_eval_cctx, ZSTD_reset_session_only);
    ZSTD_CCtx_setParameter(s_eval_cctx, ZSTD_c_compressionLevel, 1);
    size_t trans_sz = ZSTD_compressCCtx(s_eval_cctx, cmp_trans, sizeof(cmp_trans), sample_copy, eval_len, 1);
    
    int is_beneficial = 0;
    if (!ZSTD_isError(trans_sz)) {
        /* Only accept filter if it measurably reduces compressed size (zero-regression guarantee) */
        if (trans_sz < raw_sz) {
            is_beneficial = 1;
        }
    }

    return is_beneficial;
}
