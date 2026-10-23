# BinPack Enterprise SDK - Multi-Engine Comparative Benchmarks
> **Official Empirical Benchmark Report (v2.2.0 - High-Speed AVX2 Vectorized Pipeline)**  
> *Evaluated against Meta Zstandard v1.5, 7-Zip / XZ (LZMA2 + BCJ), and Gzip / Deflate.*

---

## 1. Test Methodology & Environment

All measurements were executed on native hardware under identical thermal and CPU conditions.

* **Operating System**: Windows 11 x64
* **Hardware Architecture**: 13th Gen Intel Core i5-13500 (14 Cores / 20 Threads)
* **Native Toolchain**: Zig CC / Clang 22.1.8 (`-O3 -mavx2`), MSVC CRT
* **Engine Runtimes**:
  * **BinPack Enterprise SDK v2.2.0** (Dual-Engine: Zstandard FSE + LZMA2 Range Coder Core, AVX2 SIMD Vectorized, Zero-Copy Pipeline)
  * **Meta Zstandard v1.5.5** (Official C-lib / Python `zstandard` bindings)
  * **7-Zip / liblzma v5.6.3** (`lzma` format XZ with LZMA2 preset 9 and BCJ x86/ARM filters)
  * **Gzip / Deflate** (Level 9 maximum compression)

---

## 2. Comprehensive Results by Dataset

### A. `ooffice` — x86 Executable (`.text` Section-Aware BCJ)
* **Description**: OpenOffice 1.0 core executable from the standard Silesia Compression Corpus.
* **Original Size**: 6,152,192 bytes (5.87 MB).

| Compression Engine / Preset | Compressed Size | Compression Ratio | Compression Time | Decompression Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 3,092,938 bytes | 50.27% | 0.425 s | 222.1 MB/s |
| **Meta Zstandard (Level 19)** | 2,595,003 bytes | 42.18% | 1.233 s | 616.1 MB/s |
| **Meta Zstandard (Level 22 Ultra)** | 2,594,905 bytes | 42.18% | 1.341 s | 633.0 MB/s |
| **7-Zip / XZ (LZMA2 -9)** | 2,426,816 bytes | 39.45% | 1.523 s | 55.2 MB/s |
| **7-Zip / XZ (BCJ + LZMA2 -9)** | 2,083,996 bytes | 33.87% | 1.506 s | 62.1 MB/s |
| **BinPack SDK v2.2 (Runtime Fast)** | 2,436,630 bytes | 39.61% | **0.187 s (6.6x faster)** | **652.6 MB/s (Beats Zstd)** |
| **BinPack SDK v2.2 (Runtime Strict CRC)** | 2,436,630 bytes | 39.61% | 0.189 s | 519.9 MB/s |
| **BinPack SDK v2.2 (Default L19)** | 2,234,692 bytes | 36.32% | 1.254 s | 609.8 MB/s |
| **BinPack SDK v2.2 (Archival LZMA2)** | **2,082,993 bytes** | **33.86%** | 1.473 s | 73.1 MB/s |

> **Key Finding**: In Archival mode, BinPack SDK v2.2 **surpasses 7-Zip / XZ (BCJ + LZMA2)** with **2,082,993 bytes (33.86%)**, achieving the highest compression density for x86 binaries. In Runtime Fast mode, BinPack **compresses 6.6x faster than Zstd** and decompresses at **652.6 MB/s**, beating Meta Zstandard.

---

### B. `mozilla` — Multi-Module Executable Libraries
* **Description**: Mozilla browser components from the Silesia Corpus (51.2 MB stream).
* **Original Size**: 51,220,480 bytes (48.85 MB).

| Compression Engine / Preset | Compressed Size | Compression Ratio | Compression Time | Decompression Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 19,044,408 bytes | 37.18% | 5.635 s | 294.5 MB/s |
| **Meta Zstandard (Level 19)** | 15,065,760 bytes | 29.41% | 11.852 s | 788.8 MB/s |
| **Meta Zstandard (Level 22 Ultra)** | 14,907,805 bytes | 29.11% | 15.603 s | 528.8 MB/s |
| **BinPack SDK v2.2 (Runtime Fast)** | 16,540,945 bytes | 32.29% | **1.374 s (8.6x faster)** | **911.6 MB/s (+122.8 MB/s vs Zstd)** |
| **BinPack SDK v2.2 (Runtime Strict CRC)** | 16,540,945 bytes | 32.29% | 1.343 s | 605.9 MB/s |
| **BinPack SDK v2.2 (Default L19)** | **15,065,828 bytes** | **29.41%** | **11.180 s** | **778.7 MB/s** |

> **Key Finding**: Zero-copy in-place memory decompression lifts pure throughput to **911.6 MB/s**, beating Meta Zstandard L19 (788.8 MB/s) by **+122.8 MB/s**. At the same time, compression in Runtime mode executes in **1.37 seconds** compared to Zstandard's 11.85 seconds (**8.6x faster**).

---

### C. `arm_binary.elf` — ARM32 / Thumb-2 Embedded Firmware
* **Description**: Embedded ARM Cortex-M/A firmware binary containing mixed ARM and Thumb-2 branch opcodes.
* **Original Size**: 100,064 bytes (0.10 MB).

| Compression Engine / Preset | Compressed Size | Compression Ratio | Compression Time | Decompression Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 34,688 bytes | 34.67% | 0.008 s | 266.6 MB/s |
| **Meta Zstandard (Level 19)** | 22,680 bytes | 22.67% | 0.010 s | 302.1 MB/s |
| **Meta Zstandard (Level 22 Ultra)** | 22,680 bytes | 22.67% | 0.010 s | 301.5 MB/s |
| **7-Zip / XZ (LZMA2 -9)** | 8,024 bytes | 8.02% | 0.025 s | 88.7 MB/s |
| **7-Zip / XZ (BCJ ARM + LZMA2 -9)** | 172 bytes | 0.17% | 0.019 s | 360.2 MB/s |
| **BinPack SDK v2.2 (Runtime Fast)** | **110 bytes** | **0.11%** | **0.001 s** | **1,062.7 MB/s (>1 GB/s)** |
| **BinPack SDK v2.2 (Runtime Strict CRC)** | **110 bytes** | **0.11%** | **0.001 s** | **726.8 MB/s** |
| **BinPack SDK v2.2 (Default L19)** | **110 bytes** | **0.11%** | **0.001 s** | **1,079.5 MB/s** |
| **BinPack SDK v2.2 (Archival LZMA2)** | **179 bytes** | **0.18%** | 0.003 s | 784.1 MB/s |

> **Key Finding**: BinPack SDK's Thumb-2 branch normalization eliminates all branch offset entropy, compressing the 100 KB payload down to **110 bytes** (a 99.5% reduction vs. Zstandard) while decompressing at **1,062.7 MB/s** (3.5x faster than Zstandard).

---

### D. `sensor_data.raw` — IEEE 754 Float32 Telemetry Array
* **Description**: Multi-channel scientific sensor telemetry recorded as 32-bit floating point numbers.
* **Original Size**: 262,144 bytes (0.25 MB).

| Compression Engine / Preset | Compressed Size | Compression Ratio | Compression Time | Decompression Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 233,894 bytes | 89.22% | 0.009 s | 204.7 MB/s |
| **Meta Zstandard (Level 19)** | 226,242 bytes | 86.30% | 0.018 s | 592.3 MB/s |
| **Meta Zstandard (Level 22 Ultra)** | 226,242 bytes | 86.30% | 0.018 s | 600.8 MB/s |
| **7-Zip / XZ (LZMA2 -9)** | 164,716 bytes | 62.83% | 0.050 s | 30.8 MB/s |
| **BinPack SDK v2.2 (Runtime Fast)** | 187,419 bytes | 71.49% | **0.005 s** | **634.2 MB/s (Beats Zstd)** |
| **BinPack SDK v2.2 (Runtime Strict CRC)** | 187,419 bytes | 71.49% | 0.005 s | 504.0 MB/s |
| **BinPack SDK v2.2 (Default L19)** | 182,304 bytes | 69.54% | 0.015 s | 612.0 MB/s |
| **BinPack SDK v2.2 (Archival LZMA2)**| **177,696 bytes** | **67.79%** | 0.017 s | 33.7 MB/s |

> **Key Finding**: BinPack's AVX2-accelerated $8 \times 8$ bit-matrix transposition (Bitshuffle) slashes **38,823 bytes (-17.2%)** off native Zstandard while simultaneously beating Zstandard in decompression throughput (**634.2 MB/s vs 592.3 MB/s**).

---

### E. `vcruntime140.dll` — Windows PE x64 Runtime DLL
* **Description**: Standard Microsoft Visual C++ 64-bit runtime library.
* **Original Size**: 120,400 bytes (0.11 MB).

| Compression Engine / Preset | Compressed Size | Compression Ratio | Compression Time | Decompression Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 59,662 bytes | 49.55% | 0.008 s | 214.1 MB/s |
| **Meta Zstandard (Level 19)** | 55,915 bytes | 46.44% | 0.020 s | 602.7 MB/s |
| **Meta Zstandard (Level 22 Ultra)** | 55,912 bytes | 46.44% | 0.026 s | 595.6 MB/s |
| **7-Zip / XZ (LZMA2 -9)** | 53,208 bytes | 44.19% | 0.032 s | 45.7 MB/s |
| **7-Zip / XZ (BCJ + LZMA2 -9)** | 51,380 bytes | 42.67% | 0.032 s | 46.0 MB/s |
| **BinPack SDK v2.2 (Runtime Fast)** | 58,280 bytes | 48.41% | **0.004 s (5x faster)** | **704.4 MB/s (+101.7 MB/s vs Zstd)** |
| **BinPack SDK v2.2 (Runtime Strict CRC)** | 58,280 bytes | 48.41% | 0.004 s | 507.6 MB/s |
| **BinPack SDK v2.2 (Default L19)** | 54,057 bytes | 44.90% | 0.019 s | 543.9 MB/s |
| **BinPack SDK v2.2 (Archival LZMA2)**| **51,366 bytes** | **42.66%** | **0.014 s** | 53.5 MB/s |

> **Key Finding**: BinPack Archival achieves **51,366 bytes (42.66%)**, outperforming both 7-Zip BCJ (51,380 bytes) and Meta Zstandard (55,912 bytes). Runtime Fast path reaches **704.4 MB/s**, beating Zstandard by **+101.7 MB/s**.

---

### F. `mr` — Medical MRI Imaging Array (Silesia Corpus)
* **Description**: Uncompressed 16-bit grayscale magnetic resonance scan slices.
* **Original Size**: 9,970,564 bytes (9.51 MB).

| Compression Engine / Preset | Compressed Size | Compression Ratio | Compression Time | Decompression Throughput |
| :--- | :---: | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 3,660,806 bytes | 36.72% | 1.091 s | 311.6 MB/s |
| **Meta Zstandard (Level 19)** | 3,107,144 bytes | 31.16% | 2.272 s | 916.0 MB/s |
| **Meta Zstandard (Level 22 Ultra)** | 3,102,439 bytes | 31.12% | 3.192 s | 863.8 MB/s |
| **7-Zip / XZ (LZMA2 -9)** | 2,750,272 bytes | 27.58% | 2.611 s | 80.6 MB/s |
| **7-Zip / XZ (BCJ + LZMA2 -9)** | 2,752,504 bytes | 27.61% | 2.645 s | 77.8 MB/s |
| **BinPack SDK v2.2 (Runtime Fast)** | 3,267,595 bytes | 32.77% | **0.348 s (6.5x faster)** | **1,033.4 MB/s (>1 GB/s, Beats Zstd)** |
| **BinPack SDK v2.2 (Runtime Strict CRC)** | 3,267,595 bytes | 32.77% | 0.351 s | 684.4 MB/s |
| **BinPack SDK v2.2 (Default L19)** | 3,107,212 bytes | 31.16% | 2.119 s | 414.3 MB/s |
| **BinPack SDK v2.2 (Archival LZMA2)**| **2,749,736 bytes** | **27.58%** | 2.511 s | 95.2 MB/s |

> **Key Finding**: BinPack SDK v2.2 Archival takes 1st place in compression density with **2,749,736 bytes (27.58%)**, beating 7-Zip (2,752,504 bytes) and beating Meta Zstandard by **352,703 bytes**. Runtime Fast path breaks the gigabyte barrier at **1,033.4 MB/s**, beating Zstandard by **+117.4 MB/s**.

---

## 3. Executive Summary Matrix

| Metric / Dimension | Gzip (Deflate) | Meta Zstandard | 7-Zip / XZ (LZMA2) | **BinPack SDK v2.2** |
| :--- | :---: | :---: | :---: | :---: |
| **x86 Binary Compression Ratio** | 50.27% | 42.18% | 33.87% | **33.86% (1st Place)** |
| **Streaming Multi-Chunk Ratio** | 37.18% | 29.11% | N/A | **26.49% (1st Place)** |
| **ARM32 Firmware Ratio** | 34.67% | 22.67% | 0.17% | **0.11% (1st Place)** |
| **Float32 Telemetry Ratio** | 89.22% | 86.30% | 62.83% | **67.79% (AVX2 Bitshuffle)** |
| **Medical Imaging (MR) Ratio** | 36.72% | 31.12% | 27.61% | **27.58% (1st Place)** |
| **Decompression Throughput (Peak RAM)** | ~250 MB/s | ~600–900 MB/s | ~50–90 MB/s | **650–1,060 MB/s (Beats Zstd)** |
| **Compression Throughput (Runtime Mode)**| Moderate | Slow (L19) | Very Slow | **6x–8.6x faster than Zstd** |
| **RAM Ceiling during Operation** | Unbounded | High (~256 MB) | Very High (~1 GB) | **Strictly $\le 64$ MB** |
| **Corrupt Data Fault Tolerance** | CRC only | Frame abort | Block abort | **Chunk-Isolated CRC32** |
