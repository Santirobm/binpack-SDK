# BinPack Enterprise SDK

[![Version](https://img.shields.io/badge/version-2.2.0-blue.svg)](https://github.com/Santirobm/binpack-SDK)
[![License](https://img.shields.io/badge/license-Commercial%20%2F%20Dual-green.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/platform-Windows%20%7C%20Linux%20%7C%20macOS-lightgrey.svg)](ARCHITECTURE.md)
[![C-ABI](https://img.shields.io/badge/C--ABI-C99%20%2F%20C11-orange.svg)](include/binpack_sdk.h)

**BinPack Enterprise SDK** is an industrial-grade, context-aware compression and streaming library engineered for **numerical telemetry, time-series storage engines, video game runtimes (Unity, Unreal Engine), and embedded firmware OTA pipelines**.

It bridges the fundamental architectural gap in data compression: **general-purpose engines (Zstandard, LZ4) treat all inputs as flat byte streams**, failing to compress continuous physical signals; while **time-series bitstream codecs (Facebook Gorilla, Chimp) expand by +6% on continuous floating-point signals**. BinPack leverages **AVX2 SIMD Stride Transposition & Byte-Delta preconditioning** combined with an FSE/LZMA2 backend to deliver up to **$10\times$ to $360\times$ superior compression ratios** on numerical arrays while sustaining **$500\text{–}1,200\text{ MB/s}$ throughput**.

> 🚀 **Evaluating BinPack on your own datasets?** See the [**2-Minute Enterprise Evaluation Guide**](EVALUATION_GUIDE.md) (`binpack-sdk.exe -b <file>`).  
> 🎮 **Unity Developer?** Check out the official [**Unity UPM Package**](integrations/unity/com.binpack.enterprise) with non-blocking async decompression and BC1-BC7 GPU texture helpers.  
> 📊 **Full Empirical Benchmark Suite?** Read the decisive head-to-head evaluation in [`BENCHMARKS.md`](BENCHMARKS.md).

---

## Strategic Fit: Where BinPack Dominates vs. Where Not to Use It

We believe in radical technical transparency. No compression engine is optimal for every workload:

| Workload / Data Type | Standard Industry Baseline | BinPack Advantage | Commercial Verdict |
| :--- | :--- | :--- | :--- |
| **Industrial & Aerospace Telemetry**<br>*(Turbofan sensors, vibration accelerometers, high-frequency SCADA)* | Zstd L3 (38–43%)<br>Gorilla/Chimp (106%) | **BinPack RT: 28.49% @ 509 MB/s**<br>BinPack Default: **27.25%** | **Undisputed Winner**<br>Saves 25–70% more storage with zero decompression bottlenecks. |
| **Time-Series Float Columns**<br>*(TimescaleDB, ClickHouse, InfluxDB, Parquet, HDF5)* | Gorilla (103–106%)<br>Chimp (106%) | **BinPack RT: 0.11% to 11.46%**<br>(Up to **$18\times$ better ratio** than Gorilla) | **Drop-in Upgrade**<br>Replaces failing bitwise XOR codecs on continuous physical metrics. |
| **Structured Numerical Arrays**<br>*(Float32/64 ramps, counters, multi-channel 16-bit analog matrices)* | Zstd L3 (72–90%)<br>Sprintz (70–103%) | **BinPack RT: 0.11%–34.99%**<br>(Up to **$360\times$ better ratio** than Zstd L3) | **Critical Advantage**<br>Turns arithmetic ramps and multichannel matrices into near-zero byte streams. |
| **Embedded & Mobile Firmware (OTA)**<br>*(ARM32 Thumb-2, AArch64, x86/x64 executables)* | 7-Zip BCJ / Zstd L22<br>(33–42%) | **BinPack Archival Core**<br>(**1st Place** globally, beats 7-Zip BCJ) | **Cellular/Satellite Bandwidth Saver**<br>Generates microscopic differential patches (<400 bytes). |
| **Generic Web HTTP Payloads**<br>*(Raw HTML, CSS, JavaScript, small JSON)* | Meta Zstandard L3<br>Google Brotli | Raw pass-through via fast bypass<br>(Latencia $\le 1\ \mu\text{s}$) | **Use Zstd / Brotli directly**<br>Native web formats already benefit from standard dictionaries. |
| **Micro-Packets ($\le 256$ Bytes)**<br>*(Isolated single UDP datagrams without state)* | Raw bytes / Sprintz<br>(Sub-microsecond) | Pre-trained dictionary required<br>or small block overhead | **Use Sprintz / Delta-encoding**<br>Block engines incur header cost on sub-256B payloads. |

---

## The Decisive Benchmark: Head-to-Head Comparison

Evaluated on native hardware (**Intel Core i5-13500, Windows 10 Pro 64-bit, 1 Thread Normalizado, Clang AVX2 -O3**) across real-world industrial and controlled datasets. Every run verified with **100% bit-exact byte-by-byte integrity (`memcmp == 0`)**:

```
[Speed Frontier: LZ4 / Zstd L3 / BinPack RT] <--------------------> [Density Frontier: Zstd L19 / BinPack Default]
```

### 1. Real Industrial Telemetry
| Dataset & Description | LZ4 (Fast) | Meta Zstd L3 | Meta Zstd L19 | Facebook Gorilla | Chimp (VLDB '22) | Sprintz (IoT '18) | BinPack RT (Fast-Path) | BinPack Default (L19) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **NASA Turbofan F32**<br>*(1.89 MB - 26 channels Float32)* | 56.49%<br>764 MB/s | 38.26%<br>273 MB/s | 31.02%<br>3.8 MB/s | 106.18% ❌<br>42 MB/s | 106.18% ❌<br>45 MB/s | *N/A* | **28.49% 🏆<br>509 MB/s** | **27.25% 🏆<br>13.2 MB/s** |
| **NASA Bearing IMS**<br>*(1.82 MB - Vibración 20 kHz)* | 77.13%<br>667 MB/s | 42.92%<br>199 MB/s | 37.06%<br>2.6 MB/s | *N/A* | *N/A* | *N/A* | **42.93%<br>118 MB/s** | **37.06%<br>2.6 MB/s** |
| **ETTh1 Transformer**<br>*(2.47 MB - Temp/Load CSV)* | 25.04%<br>457 MB/s | 13.94%<br>414 MB/s | 10.93%<br>2.5 MB/s | *N/A* | *N/A* | *N/A* | **13.94%<br>418 MB/s** | **10.93%<br>2.5 MB/s** |
| **Sensor Matrix 16b**<br>*(256 KB - Corriente analógica)* | 100.39% ❌<br>10,000 MB/s | 90.54%<br>147 MB/s | 86.30%<br>8.1 MB/s | *N/A* | *N/A* | 103.81% ❌<br>21 MB/s | **34.99% 🏆<br>141 MB/s** | **29.46% 🏆<br>4.8 MB/s** |

### 2. Controlled Numerical Series & Signals
| Dataset & Description | LZ4 (Fast) | Meta Zstd L3 | Meta Zstd L19 | Facebook Gorilla | Chimp (VLDB '22) | Sprintz (IoT '18) | BinPack RT (Fast-Path) | BinPack Default (L19) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **F32 Ramp (1 MB)**<br>*(Rampa lineal continua)* | 100.39% ❌<br>12,483 MB/s | 72.00%<br>672 MB/s | 14.28%<br>5.3 MB/s | 103.12% ❌<br>100 MB/s | 106.25% ❌<br>83 MB/s | *N/A* | **0.11% 🏆<br>991 MB/s** | **0.05% 🏆<br>202 MB/s** |
| **F32 Sine Smooth (1 MB)**<br>*(Onda senoidal analógica)* | 100.39% ❌<br>6,836 MB/s | 86.64%<br>261 MB/s | 70.04%<br>6.1 MB/s | 106.18% ❌<br>37 MB/s | 106.25% ❌<br>35 MB/s | *N/A* | **11.46% 🏆<br>358 MB/s** | **7.89% 🏆<br>8.9 MB/s** |
| **Int16 Sine Wave (1 MB)**<br>*(Vibración sinusoidal 16-bit)* | 97.64%<br>1,053 MB/s | 21.26%<br>280 MB/s | 15.32%<br>3.7 MB/s | *N/A* | *N/A* | 69.99%<br>32 MB/s | **14.89% 🏆<br>247 MB/s** | **9.03% 🏆<br>5.4 MB/s** |
| **Int32 Ramp (1 MB)**<br>*(Contadores y registros 32-bit)* | 100.39% ❌<br>9,144 MB/s | 72.32%<br>587 MB/s | 23.31%<br>6.9 MB/s | *N/A* | *N/A* | *N/A* | **0.20% 🏆<br>875 MB/s** | **0.02% 🏆<br>176 MB/s** |

> 💡 **Why did Gorilla and Chimp fail ($>100\%$)?** Facebook Gorilla and Chimp rely on consecutive floating-point values being identical ($v_i \oplus v_{i-1} == 0$). When a physical signal smoothly fluctuates, the XOR differences are non-zero with varying leading/trailing zeros. Gorilla is forced to emit up to 45 bits per 32-bit float ($106\%$ expansion). **BinPack's SIMD Stride Transposition separates the IEEE-754 exponent bytes from mantissa bytes**, turning continuous variations into long runs of identical byte deltas that compress to $<12\%$.

---

## Key Technical Innovations

* **AVX2 SIMD Stride Transposition & Byte-Delta**: Vectorized kernels reorganizing 16-bit, 32-bit, and 64-bit numerical arrays at $>15\text{ GB/s}$. Automatically groups identical exponent bytes to maximize LZ match lengths.
* **Dual-Engine Compression Core (FSE + LZMA2 Range Coder)**: Fast runtime execution powered by Meta Zstandard FSE (`BINPACK_MODE_RUNTIME` / `BINPACK_MODE_DEFAULT`) or ultra-dense executable compression powered by a custom Range Coder (`BINPACK_MODE_ARCHIVAL`).
* **Zero-Copy In-Place Decompression Fast-Path**: Eliminates intermediate buffer copies, achieving **650–1,100 MB/s** in-memory decompression throughput.
* **Micro-Trial Zero-Expansion Guarantee**: Evaluates a 32–64 KB probe before committing preconditioning filters. If no gain is detected, the engine falls back to raw pass-through, guaranteeing $0\%$ payload expansion.
* **Bounded-Memory Streaming Architecture (`BPK2`)**: Processes arbitrary file sizes (1 MB to 100+ GB) within a strictly bounded memory footprint ($\le 64\text{ MB RAM}$ per streaming thread), eliminating Out-Of-Memory (`OOM`) crashes.
* **Architecture-Aware Binary Preconditioning**: Section-aware `.text` branch normalization for x86/x64, ARM32 Thumb-2, and AArch64 binaries.
* **GPU Texture Decoupling (BC1–BC7 & ASTC)**: Separates 4x4 block endpoints from interpolation indices with Delta-4 encoding, unlocking 30–45% higher compression on game textures.

---

## Quickstart & Language Bindings

Distributed as a universal native dynamic library (`binpack.dll` / `libbinpack.so` / `libbinpack.dylib`) and static library (`binpack.lib` / `libbinpack.a`) with zero third-party runtime dependencies.

### 1. C# / .NET 8 / Unity
Add [`bindings/csharp/BinPackNet.cs`](bindings/csharp/BinPackNet.cs) to your project:
```csharp
using BinPack.Enterprise;

// High-speed telemetry buffer compression
byte[] sensorData = LoadSensorBuffer();
byte[] compressed = BinPackEngine.Compress(sensorData, BinPackEngineMode.Runtime);
byte[] restored = BinPackEngine.Decompress(compressed);

// Bounded-memory streaming (processes 50 GB with <= 64 MB RAM)
BinPackEngine.CompressFile("flight_telemetry.bin", "flight_telemetry.bpk2");
BinPackEngine.DecompressFile("flight_telemetry.bpk2", "flight_telemetry_restored.bin");
```

### 2. Python (Zero-Dependency C-Types)
```python
import binpack_sdk

# Compress telemetry array with auto-stride detection
raw_data = load_telemetry_floats()
compressed = binpack_sdk.compress(raw_data, mode=binpack_sdk.BinPackEngineMode.RUNTIME)
restored = binpack_sdk.decompress(compressed)
assert restored == raw_data

# Maximum archival density mode
archival_blob = binpack_sdk.compress(raw_data, mode=binpack_sdk.BinPackEngineMode.ARCHIVAL)
```

### 3. Modern C++ (C++17 / C++20 Header-Only)
Include [`bindings/cpp/binpack.hpp`](bindings/cpp/binpack.hpp):
```cpp
#include "binpack.hpp"

std::vector<uint8_t> telemetry = read_channel_buffer();
std::vector<uint8_t> compressed = binpack::compress(telemetry, binpack::Mode::Runtime);
std::vector<uint8_t> restored = binpack::decompress(compressed);
```

### 4. Standalone CLI Utility
```bash
# Compress telemetry stream using bounded streaming (16 MB chunks, auto-threads)
binpack-sdk -c sensor_array.bin sensor_array.bpk2 -l 3 -m runtime

# Compress firmware binary in Archival mode (Maximum density)
binpack-sdk -c firmware.elf firmware.bpk2 -m archival

# Decompress container
binpack-sdk -d sensor_array.bpk2 restored_sensor.bin

# Run automated reproducible benchmark on any file
binpack-sdk -b proprietary_dataset.bin
```

---

## Building from Source

```cmd
# Windows (Zig CC, MSVC, Clang)
build.bat

# Linux / macOS
make
make test
```

Outputs:
* `binpack.dll` / `libbinpack.so` (Shared Library)
* `binpack.lib` / `libbinpack.a` (Static Library)
* `binpack-sdk.exe` (Enterprise CLI Utility)
* `test_sdk.exe` (Automated 8-Battery Regression Suite)

---

## Dual-Licensing & Commercial Contact

BinPack Enterprise SDK is available under a **Dual-Licensing Model**:
* **Community & Evaluation License**: Free for open-source development, academic research, and non-commercial evaluation.
* **Commercial Enterprise License**: Required for closed-source commercial deployments, cloud telemetry historians, embedded/automotive firmware OTA, game engine distribution, and proprietary storage engines. Includes commercial SLAs, priority hardware optimization, and custom prefilter authoring.
* **Unity Asset Store License**: Grants perpetual, worldwide royalty-free rights to bundle compiled runtimes within commercial games.

For enterprise licensing inquiries, evaluation agreements, and custom engine integration:
* **Founder & Lead Architect**: Santiago Robledo (<santiagorobledo198@gmail.com>)
* **Official Repository**: [https://github.com/Santirobm/binpack-SDK](https://github.com/Santirobm/binpack-SDK)
