/**
 * @file binpack_stream.c
 * @brief BinPack Enterprise SDK - Streaming Architecture & Dual-Engine Bounded Chunk Framing
 */

#include "binpack_internal.h"

/* ========================================================================= */
/* Generic Stream Compression Engine                                         */
/* ========================================================================= */

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_stream(
    binpack_read_fn reader,
    binpack_write_fn writer,
    void *user_data,
    uint64_t total_input_size,
    const binpack_options_t *opts,
    binpack_progress_fn progress
) {
    if (!reader || !writer) return BINPACK_ERR_INVALID_PARAM;

    binpack_options_t opt;
    if (opts) {
        opt = *opts;
    } else {
        binpack_default_options(&opt);
    }

    size_t chunk_size = opt.chunk_size > 0 ? (size_t)opt.chunk_size : BINPACK_DEFAULT_CHUNK_SIZE;
    if (chunk_size < BINPACK_MIN_CHUNK_SIZE) chunk_size = BINPACK_MIN_CHUNK_SIZE;
    if (chunk_size > BINPACK_MAX_CHUNK_SIZE) chunk_size = BINPACK_MAX_CHUNK_SIZE;

    uint8_t *raw_chunk = (uint8_t*)malloc(chunk_size);
    uint8_t *prev_raw  = (uint8_t*)malloc(chunk_size);
    size_t cmp_bound   = ZSTD_compressBound(chunk_size) + 65536;
    uint8_t *cmp_chunk = (uint8_t*)malloc(cmp_bound);

    if (!raw_chunk || !prev_raw || !cmp_chunk) {
        free(raw_chunk);
        free(prev_raw);
        free(cmp_chunk);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    ZSTD_CCtx *cctx = NULL;
    int clevel = opt.clevel > 0 ? opt.clevel : 19;

    if (opt.mode != BINPACK_MODE_ARCHIVAL) {
        cctx = ZSTD_createCCtx();
        if (!cctx) {
            free(raw_chunk);
            free(prev_raw);
            free(cmp_chunk);
            return BINPACK_ERR_OUT_OF_MEMORY;
        }

        if (opt.nb_workers > 0) {
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, opt.nb_workers);
        }

        if (opt.mode == BINPACK_MODE_RUNTIME) {
            if (clevel > 12) clevel = 12;
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_strategy, ZSTD_dfast);
        } else {
            /* BINPACK_MODE_DEFAULT: Balanced High-Efficiency Profile */
            if (clevel > 19) clevel = 19;
            ZSTD_CCtx_setParameter(cctx, ZSTD_c_strategy, ZSTD_btultra);
        }
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, clevel);
    }

    /* 1. Write Stream Header */
    binpack_v2_header_t hdr;
    memset(&hdr, 0, sizeof(hdr));
    hdr.magic = BINPACK_MAGIC_V2;
    hdr.version_major = BINPACK_SDK_VERSION_MAJOR;
    hdr.version_minor = BINPACK_SDK_VERSION_MINOR;
    hdr.flags = BP_FLAG_CHECKSUM;
    hdr.default_chunk_sz = (uint32_t)chunk_size;

    if (writer(user_data, &hdr, sizeof(hdr)) != sizeof(hdr)) {
        if (cctx) ZSTD_freeCCtx(cctx);
        free(raw_chunk);
        free(prev_raw);
        free(cmp_chunk);
        return BINPACK_ERR_IO;
    }

    uint64_t total_in = 0;
    uint64_t total_out = sizeof(hdr);
    uint32_t chunk_count = 0;
    uint32_t stream_crc = 0;
    size_t prev_raw_len = 0;

    /* 2. Process Input in Bounded Chunks */
    while (1) {
        size_t bytes_read = reader(user_data, raw_chunk, chunk_size);
        if (bytes_read == 0) break;

        stream_crc = binpack_crc32(stream_crc, raw_chunk, bytes_read);
        uint32_t chunk_crc = binpack_crc32(0, raw_chunk, bytes_read);

        /* Determine Filter */
        binpack_filter_t filter = opt.filter;
        uint16_t filter_param = opt.transpose_stride;
        if (filter == BINPACK_FILTER_AUTO) {
            filter = binpack_auto_select_filter(raw_chunk, bytes_read, &filter_param);
        }

        /* Micro-Trial zero-regression validation */
        if (opt.enable_micro_trial && filter != BINPACK_FILTER_NONE) {
            if (!binpack_evaluate_filter_gain(raw_chunk, bytes_read, filter, filter_param)) {
                filter = BINPACK_FILTER_NONE;
            }
        }

        uint8_t filter_flags = 0;
        binpack_chunk_bounds_t bounds;
        bounds.code_offset = 0;
        bounds.code_length = (uint32_t)bytes_read;

        /* Apply Prefilter */
        if (filter == BINPACK_FILTER_X86_64 || filter == BINPACK_FILTER_ARM32 || filter == BINPACK_FILTER_AARCH64) {
            size_t code_off = 0, code_len = bytes_read;
            binpack_find_code_bounds(raw_chunk, bytes_read, &code_off, &code_len);
            if (code_len > 0 && code_off < bytes_read && (code_off + code_len) <= bytes_read) {
                if (code_off > 0 || code_len < bytes_read) {
                    filter_flags |= BP_CHUNK_FLAG_BOUNDS;
                    bounds.code_offset = (uint32_t)code_off;
                    bounds.code_length = (uint32_t)code_len;
                }
                uint8_t *target = raw_chunk + code_off;
                if (filter == BINPACK_FILTER_X86_64) {
                    binpack_bcj_x86_filter(target, code_len);
                } else if (filter == BINPACK_FILTER_ARM32) {
                    binpack_bcj_arm32_filter(target, code_len);
                } else if (filter == BINPACK_FILTER_AARCH64) {
                    binpack_bcj_aarch64_filter(target, code_len);
                }
            }
        } else if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
            if (filter_param < 2) filter_param = 4;
            binpack_transpose_delta_filter(raw_chunk, bytes_read, filter_param);
        } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
            if (filter_param < 1) filter_param = 4;
            binpack_bitshuffle_filter(raw_chunk, bytes_read, filter_param);
        } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
            if (filter_param != 8 && filter_param != 16) filter_param = 8;
            binpack_texture_bcn_filter(raw_chunk, bytes_read, filter_param);
        }

        size_t c_size = 0;

        /* Compress chunk based on Engine Profile */
        if (opt.mode == BINPACK_MODE_ARCHIVAL) {
            /* Backend: Bit-Level Arithmetic Range Coder (LZMA2 Core) */
            filter_flags |= BP_CHUNK_FLAG_LZMA;
            size_t lzma_dst_len = cmp_bound - 5;
            unsigned char props[5];
            size_t props_sz = 5;
            unsigned dict_sz = (unsigned)(bytes_read < (32 * 1024 * 1024) ? bytes_read : (32 * 1024 * 1024));
            if (dict_sz < 4096) dict_sz = 4096;

            int lzma_res = LzmaCompress(
                cmp_chunk + 5, &lzma_dst_len,
                raw_chunk, bytes_read,
                props, &props_sz,
                9, /* level 9 ultra */
                dict_sz,
                3, 0, 2, 64, 1
            );

            if (lzma_res != SZ_OK) {
                if (cctx) ZSTD_freeCCtx(cctx);
                free(raw_chunk);
                free(prev_raw);
                free(cmp_chunk);
                return BINPACK_ERR_COMPRESSION_FAILED;
            }

            memcpy(cmp_chunk, props, 5);
            c_size = lzma_dst_len + 5;
        } else {
            /* Backend: High-Throughput Finite State Entropy (Zstandard) */
            if (chunk_count > 0 && prev_raw_len > 0) {
                /* Rolling Context Dictionary across Chunks (Overlapping History) */
                filter_flags |= BP_CHUNK_FLAG_PREFIX;
                ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
                size_t prefix_sz = prev_raw_len > (16 * 1024 * 1024) ? (16 * 1024 * 1024) : prev_raw_len;
                const uint8_t *prefix_ptr = prev_raw + (prev_raw_len - prefix_sz);
                ZSTD_CCtx_refPrefix(cctx, prefix_ptr, prefix_sz);

                ZSTD_inBuffer in_buf = { raw_chunk, bytes_read, 0 };
                ZSTD_outBuffer out_buf = { cmp_chunk, cmp_bound, 0 };
                size_t zret = ZSTD_compressStream2(cctx, &out_buf, &in_buf, ZSTD_e_end);
                if (ZSTD_isError(zret) || in_buf.pos != in_buf.size) {
                    ZSTD_freeCCtx(cctx);
                    free(raw_chunk);
                    free(prev_raw);
                    free(cmp_chunk);
                    return BINPACK_ERR_COMPRESSION_FAILED;
                }
                c_size = out_buf.pos;
            } else {
                ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
                c_size = ZSTD_compressCCtx(cctx, cmp_chunk, cmp_bound, raw_chunk, bytes_read, clevel);
                if (ZSTD_isError(c_size)) {
                    ZSTD_freeCCtx(cctx);
                    free(raw_chunk);
                    free(prev_raw);
                    free(cmp_chunk);
                    return BINPACK_ERR_COMPRESSION_FAILED;
                }
            }

            /* Retain filtered chunk for next chunk's reference prefix */
            memcpy(prev_raw, raw_chunk, bytes_read);
            prev_raw_len = bytes_read;
        }

        /* Write Chunk Header */
        binpack_chunk_header_t chdr;
        memset(&chdr, 0, sizeof(chdr));
        chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr.uncompressed_sz = (uint32_t)bytes_read;
        chdr.compressed_sz = (uint32_t)c_size;
        chdr.filter_id = (uint8_t)filter;
        chdr.filter_flags = filter_flags;
        chdr.filter_param = filter_param;
        chdr.chunk_crc32 = chunk_crc;

        if (writer(user_data, &chdr, sizeof(chdr)) != sizeof(chdr)) {
            if (cctx) ZSTD_freeCCtx(cctx);
            free(raw_chunk);
            free(prev_raw);
            free(cmp_chunk);
            return BINPACK_ERR_IO;
        }
        total_out += sizeof(chdr);

        /* Write Bounds Subheader if present */
        if (filter_flags & BP_CHUNK_FLAG_BOUNDS) {
            if (writer(user_data, &bounds, sizeof(bounds)) != sizeof(bounds)) {
                if (cctx) ZSTD_freeCCtx(cctx);
                free(raw_chunk);
                free(prev_raw);
                free(cmp_chunk);
                return BINPACK_ERR_IO;
            }
            total_out += sizeof(bounds);
        }

        /* Write Compressed Payload */
        if (writer(user_data, cmp_chunk, c_size) != c_size) {
            if (cctx) ZSTD_freeCCtx(cctx);
            free(raw_chunk);
            free(prev_raw);
            free(cmp_chunk);
            return BINPACK_ERR_IO;
        }
        total_out += c_size;

        total_in += bytes_read;
        chunk_count++;

        if (progress) {
            progress(user_data, total_in, total_input_size, total_out);
        }
    }

    /* 3. Write V2 Container Stream Footer */
    binpack_v2_footer_t footer;
    memset(&footer, 0, sizeof(footer));
    footer.footer_magic = BINPACK_MAGIC_FOOTER;
    footer.total_uncompressed = total_in;
    footer.total_compressed = total_out + sizeof(footer);
    footer.chunk_count = chunk_count;
    footer.stream_crc32 = stream_crc;

    if (writer(user_data, &footer, sizeof(footer)) != sizeof(footer)) {
        if (cctx) ZSTD_freeCCtx(cctx);
        free(raw_chunk);
        free(prev_raw);
        free(cmp_chunk);
        return BINPACK_ERR_IO;
    }

    if (cctx) ZSTD_freeCCtx(cctx);
    free(raw_chunk);
    free(prev_raw);
    free(cmp_chunk);
    return BINPACK_OK;
}

/* ========================================================================= */
/* Generic Stream Decompression Engine                                       */
/* ========================================================================= */

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_stream(
    binpack_read_fn reader,
    binpack_write_fn writer,
    void *user_data,
    binpack_progress_fn progress
) {
    if (!reader || !writer) return BINPACK_ERR_INVALID_PARAM;

    uint32_t magic = 0;
    if (reader(user_data, &magic, 4) != 4) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* --------------------------------------------------------------------- */
    /* Case A: V2 Enterprise Streaming Format ('BPK2')                       */
    /* --------------------------------------------------------------------- */
    if (magic == BINPACK_MAGIC_V2) {
        binpack_v2_header_t hdr;
        hdr.magic = magic;
        size_t rest_hdr = sizeof(binpack_v2_header_t) - 4;
        if (reader(user_data, ((uint8_t*)&hdr) + 4, rest_hdr) != rest_hdr) {
            return BINPACK_ERR_CORRUPT_DATA;
        }

        if (hdr.version_major > BINPACK_SDK_VERSION_MAJOR) {
            return BINPACK_ERR_UNSUPPORTED_VERSION;
        }

        ZSTD_DCtx *dctx = ZSTD_createDCtx();
        if (!dctx) return BINPACK_ERR_OUT_OF_MEMORY;

        uint64_t total_in = sizeof(binpack_v2_header_t);
        uint64_t total_out = 0;
        uint32_t stream_crc = 0;

        uint8_t *raw_buf  = NULL;
        size_t raw_cap    = 0;
        uint8_t *cmp_buf  = NULL;
        size_t cmp_cap    = 0;
        uint8_t *prev_raw = NULL;
        size_t prev_cap   = 0;
        size_t prev_len   = 0;

        while (1) {
            uint32_t tag = 0;
            size_t r = reader(user_data, &tag, 4);
            if (r == 0) break;
            if (r != 4) {
                ZSTD_freeDCtx(dctx);
                free(raw_buf);
                free(cmp_buf);
                free(prev_raw);
                return BINPACK_ERR_CORRUPT_DATA;
            }

            /* Stream footer reached */
            if (tag == BINPACK_MAGIC_FOOTER) {
                binpack_v2_footer_t footer;
                footer.footer_magic = tag;
                size_t rest_footer = sizeof(binpack_v2_footer_t) - 4;
                if (reader(user_data, ((uint8_t*)&footer) + 4, rest_footer) == rest_footer) {
                    if (footer.stream_crc32 != stream_crc) {
                        ZSTD_freeDCtx(dctx);
                        free(raw_buf);
                        free(cmp_buf);
                        free(prev_raw);
                        return BINPACK_ERR_CHECKSUM_MISMATCH;
                    }
                }
                break;
            }

            if (tag != BINPACK_MAGIC_CHUNK) {
                ZSTD_freeDCtx(dctx);
                free(raw_buf);
                free(cmp_buf);
                free(prev_raw);
                return BINPACK_ERR_CORRUPT_DATA;
            }

            /* Read chunk header */
            binpack_chunk_header_t chdr;
            chdr.chunk_magic = tag;
            size_t rest_chdr = sizeof(binpack_chunk_header_t) - 4;
            if (reader(user_data, ((uint8_t*)&chdr) + 4, rest_chdr) != rest_chdr) {
                ZSTD_freeDCtx(dctx);
                free(raw_buf);
                free(cmp_buf);
                free(prev_raw);
                return BINPACK_ERR_CORRUPT_DATA;
            }
            total_in += sizeof(binpack_chunk_header_t);

            /* Check bounds subheader */
            binpack_chunk_bounds_t bounds;
            bounds.code_offset = 0;
            bounds.code_length = chdr.uncompressed_sz;
            if (chdr.filter_flags & BP_CHUNK_FLAG_BOUNDS) {
                if (reader(user_data, &bounds, sizeof(bounds)) != sizeof(bounds)) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_CORRUPT_DATA;
                }
                total_in += sizeof(bounds);
            }

            /* Resize memory buffers if necessary */
            if (chdr.uncompressed_sz > raw_cap) {
                raw_cap = chdr.uncompressed_sz;
                uint8_t *new_raw = (uint8_t*)realloc(raw_buf, raw_cap);
                if (!new_raw) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_OUT_OF_MEMORY;
                }
                raw_buf = new_raw;
            }
            if (chdr.compressed_sz > cmp_cap) {
                cmp_cap = chdr.compressed_sz;
                uint8_t *new_cmp = (uint8_t*)realloc(cmp_buf, cmp_cap);
                if (!new_cmp) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_OUT_OF_MEMORY;
                }
                cmp_buf = new_cmp;
            }

            /* Read compressed payload */
            if (reader(user_data, cmp_buf, chdr.compressed_sz) != chdr.compressed_sz) {
                ZSTD_freeDCtx(dctx);
                free(raw_buf);
                free(cmp_buf);
                free(prev_raw);
                return BINPACK_ERR_CORRUPT_DATA;
            }
            total_in += chdr.compressed_sz;

            /* Decompress payload */
            if (chdr.filter_flags & BP_CHUNK_FLAG_LZMA) {
                /* Backend: LZMA2 Range Coder */
                if (chdr.compressed_sz < 5) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_CORRUPT_DATA;
                }
                unsigned char props[5];
                memcpy(props, cmp_buf, 5);
                size_t lzma_in_sz = chdr.compressed_sz - 5;
                size_t lzma_out_sz = chdr.uncompressed_sz;
                int lzma_res = LzmaUncompress(raw_buf, &lzma_out_sz, cmp_buf + 5, &lzma_in_sz, props, 5);
                if (lzma_res != SZ_OK || lzma_out_sz != chdr.uncompressed_sz) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_DECOMPRESSION_FAILED;
                }
            } else if (chdr.filter_flags & BP_CHUNK_FLAG_PREFIX) {
                /* Backend: Zstandard with Rolling Prefix Dictionary */
                if (!prev_raw || prev_len == 0) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_CORRUPT_DATA;
                }
                ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
                size_t prefix_sz = prev_len > (16 * 1024 * 1024) ? (16 * 1024 * 1024) : prev_len;
                const uint8_t *prefix_ptr = prev_raw + (prev_len - prefix_sz);
                ZSTD_DCtx_refPrefix(dctx, prefix_ptr, prefix_sz);

                ZSTD_inBuffer din = { cmp_buf, chdr.compressed_sz, 0 };
                ZSTD_outBuffer dout = { raw_buf, chdr.uncompressed_sz, 0 };
                while (din.pos < din.size) {
                    size_t zret = ZSTD_decompressStream(dctx, &dout, &din);
                    if (ZSTD_isError(zret)) {
                        ZSTD_freeDCtx(dctx);
                        free(raw_buf);
                        free(cmp_buf);
                        free(prev_raw);
                        return BINPACK_ERR_DECOMPRESSION_FAILED;
                    }
                }
                if (dout.pos != chdr.uncompressed_sz) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_DECOMPRESSION_FAILED;
                }
            } else {
                /* Backend: Zstandard Standard Block */
                ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
                size_t d_sz = ZSTD_decompressDCtx(dctx, raw_buf, chdr.uncompressed_sz, cmp_buf, chdr.compressed_sz);
                if (ZSTD_isError(d_sz) || d_sz != chdr.uncompressed_sz) {
                    ZSTD_freeDCtx(dctx);
                    free(raw_buf);
                    free(cmp_buf);
                    free(prev_raw);
                    return BINPACK_ERR_DECOMPRESSION_FAILED;
                }
            }

            /* Preserve filtered state in prev_raw for subsequent chunk dictionary references */
            if (prev_cap < chdr.uncompressed_sz) {
                prev_cap = chdr.uncompressed_sz;
                uint8_t *new_prev = (uint8_t*)realloc(prev_raw, prev_cap);
                if (new_prev) {
                    prev_raw = new_prev;
                }
            }
            if (prev_raw && prev_cap >= chdr.uncompressed_sz) {
                memcpy(prev_raw, raw_buf, chdr.uncompressed_sz);
                prev_len = chdr.uncompressed_sz;
            }

            /* Inverse Prefilter */
            uint8_t filter_id = chdr.filter_id;
            if (filter_id == BINPACK_FILTER_X86_64 || filter_id == BINPACK_FILTER_ARM32 || filter_id == BINPACK_FILTER_AARCH64) {
                uint8_t *target = raw_buf + bounds.code_offset;
                size_t tlen = bounds.code_length;
                if (bounds.code_offset + tlen <= chdr.uncompressed_sz) {
                    if (filter_id == BINPACK_FILTER_X86_64) {
                        binpack_bcj_x86_unfilter(target, tlen);
                    } else if (filter_id == BINPACK_FILTER_ARM32) {
                        binpack_bcj_arm32_unfilter(target, tlen);
                    } else if (filter_id == BINPACK_FILTER_AARCH64) {
                        binpack_bcj_aarch64_unfilter(target, tlen);
                    }
                }
            } else if (filter_id == BINPACK_FILTER_TRANSPOSE_DELTA) {
                binpack_transpose_delta_unfilter(raw_buf, chdr.uncompressed_sz, chdr.filter_param);
            } else if (filter_id == BINPACK_FILTER_BITSHUFFLE) {
                binpack_bitshuffle_unfilter(raw_buf, chdr.uncompressed_sz, chdr.filter_param);
            } else if (filter_id == BINPACK_FILTER_TEXTURE_BCN) {
                binpack_texture_bcn_unfilter(raw_buf, chdr.uncompressed_sz, chdr.filter_param);
            }

            /* Verify Chunk CRC32 */
            uint32_t chunk_crc = binpack_crc32(0, raw_buf, chdr.uncompressed_sz);
            if (chunk_crc != chdr.chunk_crc32) {
                ZSTD_freeDCtx(dctx);
                free(raw_buf);
                free(cmp_buf);
                free(prev_raw);
                return BINPACK_ERR_CHECKSUM_MISMATCH;
            }

            stream_crc = binpack_crc32(stream_crc, raw_buf, chdr.uncompressed_sz);

            /* Write uncompressed chunk to output sink */
            if (writer(user_data, raw_buf, chdr.uncompressed_sz) != chdr.uncompressed_sz) {
                ZSTD_freeDCtx(dctx);
                free(raw_buf);
                free(cmp_buf);
                free(prev_raw);
                return BINPACK_ERR_IO;
            }
            total_out += chdr.uncompressed_sz;

            if (progress) {
                progress(user_data, total_in, 0, total_out);
            }
        }

        ZSTD_freeDCtx(dctx);
        free(raw_buf);
        free(cmp_buf);
        free(prev_raw);
        return BINPACK_OK;
    }

    /* --------------------------------------------------------------------- */
    /* Case B: V1 Single-Block Format ('BINP' Legacy Compatibility)          */
    /* --------------------------------------------------------------------- */
    if (magic == BINPACK_MAGIC_V1) {
        uint8_t rest_v1[28];
        if (reader(user_data, rest_v1, 28) != 28) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
        uint32_t orig_sz  = *(uint32_t*)&rest_v1[0];
        uint32_t comp_sz  = *(uint32_t*)&rest_v1[4];
        uint32_t orig_crc = *(uint32_t*)&rest_v1[8];
        uint8_t filter_id = rest_v1[12];
        uint16_t stride   = *(uint16_t*)&rest_v1[14];
        uint32_t code_off = *(uint32_t*)&rest_v1[20];
        uint32_t code_len = *(uint32_t*)&rest_v1[24];

        uint8_t *cbuf = (uint8_t*)malloc(comp_sz);
        uint8_t *dbuf = (uint8_t*)malloc(orig_sz);
        if (!cbuf || !dbuf) {
            free(cbuf);
            free(dbuf);
            return BINPACK_ERR_OUT_OF_MEMORY;
        }

        if (reader(user_data, cbuf, comp_sz) != comp_sz) {
            free(cbuf);
            free(dbuf);
            return BINPACK_ERR_CORRUPT_DATA;
        }

        size_t d_sz = ZSTD_decompress(dbuf, orig_sz, cbuf, comp_sz);
        free(cbuf);
        if (ZSTD_isError(d_sz) || d_sz != orig_sz) {
            free(dbuf);
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }

        if (filter_id == 2 || filter_id == 3 || filter_id == 4) {
            if (code_len == 0 || code_off + code_len > orig_sz) {
                code_off = 0;
                code_len = orig_sz;
            }
            uint8_t *tgt = dbuf + code_off;
            if (filter_id == 2) binpack_bcj_x86_unfilter(tgt, code_len);
            else if (filter_id == 3) binpack_bcj_arm32_unfilter(tgt, code_len);
            else if (filter_id == 4) binpack_bcj_aarch64_unfilter(tgt, code_len);
        } else if (filter_id == 5) {
            binpack_transpose_delta_unfilter(dbuf, orig_sz, stride > 0 ? stride : 4);
        }

        uint32_t chk = binpack_crc32(0, dbuf, orig_sz);
        if (chk != orig_crc) {
            free(dbuf);
            return BINPACK_ERR_CHECKSUM_MISMATCH;
        }

        if (writer(user_data, dbuf, orig_sz) != orig_sz) {
            free(dbuf);
            return BINPACK_ERR_IO;
        }
        free(dbuf);
        return BINPACK_OK;
    }

    return BINPACK_ERR_CORRUPT_DATA;
}
