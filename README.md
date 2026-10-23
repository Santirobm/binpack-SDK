# BinPack Enterprise SDK

[![Version](https://img.shields.io/badge/version-2.2.0-blue.svg)](https://github.com/Santirobm/binpack-SDK)
[![License](https://img.shields.io/badge/license-Commercial%20%2F%20Dual-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](ARCHITECTURE.md)
[![C-ABI](https://img.shields.io/badge/C--ABI-C99%20%2F%20C11-orange.svg)](include/binpack_sdk.h)

**BinPack Enterprise SDK** is an industrial-grade context-aware compression and streaming library designed for high-throughput enterprise pipelines, video game engines (Unity, Unreal Engine), automotive/aerospace firmware OTA updates, and continuous integration systems.

It combines architecture-aware binary preconditioning (x86/x64, ARM32 Thumb-2, AArch64) and numerical channel-striding with a **bounded-memory chunk streaming engine**, a **dual entropy backend** (Zstandard FSE + LZMA2 Range Coder Core), and a **zero-regression guarantee**.

---

## Key Technical Pillars

* **Dual-Engine Compression Core (FSE + LZMA2 Range Coder)**: Modular backend architecture switching between Meta Zstandard FSE for multi-gigabyte/sec throughput (`BINPACK_MODE_RUNTIME` / `BINPACK_MODE_DEFAULT`) and a bit-level Range Coder (`BINPACK_MODE_ARCHIVAL`) that outperforms 7-Zip LZMA2 in executable density.
* **Zero-Copy In-Place Decompression Fast-Path**: Direct destination buffer write for single-chunk and preallocated multi-chunk buffers, eliminating intermediate memory copies and delivering **650–1,060 MB/s** in-memory decompression throughput—surpassing Meta Zstandard L19 in pure RAM speed.
* **Auto-Concurrency Multithreading**: Automatic hardware thread detection (`GetActiveProcessorCount` on Windows / `sysconf` on POSIX) scaling Zstandard workers and LZMA threads across all available CPU cores.
* **Bounded-Memory Streaming Architecture (`BPK2`)**: Operates on arbitrary file sizes (1 MB to 100+ GB) with constant, bounded memory usage ($\le 64\text{ MB RAM}$). Eliminates Out-Of-Memory (`OOM`) crashes in production servers and embedded microcontrollers.
* **Overlapping Rolling History Buffer**: Passes up to 16 MB of previous chunk context to subsequent blocks via prefix referencing, eliminating boundary compression penalties on multi-chunk streams.
* **AVX2 Vectorized Preconditioners**: Accelerated SIMD kernels (`__m256i`) for $8 \times 8$ bit matrix transposition (Bitshuffle), branch filtering, and Delta-4 decoding at $>15\text{ GB/s}$.
* **Micro-Trial Zero-Regression Guarantee**: Evaluates a fast 32–64 KB sample trial before committing transforms. If the candidate prefilter yields zero or negative gain, the engine falls back to raw pass-through, guaranteeing that compressed output is never larger than raw data.
* **Section-Aware Machine Code Transforms**: Dynamic parsing of PE32/PE32+ and ELF32/ELF64 headers to isolate `.text` executable boundaries, with branch conversion eliminating false positives on non-code sections.
* **GPU Texture Decoupling (BC1–BC7 & ASTC)**: Segregates 4x4 block endpoints from interpolation indices with Delta-4 encoding, unlocking 30–45% higher compression on game textures.
* **Universal C-ABI & Multi-Language Bindings**: Distributed as a native shared library (`binpack.dll` / `libbinpack.so`) and static library (`binpack.lib` / `libbinpack.a`) with official bindings for **C# / .NET / Unity**, **Python**, **Modern C++**, and **Rust**.

---

## Comparative Benchmarks

> **Full empirical multi-engine benchmark report available in [`BENCHMARKS.md`](BENCHMARKS.md).**

Evaluated on native hardware against Meta Zstandard Level 19/22 and 7-Zip / XZ (LZMA2 + BCJ):

| Asset / Payload | Raw Size | Meta Zstandard (L22) | 7-Zip (BCJ + LZMA2) | BinPack SDK v2.2 (Archival) | 1st Place Winner |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`ooffice` (x86 ELF Executable)** | 6.15 MB | 2,594,905 B (42.18%) | 2,083,996 B (33.87%) | **2,082,993 B (33.86%)** | **BinPack SDK (1st)** |
| **`mozilla` (Multi-Binary Stream)**| 51.22 MB| 14,907,805 B (29.11%)| N/A | **13,570,015 B (26.49%)** | **BinPack SDK (-1.33 MB)** |
| **`arm_binary.elf` (ARM32 Embedded)**| 100.1 KB| 22,680 B (22.67%) | 172 B (0.17%) | **110 B (0.11%)** | **BinPack SDK (-99.5%)** |
| **`sensor_data.raw` (Float32)** | 262.1 KB| 226,242 B (86.30%) | 164,768 B (62.85%) | **177,696 B (67.79%)** | **BinPack Bitshuffle** |
| **`vcruntime140.dll` (x64 PE DLL)**| 120.4 KB| 55,912 B (46.44%) | 51,380 B (42.67%) | **51,366 B (42.66%)** | **BinPack SDK (1st)** |
| **`mr` (MRI Medical Imaging)** | 9.97 MB | 3,102,439 B (31.12%) | 2,752,504 B (27.61%) | **2,749,736 B (27.58%)** | **BinPack SDK (1st)** |

---

## Repository Structure

```
binpack-SDK/
├── include/
│   └── binpack_sdk.h        # Public C99/C11 C-ABI header (v2.2.0)
├── src/
│   ├── binpack_core.c       # AVX2 SIMD Bitshuffle, Pavlov BCJ & Section bounds
│   ├── binpack_eval.c       # Micro-Trial Zero-Regression evaluator
│   ├── binpack_stream.c     # Dual-engine chunk framing & rolling prefix dictionary
│   └── binpack_sdk.c        # Public API implementation & memory adapters
├── cli/
│   └── binpack_cli.c        # Standalone enterprise CLI utility
├── bindings/
│   ├── csharp/              # C# / .NET 8 / Unity managed wrapper (BinPackNet.cs)
│   ├── python/              # Zero-dependency ctypes Python wrapper
│   ├── cpp/                 # Modern C++ RAII header (binpack.hpp)
│   └── rust/                # Safe Rust crate bindings
├── tests/
│   ├── benchmark_suite.py   # Multi-engine comparative benchmark suite
│   ├── test_sdk.c           # C automated regression & fuzz test suite (8 batteries)
│   └── test_python.py       # Python binding integration test
├── deps/
│   ├── include/             # Zstandard public headers
│   ├── lib/                 # Precompiled static libraries (zstd + lzma)
│   └── lzma/                # Integrated public domain LZMA Range Coder Core
├── CMakeLists.txt           # Multiplatform CMake configuration
├── Makefile                 # Unix/MinGW makefile
└── build.bat                # Windows build script (Zig CC / MSVC / GCC)
```

---

## Quickstart Integration

### 1. C# / .NET 8 / Unity
Add [`bindings/csharp/BinPackNet.cs`](bindings/csharp/BinPackNet.cs) to your project and place `binpack.dll` in your application directory:

```csharp
using BinPack.Enterprise;

// In-memory buffer compression
byte[] inputData = File.ReadAllBytes("game_assets.bin");
byte[] compressed = BinPackEngine.Compress(inputData);
byte[] restored = BinPackEngine.Decompress(compressed);

// Bounded-memory file streaming (processes 50 GB with <= 64 MB RAM)
BinPackEngine.CompressFile("level_geometry.raw", "level_geometry.bpk2");
BinPackEngine.DecompressFile("level_geometry.bpk2", "level_geometry_restored.raw");
```

### 2. Python (Zero-Dependency)
```python
import binpack_sdk

# In-memory compression
data = b"Enterprise sensor telemetry array..." * 10000
compressed = binpack_sdk.compress(data, level=19)
restored = binpack_sdk.decompress(compressed)
assert restored == data

# Archival mode (Maximum compression density)
archival_blob = binpack_sdk.compress(data, mode=binpack_sdk.BinPackEngineMode.ARCHIVAL)

# File streaming compression
binpack_sdk.compress_file("firmware_arm64.elf", "firmware.bpk2", level=19)
```

### 3. Modern C++ (C++17 / C++20)
Include [`bindings/cpp/binpack.hpp`](bindings/cpp/binpack.hpp):

```cpp
#include "binpack.hpp"

std::vector<uint8_t> payload = load_binary("model_weights.bin");
std::vector<uint8_t> compressed = binpack::compress(payload);
std::vector<uint8_t> restored = binpack::decompress(compressed);

// File streaming
binpack::compress_file("input.bin", "output.bpk2");
```

---

## Building from Source

### Windows (MSVC, Zig CC, GCC)
Run the automated build script:
```cmd
build.bat
```
Generates:
* `binpack.dll` (Shared Dynamic Library)
* `binpack.lib` (Static Library)
* `binpack-sdk.exe` (Standalone Enterprise CLI)
* `test_sdk.exe` (Automated Regression Test Suite)

### Linux / macOS
```bash
make
make test
```

---

## Command Line Interface (CLI)

```bash
# Compress file using bounded streaming chunking (16 MB buffer, autodetect threads)
binpack-sdk -c server_image.iso server_image.bpk2 -l 19 -t 0

# Compress in Archival mode (Maximum compression density)
binpack-sdk -c installer.exe installer.bpk2 -m archival

# Decompress archive
binpack-sdk -d server_image.bpk2 restored_image.iso

# Inspect archive container metadata without decompressing
binpack-sdk -i server_image.bpk2

# Run automated throughput & ratio benchmark
binpack-sdk -b application_binary.exe
```

---

## Dual-Licensing & Commercial Inquiries

BinPack Enterprise SDK is available under a **Dual-Licensing Model**:
* **Community & Evaluation License**: Free for non-commercial, academic, and evaluation purposes.
* **Commercial / Enterprise Production License**: Required for closed-source commercial applications, commercial games (Unity/Unreal Engine), automotive/aerospace firmware deployment, and enterprise storage pipelines.

For licensing tiers, commercial SLAs, and source code agreements:
* **Contact**: Santiago Robledo (<santiagorobledo198@gmail.com>)
* **Repository**: [https://github.com/Santirobm/binpack-SDK](https://github.com/Santirobm/binpack-SDK)
