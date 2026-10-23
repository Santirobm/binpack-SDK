# BinPack Enterprise SDK - C-ABI API Reference

All declarations reside in [`include/binpack_sdk.h`](include/binpack_sdk.h).

---

## 1. Status & Error Codes (`binpack_status_t`)

| Constant | Value | Description |
| :--- | :---: | :--- |
| `BINPACK_OK` | `0` | Operation completed successfully with full CRC verification. |
| `BINPACK_ERR_INVALID_PARAM` | `-1` | Null pointer, invalid buffer capacity, or out-of-range argument. |
| `BINPACK_ERR_OUT_OF_MEMORY` | `-2` | Memory allocation (`malloc`/`realloc`) failure. |
| `BINPACK_ERR_COMPRESSION_FAILED` | `-3` | Backend entropy or LZ compression engine error. |
| `BINPACK_ERR_DECOMPRESSION_FAILED`| `-4` | Decompression corrupted, truncated, or malformed payload. |
| `BINPACK_ERR_CORRUPT_DATA` | `-5` | Invalid magic bytes (`BPK2`/`BINP`/`CHNK`), broken framing header. |
| `BINPACK_ERR_CHECKSUM_MISMATCH` | `-6` | CRC32 mismatch between decoded payload and header checksum. |
| `BINPACK_ERR_IO` | `-7` | File read, write, or seek error. |
| `BINPACK_ERR_UNSUPPORTED_VERSION` | `-8` | Stream requires a higher SDK version than the current library. |
| `BINPACK_ERR_BUFFER_TOO_SMALL` | `-9` | Destination buffer capacity is insufficient. |
| `BINPACK_ERR_PATCH_MISMATCH` | `-10` | Differential patch baseline CRC32 or size mismatch. |

---

## 2. Configuration Options (`binpack_options_t`)

```c
typedef struct binpack_options_s {
    int clevel;                  /* 1 = fastest, 19 = default, 22 = ultra */
    int nb_workers;              /* Worker threads (0 = auto-detect CPU cores) */
    int chunk_size;              /* Chunk size in bytes (default: 16777216 = 16 MB) */
    int enable_micro_trial;      /* 1 = enable 32KB sample trial to prevent regression */
    binpack_filter_t filter;     /* BINPACK_FILTER_AUTO, X86, ARM32, ARM64, TRANSPOSE, BITSHUFFLE, TEXTURE_BCN */
    uint16_t transpose_stride;   /* Stride for TRANSPOSE / BITSHUFFLE (e.g. 4 for float32) */
    binpack_engine_mode_t mode;  /* BINPACK_MODE_DEFAULT, RUNTIME (fast decode), ARCHIVAL (max ratio) */
} binpack_options_t;
```

### `binpack_default_options`
```c
void binpack_default_options(binpack_options_t *opts);
```
Populates the options struct with enterprise defaults:
- `clevel = 19`
- `nb_workers = 0` (auto-detect)
- `chunk_size = 16 MB`
- `enable_micro_trial = 1`
- `filter = BINPACK_FILTER_AUTO`
- `transpose_stride = 4`
- `mode = BINPACK_MODE_DEFAULT`

---

## 3. Buffer In-Memory API

### `binpack_compress_buffer`
```c
binpack_status_t binpack_compress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    const binpack_options_t *opts
);
```
Compresses an in-memory memory slice into a newly allocated BinPack V2 buffer.  
**Caller contract**: The caller must free `*dst` using `binpack_free()`.

### `binpack_decompress_buffer`
```c
binpack_status_t binpack_decompress_buffer(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz
);
```
Decompresses a BinPack buffer (`BPK2` or legacy `BINP`) with strict checksum verification.  
**Caller contract**: The caller must free `*dst` using `binpack_free()`.

### `binpack_decompress_buffer_ex`
```c
binpack_status_t binpack_decompress_buffer_ex(
    const void *src, size_t src_sz,
    void **dst, size_t *dst_sz,
    int verify_checksum
);
```
Extended high-throughput in-memory buffer decompressor.
* `verify_checksum = 1`: Performs full chunk CRC32 integrity verification (standard safety mode, ~500–700 MB/s).
* `verify_checksum = 0`: Fast-path zero-copy mode bypassing CRC recalculation for ultra-low latency runtime asset loading in game engines, delivering **650–1,060 MB/s** throughput.  
**Caller contract**: The caller must free `*dst` using `binpack_free()`.

### `binpack_free`
```c
void binpack_free(void *ptr);
```
Frees memory allocated by `binpack_compress_buffer`, `binpack_decompress_buffer`, `binpack_create_patch`, or `binpack_apply_patch`.

---

## 4. File-Based Streaming API

### `binpack_compress_file`
```c
binpack_status_t binpack_compress_file(
    const char *in_path,
    const char *out_path,
    const binpack_options_t *opts,
    binpack_progress_fn progress,
    void *user_data
);
```
Compresses a file on disk using bounded-memory streaming chunk framing. RAM consumption is strictly bounded by `chunk_size` regardless of input file size (1 MB to 100+ GB).

### `binpack_decompress_file`
```c
binpack_status_t binpack_decompress_file(
    const char *in_path,
    const char *out_path,
    binpack_progress_fn progress,
    void *user_data
);
```
Decompresses an archive file on disk using streaming chunk framing and bounded memory.

---

## 5. Metadata Query API

### `binpack_query_file_info`
```c
binpack_status_t binpack_query_file_info(
    const char *path,
    binpack_file_info_t *info
);
```
Inspects the archive container header and footer without performing full decompression. Populates `uncompressed_size`, `compressed_size`, `chunk_count`, `original_crc32`, and format version.

---

## 6. Pre-trained Dictionary API (Micro-Telemetry & CAN-bus)

Designed for packet sizes under 512 bytes (CAN-bus frames, sensor telemetry, IoT payloads) where container framing is avoided:

```c
// Train dictionary on representative samples
binpack_dict_t* binpack_train_dict(
    const void *samples,
    const size_t *sample_sizes,
    size_t sample_count,
    size_t dict_capacity
);

// Compress single packet using pre-trained dictionary
binpack_status_t binpack_compress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *compressed_sz,
    const binpack_dict_t *dict
);

// Decompress single packet using dictionary
binpack_status_t binpack_decompress_dict(
    const void *src, size_t src_sz,
    void *dst, size_t dst_cap,
    size_t *decompressed_sz,
    const binpack_dict_t *dict
);

// Free dictionary handle
void binpack_free_dict(binpack_dict_t *dict);
```

---

## 7. Binary Differential Patching API (OTA Firmware & Assets)

Generates micro-patches for software/firmware updates by combining architecture-aware branch normalization with prefix differential matching:

```c
// Create patch from baseline old_buf to target new_buf
binpack_status_t binpack_create_patch(
    const void *old_buf, size_t old_sz,
    const void *new_buf, size_t new_sz,
    void **patch_out, size_t *patch_sz,
    const binpack_options_t *opts
);

// Apply patch to reconstruct new_buf from baseline old_buf
binpack_status_t binpack_apply_patch(
    const void *old_buf, size_t old_sz,
    const void *patch_buf, size_t patch_sz,
    void **new_out, size_t *new_sz
);
```

---

## 8. Seekable Random-Access Archive API (Virtual Texturing & Streaming)

Enables instantaneous random-access reading of individual chunks from multi-gigabyte packages without unpacking the entire file:

```c
// Open archive for random access queries
binpack_archive_t* binpack_archive_open(const char *filepath);

// Query chunk count and total uncompressed size
uint32_t binpack_archive_get_chunk_count(const binpack_archive_t *archive);
uint64_t binpack_archive_get_total_uncompressed_size(const binpack_archive_t *archive);

// Query metadata of a specific chunk
binpack_status_t binpack_archive_get_chunk_info(
    const binpack_archive_t *archive,
    uint32_t chunk_idx,
    binpack_chunk_info_t *info
);

// Decompress single chunk directly into memory buffer
binpack_status_t binpack_archive_decompress_chunk(
    binpack_archive_t *archive,
    uint32_t chunk_idx,
    void *dst, size_t dst_capacity,
    size_t *decompressed_sz
);

// Close archive handle
void binpack_archive_close(binpack_archive_t *archive);
```
