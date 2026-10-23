/**
 * @file binpack_internal.h
 * @brief BinPack Enterprise SDK - Internal Architecture & Data Structures
 */

#ifndef BINPACK_INTERNAL_H
#define BINPACK_INTERNAL_H

#include "../include/binpack_sdk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <zstd.h>

#define BINPACK_MAGIC_CHUNK  0x4B4E4843 /* 'CHNK' */
#define BINPACK_MAGIC_FOOTER 0x324B5046 /* 'FPK2' */

#pragma pack(push, 1)

/**
 * @brief V2 Container Stream Header (16 bytes).
 */
typedef struct binpack_v2_header_s {
    uint32_t magic;              /* 0x324B5042 'BPK2' */
    uint8_t  version_major;      /* 2 */
    uint8_t  version_minor;      /* 0 */
    uint16_t flags;              /* Bit 0: Checksum enabled */
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
    uint8_t  filter_flags;       /* Filter flags (e.g., code section bounds present) */
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
 * @brief V2 Container Stream Footer (28 bytes).
 */
typedef struct binpack_v2_footer_s {
    uint32_t footer_magic;       /* 0x324B5046 'FPK2' */
    uint64_t total_uncompressed; /* Sum of all uncompressed bytes */
    uint64_t total_compressed;   /* Sum of all compressed stream bytes */
    uint32_t chunk_count;        /* Number of chunks */
    uint32_t stream_crc32;       /* Full uncompressed stream CRC32 */
} binpack_v2_footer_t;

#pragma pack(pop)

#define BP_FLAG_CHECKSUM 0x0001
#define BP_CHUNK_FLAG_BOUNDS 0x01

/* Internal Function Declarations */
uint32_t binpack_crc32(uint32_t crc, const uint8_t *buf, size_t len);
void binpack_find_code_bounds(const uint8_t *data, size_t size, size_t *out_offset, size_t *out_length);

void binpack_bcj_x86_filter(uint8_t *data, size_t size);
void binpack_bcj_x86_unfilter(uint8_t *data, size_t size);

void binpack_bcj_arm32_filter(uint8_t *data, size_t size);
void binpack_bcj_arm32_unfilter(uint8_t *data, size_t size);

void binpack_bcj_aarch64_filter(uint8_t *data, size_t size);
void binpack_bcj_aarch64_unfilter(uint8_t *data, size_t size);

int binpack_transpose_delta_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_transpose_delta_unfilter(uint8_t *data, size_t size, uint16_t stride);

/* Bitshuffle Bit-Level Transposition */
int binpack_bitshuffle_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_bitshuffle_unfilter(uint8_t *data, size_t size, uint16_t stride);

/* GPU Texture Block Decoupling (BC1-BC7 & ASTC) */
int binpack_texture_bcn_filter(uint8_t *data, size_t size, uint16_t stride);
int binpack_texture_bcn_unfilter(uint8_t *data, size_t size, uint16_t stride);

binpack_filter_t binpack_auto_select_filter(const uint8_t *data, size_t size, uint16_t *out_param);

#endif /* BINPACK_INTERNAL_H */
