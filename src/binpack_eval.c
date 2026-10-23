/**
 * @file binpack_eval.c
 * @brief BinPack Enterprise SDK - Micro-Trial & Zero-Regression Evaluator
 */

#include "binpack_internal.h"

int BINPACK_CALL binpack_evaluate_filter_gain(
    const uint8_t *sample, size_t sample_sz,
    binpack_filter_t filter, uint16_t stride
) {
    if (!sample || sample_sz < 64 || filter == BINPACK_FILTER_NONE) {
        return 0;
    }

    /* Clamp evaluation sample to 64 KB to keep trial overhead < 1 millisecond */
    size_t eval_len = sample_sz > 65536 ? 65536 : sample_sz;

    uint8_t *sample_copy = (uint8_t*)malloc(eval_len);
    if (!sample_copy) return 0;

    size_t bound = ZSTD_compressBound(eval_len);
    uint8_t *cmp_raw = (uint8_t*)malloc(bound);
    uint8_t *cmp_trans = (uint8_t*)malloc(bound);

    if (!cmp_raw || !cmp_trans) {
        free(sample_copy);
        free(cmp_raw);
        free(cmp_trans);
        return 0;
    }

    /* 1. Fast level 1 reference compression on raw sample */
    size_t raw_sz = ZSTD_compress(cmp_raw, bound, sample, eval_len, 1);
    if (ZSTD_isError(raw_sz)) {
        free(sample_copy);
        free(cmp_raw);
        free(cmp_trans);
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
    size_t trans_sz = ZSTD_compress(cmp_trans, bound, sample_copy, eval_len, 1);
    int is_beneficial = 0;
    if (!ZSTD_isError(trans_sz)) {
        /* Only accept filter if it measurably reduces compressed size (zero-regression guarantee) */
        if (trans_sz < raw_sz) {
            is_beneficial = 1;
        }
    }

    free(sample_copy);
    free(cmp_raw);
    free(cmp_trans);
    return is_beneficial;
}
