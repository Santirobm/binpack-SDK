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
 * @brief Binary Differential Patch Container Header (36 bytes).
 */
typedef struct binpack_patch_header_s {
    uint32_t patch_magic;        /* 0x504B5042 'BPKP' */
    uint8_t  version_major;      /* 2 */
    uint8_t  version_minor;      /* 2 */
    uint16_t flags;              /* Patch flags */
    uint64_t old_sz;             /* Baseline expected original size */
    uint64_t new_sz;             /* Restored target size */
    uint32_t old_crc32;          /* Baseline uncompressed CRC32 */
    uint32_t new_crc32;          /* Restored target CRC32 */
    uint8_t  filter_id;          /* Prefilter used on both buffers */
    uint8_t  reserved[7];        /* Alignment padding */
} binpack_patch_header_t;

#pragma pack(pop)

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
int binpack_transpose_delta_unfilter(uint8_t *data, size_t size, uint16_t stride);

/* Bitshuffle Bit-Level Transposition */
int binpack_bitshuffle_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_bitshuffle_unfilter(uint8_t *data, size_t size, uint16_t stride);

/* GPU Texture Block Decoupling (BC1-BC7 & ASTC) */
int binpack_texture_bcn_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_texture_bcn_unfilter(uint8_t *data, size_t size, uint16_t stride);

binpack_filter_t binpack_auto_select_filter(const uint8_t *data, size_t size, uint16_t *out_param);

int binpack_get_cpu_cores(void);

#endif /* BINPACK_INTERNAL_H */
