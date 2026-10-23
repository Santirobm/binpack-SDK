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
- `filter = BINPACK_FILTER_AUTO`
- `transpose_stride = 4`

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
Frees memory allocated by `binpack_compress_buffer` or `binpack_decompress_buffer`.

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
