/**
 * @file binpack_sdk.h
 * @brief BinPack Enterprise SDK - High-Performance Compression & Context Prefiltering Engine
 * @version 2.0.0
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
#define BINPACK_SDK_VERSION_MINOR 0
#define BINPACK_SDK_VERSION_PATCH 0
#define BINPACK_SDK_VERSION_STRING "2.0.0"

/**
 * @brief Magic numbers for file container headers.
 */
#define BINPACK_MAGIC_V1 0x504E4942 /* 'BINP' (Legacy single-block format) */
#define BINPACK_MAGIC_V2 0x324B5042 /* 'BPK2' (Enterprise streaming & chunked format) */
#define BINPACK_MAGIC_FOOTER 0x324B5046 /* 'FPK2' */

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
    BINPACK_ERR_BUFFER_TOO_SMALL     = -9  /**< Destination buffer capacity insufficient */
} binpack_status_t;

/* ========================================================================= */
/* Filter Identifiers & Strategy Modes                                       */
/* ========================================================================= */

typedef enum binpack_filter_e {
    BINPACK_FILTER_AUTO             = 0, /**< Auto-detect best filter via heuristics & micro-trial */
    BINPACK_FILTER_NONE             = 1, /**< Raw pass-through (no prefilter applied) */
    BINPACK_FILTER_X86_64           = 2, /**< x86/x64 BCJ relative jump & call translation */
    BINPACK_FILTER_ARM32            = 3, /**< ARMv7-A 32-bit & Thumb-2 branch normalization */
    BINPACK_FILTER_AARCH64          = 4, /**< ARMv8-A / ARM64 26-bit jump offset normalization */
    BINPACK_FILTER_TRANSPOSE_DELTA  = 5  /**< Stride-N transpose + delta for floats & telemetry */
} binpack_filter_t;

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
    uint16_t transpose_stride;   /**< Stride for TRANSPOSE_DELTA filter (e.g. 4 for float32, 8 for float64) */
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

/* ========================================================================= */
/* Stream Callbacks for Custom I/O & Progress                                */
/* ========================================================================= */

/**
 * @brief Read callback function for custom stream sources.
 * @return Bytes read, or 0 on EOF / error.
 */
typedef size_t (BINPACK_CALL *binpack_read_fn)(void *user_data, void *buffer, size_t size);

/**
 * @brief Write callback function for custom stream sinks.
 * @return Bytes written, or 0 on error.
 */
typedef size_t (BINPACK_CALL *binpack_write_fn)(void *user_data, const void *buffer, size_t size);

/**
 * @brief Progress callback.
 */
typedef void (BINPACK_CALL *binpack_progress_fn)(void *user_data, 
                                                 uint64_t bytes_in_processed, 
                                                 uint64_t total_in_bytes, 
                                                 uint64_t bytes_out_written);

/* ========================================================================= */
/* Public API Functions                                                      */
/* ========================================================================= */

/**
 * @brief Populate options structure with recommended enterprise defaults.
 */
BINPACK_API void BINPACK_CALL binpack_default_options(binpack_options_t *opts);

/**
 * @brief Compress an in-memory buffer into a dynamically allocated BinPack buffer.
 *
 * Caller MUST free *dst using binpack_free().
 *
 * @param src Input uncompressed data pointer.
 * @param src_sz Size in bytes of input.
 * @param dst Address of pointer to receive newly allocated compressed buffer.
 * @param dst_sz Pointer to receive size in bytes of compressed buffer.
 * @param opts Optional compression options (NULL for defaults).
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    const binpack_options_t *opts
);

/**
 * @brief Decompress an in-memory BinPack buffer.
 *
 * Caller MUST free *dst using binpack_free().
 *
 * @param src Input compressed data pointer.
 * @param src_sz Size in bytes of compressed input.
 * @param dst Address of pointer to receive newly allocated uncompressed buffer.
 * @param dst_sz Pointer to receive size in bytes of restored uncompressed data.
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz
);

/**
 * @brief Free a buffer allocated by binpack_compress_buffer or binpack_decompress_buffer.
 */
BINPACK_API void BINPACK_CALL binpack_free(void *ptr);

/**
 * @brief Compress a file on disk using streaming chunk framing and bounded memory.
 *
 * Operates with bounded RAM regardless of input file size (1 MB to 100+ GB).
 *
 * @param in_path Source file path.
 * @param out_path Destination archive file path.
 * @param opts Optional compression options (NULL for defaults).
 * @param progress Optional progress callback (NULL if not needed).
 * @param user_data Context pointer passed to progress callback.
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_file(
    const char *in_path,
    const char *out_path,
    const binpack_options_t *opts,
    binpack_progress_fn progress,
    void *user_data
);

/**
 * @brief Decompress an archive file on disk using streaming chunk framing and bounded memory.
 *
 * @param in_path Source archive file path.
 * @param out_path Destination restored file path.
 * @param progress Optional progress callback (NULL if not needed).
 * @param user_data Context pointer passed to progress callback.
 * @return BINPACK_OK on success, or negative error code.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_file(
    const char *in_path,
    const char *out_path,
    binpack_progress_fn progress,
    void *user_data
);

/**
 * @brief Generic stream compression using abstract read/write callbacks.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_compress_stream(
    binpack_read_fn reader,
    binpack_write_fn writer,
    void *user_data,
    uint64_t total_input_size,
    const binpack_options_t *opts,
    binpack_progress_fn progress
);

/**
 * @brief Generic stream decompression using abstract read/write callbacks.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_decompress_stream(
    binpack_read_fn reader,
    binpack_write_fn writer,
    void *user_data,
    binpack_progress_fn progress
);

/**
 * @brief Inspect archive header and chunk metadata without full decompression.
 */
BINPACK_API binpack_status_t BINPACK_CALL binpack_query_file_info(
    const char *path,
    binpack_file_info_t *info
);

/**
 * @brief Evaluate whether a prefilter improves compressibility on a sample block.
 *
 * @param sample Pointer to sample bytes.
 * @param sample_sz Size of sample (up to 64 KB).
 * @param filter Candidate filter ID.
 * @param stride Stride parameter for transpose filter.
 * @return 1 if prefilter yields positive gain, 0 if it causes regression or zero gain.
 */
BINPACK_API int BINPACK_CALL binpack_evaluate_filter_gain(
    const uint8_t *sample, size_t sample_sz,
    binpack_filter_t filter, uint16_t stride
);

/**
 * @brief Return version string ("2.0.0").
 */
BINPACK_API const char* BINPACK_CALL binpack_version_string(void);

/**
 * @brief Return human-readable error description string.
 */
BINPACK_API const char* BINPACK_CALL binpack_error_string(binpack_status_t status);

#ifdef __cplusplus
}
#endif

#endif /* BINPACK_SDK_H */
