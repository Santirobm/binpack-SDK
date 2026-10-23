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
|  - Parallel execution via worker threads                                          |
|  - CRC32 verification per chunk & global stream                                   |
+-----------------------------------------------------------------------------------+
                     |                                           |
                     v                                           v
+----------------------------------------+  +---------------------------------------+
|  MICRO-TRIAL EVALUATOR (binpack_eval.c)|  | ARCHITECTURAL PREFILTERS (core.c)    |
|  - Fast 32-64 KB sample evaluation     |  | - PE/ELF .text section bounds parser  |
|  - Compares compressed size vs raw     |  | - BCJ x86/x64 call/jmp displacement   |
|  - Rejects filter if gain <= 0%        |  | - BCJ ARM32 (Thumb-2 & ARM mode)      |
|  - Zero-Regression Guarantee           |  | - BCJ AArch64 (ARM64 26-bit branches) |
|                                        |  | - Stride-N Transpose + Delta-1        |
+----------------------------------------+  +---------------------------------------+
                                         |
                                         v
+-----------------------------------------------------------------------------------+
|               ENTROPY & LZ COMPRESSION BACKEND (libzstd / RFC 8878)               |
|                       Finite State Entropy (FSE) + LZ77                           |
+-----------------------------------------------------------------------------------+
```

---

## 2. Bounded Memory Streaming & Chunk Framing (BPK2)

Traditional compression libraries frequently rely on monolithic allocation (`malloc(file_size)`), which causes Out-Of-Memory (OOM) aborts when operating on multi-gigabyte installer packages, game pak files, or disk images.

BinPack Enterprise SDK solves this via **Bounded-Memory Chunk Framing (`BPK2`)**:
1. Files and network streams are partitioned into independent chunks (configurable from 64 KB up to 64 MB, defaulting to 16 MB).
2. Each chunk is analyzed, prefiltered, and compressed in isolation.
3. Maximum memory consumption is bounded by $\mathcal{O}(\text{chunk\_size})$, never exceeding 64 MB RAM even when processing a 100 GB payload.
4. Decompression requires only the memory necessary for one uncompressed chunk at a time, allowing deployment on resource-constrained embedded systems and IoT microcontrollers.

### Binary Framing Protocol Specification

```
[ STREAM HEADER (16 bytes) ]
  uint32_t  magic;             /* 0x324B5042 ('BPK2') */
  uint8_t   version_major;     /* 2 */
  uint8_t   version_minor;     /* 0 */
  uint16_t  flags;             /* Bit 0: Checksum enabled */
  uint32_t  default_chunk_sz;  /* e.g., 16777216 (16 MB) */
  uint32_t  reserved;          /* 0 */

[ CHUNK 1 (Variable) ]
  uint32_t  chunk_magic;       /* 0x4B4E4843 ('CHNK') */
  uint32_t  uncompressed_sz;   /* e.g., 16777216 */
  uint32_t  compressed_sz;     /* e.g., 6842104 */
  uint8_t   filter_id;         /* 1=None, 2=x86, 3=ARM32, 4=AArch64, 5=Transpose */
  uint8_t   filter_flags;      /* Bit 0: Section bounds subheader follows */
  uint16_t  filter_param;      /* Stride / Architecture flags */
  uint32_t  chunk_crc32;       /* CRC32 of uncompressed chunk */
  uint32_t  reserved;          /* 0 */
  [ Optional Bounds Subheader (8 bytes if Bit 0 set) ]
    uint32_t code_offset;      /* Offset of .text section within chunk */
    uint32_t code_length;      /* Length of .text section */
  [ Compressed Payload (compressed_sz bytes) ]

[ CHUNK 2 ... N ]

[ STREAM FOOTER (28 bytes) ]
  uint32_t  footer_magic;      /* 0x324B5046 ('FPK2') */
  uint64_t  total_uncompressed;/* 64-bit total payload bytes */
  uint64_t  total_compressed;  /* 64-bit total stream bytes */
  uint32_t  chunk_count;       /* Total number of chunks */
  uint32_t  stream_crc32;      /* CRC32 of complete uncompressed stream */
```

---

## 3. Micro-Trial Evaluator & Zero-Regression Guarantee

In production systems, automated prefilters can occasionally penalize already-compressed, encrypted, or heavily obfuscated data (UPX, Themida, DRM) by introducing perturbations that increase entropy.

The Micro-Trial evaluator guarantees that BinPack **never produces an archive larger than raw Zstandard**:
1. Before committing to a prefilter on a chunk, an evaluation window of 32 KB to 64 KB is sampled.
2. An ultra-fast single-pass trial compression is evaluated on both the raw sample ($C_{raw}$) and the transformed sample ($C_{trans}$).
3. The filter is accepted if and only if:
   $$\Delta C = C_{raw} - C_{trans} > 0$$
4. If $\Delta C \le 0$, the filter is immediately rejected, and the chunk is tagged as `BINPACK_FILTER_NONE` (pass-through).

---

## 4. Reversible Context Preconditioning

### Section-Aware Code Bounds Detection
Executable binaries contain mixed content: code (`.text`), read-only data (`.rdata`), icons/resources (`.rsrc`), and debug symbols. Applying branch filters across non-code regions causes false positives.
BinPack dynamically parses PE32/PE32+ and ELF32/ELF64 headers to isolate the exact byte interval $[ \text{offset}, \text{offset} + \text{length} ]$ corresponding to executable machine code.

### Normalization Transforms
- **x86/x64**: SWAR 64-bit branchless scanning transforms relative $CALL$ (`0xE8`) and $JMP$ (`0xE9`) displacements into absolute instruction pointers:
  $$\text{addr}_{abs} = \text{disp}_{rel} + \text{offset}_{PC} + 5$$
- **ARM32 Thumb-2**: Normalizes 32-bit paired branch-link (`BL`/`BLX`) instructions.
- **AArch64 (ARM64)**: Normalizes 26-bit immediate branch targets (`0x14000000` / `0x94000000` opcodes).
- **Bitshuffle ($8 \times 8$ Bit Matrix Transposition)**: Pure bit-level matrix transposition for multi-byte numerical elements (e.g., IEEE 754 float32/float64). Reorganizes bits so identical sign and exponent bits collapse into continuous byte runs of `0x00` / `0xFF`.
- **GPU Texture Block Decoupling (BC1-BC7 & ASTC)**: Partitions $4 \times 4$ pixel texture blocks into isolated endpoint color streams and interpolation index streams with Delta-4 inter-block filtering.
- **Stride-N Transpose + Delta**: Separates multi-byte numerical elements into discrete byte channels, followed by first-order difference encoding on high-order exponent lanes.
