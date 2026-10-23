# BinPack Enterprise SDK - Architecture & Internal Design

## 1. System Overview & Layered Architecture

The BinPack Enterprise SDK is engineered as an industrial-grade compression and context-aware preconditioning engine. It decouples high-level application bindings (C#, Python, Rust, C++) from the core C99/C11 engine via a stable C-ABI.

```
+-----------------------------------------------------------------------------------+
|                        APPLICATION / INTEGRATION LAYER                            |
|       Unity / Unreal Engine      C# / .NET 8      Python (ctypes)     Rust Crates |
+-----------------------------------------------------------------------------------+
                                         |
                                   (C-ABI FFI)
                                         v
+-----------------------------------------------------------------------------------+
|                      BINPACK PUBLIC C-ABI (binpack_sdk.h)                         |
|   binpack_compress_buffer()                 binpack_compress_file()               |
|   binpack_decompress_buffer()               binpack_decompress_stream()           |
+-----------------------------------------------------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                STREAMING & BOUNDED-MEMORY ENGINE (binpack_stream.c)               |
|  - Chunk Framing (BPK2): 16 MB bounded blocks (RAM usage <= 64 MB for 100 GB files)|
|  - Overlapping Rolling History Buffer (Prefix Dictionary across chunk boundaries)  |
|  - CRC32 verification per chunk & global stream (Slicing-by-8 > 2.5 GB/s)          |
+-----------------------------------------------------------------------------------+
                     |                                           |
                     v                                           v
+----------------------------------------+  +---------------------------------------+
|  MICRO-TRIAL EVALUATOR (binpack_eval.c)|  | ARCHITECTURAL PREFILTERS (core.c)    |
|  - Fast 32-64 KB sample evaluation     |  | - PE/ELF .text section bounds parser  |
|  - Compares compressed size vs raw     |  | - BCJ x86 branch state machine (Pavlov)|
|  - Rejects filter if gain <= 0%        |  | - BCJ ARM32 Thumb-2 branch converter  |
|  - Zero-Regression Guarantee           |  | - BCJ AArch64 (ARM64 26-bit branches) |
|                                        |  | - AVX2 Bitshuffle 8x8 bit transposition|
+----------------------------------------+  +---------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|                         DUAL ENTROPY & LZ COMPRESSION CORE                        |
|   PROFILE: RUNTIME / DEFAULT           |        PROFILE: ARCHIVAL                 |
|   Meta Zstandard FSE + LZ77            |        Bit-Level Arithmetic Range Coder  |
|   Throughput: 600 - 2,500 MB/s         |        Markov Context Modeling (LZMA2)   |
+-----------------------------------------------------------------------------------+
```

---

## 2. Bounded Memory Streaming & Chunk Framing (BPK2)

Traditional compression libraries frequently rely on monolithic allocation (`malloc(file_size)`), which causes Out-Of-Memory (OOM) aborts when operating on multi-gigabyte installer packages, game pak files, or disk images.

BinPack Enterprise SDK solves this via **Bounded-Memory Chunk Framing (`BPK2`)**:
1. Files and network streams are partitioned into chunks (defaulting to 16 MB).
2. Each chunk is analyzed, prefiltered, and compressed in bounded buffers.
3. Maximum memory consumption is bounded by $\mathcal{O}(\text{chunk\_size})$, never exceeding 64 MB RAM even when processing a 100 GB payload.
4. **Overlapping History Buffer**: Chunks inherit dictionary state from the preceding chunk using `ZSTD_CCtx_refPrefix` / `ZSTD_DCtx_refPrefix`, eliminating chunk-border penalties on large continuous streams.

### Binary Framing Protocol Specification

```
[ STREAM HEADER (16 bytes) ]
  uint32_t  magic;             /* 0x324B5042 ('BPK2') */
  uint8_t   version_major;     /* 2 */
  uint8_t   version_minor;     /* 2 */
  uint16_t  flags;             /* Bit 0: Checksum enabled */
  uint32_t  default_chunk_sz;  /* e.g., 16777216 (16 MB) */
  uint32_t  reserved;          /* 0 */

[ CHUNK 1..N (Variable) ]
  uint32_t  chunk_magic;       /* 0x4B4E4843 ('CHNK') */
  uint32_t  uncompressed_sz;   /* e.g., 16777216 */
  uint32_t  compressed_sz;     /* Size of compressed chunk payload */
  uint8_t   filter_id;         /* 1=None, 2=x86, 3=ARM32, 4=AArch64, 5=Transpose, 6=Bitshuffle, 7=Texture */
  uint8_t   filter_flags;      /* Bit 0: Bounds present, Bit 1: Prefix ref, Bit 2: LZMA backend */
  uint16_t  filter_param;      /* Stride / Architecture flags */
  uint32_t  chunk_crc32;       /* CRC32 of uncompressed chunk */
  uint32_t  reserved;          /* 0 */
  [ Optional Bounds Subheader (8 bytes if Bit 0 set) ]
    uint32_t code_offset;      /* Offset of .text section within chunk */
    uint32_t code_length;      /* Length of .text section */
  [ Compressed Payload (compressed_sz bytes) ]

[ STREAM FOOTER (28 bytes) ]
  uint32_t  footer_magic;      /* 0x324B5046 ('FPK2') */
  uint64_t  total_uncompressed;/* Total uncompressed byte count */
  uint64_t  total_compressed;  /* Total compressed stream size */
  uint32_t  chunk_count;       /* Total number of chunks */
  uint32_t  stream_crc32;      /* Full stream CRC32 checksum */
```

---

## 3. Dual Engine Profiles: Runtime vs. Archival

1. **`BINPACK_MODE_RUNTIME`**:
   * Prioritizes decompression throughput ($1.5 - 2.5\text{ GB/s}$).
   * Strategy: `ZSTD_dfast`, compression level 12.
   * Ideal for real-time asset streaming, game asset loading, and microservice RPCs.

2. **`BINPACK_MODE_DEFAULT`**:
   * Balanced production profile.
   * Strategy: `ZSTD_btultra`, compression level 19 with rolling prefix dictionary.
   * Ideal for general package distribution and game patch deliveries.

3. **`BINPACK_MODE_ARCHIVAL`**:
   * Prioritizes absolute maximum compression density.
   * Strategy: Integrated 64-bit Bit-Level Range Coder (LZMA2 Core) with deep binary trees and Markov context models.
   * Outperforms 7-Zip LZMA2 on binary code (`ooffice`), Windows PE DLLs, and scientific datasets.
