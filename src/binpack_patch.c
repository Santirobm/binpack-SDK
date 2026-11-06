/**
 * @file binpack_patch.c
 * @brief BinPack Enterprise SDK - Binary Differential OTA Patching Engine
 */

#include "binpack_internal.h"
#include <zstd.h>

BINPACK_API binpack_status_t BINPACK_CALL binpack_create_patch(
    const void *old_buf, size_t old_sz,
    const void *new_buf, size_t new_sz,
    void **patch_out, size_t *patch_sz,
    const binpack_options_t *opts
) {
    /* Defensive initialization of outputs */
    if (patch_out) *patch_out = NULL;
    if (patch_sz) *patch_sz = 0;

    /* Upfront parameter validation and bounded size ceiling (512 MiB limit) */
    if (!old_buf || old_sz == 0 || old_sz > BINPACK_MAX_PATCH_SIZE ||
        !new_buf || new_sz == 0 || new_sz > BINPACK_MAX_PATCH_SIZE ||
        !patch_out || !patch_sz) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    binpack_options_t opt;
    if (opts) {
        opt = *opts;
    } else {
        binpack_default_options(&opt);
    }

    binpack_filter_t filter = opt.filter;
    uint16_t filter_param = opt.transpose_stride;
    if (filter == BINPACK_FILTER_AUTO) {
        filter = binpack_auto_select_filter(new_buf, new_sz, &filter_param);
    }

    /* Validate filter is in supported catalog */
    if (filter != BINPACK_FILTER_NONE &&
        filter != BINPACK_FILTER_X86_64 &&
        filter != BINPACK_FILTER_ARM32 &&
        filter != BINPACK_FILTER_AARCH64 &&
        filter != BINPACK_FILTER_RISCV &&
        filter != BINPACK_FILTER_TRANSPOSE_DELTA &&
        filter != BINPACK_FILTER_BITSHUFFLE &&
        filter != BINPACK_FILTER_TEXTURE_BCN) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    /* Normalize parameter by filter type */
    if (filter == BINPACK_FILTER_NONE ||
        filter == BINPACK_FILTER_X86_64 ||
        filter == BINPACK_FILTER_ARM32 ||
        filter == BINPACK_FILTER_AARCH64 ||
        filter == BINPACK_FILTER_RISCV) {
        filter_param = 0;
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        if (filter_param < 2 || filter_param > 64) filter_param = 4;
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        if (filter_param != 1 && filter_param != 2 && filter_param != 4 &&
            filter_param != 8 && filter_param != 16) {
            filter_param = 4;
        }
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        if (filter_param != 8 && filter_param != 16) filter_param = 8;
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
        binpack_transpose_delta_filter(old_work, old_sz, filter_param);
        binpack_transpose_delta_filter(new_work, new_sz, filter_param);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_filter(old_work, old_sz, filter_param);
        binpack_bitshuffle_filter(new_work, new_sz, filter_param);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
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

    size_t cbound = ZSTD_compressBound(new_sz);
    if (cbound == 0 || cbound > SIZE_MAX - 4096) {
        ZSTD_freeCCtx(cctx);
        free(old_work);
        free(new_work);
        return BINPACK_ERR_INVALID_PARAM;
    }
    size_t cmp_cap = cbound + 4096;

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

    if (out_buf.pos > SIZE_MAX - sizeof(binpack_patch_header_t)) {
        free(cmp_payload);
        return BINPACK_ERR_COMPRESSION_FAILED;
    }
    size_t total_patch_sz = sizeof(binpack_patch_header_t) + out_buf.pos;
    uint8_t *patch_data = (uint8_t*)malloc(total_patch_sz);
    if (!patch_data) {
        free(cmp_payload);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    /* Serialize header using portable little-endian loaders */
    binpack_patch_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    binpack_store_le32((uint8_t*)&hdr.patch_magic, BINPACK_MAGIC_PATCH);
    hdr.version_major = BINPACK_SDK_VERSION_MAJOR; /* 2 */
    hdr.version_minor = BINPACK_SDK_VERSION_MINOR; /* 2 */
    binpack_store_le16((uint8_t*)&hdr.flags, 0);
    binpack_store_le64((uint8_t*)&hdr.old_sz, (uint64_t)old_sz);
    binpack_store_le64((uint8_t*)&hdr.new_sz, (uint64_t)new_sz);
    binpack_store_le32((uint8_t*)&hdr.old_crc32, old_crc);
    binpack_store_le32((uint8_t*)&hdr.new_crc32, new_crc);
    hdr.filter_id = (uint8_t)filter;
    binpack_store_le16((uint8_t*)&hdr.filter_param, filter_param);
    memset(hdr.reserved, 0, sizeof(hdr.reserved));

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
    /* Defensive clearing of return pointers on any error */
    if (new_out) *new_out = NULL;
    if (new_sz) *new_sz = 0;

    /* Upfront pointer and bounds validation */
    if (!old_buf || !patch_buf || !new_out || !new_sz) {
        return BINPACK_ERR_INVALID_PARAM;
    }
    if (old_sz == 0 || old_sz > BINPACK_MAX_PATCH_SIZE) {
        return BINPACK_ERR_INVALID_PARAM;
    }
    if (patch_sz < sizeof(binpack_patch_header_t)) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* Portable, unaligned-safe Little-Endian header decoding */
    const uint8_t *hptr = (const uint8_t*)patch_buf;
    uint32_t magic = binpack_load_le32(hptr);
    if (magic != BINPACK_MAGIC_PATCH) {
        /* Reject any mismatch with canonical BINPACK_MAGIC_PATCH (0x504B5042 'BPKP') */
        return BINPACK_ERR_CORRUPT_DATA;
    }

    uint8_t ver_major = hptr[4];
    uint8_t ver_minor = hptr[5];
    uint16_t flags = binpack_load_le16(hptr + 6);
    uint64_t hdr_old_sz = binpack_load_le64(hptr + 8);
    uint64_t hdr_new_sz = binpack_load_le64(hptr + 16);
    uint32_t hdr_old_crc32 = binpack_load_le32(hptr + 24);
    uint32_t hdr_new_crc32 = binpack_load_le32(hptr + 28);
    uint8_t filter_id = hptr[32];
    uint16_t filter_param = binpack_load_le16(hptr + 33);
    const uint8_t *reserved = hptr + 35;

    /* Version compatibility: support historical v1.0 and enterprise v2.x */
    if (ver_major == 1) {
        if (ver_minor != 0) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else if (ver_major == 2) {
        if (ver_minor > BINPACK_SDK_VERSION_MINOR) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* Flags and reserved bytes policy: must be strictly 0 */
    if (flags != 0) {
        return BINPACK_ERR_CORRUPT_DATA;
    }
    for (int i = 0; i < 5; i++) {
        if (reserved[i] != 0) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    }

    /* Filter catalog and parameter validation */
    if (filter_id == (uint8_t)BINPACK_FILTER_NONE ||
        filter_id == (uint8_t)BINPACK_FILTER_X86_64 ||
        filter_id == (uint8_t)BINPACK_FILTER_ARM32 ||
        filter_id == (uint8_t)BINPACK_FILTER_AARCH64 ||
        filter_id == (uint8_t)BINPACK_FILTER_RISCV) {
        if (filter_param != 0) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else if (filter_id == (uint8_t)BINPACK_FILTER_TRANSPOSE_DELTA) {
        if (filter_param < 2 || filter_param > 64) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else if (filter_id == (uint8_t)BINPACK_FILTER_BITSHUFFLE) {
        if (filter_param != 1 && filter_param != 2 && filter_param != 4 &&
            filter_param != 8 && filter_param != 16) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else if (filter_id == (uint8_t)BINPACK_FILTER_TEXTURE_BCN) {
        if (filter_param != 8 && filter_param != 16) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else {
        /* Unknown or unsupported filter identifier */
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* Early size bounds & overflow check before any memory allocation */
    if (hdr_old_sz == 0 || hdr_old_sz > BINPACK_MAX_PATCH_SIZE || hdr_old_sz > (uint64_t)SIZE_MAX) {
        return BINPACK_ERR_CORRUPT_DATA;
    }
    if (hdr_new_sz == 0 || hdr_new_sz > BINPACK_MAX_PATCH_SIZE || hdr_new_sz > (uint64_t)SIZE_MAX) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* Baseline matching checks */
    if (hdr_old_sz != (uint64_t)old_sz) {
        return BINPACK_ERR_PATCH_MISMATCH;
    }
    uint32_t old_crc = binpack_crc32(0, (const uint8_t*)old_buf, old_sz);
    if (hdr_old_crc32 != old_crc) {
        return BINPACK_ERR_PATCH_MISMATCH;
    }

    /* Payload presence check */
    size_t cmp_payload_sz = patch_sz - sizeof(binpack_patch_header_t);
    if (cmp_payload_sz == 0) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* Prepare old_work with filter applied */
    uint8_t *old_work = (uint8_t*)malloc(old_sz);
    if (!old_work) return BINPACK_ERR_OUT_OF_MEMORY;
    memcpy(old_work, old_buf, old_sz);

    binpack_filter_t filter = (binpack_filter_t)filter_id;
    if (filter == BINPACK_FILTER_X86_64) {
        binpack_bcj_x86_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_ARM32) {
        binpack_bcj_arm32_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_AARCH64) {
        binpack_bcj_aarch64_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_RISCV) {
        binpack_bcj_riscv_filter(old_work, old_sz);
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        binpack_transpose_delta_filter(old_work, old_sz, filter_param);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_filter(old_work, old_sz, filter_param);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        binpack_texture_bcn_filter(old_work, old_sz, filter_param);
    }

    /* Allocate buffer for reconstructed target with +1 byte extra capacity.
     * This extra byte allows the decompressor to process the frame epilogue
     * even if exact expected_new_sz bytes were already produced, while strictly
     * detecting any oversized/bomb stream attempting to write > expected_new_sz. */
    size_t expected_new_sz = (size_t)hdr_new_sz;
    if (expected_new_sz > SIZE_MAX - 1) {
        free(old_work);
        return BINPACK_ERR_CORRUPT_DATA;
    }
    size_t decode_cap = expected_new_sz + 1;
    uint8_t *new_work = (uint8_t*)malloc(decode_cap);
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

    const uint8_t *cmp_payload = hptr + sizeof(binpack_patch_header_t);
    ZSTD_inBuffer in_buf = { cmp_payload, cmp_payload_sz, 0 };
    ZSTD_outBuffer out_buf = { new_work, decode_cap, 0 };

    size_t zret = 1;
    while (in_buf.pos < in_buf.size) {
        size_t prev_in = in_buf.pos;
        size_t prev_out = out_buf.pos;
        zret = ZSTD_decompressStream(dctx, &out_buf, &in_buf);
        if (ZSTD_isError(zret)) break;
        if (zret == 0) {
            /* First Zstandard frame completed cleanly.
             * Format constraint: exactly one frame allowed. Stop immediately to
             * enforce single-frame contract and prevent concatenated frames. */
            break;
        }
        if (out_buf.pos > expected_new_sz) {
            /* Oversized stream: decoded more bytes than declared in header */
            break;
        }
        if (in_buf.pos == prev_in && out_buf.pos == prev_out) {
            break; /* Anti-infinite loop / stalled progress */
        }
    }

    ZSTD_freeDCtx(dctx);
    free(old_work);

    /* Validate valid frame termination and exact expected output size */
    if (ZSTD_isError(zret) || zret != 0 || out_buf.pos != expected_new_sz) {
        free(new_work);
        return BINPACK_ERR_DECOMPRESSION_FAILED;
    }

    /* Strict complete input consumption guard: reject trailing garbage or concatenated frames */
    if (in_buf.pos != in_buf.size) {
        free(new_work);
        return BINPACK_ERR_CORRUPT_DATA;
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
        binpack_transpose_delta_unfilter(new_work, expected_new_sz, filter_param);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_unfilter(new_work, expected_new_sz, filter_param);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        binpack_texture_bcn_unfilter(new_work, expected_new_sz, filter_param);
    }

    /* Verify target integrity */
    uint32_t calc_new_crc = binpack_crc32(0, new_work, expected_new_sz);
    if (calc_new_crc != hdr_new_crc32) {
        free(new_work);
        return BINPACK_ERR_CHECKSUM_MISMATCH;
    }

    *new_out = new_work;
    *new_sz = expected_new_sz;
    return BINPACK_OK;
}
