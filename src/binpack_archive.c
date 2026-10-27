/**
 * @file binpack_archive.c
 * @brief BinPack Enterprise SDK - O(1) Seekable Random-Access Archive Engine (Virtual Texturing)
 */

#include "binpack_internal.h"

BINPACK_API binpack_archive_t* BINPACK_CALL binpack_archive_open(const char *filepath) {
    if (!filepath) return NULL;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    binpack_v2_header_t hdr;
    if (fread(&hdr, 1, sizeof(hdr), fp) != sizeof(hdr)) {
        fclose(fp);
        return NULL;
    }

    if (hdr.magic != BINPACK_MAGIC_V2) {
        fclose(fp);
        return NULL;
    }

    binpack_archive_t *arch = (binpack_archive_t*)calloc(1, sizeof(binpack_archive_t));
    if (!arch) {
        fclose(fp);
        return NULL;
    }
    arch->fp = fp;
    arch->header = hdr;
    arch->dctx = ZSTD_createDCtx();
    strncpy(arch->filepath, filepath, sizeof(arch->filepath) - 1);
    arch->filepath[sizeof(arch->filepath) - 1] = '\0';

    /* 1. Try O(1) Footer + Seek Table Index lookup at EOF */
    if (binpack_fseek64(fp, 0, SEEK_END) == 0) {
        int64_t file_sz = binpack_ftell64(fp);
        if (file_sz >= (int64_t)(sizeof(binpack_v2_header_t) + sizeof(binpack_v2_footer_t))) {
            int64_t footer_pos = file_sz - (int64_t)sizeof(binpack_v2_footer_t);
            if (binpack_fseek64(fp, footer_pos, SEEK_SET) == 0) {
                binpack_v2_footer_t footer;
                if (fread(&footer, 1, sizeof(footer), fp) == sizeof(footer) &&
                    footer.footer_magic == BINPACK_MAGIC_FOOTER) {
                    arch->footer = footer;

                    /* Multi-chunk O(1) Seek Table Index right before footer */
                    if (footer.chunk_count > 1 && footer.chunk_count <= 1000000) {
                        int64_t table_bytes = (int64_t)footer.chunk_count * (int64_t)sizeof(binpack_chunk_info_t);
                        int64_t idx_pos = footer_pos - (int64_t)sizeof(binpack_index_header_t) - table_bytes;
                        if (idx_pos >= (int64_t)sizeof(binpack_v2_header_t) &&
                            binpack_fseek64(fp, idx_pos, SEEK_SET) == 0) {
                            binpack_index_header_t idx_hdr;
                            if (fread(&idx_hdr, 1, sizeof(idx_hdr), fp) == sizeof(idx_hdr) &&
                                idx_hdr.index_magic == BINPACK_MAGIC_INDEX &&
                                idx_hdr.entry_count == footer.chunk_count) {
                                arch->chunks = (binpack_chunk_info_t*)malloc((size_t) table_bytes);
                                if (!arch->chunks) {
                                    binpack_archive_close(arch);
                                    return NULL;
                                }
                                if (fread(arch->chunks, 1, (size_t)table_bytes, fp) == (size_t)table_bytes) {
                                    arch->chunk_count = footer.chunk_count;
                                    return arch;
                                }
                                free(arch->chunks);
                                arch->chunks = NULL;
                            }
                        }
                    }
                }
            }
        }
    }

    /* 2. Fallback: 64-bit sequential chunk header scan (also O(1) for 1-chunk archives) */
    if (binpack_fseek64(fp, (int64_t)sizeof(binpack_v2_header_t), SEEK_SET) != 0) {
        binpack_archive_close(arch);
        return NULL;
    }

    size_t capacity = 16;
    arch->chunks = (binpack_chunk_info_t*)malloc(capacity * sizeof(binpack_chunk_info_t));
    if (!arch->chunks) {
        binpack_archive_close(arch);
        return NULL;
    }

    uint32_t chunk_idx = 0;
    while (1) {
        int64_t cur_pos = binpack_ftell64(fp);
        if (cur_pos < 0) break;

        uint32_t tag = 0;
        if (fread(&tag, 1, 4, fp) != 4) break;

        if (tag == BINPACK_MAGIC_INDEX) {
            uint32_t entry_count = 0;
            if (fread(&entry_count, 1, 4, fp) != 4) break;
            int64_t skip = (int64_t)entry_count * (int64_t)sizeof(binpack_chunk_info_t);
            if (binpack_fseek64(fp, skip, SEEK_CUR) != 0) break;
            continue;
        }

        if (tag == BINPACK_MAGIC_FOOTER) {
            binpack_fseek64(fp, cur_pos, SEEK_SET);
            fread(&arch->footer, 1, sizeof(binpack_v2_footer_t), fp);
            break;
        }

        if (tag != BINPACK_MAGIC_CHUNK) {
            break;
        }

        binpack_fseek64(fp, cur_pos, SEEK_SET);
        binpack_chunk_header_t chdr;
        if (fread(&chdr, 1, sizeof(chdr), fp) != sizeof(chdr)) break;

        binpack_chunk_bounds_t bounds;
        bounds.code_offset = 0;
        bounds.code_length = chdr.uncompressed_sz;
        if (chdr.filter_flags & BP_CHUNK_FLAG_BOUNDS) {
            if (fread(&bounds, 1, sizeof(bounds), fp) != sizeof(bounds)) break;
        }

        int64_t payload_offset = binpack_ftell64(fp);
        if (payload_offset < 0) break;

        if (chunk_idx >= capacity) {
            capacity *= 2;
            binpack_chunk_info_t *new_chunks = (binpack_chunk_info_t*)realloc(arch->chunks, capacity * sizeof(binpack_chunk_info_t));
            if (!new_chunks) {
                binpack_archive_close(arch);
                return NULL;
            }
            arch->chunks = new_chunks;
        }

        binpack_chunk_info_t *info = &arch->chunks[chunk_idx];
        info->chunk_index = chunk_idx;
        info->file_offset = (uint64_t)payload_offset;
        info->uncompressed_sz = chdr.uncompressed_sz;
        info->compressed_sz = chdr.compressed_sz;
        info->filter_id = chdr.filter_id;
        info->filter_flags = chdr.filter_flags;
        info->filter_param = chdr.filter_param;
        info->code_offset = bounds.code_offset;
        info->code_length = bounds.code_length;
        info->chunk_crc32 = chdr.chunk_crc32;

        chunk_idx++;

        /* Skip compressed payload to next chunk using 64-bit seek */
        if (binpack_fseek64(fp, payload_offset + (int64_t)chdr.compressed_sz, SEEK_SET) != 0) break;
    }

    arch->chunk_count = chunk_idx;
    return arch;
}

BINPACK_API uint32_t BINPACK_CALL binpack_archive_get_chunk_count(const binpack_archive_t *archive) {
    return archive ? archive->chunk_count : 0;
}

BINPACK_API uint64_t BINPACK_CALL binpack_archive_get_total_uncompressed_size(const binpack_archive_t *archive) {
    if (!archive) return 0;
    if (archive->footer.footer_magic == BINPACK_MAGIC_FOOTER) {
        return archive->footer.total_uncompressed;
    }
    uint64_t total = 0;
    for (uint32_t i = 0; i < archive->chunk_count; i++) {
        total += archive->chunks[i].uncompressed_sz;
    }
    return total;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_archive_get_chunk_info(
    const binpack_archive_t *archive,
    uint32_t chunk_idx,
    binpack_chunk_info_t *info
) {
    if (!archive || !info || chunk_idx >= archive->chunk_count) {
        return BINPACK_ERR_INVALID_PARAM;
    }
    *info = archive->chunks[chunk_idx];
    return BINPACK_OK;
}

/* Internal helper: decompresses a chunk's raw (still prefiltered) bytes, handling rolling prefix chains */
static binpack_status_t bp_archive_decompress_filtered_raw(
    const binpack_archive_t *archive,
    FILE *fp,
    ZSTD_DCtx *dctx,
    uint32_t chunk_idx,
    uint8_t *dst,
    size_t dst_capacity
) {
    const binpack_chunk_info_t *c = &archive->chunks[chunk_idx];
    if (dst_capacity < c->uncompressed_sz) {
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }
    if (c->uncompressed_sz == 0 || c->uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
        c->compressed_sz == 0 || c->compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    uint8_t *cmp_buf = (uint8_t*)malloc(c->compressed_sz);
    if (!cmp_buf) return BINPACK_ERR_OUT_OF_MEMORY;

    if (binpack_fseek64(fp, (int64_t)c->file_offset, SEEK_SET) != 0) {
        free(cmp_buf);
        return BINPACK_ERR_IO;
    }

    if (fread(cmp_buf, 1, c->compressed_sz, fp) != c->compressed_sz) {
        free(cmp_buf);
        return BINPACK_ERR_IO;
    }

    if (c->filter_flags & BP_CHUNK_FLAG_LZMA) {
        if (c->compressed_sz < 5) {
            free(cmp_buf);
            return BINPACK_ERR_CORRUPT_DATA;
        }
        size_t lzma_dst = c->uncompressed_sz;
        size_t lzma_src = c->compressed_sz - 5;
        int res = LzmaUncompress(
            dst, &lzma_dst,
            cmp_buf + 5, &lzma_src,
            cmp_buf, 5
        );
        free(cmp_buf);
        if (res != SZ_OK || lzma_dst != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    } else if (c->filter_flags & BP_CHUNK_FLAG_PREFIX) {
        if (chunk_idx == 0) {
            free(cmp_buf);
            return BINPACK_ERR_CORRUPT_DATA;
        }
        const binpack_chunk_info_t *prev_c = &archive->chunks[chunk_idx - 1];
        uint8_t *prev_buf = (uint8_t*)malloc(prev_c->uncompressed_sz);
        if (!prev_buf) {
            free(cmp_buf);
            return BINPACK_ERR_OUT_OF_MEMORY;
        }
        binpack_status_t st = bp_archive_decompress_filtered_raw(
            archive, fp, dctx, chunk_idx - 1, prev_buf, prev_c->uncompressed_sz
        );
        if (st != BINPACK_OK) {
            free(prev_buf);
            free(cmp_buf);
            return st;
        }
        ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
        size_t prefix_sz = prev_c->uncompressed_sz > (16 * 1024 * 1024) ? (16 * 1024 * 1024) : prev_c->uncompressed_sz;
        const uint8_t *prefix_ptr = prev_buf + (prev_c->uncompressed_sz - prefix_sz);
        ZSTD_DCtx_refPrefix(dctx, prefix_ptr, prefix_sz);

        ZSTD_inBuffer din = { cmp_buf, c->compressed_sz, 0 };
        ZSTD_outBuffer dout = { dst, c->uncompressed_sz, 0 };
        while (din.pos < din.size) {
            size_t zret = ZSTD_decompressStream(dctx, &dout, &din);
            if (ZSTD_isError(zret)) {
                free(prev_buf);
                free(cmp_buf);
                return BINPACK_ERR_DECOMPRESSION_FAILED;
            }
        }
        free(prev_buf);
        free(cmp_buf);
        if (dout.pos != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    } else {
        ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
        size_t d_sz = ZSTD_decompressDCtx(dctx, dst, c->uncompressed_sz, cmp_buf, c->compressed_sz);
        free(cmp_buf);
        if (ZSTD_isError(d_sz) || d_sz != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    }

    return BINPACK_OK;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_archive_decompress_chunk(
    binpack_archive_t *archive,
    uint32_t chunk_idx,
    void *dst, size_t dst_capacity,
    size_t *decompressed_sz
) {
    if (!archive || !archive->fp || !dst || !decompressed_sz || chunk_idx >= archive->chunk_count) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    const binpack_chunk_info_t *c = &archive->chunks[chunk_idx];
    if (dst_capacity < c->uncompressed_sz) {
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }

    /* Thread-safe local file handle & ZSTD_DCtx so concurrent Unity DecompressChunkAsync calls never race */
    FILE *local_fp = NULL;
    if (archive->filepath[0] != '\0') {
        local_fp = fopen(archive->filepath, "rb");
    }
    FILE *use_fp = local_fp ? local_fp : archive->fp;

    ZSTD_DCtx *local_dctx = ZSTD_createDCtx();
    if (!local_dctx) {
        if (local_fp) fclose(local_fp);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    binpack_status_t st = bp_archive_decompress_filtered_raw(
        archive, use_fp, local_dctx, chunk_idx, (uint8_t*)dst, dst_capacity
    );

    ZSTD_freeDCtx(local_dctx);
    if (local_fp) fclose(local_fp);

    if (st != BINPACK_OK) {
        return st;
    }

    /* Invert filter on chunk payload using exact code bounds and stride parameters */
    uint8_t *raw_buf = (uint8_t*)dst;
    binpack_filter_t filter = (binpack_filter_t)c->filter_id;
    uint16_t stride = c->filter_param;

    if (filter == BINPACK_FILTER_X86_64 || filter == BINPACK_FILTER_ARM32 ||
        filter == BINPACK_FILTER_AARCH64 || filter == BINPACK_FILTER_RISCV) {
        uint32_t code_off = (c->filter_flags & BP_CHUNK_FLAG_BOUNDS) ? c->code_offset : 0;
        uint32_t code_len = (c->filter_flags & BP_CHUNK_FLAG_BOUNDS) ? c->code_length : c->uncompressed_sz;
        if ((uint64_t)code_off + (uint64_t)code_len <= (uint64_t)c->uncompressed_sz) {
            uint8_t *target = raw_buf + code_off;
            if (filter == BINPACK_FILTER_X86_64) {
                binpack_bcj_x86_unfilter(target, code_len);
            } else if (filter == BINPACK_FILTER_ARM32) {
                binpack_bcj_arm32_unfilter(target, code_len);
            } else if (filter == BINPACK_FILTER_AARCH64) {
                binpack_bcj_aarch64_unfilter(target, code_len);
            } else if (filter == BINPACK_FILTER_RISCV) {
                binpack_bcj_riscv_unfilter(target, code_len);
            }
        }
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        binpack_transpose_delta_unfilter(raw_buf, c->uncompressed_sz, stride >= 2 ? stride : 4);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_unfilter(raw_buf, c->uncompressed_sz, stride >= 1 ? stride : 4);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        binpack_texture_bcn_unfilter(raw_buf, c->uncompressed_sz, (stride == 8 || stride == 16) ? stride : 8);
    }

    /* Verify Chunk CRC32 integrity when checksum flag is active */
    if (archive->header.flags & BP_FLAG_CHECKSUM) {
        uint32_t actual_crc = binpack_crc32(0, raw_buf, c->uncompressed_sz);
        if (actual_crc != c->chunk_crc32) {
            return BINPACK_ERR_CHECKSUM_MISMATCH;
        }
    }

    *decompressed_sz = c->uncompressed_sz;
    return BINPACK_OK;
}

BINPACK_API void BINPACK_CALL binpack_archive_close(binpack_archive_t *archive) {
    if (!archive) return;
    if (archive->fp) fclose(archive->fp);
    if (archive->chunks) free(archive->chunks);
    if (archive->dctx) ZSTD_freeDCtx(archive->dctx);
    free(archive);
}
