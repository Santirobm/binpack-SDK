/**
 * @file binpack_sdk.h
 * @brief BinPack Enterprise SDK - High-Performance Compression & Context Prefiltering Engine
 * @version 2.2.0
 * @copyright (c) 2026 Santiago Robledo. All Rights Reserved.
 *
 * Licensed under the BinPack Dual Licensing Agreement (Commercial & Community).
 * See LICENSE file for full legal terms.
 */

#ifndef BINPACK_SDK_H
#define BINPACK_SDK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ========================================================================= */
/* API Export / Import Macros                                                */
/* ========================================================================= */

#if defined(_WIN32) || defined(__CYGWIN__)
    #if defined(BINPACK_BUILD_DLL)
        #define BINPACK_API __declspec(dllexport)
    #elif defined(BINPACK_USE_DLL)
        #define BINPACK_API __declspec(dllimport)
    #else
        #define BINPACK_API
    #endif
    #define BINPACK_CALL __cdecl
#else
    #if defined(__GNUC__) && __GNUC__ >= 4
        #define BINPACK_API __attribute__((visibility("default")))
    #else
        #define BINPACK_API
    #endif
    #define BINPACK_CALL
#endif

/* ========================================================================= */
/* Version & Format Constants                                                */
/* ========================================================================= */

#define BINPACK_SDK_VERSION_MAJOR 2
#define BINPACK_SDK_VERSION_MINOR 2
#define BINPACK_SDK_VERSION_PATCH 0
#define BINPACK_SDK_VERSION_STRING "2.2.0"

/**
 * @brief Magic numbers for file container headers.
 */
#define BINPACK_MAGIC_V1     0x504E4942 /* 'BINP' (Legacy single-block format) */
#define BINPACK_MAGIC_V2     0x324B5042 /* 'BPK2' (Enterprise streaming & chunked format) */
#define BINPACK_MAGIC_FOOTER 0x324B5046 /* 'FPK2' */
#define BINPACK_MAGIC_PATCH  0x504B5042 /* 'BPKP' (Binary differential OTA patch) */

#define BINPACK_DEFAULT_CHUNK_SIZE (16 * 1024 * 1024) /* 16 MB chunk default */
#define BINPACK_MAX_CHUNK_SIZE     (64 * 1024 * 1024) /* 64 MB chunk limit */
#define BINPACK_MIN_CHUNK_SIZE     (64 * 1024)        /* 64 KB chunk lower bound */

/* ========================================================================= */
/* Error & Status Codes                                                      */
/* ========================================================================= */

typedef enum binpack_status_e {
    BINPACK_OK                       =  0, /**< Operation completed successfully */
    BINPACK_ERR_INVALID_PARAM        = -1, /**< Invalid parameter, pointer or bounds */
    BINPACK_ERR_OUT_OF_MEMORY        = -2, /**< Memory allocation failure */
    BINPACK_ERR_COMPRESSION_FAILED   = -3, /**< Backend entropy/LZ compression engine failed */
    BINPACK_ERR_DECOMPRESSION_FAILED = -4, /**< Compressed stream corrupted or truncated */
    BINPACK_ERR_CORRUPT_DATA         = -5, /**< Header, chunk frame or structure corrupt */
    BINPACK_ERR_CHECKSUM_MISMATCH    = -6, /**< CRC32 integrity check failed */
    BINPACK_ERR_IO                   = -7, /**< File read, write, or seek failure */
    BINPACK_ERR_UNSUPPORTED_VERSION  = -8, /**< Stream requires higher SDK version */
    BINPACK_ERR_BUFFER_TOO_SMALL     = -9, /**< Destination buffer capacity insufficient */
    BINPACK_ERR_PATCH_MISMATCH       = -10 /**< Differential patch baseline CRC/size mismatch */
} binpack_status_t;

/* ========================================================================= */
/* Filter Identifiers & Strategy Modes                                       */
/* ========================================================================= */

typedef enum binpack_filter_e {
    BINPACK_FILTER_AUTO             = 0, /**< Auto-detect best filter via heuristics & micro-trial */
    BINPACK_FILTER_NONE             = 1, /**< Raw pass-through (no prefilter applied) */
    BINPACK_FILTER_X86_64           = 2, /**< x86/x64 BCJ relative jump & call translation */
    BINPACK_FILTER_ARM32            = 3, /**< ARMv7-A 32-bit & Thumb-2 branch normalization */
    BINPACK_FILTER_AARCH64          = 4, /**< ARMv8-A / ARM64 26-bit B/BL jump offset normalization */
    BINPACK_FILTER_TRANSPOSE_DELTA  = 5, /**< Stride-N transpose + delta for floats & telemetry */
    BINPACK_FILTER_BITSHUFFLE       = 6, /**< Bit-level transposition for tensors & scientific IEEE 754 data */
    BINPACK_FILTER_TEXTURE_BCN      = 7  /**< GPU texture block decoupling (BC1-BC7 & ASTC formats) */
} binpack_filter_t;

/**
 * @brief Engine operational profiles.
 */
typedef enum binpack_engine_mode_e {
    BINPACK_MODE_DEFAULT  = 0, /**< Balanced production profile (Level 19, high ratio, fast decode) */
    BINPACK_MODE_RUNTIME  = 1, /**< Maximum decompression throughput profile (650 - 1060 MB/s) */
    BINPACK_MODE_ARCHIVAL = 2  /**< Maximum compression density profile (btultra2, beats 7-Zip LZMA2) */
} binpack_engine_mode_t;

/* ========================================================================= */
/* Configuration & Metadata Structures                                       */
/* ========================================================================= */

/**
 * @brief Compression parameters and runtime execution flags.
 */
typedef struct binpack_options_s {
    int clevel;                  /**< Compression level (1 = fastest, 19 = default, 22 = ultra) */
    int nb_workers;              /**< Worker threads (0 = auto-detect hardware concurrency) */
    int chunk_size;              /**< Chunk framing size in bytes (0 = 16 MB default) */
    int enable_micro_trial;      /**< 1 = run fast 32KB sample trial to guarantee zero regression */
    binpack_filter_t filter;     /**< Force specific filter or use BINPACK_FILTER_AUTO */
    uint16_t transpose_stride;   /**< Stride for TRANSPOSE_DELTA / BITSHUFFLE (e.g. 4 for float32, 8 for float64) */
    binpack_engine_mode_t mode;  /**< Engine operational mode (DEFAULT, RUNTIME, or ARCHIVAL) */
} binpack_options_t;

/**
 * @brief Archive query information.
 */
typedef struct binpack_file_info_s {
    uint32_t version;            /**< Format version (1 or 2) */
    uint64_t uncompressed_size;  /**< Total original payload size in bytes */
    uint64_t compressed_size;    /**< Total on-disk compressed stream size in bytes */
    uint32_t chunk_count;        /**< Number of independent chunks in archive */
    uint32_t original_crc32;     /**< CRC32 of original uncompressed stream */
    uint8_t  primary_filter;     /**< Predominant filter utilized */
    uint8_t  flags;              /**< Stream flags (e.g., checksum enabled) */
} binpack_file_info_t;

/**
 * @brief Information for an individual seekable chunk.
 */
typedef struct binpack_chunk_info_s {
    uint32_t chunk_index;        /**< Zero-based index of chunk */
    uint64_t file_offset;        /**< Byte offset of chunk payload in archive file */
    uint32_t uncompressed_sz;    /**< Restored uncompressed byte size of chunk */
    uint32_t compressed_sz;      /**< Compressed payload size of chunk */
    uint8_t  filter_id;          /**< Filter ID utilized */
    uint8_t  filter_flags;       /**< Filter flags (bounds, prefix, backend) */
    uint32_t chunk_crc32;        /**< Expected CRC32 checksum of chunk */
} binpack_chunk_info_t;

/**
 * @brief Opaque handle for random-access archives.
 */
typedef struct binpack_archive_s binpack_archive_t;

/**
 * @brief Opaque pre-trained dictionary handle.
 */
typedef struct binpack_dict_s binpack_dict_t;

/* ========================================================================= */
/* Stream Callbacks for Custom I/O & Progress                                */
/* ========================================================================= */

typedef size_t (BINPACK_CALL *binpack_read_fn)(void *user_data, void *buffer, size_t size);
typedef size_t (BINPACK_CALL *binpack_write_fn)(void *user_data, const void *buffer, size_t size);
typedef void (BINPACK_CALL *binpack_progress_fn)(void *user_data, 
                                                 uint64_t bytes_in_processed, 
                                                 uint64_t total_in_bytes, 
                                                 uint64_t bytes_out_written);

/* ========================================================================= */
/* Core In-Memory & File API Functions                                       */
/* ========================================================================= */

BINPACK_API void BINPACK_CALL binpack_default_options(binpack_options_t *opts);

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    const binpack_options_t *opts
);

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz
);

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_buffer_ex(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    int verify_checksum
);

BINPACK_API void BINPACK_CALL binpack_free(void *ptr);

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_file(
    const char *in_path,
    const char *out_path,
    const binpack_options_t *opts,
    binpack_progress_fn progress,
    void *user_data
);

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_file(
    const char *in_path,
    const char *out_path,
    binpack_progress_fn progress,
    void *user_data
);

BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_stream(
    binpack_read_fn reader,
    binpack_write_fn writer,
    void *user_data,
    uint64_t total_input_size,
    const binpack_options_t *opts,
    binpack_progress_fn progress
);

BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_stream(
    binpack_read_fn reader,
    binpack_write_fn writer,
    void *user_data,
    binpack_progress_fn progress
);

BINPACK_API binpack_status_t BINPACK_CALL binpack_query_file_info(
    const char *path,
    binpack_file_info_t *info
);

BINPACK_API int BINPACK_CALL binpack_evaluate_filter_gain(
    const uint8_t *sample, size_t sample_sz,
    binpack_filter_t filter, uint16_t stride
);

/* ========================================================================= */
/* Pre-trained Dictionary API (Micro-Telemetry & CAN-bus Packets)            */
/* ========================================================================= */

/**
 * @brief Train an optimized dictionary from a collection of sample payloads.
 *
 * @param samples Flat buffer containing all sample concatenated payloads.
 * @param sample_sizes Array of sizes for each individual sample payload.
 * @param sample_count Number of samples in sample_sizes.
 * @param dict_capacity Desired maximum dictionary size in bytes (e.g. 32768 or 65536).
 * @return Newly allocated dictionary handle, or NULL on failure. Free with binpack_free_dict().
 */
BINPACK_API binpack_dict_t* BINPACK_CALL binpack_train_dict(
    const void *samples,
    const size_t *sample_sizes,
    size_t sample_count,
    size_t dict_capacity
);

/**
 * @brief Create a dictionary handle from an existing pre-trained dictionary buffer.
 */
BINPACK_API binpack_dict_t* BINPACK_CALL binpack_create_dict(
    const void *dict_buffer,
    size_t dict_size,
    int clevel
);

/**
 * @brief Extract raw dictionary bytes from a dictionary handle for saving to disk or transmission.
 */
BINPACK_API const void* BINPACK_CALL binpack_get_dict_buffer(
    const binpack_dict_t *dict,
    size_t *out_size
);

/**
 * @brief Compress a single micro-payload using a pre-trained dictionary with zero framing overhead.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *compressed_sz,
    const binpack_dict_t *dict
);

/**
 * @brief Decompress a single micro-payload using a pre-trained dictionary.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *decompressed_sz,
    const binpack_dict_t *dict
);

/**
 * @brief Free a dictionary handle.
 */
BINPACK_API void BINPACK_CALL binpack_free_dict(binpack_dict_t *dict);

/* ========================================================================= */
/* Binary Differential Patching API (OTA Firmware & Asset Updates)           */
/* ========================================================================= */

/**
 * @brief Generate a binary differential patch between old_buf (base) and new_buf (target).
 * Combines architecture-aware BCJ branch normalization with prefix differential matching.
 *
 * @param old_buf Pointer to baseline original data.
 * @param old_sz Size of baseline original data.
 * @param new_buf Pointer to updated target data.
 * @param new_sz Size of updated target data.
 * @param patch_out Address of pointer to receive allocated patch buffer (Caller frees via binpack_free).
 * @param patch_sz Pointer to receive size of patch buffer.
 * @param opts Optional compression options (NULL for defaults).
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_create_patch(
    const void *old_buf, size_t old_sz,
    const void *new_buf, size_t new_sz,
    void **patch_out, size_t *patch_sz,
    const binpack_options_t *opts
);

/**
 * @brief Apply a binary differential patch to reconstruct the updated new_buf from old_buf.
 *
 * @param old_buf Pointer to baseline original data.
 * @param old_sz Size of baseline original data.
 * @param patch_buf Pointer to patch data generated by binpack_create_patch.
 * @param patch_sz Size of patch data.
 * @param new_out Address of pointer to receive allocated restored data (Caller frees via binpack_free).
 * @param new_sz Pointer to receive size of restored data.
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_apply_patch(
    const void *old_buf, size_t old_sz,
    const void *patch_buf, size_t patch_sz,
    void **new_out, size_t *new_sz
);

/* ========================================================================= */
/* Seekable Chunk Archive API (Random Access & Virtual Texturing)             */
/* ========================================================================= */

/**
 * @brief Open an archive file for random-access chunk queries and zero-allocation decompression.
 */
BINPACK_API binpack_archive_t* BINPACK_CALL binpack_archive_open(const char *filepath);

/**
 * @brief Return total number of chunks present in the opened archive.
 */
BINPACK_API uint32_t BINPACK_CALL binpack_archive_get_chunk_count(const binpack_archive_t *archive);

/**
 * @brief Return total uncompressed payload size of the opened archive.
 */
BINPACK_API uint64_t BINPACK_CALL binpack_archive_get_total_uncompressed_size(const binpack_archive_t *archive);

/**
 * @brief Query metadata for a specific chunk index without reading or decompressing it.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_archive_get_chunk_info(
    const binpack_archive_t *archive,
    uint32_t chunk_idx,
    binpack_chunk_info_t *info
);

/**
 * @brief Decompress a single chunk directly into a caller-provided destination memory buffer.
 *
 * @param archive Open archive handle.
 * @param chunk_idx Zero-based index of target chunk.
 * @param dst Caller-provided destination buffer (capacity >= uncompressed_sz).
 * @param dst_capacity Capacity in bytes of dst.
 * @param decompressed_sz Pointer to receive actual bytes decompressed.
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_archive_decompress_chunk(
    binpack_archive_t *archive,
    uint32_t chunk_idx,
    void *dst, size_t dst_capacity,
    size_t *decompressed_sz
);

/**
 * @brief Close an opened archive handle and release its internal resources.
 */
BINPACK_API void BINPACK_CALL binpack_archive_close(binpack_archive_t *archive);

/* ========================================================================= */
/* Informational & Status Functions                                          */
/* ========================================================================= */

BINPACK_API const char* BINPACK_CALL binpack_version_string(void);
BINPACK_API const char* BINPACK_CALL binpack_error_string(binpack_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* BINPACK_SDK_H */
