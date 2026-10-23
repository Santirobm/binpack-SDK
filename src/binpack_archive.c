/**
 * @file binpack_archive.c
 * @brief BinPack Enterprise SDK - Seekable Random-Access Archive Engine (Virtual Texturing)
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

    /* Scan archive chunks to build fast index table */
    size_t capacity = 16;
    arch->chunks = (binpack_chunk_info_t*)malloc(capacity * sizeof(binpack_chunk_info_t));
    if (!arch->chunks) {
        binpack_archive_close(arch);
        return NULL;
    }

    uint32_t chunk_idx = 0;
    while (1) {
        long cur_pos = ftell(fp);
        uint32_t tag = 0;
        if (fread(&tag, 1, 4, fp) != 4) break;

        if (tag == BINPACK_MAGIC_FOOTER) {
            fseek(fp, cur_pos, SEEK_SET);
            fread(&arch->footer, 1, sizeof(binpack_v2_footer_t), fp);
            break;
        }

        if (tag != BINPACK_MAGIC_CHUNK) {
            break; /* Malformed chunk or EOF */
        }

        fseek(fp, cur_pos, SEEK_SET);
        binpack_chunk_header_t chdr;
        if (fread(&chdr, 1, sizeof(chdr), fp) != sizeof(chdr)) break;

        binpack_chunk_bounds_t bounds;
        if (chdr.filter_flags & BP_CHUNK_FLAG_BOUNDS) {
            if (fread(&bounds, 1, sizeof(bounds), fp) != sizeof(bounds)) break;
        }

        long payload_offset = ftell(fp);

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
        info->chunk_crc32 = chdr.chunk_crc32;

        chunk_idx++;

        /* Skip compressed payload to next chunk */
        fseek(fp, payload_offset + chdr.compressed_sz, SEEK_SET);
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

BINPACK_API binpack_status_t BINPACK_CALL binpack_archive_decompress_chunk(
    binpack_archive_t *archive,
    uint32_t chunk_idx,
    void *dst, size_t dst_capacity,
    size_t *decompressed_sz
) {
    if (!archive || !archive->fp || !dst || !decompressed_sz || chunk_idx >= archive->chunk_count) {
        return BINPACK_ERR_INVALID_PARAM;
    }

    binpack_chunk_info_t *c = &archive->chunks[chunk_idx];
    if (dst_capacity < c->uncompressed_sz) {
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t *cmp_buf = (uint8_t*)malloc(c->compressed_sz);
    if (!cmp_buf) return BINPACK_ERR_OUT_OF_MEMORY;

    if (fseek(archive->fp, (long)c->file_offset, SEEK_SET) != 0) {
        free(cmp_buf);
        return BINPACK_ERR_IO;
    }

    if (fread(cmp_buf, 1, c->compressed_sz, archive->fp) != c->compressed_sz) {
        free(cmp_buf);
        return BINPACK_ERR_IO;
    }

    /* Decompress chunk payload */
    if (c->filter_flags & BP_CHUNK_FLAG_LZMA) {
        /* LZMA2 Range Coder Core */
        if (c->compressed_sz < 5) {
            free(cmp_buf);
            return BINPACK_ERR_CORRUPT_DATA;
        }
        size_t lzma_dst = c->uncompressed_sz;
        size_t lzma_src = c->compressed_sz - 5;
        int res = LzmaUncompress(
            (unsigned char*)dst, &lzma_dst,
            cmp_buf + 5, &lzma_src,
            cmp_buf, 5
        );
        free(cmp_buf);
        if (res != SZ_OK || lzma_dst != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    } else {
        /* Zstandard FSE Core */
        if (!archive->dctx) {
            archive->dctx = ZSTD_createDCtx();
            if (!archive->dctx) {
                free(cmp_buf);
                return BINPACK_ERR_OUT_OF_MEMORY;
            }
        }
        ZSTD_DCtx_reset(archive->dctx, ZSTD_reset_session_only);
        size_t d_sz = ZSTD_decompressDCtx(archive->dctx, dst, c->uncompressed_sz, cmp_buf, c->compressed_sz);
        free(cmp_buf);
        if (ZSTD_isError(d_sz) || d_sz != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    }

    /* Invert filter on chunk payload */
    binpack_filter_t filter = (binpack_filter_t)c->filter_id;
    if (filter == BINPACK_FILTER_X86_64) {
        binpack_bcj_x86_unfilter((uint8_t*)dst, c->uncompressed_sz);
    } else if (filter == BINPACK_FILTER_ARM32) {
        binpack_bcj_arm32_unfilter((uint8_t*)dst, c->uncompressed_sz);
    } else if (filter == BINPACK_FILTER_AARCH64) {
        binpack_bcj_aarch64_unfilter((uint8_t*)dst, c->uncompressed_sz);
    } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
        binpack_transpose_delta_unfilter((uint8_t*)dst, c->uncompressed_sz, 4);
    } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
        binpack_bitshuffle_unfilter((uint8_t*)dst, c->uncompressed_sz, 4);
    } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
        binpack_texture_bcn_unfilter((uint8_t*)dst, c->uncompressed_sz, 8);
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
