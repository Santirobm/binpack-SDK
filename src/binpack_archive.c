/**
 * @file binpack_archive.c
 * @brief BinPack Enterprise SDK - O(1) Seekable Random-Access Archive Engine (Virtual Texturing)
 */

#include "binpack_internal.h"

BINPACK_API binpack_archive_t* BINPACK_CALL binpack_archive_open(const char *filepath) {
    if (!filepath) return NULL;

    FILE *fp = fopen(filepath, "rb");
    if (!fp) return NULL;

    /* C9: Determinación obligatoria y estricta del tamaño físico del archivo */
    if (binpack_fseek64(fp, 0, SEEK_END) != 0) {
        fclose(fp);
        return NULL;
    }
    int64_t file_sz = binpack_ftell64(fp);
    if (file_sz < 0) {
        fclose(fp);
        return NULL;
    }

    /* C7 & C8: Rechazar archivos vacíos o menores que la cabecera V2 mínima */
    if (file_sz < (int64_t)sizeof(binpack_v2_header_t)) {
        fclose(fp);
        return NULL;
    }

    /* Reubicar al inicio para leer cabecera V2 */
    if (binpack_fseek64(fp, 0, SEEK_SET) != 0) {
        fclose(fp);
        return NULL;
    }

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
    if (!arch->dctx) {
        binpack_archive_close(arch);
        return NULL;
    }
    strncpy(arch->filepath, filepath, sizeof(arch->filepath) - 1);
    arch->filepath[sizeof(arch->filepath) - 1] = '\0';

    /* 1. Try O(1) Footer + Seek Table Index lookup at EOF */
    if (file_sz >= (int64_t)(sizeof(binpack_v2_header_t) + sizeof(binpack_v2_footer_t))) {
        int64_t footer_pos = file_sz - (int64_t)sizeof(binpack_v2_footer_t);
        if (binpack_fseek64(fp, footer_pos, SEEK_SET) == 0) {
            binpack_v2_footer_t footer;
            if (fread(&footer, 1, sizeof(footer), fp) == sizeof(footer) &&
                footer.footer_magic == BINPACK_MAGIC_FOOTER) {
                arch->footer = footer;

                /* Multi-chunk O(1) Seek Table Index right before footer */
                if (footer.chunk_count > 1 && footer.chunk_count <= 1000000 &&
                    (size_t)footer.chunk_count <= SIZE_MAX / sizeof(binpack_chunk_info_t)) {
                    int64_t table_bytes = (int64_t)footer.chunk_count * (int64_t)sizeof(binpack_chunk_info_t);
                    int64_t idx_pos = footer_pos - (int64_t)sizeof(binpack_index_header_t) - table_bytes;
                    if (idx_pos >= (int64_t)sizeof(binpack_v2_header_t) &&
                        binpack_fseek64(fp, idx_pos, SEEK_SET) == 0) {
                        binpack_index_header_t idx_hdr;
                        if (fread(&idx_hdr, 1, sizeof(idx_hdr), fp) == sizeof(idx_hdr) &&
                            idx_hdr.index_magic == BINPACK_MAGIC_INDEX &&
                            idx_hdr.entry_count == footer.chunk_count) {
                            arch->chunks = (binpack_chunk_info_t*)malloc((size_t) table_bytes);
                            if (arch->chunks && fread(arch->chunks, 1, (size_t)table_bytes, fp) == (size_t)table_bytes) {
                                /* C12-C17: Validación integral del Seek Table Index y continuidad estructural */
                                int valid_table = 1;
                                int64_t prev_payload_end = (int64_t)sizeof(binpack_v2_header_t);
                                uint64_t computed_total_uncompressed = 0;

                                for (uint32_t i = 0; i < footer.chunk_count; i++) {
                                    const binpack_chunk_info_t *c = &arch->chunks[i];

                                    /* Validar índice consecutivo desde cero */
                                    if (c->chunk_index != i) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Límites de tamaño de chunk */
                                    if (c->uncompressed_sz == 0 || c->uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
                                        c->compressed_sz == 0 || c->compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Acumulación y protección contra overflow de total descomprimido */
                                    if (binpack_safe_add_u64(&computed_total_uncompressed, c->uncompressed_sz) != BINPACK_OK) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Límites de filtrado sustractivos */
                                    if (c->code_offset > c->uncompressed_sz ||
                                        c->code_length > c->uncompressed_sz - c->code_offset) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Cabecera CHNK mínima requerida antes del payload */
                                    size_t min_hdr_sz = sizeof(binpack_chunk_header_t);
                                    if (c->filter_flags & BP_CHUNK_FLAG_BOUNDS) {
                                        min_hdr_sz += sizeof(binpack_chunk_bounds_t);
                                    }

                                    /* Continuidad estructural estricta: cada cabecera debe comenzar exactamente donde terminó el payload anterior */
                                    if (c->file_offset < min_hdr_sz) {
                                        valid_table = 0;
                                        break;
                                    }
                                    uint64_t chunk_hdr_offset = c->file_offset - min_hdr_sz;
                                    if (chunk_hdr_offset != (uint64_t)prev_payload_end) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Payload íntegramente dentro del archivo y antes del índice */
                                    if (c->file_offset > (uint64_t)file_sz ||
                                        (uint64_t)c->compressed_sz > (uint64_t)file_sz - c->file_offset ||
                                        (int64_t)c->file_offset + (int64_t)c->compressed_sz > idx_pos) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Verificación cruzada exhaustiva con cabecera CHNK y bounds en disco */
                                    if (binpack_fseek64(fp, (int64_t)chunk_hdr_offset, SEEK_SET) != 0) {
                                        valid_table = 0;
                                        break;
                                    }
                                    binpack_chunk_header_t on_disk_chdr;
                                    if (fread(&on_disk_chdr, 1, sizeof(on_disk_chdr), fp) != sizeof(on_disk_chdr)) {
                                        valid_table = 0;
                                        break;
                                    }
                                    if (on_disk_chdr.chunk_magic != BINPACK_MAGIC_CHUNK ||
                                        on_disk_chdr.uncompressed_sz != c->uncompressed_sz ||
                                        on_disk_chdr.compressed_sz != c->compressed_sz ||
                                        on_disk_chdr.chunk_crc32 != c->chunk_crc32 ||
                                        on_disk_chdr.filter_id != c->filter_id ||
                                        on_disk_chdr.filter_flags != c->filter_flags ||
                                        on_disk_chdr.filter_param != c->filter_param) {
                                        valid_table = 0;
                                        break;
                                    }

                                    /* Si el chunk tiene bounds físicos, verificar coincidencia exacta */
                                    if (c->filter_flags & BP_CHUNK_FLAG_BOUNDS) {
                                        binpack_chunk_bounds_t on_disk_bounds;
                                        if (fread(&on_disk_bounds, 1, sizeof(on_disk_bounds), fp) != sizeof(on_disk_bounds)) {
                                            valid_table = 0;
                                            break;
                                        }
                                        if (on_disk_bounds.code_offset != c->code_offset ||
                                            on_disk_bounds.code_length != c->code_length) {
                                            valid_table = 0;
                                            break;
                                        }
                                    }

                                    prev_payload_end = (int64_t)c->file_offset + (int64_t)c->compressed_sz;
                                }

                                /* Comprobar que no haya gaps entre el último chunk y el índice, y que total_uncompressed coincida */
                                if (valid_table && prev_payload_end == idx_pos && computed_total_uncompressed == footer.total_uncompressed) {
                                    arch->chunk_count = footer.chunk_count;
                                    return arch;
                                }
                            }
                            if (arch->chunks) {
                                free(arch->chunks);
                                arch->chunks = NULL;
                            }
                        }
                    }
                }
            }
        }
    }

    /* 2. Fallback: 64-bit sequential chunk header scan (fail-closed ante cualquier truncamiento) */
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
    int saw_index = 0;
    int saw_footer = 0;
    uint64_t computed_total_uncompressed = 0;

    while (1) {
        int64_t cur_pos = binpack_ftell64(fp);
        if (cur_pos < 0) {
            binpack_archive_close(arch);
            return NULL;
        }

        /* Si llegamos exactamente al final del archivo */
        if (cur_pos == file_sz) {
            break;
        }

        /* Si quedan menos de 4 bytes, hay datos truncados */
        if (cur_pos > file_sz - 4) {
            binpack_archive_close(arch);
            return NULL;
        }

        uint32_t tag = 0;
        if (fread(&tag, 1, 4, fp) != 4) {
            binpack_archive_close(arch);
            return NULL;
        }

        if (tag == BINPACK_MAGIC_INDEX) {
            /* Regla 1: Como máximo un único bloque INDEX en todo el contenedor */
            /* Regla 2: Debe aparecer estrictamente después de haber escaneado todos los chunks */
            if (saw_index || chunk_idx == 0) {
                binpack_archive_close(arch);
                return NULL;
            }
            uint32_t entry_count = 0;
            if (fread(&entry_count, 1, 4, fp) != 4) {
                binpack_archive_close(arch);
                return NULL;
            }
            /* Regla 3: entry_count == chunk_idx */
            if (entry_count != chunk_idx || entry_count > 1000000 || (size_t)entry_count > SIZE_MAX / sizeof(binpack_chunk_info_t)) {
                binpack_archive_close(arch);
                return NULL;
            }
            int64_t skip = (int64_t)entry_count * (int64_t)sizeof(binpack_chunk_info_t);
            int64_t after_tag_pos = binpack_ftell64(fp);
            if (after_tag_pos < 0 || skip > file_sz - after_tag_pos) {
                binpack_archive_close(arch);
                return NULL;
            }
            if (binpack_fseek64(fp, skip, SEEK_CUR) != 0) {
                binpack_archive_close(arch);
                return NULL;
            }
            saw_index = 1;
            continue;
        }

        if (tag == BINPACK_MAGIC_FOOTER) {
            if (binpack_fseek64(fp, cur_pos, SEEK_SET) != 0) {
                binpack_archive_close(arch);
                return NULL;
            }
            if (fread(&arch->footer, 1, sizeof(binpack_v2_footer_t), fp) != sizeof(binpack_v2_footer_t)) {
                binpack_archive_close(arch);
                return NULL;
            }
            int64_t footer_end = binpack_ftell64(fp);
            if (footer_end < 0 || footer_end != file_sz) {
                binpack_archive_close(arch);
                return NULL;
            }
            saw_footer = 1;
            break;
        }

        /* Regla 4: Tras saltar el bloque INDEX, el siguiente bloque debe ser obligatoriamente FOOTER;
         * cualquier chunk u otro tag posterior es una violación estructural */
        if (saw_index || tag != BINPACK_MAGIC_CHUNK) {
            /* Corrupción estructural o datos truncados: rechazo total */
            binpack_archive_close(arch);
            return NULL;
        }

        if (binpack_fseek64(fp, cur_pos, SEEK_SET) != 0) {
            binpack_archive_close(arch);
            return NULL;
        }
        binpack_chunk_header_t chdr;
        if (fread(&chdr, 1, sizeof(chdr), fp) != sizeof(chdr)) {
            binpack_archive_close(arch);
            return NULL;
        }

        /* Anti-corruption / zip-bomb limit check */
        if (chdr.uncompressed_sz == 0 || chdr.uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
            chdr.compressed_sz == 0 || chdr.compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
            binpack_archive_close(arch);
            return NULL;
        }

        /* Acumular total descomprimido con protección contra overflow */
        if (binpack_safe_add_u64(&computed_total_uncompressed, chdr.uncompressed_sz) != BINPACK_OK) {
            binpack_archive_close(arch);
            return NULL;
        }

        binpack_chunk_bounds_t bounds;
        bounds.code_offset = 0;
        bounds.code_length = chdr.uncompressed_sz;
        if (chdr.filter_flags & BP_CHUNK_FLAG_BOUNDS) {
            if (fread(&bounds, 1, sizeof(bounds), fp) != sizeof(bounds)) {
                binpack_archive_close(arch);
                return NULL;
            }
        }

        /* V-07: Comprobación sustractiva de límites */
        if (bounds.code_offset > chdr.uncompressed_sz ||
            bounds.code_length > chdr.uncompressed_sz - bounds.code_offset) {
            binpack_archive_close(arch);
            return NULL;
        }

        int64_t payload_offset = binpack_ftell64(fp);
        if (payload_offset < 0) {
            binpack_archive_close(arch);
            return NULL;
        }

        /* V-10, C10, C11: Verificación de payload completo dentro del archivo (fail-closed) */
        if ((uint64_t)payload_offset > (uint64_t)file_sz ||
            (uint64_t)chdr.compressed_sz > (uint64_t)file_sz - (uint64_t)payload_offset) {
            binpack_archive_close(arch);
            return NULL;
        }

        /* V-04: Crecimiento seguro de capacidad con protección contra overflow */
        if (chunk_idx >= capacity) {
            if (capacity > SIZE_MAX / (2 * sizeof(binpack_chunk_info_t))) {
                binpack_archive_close(arch);
                return NULL;
            }
            size_t new_cap = capacity * 2;
            binpack_chunk_info_t *new_chunks = (binpack_chunk_info_t*)realloc(arch->chunks, new_cap * sizeof(binpack_chunk_info_t));
            if (!new_chunks) {
                binpack_archive_close(arch);
                return NULL;
            }
            arch->chunks = new_chunks;
            capacity = new_cap;
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

        /* V-04: Protección contra overflow del contador de chunks */
        if (chunk_idx == UINT32_MAX) {
            binpack_archive_close(arch);
            return NULL;
        }
        chunk_idx++;

        /* Skip compressed payload to next chunk using 64-bit seek */
        if (binpack_fseek64(fp, payload_offset + (int64_t)chdr.compressed_sz, SEEK_SET) != 0) {
            binpack_archive_close(arch);
            return NULL;
        }
    }

    /* Rechazo de archivos sin chunks válidos, sin footer o con discrepancias */
    if (chunk_idx == 0 || !saw_footer) {
        binpack_archive_close(arch);
        return NULL;
    }
    if (arch->footer.chunk_count != chunk_idx) {
        binpack_archive_close(arch);
        return NULL;
    }
    if (arch->footer.total_uncompressed != computed_total_uncompressed) {
        binpack_archive_close(arch);
        return NULL;
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

#define BINPACK_ARCHIVE_MAX_PREFIX_CHAIN 8
#define BINPACK_ARCHIVE_MAX_PREFIX_MEM_BUDGET (128ULL * 1024ULL * 1024ULL)

#ifdef BINPACK_TESTING
/* Internal test instrumentation and fault injection hooks (BP-006A) */
typedef void* (*bp_archive_alloc_fn)(size_t sz);
static bp_archive_alloc_fn s_test_alloc_hook = NULL;
static uint32_t s_test_deps_inspected = 0;
static uint32_t s_test_payload_reads = 0;
static uint32_t s_test_prefix_buf_alloc_count = 0;
static uint32_t s_test_cmp_buf_alloc_count = 0;

BINPACK_API void BINPACK_CALL binpack_test_archive_set_alloc_hook(bp_archive_alloc_fn fn) {
    s_test_alloc_hook = fn;
}

BINPACK_API void BINPACK_CALL binpack_test_archive_reset_stats(void) {
    s_test_deps_inspected = 0;
    s_test_payload_reads = 0;
    s_test_prefix_buf_alloc_count = 0;
    s_test_cmp_buf_alloc_count = 0;
}

BINPACK_API void BINPACK_CALL binpack_test_archive_get_stats(
    uint32_t *deps_inspected,
    uint32_t *payload_reads,
    uint32_t *prefix_buf_alloc_count,
    uint32_t *total_alloc_count
) {
    if (deps_inspected) *deps_inspected = s_test_deps_inspected;
    if (payload_reads) *payload_reads = s_test_payload_reads;
    if (prefix_buf_alloc_count) *prefix_buf_alloc_count = s_test_prefix_buf_alloc_count;
    if (total_alloc_count) *total_alloc_count = (s_test_prefix_buf_alloc_count + s_test_cmp_buf_alloc_count);
}

static void* bp_archive_alloc(size_t sz) {
    s_test_prefix_buf_alloc_count++;
    if (s_test_alloc_hook) {
        return s_test_alloc_hook(sz);
    }
    return malloc(sz);
}

static void* bp_archive_alloc_cmp(size_t sz) {
    s_test_cmp_buf_alloc_count++;
    return malloc(sz);
}
#else
/* Production build: zero global testing state, zero overhead, zero ABI exposure */
#define bp_archive_alloc(sz) malloc(sz)
#define bp_archive_alloc_cmp(sz) malloc(sz)
#endif

/* Internal helper: decompresses a single chunk's raw bytes (optionally using a prefix) */
static binpack_status_t bp_archive_decompress_single_chunk_raw(
    const binpack_archive_t *archive,
    FILE *fp,
    ZSTD_DCtx *dctx,
    uint32_t chunk_idx,
    const uint8_t *prefix_buf,
    size_t prefix_sz,
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

    /* Validate algorithm and prefix flag consistency BEFORE allocating memory or reading payload */
    if (c->filter_flags & BP_CHUNK_FLAG_LZMA) {
        if ((c->filter_flags & BP_CHUNK_FLAG_PREFIX) || prefix_buf != NULL) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
        if (c->compressed_sz < 5) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else if (c->filter_flags & BP_CHUNK_FLAG_PREFIX) {
        /* Rolling prefix Zstd chunk: strictly requires non-NULL prefix_buf and prefix_sz > 0 */
        if (!prefix_buf || prefix_sz == 0) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    } else {
        /* Standalone Zstd chunk: strictly requires prefix_buf == NULL */
        if (prefix_buf != NULL) {
            return BINPACK_ERR_CORRUPT_DATA;
        }
    }

    uint8_t *cmp_buf = (uint8_t*)bp_archive_alloc_cmp(c->compressed_sz);
    if (!cmp_buf) return BINPACK_ERR_OUT_OF_MEMORY;

    if (binpack_fseek64(fp, (int64_t)c->file_offset, SEEK_SET) != 0) {
        free(cmp_buf);
        return BINPACK_ERR_IO;
    }

#ifdef BINPACK_TESTING
    s_test_payload_reads++;
#endif
    if (fread(cmp_buf, 1, c->compressed_sz, fp) != c->compressed_sz) {
        free(cmp_buf);
        return BINPACK_ERR_IO;
    }

    if (c->filter_flags & BP_CHUNK_FLAG_LZMA) {
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
        size_t zreset = ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
        if (ZSTD_isError(zreset)) {
            free(cmp_buf);
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
        size_t actual_prefix_sz = prefix_sz > (16 * 1024 * 1024) ? (16 * 1024 * 1024) : prefix_sz;
        const uint8_t *prefix_ptr = prefix_buf + (prefix_sz - actual_prefix_sz);
        size_t zpref = ZSTD_DCtx_refPrefix(dctx, prefix_ptr, actual_prefix_sz);
        if (ZSTD_isError(zpref)) {
            free(cmp_buf);
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }

        ZSTD_inBuffer din = { cmp_buf, c->compressed_sz, 0 };
        ZSTD_outBuffer dout = { dst, c->uncompressed_sz, 0 };
        size_t zret = 0;
        while (din.pos < din.size) {
            size_t prev_in = din.pos;
            size_t prev_out = dout.pos;
            zret = ZSTD_decompressStream(dctx, &dout, &din);
            if (ZSTD_isError(zret)) {
                free(cmp_buf);
                return BINPACK_ERR_DECOMPRESSION_FAILED;
            }
            if (din.pos == prev_in && dout.pos == prev_out) {
                /* Anti-infinite loop: no progress made on corrupt frame */
                free(cmp_buf);
                return BINPACK_ERR_CORRUPT_DATA;
            }
            if (zret == 0 && din.pos < din.size) {
                /* Single frame per chunk contract: frame finished before consuming all chunk payload */
                free(cmp_buf);
                return BINPACK_ERR_CORRUPT_DATA;
            }
        }
        free(cmp_buf);
        if (zret != 0 || dout.pos != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    } else {
        size_t frame_c_sz = ZSTD_findFrameCompressedSize(cmp_buf, c->compressed_sz);
        if (ZSTD_isError(frame_c_sz) || frame_c_sz != c->compressed_sz) {
            /* Trailing bytes, concatenated frames or invalid frame size within chunk payload */
            free(cmp_buf);
            return BINPACK_ERR_CORRUPT_DATA;
        }
        size_t zreset = ZSTD_DCtx_reset(dctx, ZSTD_reset_session_only);
        if (ZSTD_isError(zreset)) {
            free(cmp_buf);
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
        size_t d_sz = ZSTD_decompressDCtx(dctx, dst, c->uncompressed_sz, cmp_buf, c->compressed_sz);
        free(cmp_buf);
        if (ZSTD_isError(d_sz) || d_sz != c->uncompressed_sz) {
            return BINPACK_ERR_DECOMPRESSION_FAILED;
        }
    }

    return BINPACK_OK;
}

/* Internal helper: decompresses a chunk's raw bytes using bounded iterative prefix resolution (zero recursion) */
static binpack_status_t bp_archive_decompress_filtered_raw(
    const binpack_archive_t *archive,
    FILE *fp,
    ZSTD_DCtx *dctx,
    uint32_t chunk_idx,
    uint8_t *dst,
    size_t dst_capacity
) {
    const binpack_chunk_info_t *target_c = &archive->chunks[chunk_idx];
    if (dst_capacity < target_c->uncompressed_sz) {
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }

    /* Base case: chunk does not require rolling prefix */
    if (!(target_c->filter_flags & BP_CHUNK_FLAG_PREFIX)) {
        return bp_archive_decompress_single_chunk_raw(
            archive, fp, dctx, chunk_idx, NULL, 0, dst, dst_capacity
        );
    }

    /* Target chunk requires prefix: chunk 0 can NEVER have prefix */
    if (chunk_idx == 0) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    /* ===================================================================== */
    /* FASE 1: Validación Previa de la Cadena (O(M) tiempo, O(1) memoria)    */
    /* ===================================================================== */
    uint32_t chain_depth = 0;
    uint32_t cur = chunk_idx;
    uint64_t cumulative_work = target_c->uncompressed_sz;

    if (target_c->uncompressed_sz == 0 ||
        target_c->uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
        target_c->compressed_sz == 0 ||
        target_c->compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    while (cur > 0 && (archive->chunks[cur].filter_flags & BP_CHUNK_FLAG_PREFIX)) {
#ifdef BINPACK_TESTING
        s_test_deps_inspected++;
#endif
        chain_depth++;
        if (chain_depth > BINPACK_ARCHIVE_MAX_PREFIX_CHAIN) {
            /* Early rejection! Exceeds maximum 8 dependencies. ZERO allocations made! */
            return BINPACK_ERR_CORRUPT_DATA;
        }

        uint32_t prev_idx = cur - 1;
        const binpack_chunk_info_t *prev_c = &archive->chunks[prev_idx];

        /* Validate individual chunk bounds */
        if (prev_c->uncompressed_sz == 0 ||
            prev_c->uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
            prev_c->compressed_sz == 0 ||
            prev_c->compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
            return BINPACK_ERR_CORRUPT_DATA;
        }

        /* Check cumulative historical work */
        if (binpack_safe_add_u64(&cumulative_work, prev_c->uncompressed_sz) != BINPACK_OK ||
            cumulative_work > BINPACK_ARCHIVE_MAX_PREFIX_MEM_BUDGET) {
            return BINPACK_ERR_CORRUPT_DATA;
        }

        /* Check peak transitory memory between consecutive buffers (cur and prev) */
        uint64_t peak_pair = 0;
        if (binpack_safe_add_u64(&peak_pair, archive->chunks[cur].uncompressed_sz) != BINPACK_OK ||
            binpack_safe_add_u64(&peak_pair, prev_c->uncompressed_sz) != BINPACK_OK ||
            peak_pair > BINPACK_ARCHIVE_MAX_PREFIX_MEM_BUDGET) {
            return BINPACK_ERR_CORRUPT_DATA;
        }

        cur = prev_idx;
    }

    /* If we reached chunk 0 and it still has BP_CHUNK_FLAG_PREFIX, invalid archive */
    if (archive->chunks[cur].filter_flags & BP_CHUNK_FLAG_PREFIX) {
        return BINPACK_ERR_CORRUPT_DATA;
    }

    uint32_t root_idx = cur;

    /* ===================================================================== */
    /* FASE 2: Pipeline Deslizante Iterativo (Máximo 2 buffers en RAM)        */
    /* ===================================================================== */
    uint8_t *prefix_buf = NULL;
    size_t prefix_sz = 0;
    binpack_status_t status = BINPACK_OK;

    /* 2a. Descomprimir root chunk en prefix_buf (es independiente, no usa prefijo) */
    prefix_sz = archive->chunks[root_idx].uncompressed_sz;
    prefix_buf = (uint8_t*)bp_archive_alloc(prefix_sz);
    if (!prefix_buf) {
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    status = bp_archive_decompress_single_chunk_raw(
        archive, fp, dctx, root_idx, NULL, 0, prefix_buf, prefix_sz
    );
    if (status != BINPACK_OK) {
        free(prefix_buf);
        return status;
    }

    /* 2b. Descomprimir chunks intermedios: root_idx + 1 hasta chunk_idx - 1 */
    for (uint32_t i = root_idx + 1; i < chunk_idx; i++) {
        size_t next_sz = archive->chunks[i].uncompressed_sz;
        uint8_t *next_buf = (uint8_t*)bp_archive_alloc(next_sz);
        if (!next_buf) {
            free(prefix_buf);
            return BINPACK_ERR_OUT_OF_MEMORY;
        }

        /* prefix_buf permanece inmutable y válido durante toda la descompresión de chunk i */
        status = bp_archive_decompress_single_chunk_raw(
            archive, fp, dctx, i, prefix_buf, prefix_sz, next_buf, next_sz
        );
        if (status != BINPACK_OK) {
            free(prefix_buf);
            free(next_buf);
            return status;
        }

        /* Avanzar ventana deslizante: liberar buffer viejo */
        free(prefix_buf);
        prefix_buf = next_buf;
        prefix_sz = next_sz;
    }

    /* 2c. Descomprimir chunk final (chunk_idx) usando prefix_buf directamente en dst */
    status = bp_archive_decompress_single_chunk_raw(
        archive, fp, dctx, chunk_idx, prefix_buf, prefix_sz, dst, dst_capacity
    );

    /* Liberar último buffer de prefijo */
    free(prefix_buf);
    prefix_buf = NULL;

    return status;
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
        if (code_off <= c->uncompressed_sz && code_len <= c->uncompressed_sz - code_off) {
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
