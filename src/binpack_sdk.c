/**
 * @file binpack_sdk.c
 * @brief BinPack Enterprise SDK - Public C ABI Implementation & Entry Points
 */

#include "binpack_internal.h"

/* ========================================================================= */
/* Options & Utility Helpers                                                 */
/* ========================================================================= */

BINPACK_API void BINPACK_CALL binpack_default_options(binpack_options_t *opts) {
    if (!opts) return;
    opts->clevel = 19;
    opts->nb_workers = 0; /* Auto-detect hardware cores */
    opts->chunk_size = BINPACK_DEFAULT_CHUNK_SIZE;
    opts->enable_micro_trial = 1; /* Zero-regression guard enabled by default */
    opts->filter = BINPACK_FILTER_AUTO;
    opts->transpose_stride = 4;
    opts->mode = BINPACK_MODE_DEFAULT;
}

BINPACK_API void BINPACK_CALL binpack_free(void *ptr) {
    if (ptr) free(ptr);
}

BINPACK_API const char* BINPACK_CALL binpack_version_string(void) {
    return BINPACK_SDK_VERSION_STRING;
}

BINPACK_API const char* BINPACK_CALL binpack_error_string(binpack_status_t status) {
    switch (status) {
        case BINPACK_OK:                       return "Success";
        case BINPACK_ERR_INVALID_PARAM:        return "Invalid parameter or configuration";
        case BINPACK_ERR_OUT_OF_MEMORY:        return "Out of memory";
        case BINPACK_ERR_COMPRESSION_FAILED:   return "Compression backend error";
        case BINPACK_ERR_DECOMPRESSION_FAILED: return "Decompression backend error";
        case BINPACK_ERR_CORRUPT_DATA:         return "Corrupted header or archive data";
        case BINPACK_ERR_CHECKSUM_MISMATCH:    return "CRC32 integrity check failed";
        case BINPACK_ERR_IO:                   return "File or stream I/O failure";
        case BINPACK_ERR_UNSUPPORTED_VERSION:  return "Unsupported stream version";
        case BINPACK_ERR_BUFFER_TOO_SMALL:     return "Destination buffer too small";
        default:                               return "Unknown error";
    }
}

/* ========================================================================= */
/* In-Memory Buffer Stream Adapters                                          */
/* ========================================================================= */

typedef struct mem_stream_ctx_s {
    const uint8_t *src;
    size_t src_size;
    size_t src_pos;

    uint8_t *dst;
    size_t dst_cap;
    size_t dst_size;
} mem_stream_ctx_t;

static size_t mem_read_cb(void *user_data, void *buffer, size_t size) {
    mem_stream_ctx_t *ctx = (mem_stream_ctx_t*)user_data;
    if (ctx->src_pos >= ctx->src_size) return 0;
    size_t avail = ctx->src_size - ctx->src_pos;
    size_t to_read = size < avail ? size : avail;
    memcpy(buffer, ctx->src + ctx->src_pos, to_read);
    ctx->src_pos += to_read;
    return to_read;
}

static size_t mem_write_cb(void *user_data, const void *buffer, size_t size) {
    mem_stream_ctx_t *ctx = (mem_stream_ctx_t*)user_data;
    if (ctx->dst_size + size > ctx->dst_cap) {
        size_t new_cap = ctx->dst_cap ? ctx->dst_cap * 2 : 65536;
        while (new_cap < ctx->dst_size + size) new_cap *= 2;
        uint8_t *new_data = (uint8_t*)realloc(ctx->dst, new_cap);
        if (!new_data) return 0;
        ctx->dst = new_data;
        ctx->dst_cap = new_cap;
    }
    memcpy(ctx->dst + ctx->dst_size, buffer, size);
    ctx->dst_size += size;
    return size;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    const binpack_options_t *opts
) {
    if (!src || !dst || !dst_sz) return BINPACK_ERR_INVALID_PARAM;

    mem_stream_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = (const uint8_t*)src;
    ctx.src_size = src_sz;

    binpack_options_t opt;
    if (opts) {
        opt = *opts;
        if (opt.chunk_size == 0 && src_sz > 0) {
            opt.chunk_size = (src_sz <= 64 * 1024 * 1024) ? (int)src_sz : (64 * 1024 * 1024);
        }
    } else {
        binpack_default_options(&opt);
        if (src_sz > (size_t)opt.chunk_size && src_sz <= 64 * 1024 * 1024) {
            opt.chunk_size = (int)src_sz;
        }
    }
    if (src_sz < (size_t)opt.chunk_size && src_sz >= BINPACK_MIN_CHUNK_SIZE) {
        opt.chunk_size = (int)src_sz;
    }

    binpack_status_t st = binpack_compress_stream(mem_read_cb, mem_write_cb, &ctx, src_sz, &opt, NULL);
    if (st == BINPACK_OK) {
        *dst = ctx.dst;
        *dst_sz = ctx.dst_size;
    } else {
        if (ctx.dst) free(ctx.dst);
        *dst = NULL;
        *dst_sz = 0;
    }
    return st;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_buffer_ex(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    int verify_checksum
) {
    if (!src || !dst || !dst_sz) return BINPACK_ERR_INVALID_PARAM;

    /* --------------------------------------------------------------------- */
    /* Fast-Path: Single-Chunk Zero-Copy Direct In-Place Decompression       */
    /* --------------------------------------------------------------------- */
    if (src_sz >= sizeof(binpack_v2_header_t) + sizeof(binpack_chunk_header_t)) {
        const uint8_t *s = (const uint8_t*)src;
        uint32_t magic = *(const uint32_t*)s;
        if (magic == BINPACK_MAGIC_V2) {
            binpack_chunk_header_t *chdr = (binpack_chunk_header_t*)(s + sizeof(binpack_v2_header_t));
            if (chdr->chunk_magic == BINPACK_MAGIC_CHUNK) {
                size_t off = sizeof(binpack_v2_header_t) + sizeof(binpack_chunk_header_t);
                size_t code_off = 0;
                size_t code_len = chdr->uncompressed_sz;
                if (chdr->filter_flags & BP_CHUNK_FLAG_BOUNDS) {
                    if (src_sz >= off + sizeof(binpack_chunk_bounds_t)) {
                        binpack_chunk_bounds_t *b = (binpack_chunk_bounds_t*)(s + off);
                        code_off = b->code_offset;
                        code_len = b->code_length;
                        off += sizeof(binpack_chunk_bounds_t);
                    }
                }
                if (src_sz >= off + chdr->compressed_sz) {
                    size_t after_payload = off + chdr->compressed_sz;
                    int is_single_chunk = 0;
                    if (after_payload == src_sz) {
                        is_single_chunk = 1;
                    } else if (after_payload + sizeof(binpack_v2_footer_t) <= src_sz) {
                        uint32_t next_tag = *(const uint32_t*)(s + after_payload);
                        if (next_tag == BINPACK_MAGIC_FOOTER) {
                            is_single_chunk = 1;
                        }
                    }
                    if (is_single_chunk) {
                        uint8_t *out = (uint8_t*)malloc(chdr->uncompressed_sz);
                        if (!out) return BINPACK_ERR_OUT_OF_MEMORY;

                        const uint8_t *payload = s + off;
                        int decomp_ok = 0;

                        if (chdr->filter_flags & BP_CHUNK_FLAG_LZMA) {
                            if (chdr->compressed_sz >= 5) {
                                unsigned char props[5];
                                memcpy(props, payload, 5);
                                size_t lzma_in = chdr->compressed_sz - 5;
                                size_t lzma_out = chdr->uncompressed_sz;
                                int lzma_res = LzmaUncompress(out, &lzma_out, payload + 5, &lzma_in, props, 5);
                                if (lzma_res == SZ_OK && lzma_out == chdr->uncompressed_sz) {
                                    decomp_ok = 1;
                                }
                            }
                        } else {
                            size_t d_sz = ZSTD_decompress(out, chdr->uncompressed_sz, payload, chdr->compressed_sz);
                            if (!ZSTD_isError(d_sz) && d_sz == chdr->uncompressed_sz) {
                                decomp_ok = 1;
                            }
                        }

                        if (!decomp_ok) {
                            free(out);
                            return BINPACK_ERR_DECOMPRESSION_FAILED;
                        }

                        /* In-place unfilter directly on destination buffer */
                        uint8_t fid = chdr->filter_id;
                        if (fid == BINPACK_FILTER_X86_64) {
                            binpack_bcj_x86_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_ARM32) {
                            binpack_bcj_arm32_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_AARCH64) {
                            binpack_bcj_aarch64_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_BITSHUFFLE) {
                            binpack_bitshuffle_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        } else if (fid == BINPACK_FILTER_TRANSPOSE_DELTA) {
                            binpack_transpose_delta_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        } else if (fid == BINPACK_FILTER_TEXTURE_BCN) {
                            binpack_texture_bcn_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        }

                        /* Integrity verification */
                        if (verify_checksum) {
                            uint32_t chk = binpack_crc32(0, out, chdr->uncompressed_sz);
                            if (chk != chdr->chunk_crc32) {
                                free(out);
                                return BINPACK_ERR_CHECKSUM_MISMATCH;
                            }
                        }

                        *dst = out;
                        *dst_sz = chdr->uncompressed_sz;
                        return BINPACK_OK;
                    }
                }
            }
        }
    }

    /* Fallback: Multi-chunk general streaming engine */
    mem_stream_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = (const uint8_t*)src;
    ctx.src_size = src_sz;

    /* Preallocate contiguous destination from footer if available to eliminate realloc overhead */
    if (src_sz >= sizeof(binpack_v2_footer_t)) {
        const binpack_v2_footer_t *ftr = (const binpack_v2_footer_t*)((const uint8_t*)src + src_sz - sizeof(binpack_v2_footer_t));
        if (ftr->footer_magic == BINPACK_MAGIC_FOOTER && ftr->total_uncompressed > 0 && ftr->total_uncompressed <= 0x7FFFFFFF) {
            ctx.dst = (uint8_t*)malloc((size_t)ftr->total_uncompressed);
            if (ctx.dst) {
                ctx.dst_cap = (size_t)ftr->total_uncompressed;
            }
        }
    }

    binpack_status_t st = binpack_decompress_stream(mem_read_cb, mem_write_cb, &ctx, NULL);
    if (st == BINPACK_OK) {
        *dst = ctx.dst;
        *dst_sz = ctx.dst_size;
    } else {
        if (ctx.dst) free(ctx.dst);
        *dst = NULL;
        *dst_sz = 0;
    }
    return st;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz
) {
    return binpack_decompress_buffer_ex(src, src_sz, dst, dst_sz, 1);
}

/* ========================================================================= */
/* File-Based Streaming Implementations                                      */
/* ========================================================================= */

typedef struct file_stream_ctx_s {
    FILE *fin;
    FILE *fout;
    binpack_progress_fn progress;
    void *user_data;
} file_stream_ctx_t;

static size_t file_read_cb(void *user_data, void *buffer, size_t size) {
    file_stream_ctx_t *ctx = (file_stream_ctx_t*)user_data;
    return fread(buffer, 1, size, ctx->fin);
}

static size_t file_write_cb(void *user_data, const void *buffer, size_t size) {
    file_stream_ctx_t *ctx = (file_stream_ctx_t*)user_data;
    return fwrite(buffer, 1, size, ctx->fout);
}

static void file_progress_cb(void *user_data, uint64_t in_bytes, uint64_t total_in, uint64_t out_bytes) {
    file_stream_ctx_t *ctx = (file_stream_ctx_t*)user_data;
    if (ctx->progress) {
        ctx->progress(ctx->user_data, in_bytes, total_in, out_bytes);
    }
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_file(
    const char *in_path,
    const char *out_path,
    const binpack_options_t *opts,
    binpack_progress_fn progress,
    void *user_data
) {
    if (!in_path || !out_path) return BINPACK_ERR_INVALID_PARAM;

    FILE *fin = fopen(in_path, "rb");
    if (!fin) return BINPACK_ERR_IO;

    /* Get file size */
    _fseeki64(fin, 0, SEEK_END);
    uint64_t total_sz = (uint64_t)_ftelli64(fin);
    _fseeki64(fin, 0, SEEK_SET);

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        fclose(fin);
        return BINPACK_ERR_IO;
    }

    /* Enable 256 KB full buffering for high-throughput disk streaming */
    setvbuf(fin, NULL, _IOFBF, 256 * 1024);
    setvbuf(fout, NULL, _IOFBF, 256 * 1024);

    file_stream_ctx_t ctx = { fin, fout, progress, user_data };
    binpack_status_t st = binpack_compress_stream(file_read_cb, file_write_cb, &ctx, total_sz, opts, progress ? file_progress_cb : NULL);

    fclose(fin);
    fclose(fout);
    return st;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_file(
    const char *in_path,
    const char *out_path,
    binpack_progress_fn progress,
    void *user_data
) {
    if (!in_path || !out_path) return BINPACK_ERR_INVALID_PARAM;

    FILE *fin = fopen(in_path, "rb");
    if (!fin) return BINPACK_ERR_IO;

    FILE *fout = fopen(out_path, "wb");
    if (!fout) {
        fclose(fin);
        return BINPACK_ERR_IO;
    }

    /* Enable 256 KB full buffering for high-throughput disk streaming */
    setvbuf(fin, NULL, _IOFBF, 256 * 1024);
    setvbuf(fout, NULL, _IOFBF, 256 * 1024);

    file_stream_ctx_t ctx = { fin, fout, progress, user_data };
    binpack_status_t st = binpack_decompress_stream(file_read_cb, file_write_cb, &ctx, progress ? file_progress_cb : NULL);

    fclose(fin);
    fclose(fout);
    return st;
}

/* ========================================================================= */
/* Archive Metadata Query                                                    */
/* ========================================================================= */

BINPACK_API binpack_status_t BINPACK_CALL binpack_query_file_info(
    const char *path,
    binpack_file_info_t *info
) {
    if (!path || !info) return BINPACK_ERR_INVALID_PARAM;
    memset(info, 0, sizeof(*info));

    FILE *f = fopen(path, "rb");
    if (!f) return BINPACK_ERR_IO;

    _fseeki64(f, 0, SEEK_END);
    uint64_t file_len = (uint64_t)_ftelli64(f);
    _fseeki64(f, 0, SEEK_SET);

    if (file_len < 16) {
        fclose(f);
        return BINPACK_ERR_CORRUPT_DATA;
    }

    uint32_t magic = 0;
    if (fread(&magic, 1, 4, f) != 4) {
        fclose(f);
        return BINPACK_ERR_IO;
    }

    if (magic == BINPACK_MAGIC_V2) {
        binpack_v2_header_t hdr;
        _fseeki64(f, 0, SEEK_SET);
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
            fclose(f);
            return BINPACK_ERR_IO;
        }

        info->version = 2;
        info->compressed_size = file_len;
        info->flags = (uint8_t)hdr.flags;

        /* Read footer if present */
        if (file_len >= sizeof(binpack_v2_header_t) + sizeof(binpack_v2_footer_t)) {
            binpack_v2_footer_t footer;
            _fseeki64(f, file_len - sizeof(binpack_v2_footer_t), SEEK_SET);
            if (fread(&footer, 1, sizeof(footer), f) == sizeof(footer) &&
                footer.footer_magic == BINPACK_MAGIC_FOOTER) {
                info->uncompressed_size = footer.total_uncompressed;
                info->chunk_count = footer.chunk_count;
                info->original_crc32 = footer.stream_crc32;
            }
        }
        fclose(f);
        return BINPACK_OK;
    } else if (magic == BINPACK_MAGIC_V1) {
        uint8_t rest_v1[28];
        if (fread(rest_v1, 1, 28, f) != 28) {
            fclose(f);
            return BINPACK_ERR_IO;
        }
        info->version = 1;
        info->uncompressed_size = *(uint32_t*)&rest_v1[0];
        info->compressed_size = file_len;
        info->chunk_count = 1;
        info->original_crc32 = *(uint32_t*)&rest_v1[8];
        info->primary_filter = rest_v1[12];
        fclose(f);
        return BINPACK_OK;
    }

    fclose(f);
    return BINPACK_ERR_CORRUPT_DATA;
}
