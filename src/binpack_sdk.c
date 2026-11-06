/**
 * @file binpack_sdk.c
 * @brief BinPack Enterprise SDK - Public C ABI Implementation & Entry Points
 */

#include "binpack_internal.h"

#if defined(_WIN32)
    #ifndef WIN32_LEAN_AND_MEAN
        #define WIN32_LEAN_AND_MEAN
    #endif
    #ifndef NOMINMAX
        #define NOMINMAX
    #endif
    #include <windows.h>
#else
    #include <pthread.h>
#endif

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
    if (size == 0) return 0;
    mem_stream_ctx_t *ctx = (mem_stream_ctx_t*)user_data;
    if (!ctx) return 0;

    /* V-01: Proteger suma de tamaño contra desbordamiento de size_t */
    if (size > SIZE_MAX - ctx->dst_size) {
        return 0;
    }
    size_t required = ctx->dst_size + size;

    if (required > ctx->dst_cap) {
        size_t new_cap = ctx->dst_cap ? ctx->dst_cap : 65536;
        /* V-02: Proteger duplicación contra wrap-around y bucle infinito */
        while (new_cap < required) {
            if (new_cap > SIZE_MAX / 2) {
                return 0;
            }
            new_cap *= 2;
        }
        if (new_cap > SIZE_MAX / sizeof(*ctx->dst)) {
            return 0;
        }
        uint8_t *new_data = (uint8_t*)realloc(ctx->dst, new_cap);
        if (!new_data) {
            return 0; /* Preserva ctx->dst original si realloc falla */
        }
        ctx->dst = new_data;
        ctx->dst_cap = new_cap;
    }
    memcpy(ctx->dst + ctx->dst_size, buffer, size);
    ctx->dst_size += size;
    return size;
}

/* ========================================================================= */
/* Thread-Local Context Lifecycle & Multiplatform Auto-Cleanup (SDK Context)  */
/* ========================================================================= */

typedef struct bp_sdk_tls_s {
    ZSTD_CCtx *fast_cctx;
    uint8_t   *sdk_scratch;
    size_t     sdk_scratch_cap;
} bp_sdk_tls_t;

#if defined(BINPACK_TESTING)
#include <stdatomic.h>
atomic_size_t g_dbg_sdk_tls_created = 0;
atomic_size_t g_dbg_sdk_tls_destroyed = 0;
atomic_size_t g_dbg_sdk_cctx_created = 0;
atomic_size_t g_dbg_sdk_cctx_reused = 0;
atomic_size_t g_dbg_sdk_cctx_destroyed = 0;
atomic_size_t g_dbg_sdk_scratch_allocs = 0;
atomic_size_t g_dbg_sdk_scratch_frees = 0;
#endif

static void bp_sdk_tls_free_contents(bp_sdk_tls_t *tls) {
    if (!tls) return;
    if (tls->fast_cctx) {
        ZSTD_freeCCtx(tls->fast_cctx);
        tls->fast_cctx = NULL;
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_cctx_destroyed, 1, memory_order_relaxed);
#endif
    }
    if (tls->sdk_scratch) {
        free(tls->sdk_scratch);
        tls->sdk_scratch = NULL;
        tls->sdk_scratch_cap = 0;
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_scratch_frees, 1, memory_order_relaxed);
#endif
    }
}

#if !defined(_WIN32)

static pthread_key_t  s_sdk_tls_key;
static pthread_once_t s_sdk_tls_once = PTHREAD_ONCE_INIT;
static int            s_sdk_tls_key_valid = 0;

static void bp_sdk_posix_destructor(void *arg) {
    bp_sdk_tls_t *tls = (bp_sdk_tls_t*)arg;
    if (tls) {
        bp_sdk_tls_free_contents(tls);
        free(tls);
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_tls_destroyed, 1, memory_order_relaxed);
#endif
    }
}

static void bp_sdk_posix_key_init(void) {
    if (pthread_key_create(&s_sdk_tls_key, bp_sdk_posix_destructor) == 0) {
        s_sdk_tls_key_valid = 1;
    }
}

static bp_sdk_tls_t* bp_get_sdk_tls(void) {
    if (pthread_once(&s_sdk_tls_once, bp_sdk_posix_key_init) != 0 || !s_sdk_tls_key_valid) {
        return NULL;
    }
    bp_sdk_tls_t *tls = (bp_sdk_tls_t*)pthread_getspecific(s_sdk_tls_key);
    if (!tls) {
        tls = (bp_sdk_tls_t*)calloc(1, sizeof(bp_sdk_tls_t));
        if (!tls) return NULL;
        if (pthread_setspecific(s_sdk_tls_key, tls) != 0) {
            free(tls);
            return NULL;
        }
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_tls_created, 1, memory_order_relaxed);
#endif
    }
    return tls;
}

#else /* _WIN32 */

static DWORD s_sdk_fls_idx = FLS_OUT_OF_INDEXES;
static INIT_ONCE s_sdk_fls_once = INIT_ONCE_STATIC_INIT;

static void NTAPI bp_sdk_fls_destructor(PVOID ptr) {
    bp_sdk_tls_t *tls = (bp_sdk_tls_t*)ptr;
    if (tls) {
        bp_sdk_tls_free_contents(tls);
        free(tls);
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_tls_destroyed, 1, memory_order_relaxed);
#endif
    }
}

static BOOL WINAPI bp_sdk_init_fls_once(PINIT_ONCE once, PVOID param, PVOID *context) {
    (void)once; (void)param; (void)context;
    s_sdk_fls_idx = FlsAlloc(bp_sdk_fls_destructor);
    return (s_sdk_fls_idx != FLS_OUT_OF_INDEXES) ? TRUE : FALSE;
}

static bp_sdk_tls_t* bp_get_sdk_tls(void) {
    if (!InitOnceExecuteOnce(&s_sdk_fls_once, bp_sdk_init_fls_once, NULL, NULL)) {
        return NULL;
    }
    if (s_sdk_fls_idx == FLS_OUT_OF_INDEXES) {
        return NULL;
    }
    bp_sdk_tls_t *tls = (bp_sdk_tls_t*)FlsGetValue(s_sdk_fls_idx);
    if (!tls) {
        tls = (bp_sdk_tls_t*)calloc(1, sizeof(bp_sdk_tls_t));
        if (!tls) return NULL;
        if (!FlsSetValue(s_sdk_fls_idx, tls)) {
            free(tls);
            return NULL;
        }
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_tls_created, 1, memory_order_relaxed);
#endif
    }
    return tls;
}

#endif /* _WIN32 */

#if defined(BINPACK_TESTING)
void bp_sdk_cleanup_current_thread(void) {
#if !defined(_WIN32)
    if (!s_sdk_tls_key_valid) return;
    bp_sdk_tls_t *tls = (bp_sdk_tls_t*)pthread_getspecific(s_sdk_tls_key);
    if (tls) {
        if (pthread_setspecific(s_sdk_tls_key, NULL) == 0) {
            bp_sdk_posix_destructor(tls);
        }
    }
#else
    if (s_sdk_fls_idx != FLS_OUT_OF_INDEXES) {
        bp_sdk_tls_t *tls = (bp_sdk_tls_t*)FlsGetValue(s_sdk_fls_idx);
        if (tls) {
            if (FlsSetValue(s_sdk_fls_idx, NULL)) {
                bp_sdk_fls_destructor(tls);
            }
        }
    }
#endif
}
#endif

static uint8_t* bp_acquire_sdk_scratch(size_t size, int *out_must_free) {
    if (size <= (64ULL * 1024 * 1024)) {
        bp_sdk_tls_t *tls = bp_get_sdk_tls();
        if (tls) {
            if (tls->sdk_scratch_cap < size) {
                int had_prev = (tls->sdk_scratch != NULL);
                uint8_t *new_buf = (uint8_t*)realloc(tls->sdk_scratch, size);
                if (!new_buf) return NULL;
                tls->sdk_scratch = new_buf;
                tls->sdk_scratch_cap = size;
#if defined(BINPACK_TESTING)
                if (had_prev) {
                    atomic_fetch_add_explicit(&g_dbg_sdk_scratch_frees, 1, memory_order_relaxed);
                }
                atomic_fetch_add_explicit(&g_dbg_sdk_scratch_allocs, 1, memory_order_relaxed);
#endif
            }
            *out_must_free = 0;
            return tls->sdk_scratch;
        }
    }
    *out_must_free = 1;
    uint8_t *heap_buf = (uint8_t*)malloc(size);
#if defined(BINPACK_TESTING)
    if (heap_buf) {
        atomic_fetch_add_explicit(&g_dbg_sdk_scratch_allocs, 1, memory_order_relaxed);
    }
#endif
    return heap_buf;
}

static inline void bp_release_sdk_scratch(uint8_t *ptr, int must_free) {
    if (must_free && ptr) {
        free(ptr);
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_scratch_frees, 1, memory_order_relaxed);
#endif
    }
}

static binpack_status_t binpack_compress_into_fast(
    const void *src, size_t src_sz,
    void *dst, size_t dst_capacity,
    size_t *compressed_sz,
    const binpack_options_t *opt
) {
    /* Determine Filter */
    binpack_filter_t filter = opt->filter;
    uint16_t filter_param = opt->transpose_stride;
    if (filter == BINPACK_FILTER_AUTO) {
        filter = binpack_auto_select_filter((const uint8_t*)src, src_sz, &filter_param);
    }

    size_t code_off = 0, code_len = src_sz;
    if (filter == BINPACK_FILTER_X86_64 || filter == BINPACK_FILTER_ARM32 ||
        filter == BINPACK_FILTER_AARCH64 || filter == BINPACK_FILTER_RISCV) {
        binpack_find_code_bounds((const uint8_t*)src, src_sz, &code_off, &code_len);
        if (code_len == 0 || code_off >= src_sz || (code_off + code_len) > src_sz) {
            code_off = 0;
            code_len = src_sz;
        }
    }

    /* Micro-Trial zero-regression validation (active in DEFAULT & ARCHIVAL modes) */
    if (opt->enable_micro_trial && opt->mode != BINPACK_MODE_RUNTIME && filter != BINPACK_FILTER_NONE) {
        const uint8_t *eval_ptr = (const uint8_t*)src + code_off;
        size_t eval_len = code_len;
        if (!binpack_evaluate_filter_gain(eval_ptr, eval_len, filter, filter_param)) {
            filter = BINPACK_FILTER_NONE;
            code_off = 0;
            code_len = src_sz;
        }
    }

    uint8_t filter_flags = 0;
    binpack_chunk_bounds_t bounds;
    bounds.code_offset = 0;
    bounds.code_length = (uint32_t)src_sz;

    /* Prepare Data for compression */
    int scratch_must_free = 0;
    uint8_t *scratch = NULL;
    const uint8_t *data_to_compress = (const uint8_t*)src;

    if (filter != BINPACK_FILTER_NONE) {
        scratch = bp_acquire_sdk_scratch(src_sz, &scratch_must_free);
        if (!scratch) return BINPACK_ERR_OUT_OF_MEMORY;
        data_to_compress = scratch;

        if (filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
            if (filter_param < 2) filter_param = 4;
            /* Out-of-place single-pass deinterleave + SIMD delta without memcpy */
            binpack_transpose_delta_filter_into((const uint8_t*)src, scratch, src_sz, filter_param);
        } else {
            memcpy(scratch, src, src_sz);
            if (filter == BINPACK_FILTER_X86_64 || filter == BINPACK_FILTER_ARM32 ||
                filter == BINPACK_FILTER_AARCH64 || filter == BINPACK_FILTER_RISCV) {
                if (code_len > 0 && code_off < src_sz && (code_off + code_len) <= src_sz) {
                    if (code_off > 0 || code_len < src_sz) {
                        filter_flags |= BP_CHUNK_FLAG_BOUNDS;
                        bounds.code_offset = (uint32_t)code_off;
                        bounds.code_length = (uint32_t)code_len;
                    }
                    uint8_t *target = scratch + code_off;
                    if (filter == BINPACK_FILTER_X86_64) binpack_bcj_x86_filter(target, code_len);
                    else if (filter == BINPACK_FILTER_ARM32) binpack_bcj_arm32_filter(target, code_len);
                    else if (filter == BINPACK_FILTER_AARCH64) binpack_bcj_aarch64_filter(target, code_len);
                    else if (filter == BINPACK_FILTER_RISCV) binpack_bcj_riscv_filter(target, code_len);
                }
            } else if (filter == BINPACK_FILTER_BITSHUFFLE) {
                if (filter_param < 1) filter_param = 4;
                binpack_bitshuffle_filter(scratch, src_sz, filter_param);
            } else if (filter == BINPACK_FILTER_TEXTURE_BCN) {
                if (filter_param != 8 && filter_param != 16) filter_param = 8;
                binpack_texture_bcn_filter(scratch, src_sz, filter_param);
            }
        }
    }

    /* Compute destination layout */
    size_t hdr_sz = sizeof(binpack_v2_header_t);
    size_t chdr_sz = sizeof(binpack_chunk_header_t);
    size_t bounds_sz = (filter_flags & BP_CHUNK_FLAG_BOUNDS) ? sizeof(binpack_chunk_bounds_t) : 0;
    size_t payload_offset = hdr_sz + chdr_sz + bounds_sz;
    size_t ftr_sz = sizeof(binpack_v2_footer_t);

    if (dst_capacity < payload_offset + ftr_sz + 64) {
        bp_release_sdk_scratch(scratch, scratch_must_free);
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }

    uint8_t *out = (uint8_t*)dst;

    /* Lazy-init persistent thread-local compression context */
    bp_sdk_tls_t *tls = bp_get_sdk_tls();
    if (!tls) {
        bp_release_sdk_scratch(scratch, scratch_must_free);
        return BINPACK_ERR_OUT_OF_MEMORY;
    }

    if (!tls->fast_cctx) {
        tls->fast_cctx = ZSTD_createCCtx();
        if (!tls->fast_cctx) {
            bp_release_sdk_scratch(scratch, scratch_must_free);
            return BINPACK_ERR_OUT_OF_MEMORY;
        }
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_cctx_created, 1, memory_order_relaxed);
#endif
    } else {
#if defined(BINPACK_TESTING)
        atomic_fetch_add_explicit(&g_dbg_sdk_cctx_reused, 1, memory_order_relaxed);
#endif
    }

    ZSTD_CCtx *fast_cctx = tls->fast_cctx;

    ZSTD_CCtx_reset(fast_cctx, ZSTD_reset_session_only);

    int clevel = opt->clevel > 0 ? opt->clevel : 19;
    if (opt->mode == BINPACK_MODE_RUNTIME) {
        if (clevel > 3) clevel = 3;
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_compressionLevel, clevel);
    } else {
        if (clevel > 19) clevel = 19;
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_compressionLevel, clevel);
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_strategy, ZSTD_btultra);
    }

    /* For large buffers (>= 8 MB) or explicit worker request, activate multi-threading */
    int num_workers = opt->nb_workers;
    if (num_workers == 0 && src_sz >= (8ULL * 1024 * 1024)) {
        num_workers = binpack_get_cpu_cores();
    }
    if (num_workers > 1) {
        if (num_workers > 32) num_workers = 32;
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_nbWorkers, num_workers);
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_overlapLog, 6);
        size_t job_sz = (size_t)(src_sz / num_workers);
        if (job_sz < 524288) job_sz = 524288;
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_jobSize, (int)job_sz);
    } else {
        ZSTD_CCtx_setParameter(fast_cctx, ZSTD_c_nbWorkers, 0);
    }

    size_t avail_dst = dst_capacity - payload_offset - ftr_sz;
    /* Compress DIRECTLY into destination payload slot with ZERO redundant memcpy */
    size_t c_size = 0;
    if (num_workers > 1) {
        ZSTD_inBuffer in = { data_to_compress, src_sz, 0 };
        ZSTD_outBuffer out_buf = { out + payload_offset, avail_dst, 0 };
        size_t rem = ZSTD_compressStream2(fast_cctx, &out_buf, &in, ZSTD_e_end);
        if (ZSTD_isError(rem)) {
            bp_release_sdk_scratch(scratch, scratch_must_free);
            return BINPACK_ERR_COMPRESSION_FAILED;
        }
        c_size = out_buf.pos;
    } else {
        c_size = ZSTD_compressCCtx(
            fast_cctx,
            out + payload_offset, avail_dst,
            data_to_compress, src_sz,
            clevel
        );
    }

    bp_release_sdk_scratch(scratch, scratch_must_free);

    if (ZSTD_isError(c_size)) {
        return BINPACK_ERR_COMPRESSION_FAILED;
    }

    uint32_t chunk_crc = 0;
    uint16_t hdr_flags = 0;
    if (opt->mode != BINPACK_MODE_RUNTIME || opt->enable_micro_trial) {
        chunk_crc = binpack_crc32(0, (const uint8_t*)src, src_sz);
        hdr_flags = BP_FLAG_CHECKSUM;
    }

    /* Fill Framing Headers */
    binpack_v2_header_t *v2_hdr = (binpack_v2_header_t*)out;
    v2_hdr->magic = BINPACK_MAGIC_V2;
    v2_hdr->version_major = BINPACK_SDK_VERSION_MAJOR;
    v2_hdr->version_minor = BINPACK_SDK_VERSION_MINOR;
    v2_hdr->flags = hdr_flags;
    v2_hdr->default_chunk_sz = (uint32_t)src_sz;
    v2_hdr->reserved = 0;

    binpack_chunk_header_t *chdr = (binpack_chunk_header_t*)(out + hdr_sz);
    chdr->chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr->uncompressed_sz = (uint32_t)src_sz;
    chdr->compressed_sz = (uint32_t)c_size;
    chdr->filter_id = (uint8_t)filter;
    chdr->filter_flags = filter_flags;
    chdr->filter_param = filter_param;
    chdr->chunk_crc32 = chunk_crc;
    chdr->reserved = 0;

    if (filter_flags & BP_CHUNK_FLAG_BOUNDS) {
        binpack_chunk_bounds_t *b = (binpack_chunk_bounds_t*)(out + hdr_sz + chdr_sz);
        b->code_offset = bounds.code_offset;
        b->code_length = bounds.code_length;
    }

    /* Fill Footer */
    size_t total_stream_len = payload_offset + c_size;
    binpack_v2_footer_t *ftr = (binpack_v2_footer_t*)(out + total_stream_len);
    ftr->footer_magic = BINPACK_MAGIC_FOOTER;
    ftr->total_uncompressed = (uint64_t)src_sz;
    ftr->total_compressed = (uint64_t)(total_stream_len + ftr_sz);
    ftr->chunk_count = 1;
    ftr->stream_crc32 = chunk_crc;

    *compressed_sz = total_stream_len + ftr_sz;
    return BINPACK_OK;
}

static binpack_status_t binpack_compress_buffer_fast(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    const binpack_options_t *opt
) {
    size_t bound = binpack_compress_bound(src_sz);
    uint8_t *out = (uint8_t*)malloc(bound);
    if (!out) return BINPACK_ERR_OUT_OF_MEMORY;

    size_t comp_sz = 0;
    binpack_status_t st = binpack_compress_into_fast(src, src_sz, out, bound, &comp_sz, opt);
    if (st == BINPACK_OK) {
        *dst = out;
        *dst_sz = comp_sz;
    } else {
        free(out);
        *dst = NULL;
        *dst_sz = 0;
    }
    return st;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    const binpack_options_t *opts
) {
    if (!src || !dst || !dst_sz) return BINPACK_ERR_INVALID_PARAM;

    binpack_options_t opt;
    if (opts) {
        opt = *opts;
    } else {
        binpack_default_options(&opt);
    }

    /* Fast-Path: Single-Chunk Zero-Copy Direct In-Place Compression */
    if (opt.mode != BINPACK_MODE_ARCHIVAL && src_sz <= 64 * 1024 * 1024) {
        return binpack_compress_buffer_fast(src, src_sz, dst, dst_sz, &opt);
    }

    mem_stream_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = (const uint8_t*)src;
    ctx.src_size = src_sz;

    if (opts) {
        if (opt.chunk_size == 0 && src_sz > 0) {
            opt.chunk_size = (src_sz <= 64 * 1024 * 1024) ? (int)src_sz : (64 * 1024 * 1024);
        }
    } else {
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
        uint32_t magic = binpack_load_le32(s);
        if (magic == BINPACK_MAGIC_V2) {
            const binpack_v2_header_t *v2_hdr = (const binpack_v2_header_t*)s;
            binpack_chunk_header_t *chdr = (binpack_chunk_header_t*)(s + sizeof(binpack_v2_header_t));
            if (chdr->chunk_magic == BINPACK_MAGIC_CHUNK) {
                /* B2: Validación upfront uniforme de límites contra corrupción */
                if (chdr->uncompressed_sz == 0 || chdr->uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
                    chdr->compressed_sz == 0 || chdr->compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
                    return BINPACK_ERR_CORRUPT_DATA;
                }
                size_t off = sizeof(binpack_v2_header_t) + sizeof(binpack_chunk_header_t);
                size_t code_off = 0;
                size_t code_len = chdr->uncompressed_sz;
                if (chdr->filter_flags & BP_CHUNK_FLAG_BOUNDS) {
                    if (src_sz >= off + sizeof(binpack_chunk_bounds_t)) {
                        binpack_chunk_bounds_t *b = (binpack_chunk_bounds_t*)(s + off);
                        code_off = b->code_offset;
                        code_len = b->code_length;
                        /* V-07: Comprobación sustractiva segura de bounds */
                        if (code_off > (size_t)chdr->uncompressed_sz ||
                            code_len > (size_t)chdr->uncompressed_sz - code_off) {
                            return BINPACK_ERR_CORRUPT_DATA;
                        }
                        off += sizeof(binpack_chunk_bounds_t);
                    }
                }
                if (src_sz >= off + chdr->compressed_sz) {
                    size_t after_payload = off + chdr->compressed_sz;
                    int is_single_chunk = 0;
                    if (after_payload == src_sz) {
                        is_single_chunk = 1;
                    } else if (after_payload + sizeof(binpack_v2_footer_t) <= src_sz) {
                        uint32_t next_tag = binpack_load_le32(s + after_payload);
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
                        } else if (fid == BINPACK_FILTER_RISCV) {
                            binpack_bcj_riscv_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_BITSHUFFLE) {
                            binpack_bitshuffle_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        } else if (fid == BINPACK_FILTER_TRANSPOSE_DELTA) {
                            binpack_transpose_delta_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        } else if (fid == BINPACK_FILTER_TEXTURE_BCN) {
                            binpack_texture_bcn_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        }

                        /* Integrity verification */
                        if (verify_checksum && (v2_hdr->flags & BP_FLAG_CHECKSUM)) {
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
/* Zero-Allocation Pre-Allocated Buffer API (Unity NativeArray / Embedded)   */
/* ========================================================================= */

typedef struct fixed_mem_stream_ctx_s {
    const uint8_t *src;
    size_t src_size;
    size_t src_pos;

    uint8_t *dst;
    size_t dst_cap;
    size_t dst_size;
    int overflow;
} fixed_mem_stream_ctx_t;

static size_t fixed_mem_read_cb(void *user_data, void *buffer, size_t size) {
    fixed_mem_stream_ctx_t *ctx = (fixed_mem_stream_ctx_t*)user_data;
    if (ctx->src_pos >= ctx->src_size) return 0;
    size_t avail = ctx->src_size - ctx->src_pos;
    size_t to_read = size < avail ? size : avail;
    memcpy(buffer, ctx->src + ctx->src_pos, to_read);
    ctx->src_pos += to_read;
    return to_read;
}

static size_t fixed_mem_write_cb(void *user_data, const void *buffer, size_t size) {
    if (size == 0) return 0;
    fixed_mem_stream_ctx_t *ctx = (fixed_mem_stream_ctx_t*)user_data;
    if (!ctx) return 0;

    /* V-03: Doble comprobación defensiva sin sumas desbordantes */
    if (ctx->dst_size > ctx->dst_cap || size > ctx->dst_cap - ctx->dst_size) {
        ctx->overflow = 1;
        return 0;
    }
    memcpy(ctx->dst + ctx->dst_size, buffer, size);
    ctx->dst_size += size;
    return size;
}

BINPACK_API size_t BINPACK_CALL binpack_compress_bound(size_t src_sz) {
    size_t num_chunks = (src_sz / BINPACK_DEFAULT_CHUNK_SIZE) + 2;
    return ZSTD_compressBound(src_sz) + 65536 +
           sizeof(binpack_v2_header_t) +
           num_chunks * (sizeof(binpack_chunk_header_t) + sizeof(binpack_chunk_bounds_t) + sizeof(binpack_chunk_info_t)) +
           sizeof(binpack_index_header_t) +
           sizeof(binpack_v2_footer_t);
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_into(
    const void *src, size_t src_sz,
    void *dst, size_t dst_capacity,
    size_t *compressed_sz,
    const binpack_options_t *opts
) {
    if (!src || !dst || !compressed_sz) return BINPACK_ERR_INVALID_PARAM;

    binpack_options_t opt;
    if (opts) {
        opt = *opts;
    } else {
        binpack_default_options(&opt);
    }

    /* Fast-Path: Pre-allocated direct buffer compression */
    if (opt.mode != BINPACK_MODE_ARCHIVAL && src_sz <= 64 * 1024 * 1024) {
        return binpack_compress_into_fast(src, src_sz, dst, dst_capacity, compressed_sz, &opt);
    }

    fixed_mem_stream_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = (const uint8_t*)src;
    ctx.src_size = src_sz;
    ctx.dst = (uint8_t*)dst;
    ctx.dst_cap = dst_capacity;

    binpack_status_t st = binpack_compress_stream(fixed_mem_read_cb, fixed_mem_write_cb, &ctx, src_sz, &opt, NULL);
    if (ctx.overflow) {
        *compressed_sz = 0;
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }
    if (st == BINPACK_OK) {
        *compressed_sz = ctx.dst_size;
    } else {
        *compressed_sz = 0;
    }
    return st;
}

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_into(
    const void *src, size_t src_sz,
    void *dst, size_t dst_capacity,
    size_t *decompressed_sz,
    int verify_checksum
) {
    if (!src || !dst || !decompressed_sz) return BINPACK_ERR_INVALID_PARAM;

    /* Fast-Path: Single-Chunk Zero-Allocation Direct In-Place Decompression into caller's buffer */
    if (src_sz >= sizeof(binpack_v2_header_t) + sizeof(binpack_chunk_header_t)) {
        const uint8_t *s = (const uint8_t*)src;
        uint32_t magic = binpack_load_le32(s);
        if (magic == BINPACK_MAGIC_V2) {
            const binpack_v2_header_t *v2_hdr = (const binpack_v2_header_t*)s;
            const binpack_chunk_header_t *chdr = (const binpack_chunk_header_t*)(s + sizeof(binpack_v2_header_t));
            if (chdr->chunk_magic == BINPACK_MAGIC_CHUNK) {
                /* B2: Validación upfront uniforme de límites contra corrupción */
                if (chdr->uncompressed_sz == 0 || chdr->uncompressed_sz > (BINPACK_MAX_CHUNK_SIZE * 4ULL) ||
                    chdr->compressed_sz == 0 || chdr->compressed_sz > (BINPACK_MAX_CHUNK_SIZE * 5ULL)) {
                    return BINPACK_ERR_CORRUPT_DATA;
                }
                size_t off = sizeof(binpack_v2_header_t) + sizeof(binpack_chunk_header_t);
                size_t code_off = 0;
                size_t code_len = chdr->uncompressed_sz;
                if (chdr->filter_flags & BP_CHUNK_FLAG_BOUNDS) {
                    if (src_sz >= off + sizeof(binpack_chunk_bounds_t)) {
                        const binpack_chunk_bounds_t *b = (const binpack_chunk_bounds_t*)(s + off);
                        code_off = b->code_offset;
                        code_len = b->code_length;
                        /* V-07: Comprobación sustractiva segura de bounds */
                        if (code_off > (size_t)chdr->uncompressed_sz ||
                            code_len > (size_t)chdr->uncompressed_sz - code_off) {
                            return BINPACK_ERR_CORRUPT_DATA;
                        }
                        off += sizeof(binpack_chunk_bounds_t);
                    }
                }
                if (src_sz >= off + chdr->compressed_sz) {
                    size_t after_payload = off + chdr->compressed_sz;
                    int is_single_chunk = 0;
                    if (after_payload == src_sz) {
                        is_single_chunk = 1;
                    } else if (after_payload + sizeof(binpack_v2_footer_t) <= src_sz) {
                        uint32_t next_tag = binpack_load_le32(s + after_payload);
                        if (next_tag == BINPACK_MAGIC_FOOTER) {
                            is_single_chunk = 1;
                        }
                    }
                    if (is_single_chunk) {
                        if (dst_capacity < chdr->uncompressed_sz) {
                            *decompressed_sz = 0;
                            return BINPACK_ERR_BUFFER_TOO_SMALL;
                        }
                        uint8_t *out = (uint8_t*)dst;
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
                            *decompressed_sz = 0;
                            return BINPACK_ERR_DECOMPRESSION_FAILED;
                        }

                        uint8_t fid = chdr->filter_id;
                        if (fid == BINPACK_FILTER_X86_64) {
                            binpack_bcj_x86_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_ARM32) {
                            binpack_bcj_arm32_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_AARCH64) {
                            binpack_bcj_aarch64_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_RISCV) {
                            binpack_bcj_riscv_unfilter(out + code_off, code_len);
                        } else if (fid == BINPACK_FILTER_BITSHUFFLE) {
                            binpack_bitshuffle_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        } else if (fid == BINPACK_FILTER_TRANSPOSE_DELTA) {
                            binpack_transpose_delta_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        } else if (fid == BINPACK_FILTER_TEXTURE_BCN) {
                            binpack_texture_bcn_unfilter(out, chdr->uncompressed_sz, chdr->filter_param);
                        }

                        if (verify_checksum && (v2_hdr->flags & BP_FLAG_CHECKSUM)) {
                            uint32_t chk = binpack_crc32(0, out, chdr->uncompressed_sz);
                            if (chk != chdr->chunk_crc32) {
                                *decompressed_sz = 0;
                                return BINPACK_ERR_CHECKSUM_MISMATCH;
                            }
                        }

                        *decompressed_sz = chdr->uncompressed_sz;
                        return BINPACK_OK;
                    }
                }
            }
        }
    }

    fixed_mem_stream_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = (const uint8_t*)src;
    ctx.src_size = src_sz;
    ctx.dst = (uint8_t*)dst;
    ctx.dst_cap = dst_capacity;

    binpack_status_t st = binpack_decompress_stream(fixed_mem_read_cb, fixed_mem_write_cb, &ctx, NULL);
    if (ctx.overflow) {
        *decompressed_sz = 0;
        return BINPACK_ERR_BUFFER_TOO_SMALL;
    }
    if (st == BINPACK_OK) {
        *decompressed_sz = ctx.dst_size;
    } else {
        *decompressed_sz = 0;
    }
    return st;
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
    binpack_fseek64(fin, 0, SEEK_END);
    uint64_t total_sz = (uint64_t)binpack_ftell64(fin);
    binpack_fseek64(fin, 0, SEEK_SET);

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

    binpack_fseek64(f, 0, SEEK_END);
    uint64_t file_len = (uint64_t)binpack_ftell64(f);
    binpack_fseek64(f, 0, SEEK_SET);

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
        binpack_fseek64(f, 0, SEEK_SET);
        if (fread(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
            fclose(f);
            return BINPACK_ERR_IO;
        }

        info->version = 2;
        info->compressed_size = file_len;
        info->flags = (uint8_t)hdr.flags;

        /* Read first chunk header to determine primary filter */
        if (file_len >= sizeof(binpack_v2_header_t) + sizeof(binpack_chunk_header_t)) {
            binpack_chunk_header_t chdr;
            if (fread(&chdr, 1, sizeof(chdr), f) == sizeof(chdr) &&
                chdr.chunk_magic == BINPACK_MAGIC_CHUNK) {
                info->primary_filter = chdr.filter_id;
            }
        }

        /* Read footer if present */
        if (file_len >= sizeof(binpack_v2_header_t) + sizeof(binpack_v2_footer_t)) {
            binpack_v2_footer_t footer;
            binpack_fseek64(f, file_len - sizeof(binpack_v2_footer_t), SEEK_SET);
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
        info->uncompressed_size = binpack_load_le32(&rest_v1[0]);
        info->compressed_size = file_len;
        info->chunk_count = 1;
        info->original_crc32 = binpack_load_le32(&rest_v1[8]);
        info->primary_filter = rest_v1[12];
        fclose(f);
        return BINPACK_OK;
    }

    fclose(f);
    return BINPACK_ERR_CORRUPT_DATA;
}
