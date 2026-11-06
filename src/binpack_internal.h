/**
 * @file binpack_internal.h
 * @brief BinPack Enterprise SDK - Internal Architecture & Data Structures
 */

#ifndef BINPACK_INTERNAL_H
#define BINPACK_INTERNAL_H

#if !defined(_WIN32) && !defined(_FILE_OFFSET_BITS)
    #define _FILE_OFFSET_BITS 64
#endif

#include "../include/binpack_sdk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zstd.h>
#include <zdict.h>
#include "../deps/lzma/LzmaLib.h"
#include "../deps/lzma/Bra.h"

#if defined(_WIN32)
    #define binpack_fseek64 _fseeki64
    #define binpack_ftell64 _ftelli64
#else
    #define binpack_fseek64 fseeko
    #define binpack_ftell64 ftello
#endif

/* ========================================================================= */
/* Safe Endianness-Aware Memory Loaders (Unaligned-safe & Strict-Aliasing)  */
/* ========================================================================= */

static inline uint16_t binpack_load_le16(const uint8_t *p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

static inline uint32_t binpack_load_le32(const uint8_t *p) {
    return ((uint32_t)p[0])       |
           ((uint32_t)p[1] << 8)  |
           ((uint32_t)p[2] << 16) |
           ((uint32_t)p[3] << 24);
}

static inline uint64_t binpack_load_le64(const uint8_t *p) {
    return ((uint64_t)p[0])       |
           ((uint64_t)p[1] << 8)  |
           ((uint64_t)p[2] << 16) |
           ((uint64_t)p[3] << 24) |
           ((uint64_t)p[4] << 32) |
           ((uint64_t)p[5] << 40) |
           ((uint64_t)p[6] << 48) |
           ((uint64_t)p[7] << 56);
}

static inline uint16_t binpack_load_be16(const uint8_t *p) {
    return (uint16_t)(((uint16_t)p[0] << 8) | (uint16_t)p[1]);
}

static inline uint32_t binpack_load_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8)  |
           ((uint32_t)p[3]);
}

static inline uint64_t binpack_load_be64(const uint8_t *p) {
    return ((uint64_t)p[0] << 56) |
           ((uint64_t)p[1] << 48) |
           ((uint64_t)p[2] << 40) |
           ((uint64_t)p[3] << 32) |
           ((uint64_t)p[4] << 24) |
           ((uint64_t)p[5] << 16) |
           ((uint64_t)p[6] << 8)  |
           ((uint64_t)p[7]);
}

static inline void binpack_store_le16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
}

static inline void binpack_store_le32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
}

static inline void binpack_store_le64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)(v & 0xFF);
    p[1] = (uint8_t)((v >> 8) & 0xFF);
    p[2] = (uint8_t)((v >> 16) & 0xFF);
    p[3] = (uint8_t)((v >> 24) & 0xFF);
    p[4] = (uint8_t)((v >> 32) & 0xFF);
    p[5] = (uint8_t)((v >> 40) & 0xFF);
    p[6] = (uint8_t)((v >> 48) & 0xFF);
    p[7] = (uint8_t)((v >> 56) & 0xFF);
}

static inline void binpack_store_be16(uint8_t *p, uint16_t v) {
    p[0] = (uint8_t)((v >> 8) & 0xFF);
    p[1] = (uint8_t)(v & 0xFF);
}

static inline void binpack_store_be32(uint8_t *p, uint32_t v) {
    p[0] = (uint8_t)((v >> 24) & 0xFF);
    p[1] = (uint8_t)((v >> 16) & 0xFF);
    p[2] = (uint8_t)((v >> 8) & 0xFF);
    p[3] = (uint8_t)(v & 0xFF);
}

static inline void binpack_store_be64(uint8_t *p, uint64_t v) {
    p[0] = (uint8_t)((v >> 56) & 0xFF);
    p[1] = (uint8_t)((v >> 48) & 0xFF);
    p[2] = (uint8_t)((v >> 40) & 0xFF);
    p[3] = (uint8_t)((v >> 32) & 0xFF);
    p[4] = (uint8_t)((v >> 24) & 0xFF);
    p[5] = (uint8_t)((v >> 16) & 0xFF);
    p[6] = (uint8_t)((v >> 8) & 0xFF);
    p[7] = (uint8_t)(v & 0xFF);
}

/* Guarda aritmetica segura sustractiva para acumulaciones de 64 bits */
static inline binpack_status_t binpack_safe_add_u64(uint64_t *acc, uint64_t delta) {
    if (delta > UINT64_MAX - *acc) {
        return BINPACK_ERR_CORRUPT_DATA;
    }
    *acc += delta;
    return BINPACK_OK;
}

#define BINPACK_MAGIC_CHUNK  0x4B4E4843 /* 'CHNK' */
#define BINPACK_MAGIC_INDEX  0x58444942 /* 'BIDX' */
#define BINPACK_MAGIC_FOOTER 0x324B5046 /* 'FPK2' */

#pragma pack(push, 1)

/**
 * @brief V2 Container Stream Header (16 bytes).
 */
typedef struct binpack_v2_header_s {
    uint32_t magic;              /* 0x324B5042 'BPK2' */
    uint8_t  version_major;      /* 2 */
    uint8_t  version_minor;      /* 2 */
    uint16_t flags;              /* Bit 0: Checksum enabled, Bit 1: O(1) Seek Table present */
    uint32_t default_chunk_sz;   /* Standard chunk size */
    uint32_t reserved;           /* 0 */
} binpack_v2_header_t;

/**
 * @brief Per-Chunk Framing Header (24 bytes).
 */
typedef struct binpack_chunk_header_s {
    uint32_t chunk_magic;        /* 0x4B4E4843 'CHNK' */
    uint32_t uncompressed_sz;    /* Size of uncompressed chunk data */
    uint32_t compressed_sz;      /* Size of compressed chunk payload */
    uint8_t  filter_id;          /* binpack_filter_t utilized */
    uint8_t  filter_flags;       /* Filter flags (bounds, prefix, backend) */
    uint16_t filter_param;       /* Filter-specific param (e.g. stride) */
    uint32_t chunk_crc32;        /* CRC32 of uncompressed chunk */
    uint32_t reserved;           /* 0 */
} binpack_chunk_header_t;

/**
 * @brief Per-Chunk Section Bounds Subheader (8 bytes). Present when filter_flags & 0x01.
 */
typedef struct binpack_chunk_bounds_s {
    uint32_t code_offset;        /* Offset within chunk where executable section begins */
    uint32_t code_length;        /* Length of executable section */
} binpack_chunk_bounds_t;

/**
 * @brief O(1) Seek Table Index Block Header (12 bytes).
 */
typedef struct binpack_index_header_s {
    uint32_t index_magic;        /* 0x58444942 'BIDX' */
    uint32_t entry_count;        /* Number of binpack_chunk_info_t entries following */
} binpack_index_header_t;

/**
 * @brief V2 Container Stream Footer (28 bytes).
 */
typedef struct binpack_v2_footer_s {
    uint32_t footer_magic;       /* 0x324B5046 'FPK2' */
    uint64_t total_uncompressed; /* Sum of all uncompressed bytes */
    uint64_t total_compressed;   /* Sum of all compressed stream bytes */
    uint32_t chunk_count;        /* Number of chunks */
    uint32_t stream_crc32;       /* Full uncompressed stream CRC32 */
} binpack_v2_footer_t;

/**
 * @brief Maximum differential patch target image size (512 MiB OTA safety budget).
 */
#define BINPACK_MAX_PATCH_SIZE (512ULL * 1024ULL * 1024ULL)

/**
 * @brief Maximum pre-trained dictionary buffer capacity (32 MiB telemetry safety budget).
 */
#define BINPACK_MAX_DICT_SIZE (32ULL * 1024ULL * 1024ULL)

/**
 * @brief Binary Differential Patch Container Header (40 bytes packed).
 */
typedef struct binpack_patch_header_s {
    uint32_t patch_magic;        /* 0x504B5042 'BPKP' (BINPACK_MAGIC_PATCH) */
    uint8_t  version_major;      /* 1 (Historical) or 2 (Enterprise) */
    uint8_t  version_minor;      /* Minor revision */
    uint16_t flags;              /* Patch flags (must be 0) */
    uint64_t old_sz;             /* Baseline expected original size */
    uint64_t new_sz;             /* Restored target size */
    uint32_t old_crc32;          /* Baseline uncompressed CRC32 */
    uint32_t new_crc32;          /* Restored target CRC32 */
    uint8_t  filter_id;          /* Prefilter used on both buffers */
    uint16_t filter_param;       /* Filter parameter (e.g. stride) */
    uint8_t  reserved[5];        /* Alignment padding / reserved (must be 0) */
} binpack_patch_header_t;

#pragma pack(pop)

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
_Static_assert(sizeof(binpack_patch_header_t) == 40, "sizeof(binpack_patch_header_t) must be 40 bytes");
#elif defined(__cplusplus) && __cplusplus >= 201103L
static_assert(sizeof(binpack_patch_header_t) == 40, "sizeof(binpack_patch_header_t) must be 40 bytes");
#else
typedef char binpack_patch_header_size_assert[(sizeof(binpack_patch_header_t) == 40) ? 1 : -1];
#endif

#define BP_FLAG_CHECKSUM     0x0001
#define BP_FLAG_SEEK_INDEX   0x0002

#define BP_CHUNK_FLAG_BOUNDS 0x01
#define BP_CHUNK_FLAG_PREFIX 0x02
#define BP_CHUNK_FLAG_LZMA   0x04

/**
 * @brief Internal definition of pre-trained dictionary structure.
 */
struct binpack_dict_s {
    void *buffer;
    size_t size;
    ZSTD_CDict *cdict;
    ZSTD_DDict *ddict;
};

/**
 * @brief Internal definition of seekable random-access archive structure.
 */
struct binpack_archive_s {
    FILE *fp;
    char filepath[1024];
    binpack_v2_header_t header;
    binpack_v2_footer_t footer;
    uint32_t chunk_count;
    binpack_chunk_info_t *chunks;
    ZSTD_DCtx *dctx;
};

/* Internal Function Declarations */
uint32_t binpack_crc32(uint32_t crc, const uint8_t *buf, size_t len);
void binpack_find_code_bounds(const uint8_t *data, size_t size, size_t *out_offset, size_t *out_length);

void binpack_bcj_x86_filter(uint8_t *data, size_t size);
void binpack_bcj_x86_unfilter(uint8_t *data, size_t size);

void binpack_bcj_arm32_filter(uint8_t *data, size_t size);
void binpack_bcj_arm32_unfilter(uint8_t *data, size_t size);

void binpack_bcj_aarch64_filter(uint8_t *data, size_t size);
void binpack_bcj_aarch64_unfilter(uint8_t *data, size_t size);

void binpack_bcj_riscv_filter(uint8_t *data, size_t size);
void binpack_bcj_riscv_unfilter(uint8_t *data, size_t size);

int binpack_transpose_delta_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_transpose_delta_filter_into(const uint8_t *src, uint8_t *dst, size_t size, uint16_t stride);
int binpack_transpose_delta_unfilter(uint8_t *data, size_t size, uint16_t stride);

/* Bitshuffle Bit-Level Transposition */
int binpack_bitshuffle_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_bitshuffle_unfilter(uint8_t *data, size_t size, uint16_t stride);

/* GPU Texture Block Decoupling (BC1-BC7 & ASTC) */
int binpack_texture_bcn_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_texture_bcn_unfilter(uint8_t *data, size_t size, uint16_t stride);

binpack_filter_t binpack_auto_select_filter(const uint8_t *data, size_t size, uint16_t *out_param);

int binpack_get_cpu_cores(void);

/* ========================================================================= */
/* Architecture SIMD Dispatch Engine & Kernel Contracts                      */
/* ========================================================================= */

#define BINPACK_BACKEND_SCALAR 0
#define BINPACK_BACKEND_AVX2   1

typedef void (*bp_bcj_fn)(uint8_t *data, size_t size);
typedef int  (*bp_bitshuffle_fn)(uint8_t *data, size_t size, uint16_t stride);

typedef struct {
    bp_bcj_fn        bcj_aarch64_filter;
    bp_bcj_fn        bcj_aarch64_unfilter;
    bp_bcj_fn        bcj_riscv_filter;
    bp_bcj_fn        bcj_riscv_unfilter;
    bp_bitshuffle_fn bitshuffle_filter;
    bp_bitshuffle_fn bitshuffle_unfilter;
    int              backend_id;
} binpack_dispatch_table_t;

extern binpack_dispatch_table_t g_bp_dispatch;

void bp_ensure_cpu_dispatch_initialized(void);
int  bp_cpu_dispatch_get_active_backend(void);

/* Portable Baseline / Scalar Kernels */
void bp_bcj_aarch64_filter_scalar(uint8_t *data, size_t size);
void bp_bcj_aarch64_unfilter_scalar(uint8_t *data, size_t size);
void bp_bcj_riscv_filter_scalar(uint8_t *data, size_t size);
void bp_bcj_riscv_unfilter_scalar(uint8_t *data, size_t size);
int  bp_bitshuffle_filter_scalar(uint8_t *data, size_t size, uint16_t stride);
int  bp_bitshuffle_unfilter_scalar(uint8_t *data, size_t size, uint16_t stride);

/* AVX2 Accelerated Kernels (binpack_core_avx2.c) */
void bp_bcj_aarch64_filter_avx2(uint8_t *data, size_t size);
void bp_bcj_aarch64_unfilter_avx2(uint8_t *data, size_t size);
void bp_bcj_riscv_filter_avx2(uint8_t *data, size_t size);
void bp_bcj_riscv_unfilter_avx2(uint8_t *data, size_t size);
int  bp_bitshuffle_filter_avx2(uint8_t *data, size_t size, uint16_t stride);
int  bp_bitshuffle_unfilter_avx2(uint8_t *data, size_t size, uint16_t stride);

/* Core Scratch Buffer Management (Thread-Local Pool) */
uint8_t* bp_acquire_core_scratch(size_t size, int *out_must_free);
void     bp_release_core_scratch(uint8_t *ptr, int must_free);

void bp_deinterleave_channels(const uint8_t *src, uint8_t *dst, size_t num_elements, uint16_t stride);
void bp_interleave_channels(const uint8_t *src, uint8_t *dst, size_t num_elements, uint16_t stride);

static inline uint64_t bp_transpose_8x8_bits(uint64_t x) {
    uint64_t t;
    t = (x ^ (x >> 7)) & 0x00AA00AA00AA00AAULL;
    x = x ^ t ^ (t << 7);
    t = (x ^ (x >> 14)) & 0x0000CCCC0000CCCCULL;
    x = x ^ t ^ (t << 14);
    t = (x ^ (x >> 28)) & 0x00000000F0F0F0F0ULL;
    x = x ^ t ^ (t << 28);
    return x;
}

#endif /* BINPACK_INTERNAL_H */
