/**
 * @file binpack_patch.c
 * @brief BinPack Enterprise SDK - Binary Differential OTA Patching Engine
 */

#include "binpack_internal.h"

BINPACK_API binpack_status_t BINPACK_CALL binpack_create_patch(
    const void *old_buf, size_t old_sz,
    const void *new_buf, size_t new_sz,
    void **patch_out, size_t *patch_sz,
    const binpack_options_t *opts
) {
    if (!old_buf || old_sz == 0 || !new_buf || new_sz == 0 || !patch_out || !patch_sz) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    binpack_options_t opt;
    if (opts) {
        opt = *opts;
    } else {
        binpack_default_options(&opt);
    }

    uint32_t old_crc = binpack_crc32(0, (const uint8_t*)old_buf, old_sz);
    uint32_t new_crc = binpack_crc32(0, (const uint8_t*)new_buf, new_sz);

    /* Allocate work buffers to apply architecture transforms */
    uint8_t *old_work = (uint8_t*)malloc(old_sz);
    uint8_t *new_work = (uint8_t*)malloc(new_sz);
    if (!old_work || !new_work) {
        free(old_work);
        free(new_work);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }
    memcpy(old_work, old_buf, old_sz);
    memcpy(new_work, new_buf, new_sz);

    binpack_filter_t filter = opt.filter;
    uint16_t filter_param = opt.transpose_stride;
    if (filter == BINPACK_FILTER_AUTO) {
        filter = binpack_auto_select_filter(new_work, new_sz, &filter_param);
    }

    /* Apply architectural or structural prefilter on both buffers */
    if (filter == BINPACK_FILTER_X86_64) {
        binpack_bcj_x86_filter(old_work, old_sz);
        binpack_bcj_x86_filter(new_work, new_sz);
    } else if (filter == BINPACK_FILTER_ARM32) {
        binpack_bcj_arm32_filter(old_work, old_sz);
        binpack_bcj_arm32_filter(new_work, new_sz);
    } else if (filter == BINPACK_FILTER_AARCH64) {
        binpack_bcj_aarch64_filter(old_work, old_sz);
        binpack_bcj_aarch64_filter(new_work, new_sz);
    } else if (filter == BINPACK_FILTER_RISCV) {
        binpack_bcj_riscv_filter(old_work, old_sz);
        binpack_bcj_riscv_filter(new_work, new_sz);
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        if (filter_param < 2) filter_param = 4;
        binpack_transpose_delta_filter(old_work, old_sz, filter_param);
        binpack_transpose_delta_filter(new_work, new_sz, filter_param);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        if (filter_param < 1) filter_param = 4;
        binpack_bitshuffle_filter(old_work, old_sz, filter_param);
        binpack_bitshuffle_filter(new_work, new_sz, filter_param);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        if (filter_param != 8 && filter_param != 16) filter_param = 8;
        binpack_texture_bcn_filter(old_work, old_sz, filter_param);
        binpack_texture_bcn_filter(new_work, new_sz, filter_param);
    }

    /* Use Zstandard sliding prefix dictionary compression with old_work as base */
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) {
        free(old_work);
        free(new_work);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    int clevel = opt.clevel > 0 ? opt.clevel : 19;
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, clevel);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_strategy, ZSTD_btultra);
    ZSTD_CCtx_refPrefix(cctx, old_work, old_sz);

    size_t cmp_cap = ZSTD_compressBound(new_sz) + 4096;
    uint8_t *cmp_payload = (uint8_t*)malloc(cmp_cap);
    if (!cmp_payload) {
        ZSTD_freeCCtx(cctx);
        free(old_work);
        free(new_work);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    ZSTD_inBuffer in_buf = { new_work, new_sz, 0 };
    ZSTD_outBuffer out_buf = { cmp_payload, cmp_cap, 0 };
    size_t zret = ZSTD_compressStream2(cctx, &out_buf, &in_buf, ZSTD_e_end);

    ZSTD_freeCCtx(cctx);
    free(old_work);
    free(new_work);

    if (ZSTD_isError(zret) || in_buf.pos != in_buf.size) {
        free(cmp_payload);
        return BINPACK_ERR_COMPRESSION_FAILED;
    }

    size_t total_patch_sz = sizeof(binpack_patch_header_t) + out_buf.pos;
    uint8_t *patch_data = (uint8_t*)malloc(total_patch_sz);
    if (!patch_data) {
        free(cmp_payload);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    binpack_patch_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.patch_magic = BINPACK_MAGIC_PATCH;
    hdr.version_major = BINPACK_SDK_VERSION_MAJOR;
    hdr.version_minor = BINPACK_SDK_VERSION_MINOR;
    hdr.old_sz = old_sz;
    hdr.new_sz = new_sz;
    hdr.old_crc32 = old_crc;
    hdr.new_crc32 = new_crc;
    hdr.filter_id = (uint8_t)filter;
    hdr.filter_param = filter_param;

    memcpy(patch_data, &hdr, sizeof(hdr));
    memcpy(patch_data + sizeof(hdr), cmp_payload, out_buf.pos);
    free(cmp_payload);

    *patch_out = patch_data;
    *patch_sz = total_patch_sz;
    return BINPACK_OK;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_apply_patch(
    const void *old_buf, size_t old_sz,
    const void *patch_buf, size_t patch_sz,
    void **new_out, size_t *new_sz
) {
    if (!old_buf || old_sz == 0 || !patch_buf || patch_sz < sizeof(binpack_patch_header_t) || !new_out || !new_sz) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    const binpack_patch_header_t *hdr = (const binpack_patch_header_t*)patch_buf;
    if (hdr->patch_magic != BINPACK_MAGIC_PATCH) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* Verify old baseline matches what the patch expects */
    if (hdr->old_sz != (uint64_t)old_sz) {
        return BINPACK_ERR_PATCH_MISMATCH;
    }
    uint32_t old_crc = binpack_crc32(0, (const uint8_t*)old_buf, old_sz);
    if (hdr->old_crc32 != old_crc) {
        return BINPACK_ERR_PATCH_MISMATCH;
    }

    /* Prepare old_work with same filter */
    uint8_t *old_work = (uint8_t*)malloc(old_sz);
    if (!old_work) return BINPACK_ERR_OUT_OF_MEMORY;
    memcpy(old_work, old_buf, old_sz);

    binpack_filter_t filter = (binpack_filter_t)hdr->filter_id;
    uint16_t filter_param = hdr->filter_param;
    if (filter == BINPACK_FILTER_X86_64) {
        binpack_bcj_x86_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_ARM32) {
        binpack_bcj_arm32_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_AARCH64) {
        binpack_bcj_aarch64_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_RISCV) {
        binpack_bcj_riscv_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        binpack_transpose_delta_filter(old_work, old_sz, filter_param >= 2 ? filter_param : 4);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_filter(old_work, old_sz, filter_param >= 1 ? filter_param : 4);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        binpack_texture_bcn_filter(old_work, old_sz, (filter_param == 8 || filter_param == 16) ? filter_param : 8);
    }

    /* Allocate buffer for reconstructed target */
    size_t expected_new_sz = (size_t)hdr->new_sz;
    uint8_t *new_work = (uint8_t*)malloc(expected_new_sz);
    if (!new_work) {
        free(old_work);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!dctx) {
        free(old_work);
        free(new_work);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    ZSTD_DCtx_refPrefix(dctx, old_work, old_sz);

    const uint8_t *cmp_payload = (const uint8_t*)patch_buf + sizeof(binpack_patch_header_t);
    size_t cmp_payload_sz = patch_sz - sizeof(binpack_patch_header_t);

    ZSTD_inBuffer in_buf = { cmp_payload, cmp_payload_sz, 0 };
    ZSTD_outBuffer out_buf = { new_work, expected_new_sz, 0 };

    size_t zret = ZSTD_decompressStream(dctx, &out_buf, &in_buf);
    ZSTD_freeDCtx(dctx);
    free(old_work);

    if (ZSTD_isError(zret) || out_buf.pos != expected_new_sz) {
        free(new_work);
        return BINPACK_ERR_DECOMPRESSION_FAILED;
    }

    /* Invert filter on reconstructed target */
    if (filter == BINPACK_FILTER_X86_64) {
        binpack_bcj_x86_unfilter(new_work, expected_new_sz);
    } else if (filter == BINPACK_FILTER_ARM32) {
        binpack_bcj_arm32_unfilter(new_work, expected_new_sz);
    } else if (filter == BINPACK_FILTER_AARCH64) {
        binpack_bcj_aarch64_unfilter(new_work, expected_new_sz);
    } else if (filter == BINPACK_FILTER_RISCV) {
        binpack_bcj_riscv_unfilter(new_work, expected_new_sz);
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        binpack_transpose_delta_unfilter(new_work, expected_new_sz, filter_param >= 2 ? filter_param : 4);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_unfilter(new_work, expected_new_sz, filter_param >= 1 ? filter_param : 4);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        binpack_texture_bcn_unfilter(new_work, expected_new_sz, (filter_param == 8 || filter_param == 16) ? filter_param : 8);
    }

    /* Verify target integrity */
    uint32_t calc_new_crc = binpack_crc32(0, new_work, expected_new_sz);
    if (calc_new_crc != hdr->new_crc32) {
        free(new_work);
        return BINPACK_ERR_CHECKSUM_MISMATCH;
    }

    *new_out = new_work;
    *new_sz = expected_new_sz;
    return BINPACK_OK;
}
