# BinPack Enterprise SDK

[![Version](https://img.shields.io/badge/version-2.0.0-blue.svg)](https://github.com/Santirobm/binpack-SDK)
[![License](https://img.shields.io/badge/license-Commercial%20%2F%20Dual-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](ARCHITECTURE.md)
[![C-ABI](https://img.shields.io/badge/C--ABI-C99%20%2F%20C11-orange.svg)](include/binpack_sdk.h)

**BinPack Enterprise SDK** is an industrial-grade context-aware compression and streaming library designed for high-throughput enterprise pipelines, video game engines (Unity, Unreal Engine), automotive/aerospace firmware OTA updates, and continuous integration systems.

It combines architecture-aware binary preconditioning (x86/x64, ARM32 Thumb-2, AArch64) and numerical channel-striding with a **bounded-memory chunk streaming engine** and a **zero-regression guarantee**.

---

## Key Technical Pillars

* **Bounded-Memory Streaming Architecture (`BPK2`)**: Operates on arbitrary file sizes (1 MB to 100+ GB) with constant, bounded memory usage ($\le 64\text{ MB RAM}$). Eliminates Out-Of-Memory (`OOM`) crashes in production servers and embedded microcontrollers.
* **Micro-Trial Zero-Regression Guarantee**: Evaluates a fast 32–64 KB sample trial before committing transforms. If the candidate prefilter yields zero or negative gain, the engine falls back to raw pass-through, guaranteeing that compressed output is never larger than raw Zstandard.
* **Section-Aware Machine Code Transforms**: Dynamic parsing of PE32/PE32+ and ELF32/ELF64 headers to isolate `.text` executable boundaries, converting relative branch displacements into absolute addresses for maximum LZ77/FSE match entropy reduction.
* **Cascaded Numerical Filters**: Stride-$N$ matrix transposition paired with first-order difference (Delta-1) encoding on exponent and high-order mantissa lanes for floating-point tensores, 3D meshes, and sensor telemetry.
* **Universal C-ABI & Multi-Language Bindings**: Distributed as a native shared library (`binpack.dll` / `libbinpack.so`) and static library (`binpack.lib` / `libbinpack.a`) with official bindings for **C# / .NET / Unity**, **Python**, **Modern C++**, and **Rust**.

---

## Comparative Benchmarks

Evaluated on x64 production binaries and numerical matrices using native Zstandard Level 19 vs. BinPack Enterprise SDK v2.0:

| Asset / Payload | Raw Size | Zstandard Level 19 | BinPack SDK v2.0 | Net Reduction | Throughput (Decompress) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **`ooffice.exe` (OpenOffice Core)** | 6.15 MB | 2,741,894 B | **2,427,080 B** | **-314.8 KB (-11.5%)** | 120.4 MB/s |
| **`shell32.dll` (Windows Shell)** | 23.94 MB | 9,842,108 B | **9,721,192 B** | **-120.9 KB (-1.2%)** | 145.2 MB/s |
| **`sensor_data.raw` (Float32)** | 262.1 KB | 179,420 B | **141,180 B** | **-38.2 KB (-21.3%)** | 184.6 MB/s |
| **`binpack-sdk.exe` (CLI Binary)** | 1.15 MB | 451,120 B | **446,772 B** | **-4.3 KB (-1.0%)** | 78.2 MB/s |

---

## Repository Structure

```
binpack-SDK/
├── include/
│   └── binpack_sdk.h        # Public C99/C11 C-ABI header
├── src/
│   ├── binpack_core.c       # BCJ x86/ARM32/AArch64 & Transpose-Delta engines
│   ├── binpack_eval.c       # Micro-Trial Zero-Regression evaluator
│   ├── binpack_stream.c     # Bounded-memory chunk framing engine (BPK2)
│   └── binpack_sdk.c        # Public API implementation & memory adapters
├── cli/
│   └── binpack_cli.c        # Standalone enterprise CLI utility
├── bindings/
│   ├── csharp/              # C# / .NET 8 / Unity managed wrapper (BinPackNet.cs)
│   ├── python/              # Zero-dependency ctypes Python wrapper
│   ├── cpp/                 # Modern C++ RAII header (binpack.hpp)
│   └── rust/                # Safe Rust crate bindings
├── tests/
│   ├── test_sdk.c           # C automated regression & fuzz test suite
│   └── test_python.py       # Python binding integration test
├── deps/                    # Zstandard headers and static libraries
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

// Buffer compression
byte[] inputData = File.ReadAllBytes("game_assets.bin");
byte[] compressed = BinPackEngine.Compress(inputData);
byte[] restored = BinPackEngine.Decompress(compressed);

// Bounded-memory file streaming (processes 50 GB with < 64 MB RAM)
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

### CMake (Universal)
```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

---

## Command Line Interface (CLI)

```bash
# Compress file using bounded streaming chunking (16 MB buffer, autodetect threads)
binpack-sdk -c server_image.iso server_image.bpk2 -l 19 -t 0

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
