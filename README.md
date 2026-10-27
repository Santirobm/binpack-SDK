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
| **Industrial & Aerospace Telemetry**<br>*(Turbofan sensors, vibration accelerometers, high-frequency SCADA)* | Zstd L3 (38–59%)<br>Gorilla/Chimp (106%) | **BinPack RT: 10.20%–28.49% @ 690–818 MB/s**<br>BinPack Default: **7.63%–27.25%** | **Undisputed Winner**<br>Up to **$5.8\times$ smaller** and **$3.2\times$ faster** than Zstd L3; **$1,685–2,040\text{ MB/s}$** decompression. |
| **Time-Series Float32 / Float64 Columns**<br>*(TimescaleDB, ClickHouse, InfluxDB, Parquet, HDF5)* | Zstd L3 (87–95%)<br>Gorilla/Chimp (103–106%) | **BinPack RT: 0.20%–68.23%**<br>(Up to **$360\times$ smaller**, **$3,060–4,740\text{ MB/s}$** decomp) | **Drop-in Upgrade**<br>Replaces failing bitwise XOR codecs on continuous and noisy physical signals. |
| **Structured Numerical Arrays**<br>*(Float32/64 ramps, counters, multi-channel 16-bit analog matrices)* | Zstd L3 (72–90%)<br>Sprintz (70–103%) | **BinPack RT: 0.20%–34.99%**<br>(Up to **$4,510\text{ MB/s}$** comp, **$5,198\text{ MB/s}$** decomp) | **Critical Advantage**<br>Turns arithmetic ramps and multichannel matrices into near-zero byte streams. |
| **Embedded & Mobile Firmware (OTA)**<br>*(ARM32 Thumb-2, AArch64, RISC-V, x86/x64 executables)* | 7-Zip BCJ / Zstd L22<br>(33–42%) | **BinPack Archival Core**<br>(**1st Place** globally, beats 7-Zip BCJ) | **Cellular/Satellite Bandwidth Saver**<br>Generates microscopic differential patches (<400 bytes). |
| **Generic Web HTTP Payloads & CSV/Text**<br>*(Raw HTML, CSS, JavaScript, CSV, JSON)* | Meta Zstandard L3<br>Google Brotli | Instant ASCII fast-exit bypass<br>(Latency $\le 1\ \mu\text{s}$, 100% Zstd parity) | **Use Zstd / Brotli directly**<br>Native web formats already benefit from standard dictionaries. |
| **Micro-Packets ($\le 256$ Bytes)**<br>*(Isolated single UDP datagrams without state)* | Raw bytes / Sprintz<br>(Sub-microsecond) | Pre-trained dictionary (`binpack_dict_t`)<br>recommended for $< 256\text{ B}$ | **Use `binpack_compress_dict`**<br>Avoids 68-byte container header on sub-256B CAN-bus payloads. |

---

## The Decisive Benchmark: Head-to-Head Comparison

Evaluated on native hardware (**Intel Core i5-13500, Windows 10 Pro 64-bit, Single-Thread Normalized, Clang AVX2 -O3**) across 14 real-world industrial, numerical, and mixed SCADA datasets. Every run is verified with **100% bit-exact byte-by-byte integrity (`memcmp == 0`)**:

```
[Speed Frontier: LZ4 / Zstd L3 / BinPack RT] <--------------------> [Density Frontier: Zstd L19 / BinPack Default]
```

### 1. Real Industrial Telemetry
| Dataset & Description | LZ4 (Fast) | Meta Zstd L3 | Meta Zstd L19 | Facebook Gorilla | Chimp (VLDB '22) | Sprintz (IoT '18) | BinPack RT (Fast-Path) | BinPack Default (L19) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **NASA Turbofan F32**<br>*(1.89 MB - 21 channels Float32)* | 56.49%<br>774 / 6,208 MB/s | 38.26%<br>283 / 1,308 MB/s | 31.02%<br>4.5 / 1,506 MB/s | 106.18% ❌<br>42 / 227 MB/s | 106.18% ❌<br>47 / 230 MB/s | *N/A* | **28.49% 🏆<br>690 / 1,686 MB/s** | **27.25% 🏆<br>13.5 / 1,418 MB/s** |
| **NASA Bearing IMS**<br>*(1.82 MB - 20 kHz Vibration CSV)* | 77.13%<br>659 / 4,285 MB/s | 42.92%<br>191 / 1,293 MB/s | 37.06%<br>3.9 / 1,619 MB/s | *N/A* | *N/A* | *N/A* | **42.93%<br>197 / 1,303 MB/s** | **37.06%<br>4.0 / 1,509 MB/s** |
| **ETTh1 Transformer**<br>*(2.47 MB - Grid Load/Temp CSV)* | 25.04%<br>751 / 3,968 MB/s | 13.94%<br>714 / 2,538 MB/s | 10.93%<br>4.1 / 2,825 MB/s | *N/A* | *N/A* | *N/A* | **13.94%<br>711 / 2,527 MB/s** | **10.93%<br>3.9 / 2,788 MB/s** |
| **Sensor Matrix 16b**<br>*(256 KB - 16-bit Analog Matrix)* | 100.39% ❌<br>19,968 MB/s | 90.54%<br>275 / 1,334 MB/s | 86.30%<br>16.0 / 932 MB/s | *N/A* | *N/A* | 103.81% ❌<br>28 / 228 MB/s | **34.99% 🏆<br>262 / 1,649 MB/s** | **29.46% 🏆<br>8.3 / 1,189 MB/s** |

### 2. Controlled Numerical Series & Signals
| Dataset & Description | LZ4 (Fast) | Meta Zstd L3 | Meta Zstd L19 | Facebook Gorilla | Chimp (VLDB '22) | Sprintz (IoT '18) | BinPack RT (Fast-Path) | BinPack Default (L19) |
| :--- | :---: | :---: | :---: | :---: | :---: | :---: | :---: | :---: |
| **F32 Ramp (1 MB)**<br>*(Continuous Linear Float32)* | 100.39% ❌<br>18,166 MB/s | 72.00%<br>987 / 1,639 MB/s | 14.28%<br>9.0 / 596 MB/s | 103.12% ❌<br>118 / 239 MB/s | 106.25% ❌<br>125 / 231 MB/s | *N/A* | **0.20% 🏆<br>2,156 / 3,472 MB/s** | **0.11% 🏆<br>160 / 3,513 MB/s** |
| **F32 Sine Smooth (1 MB)**<br>*(Smooth Analog Sine Wave)* | 100.39% ❌<br>11,073 MB/s | 86.64%<br>418 / 1,296 MB/s | 70.04%<br>11.1 / 633 MB/s | 106.18% ❌<br>46 / 226 MB/s | 106.25% ❌<br>48 / 231 MB/s | *N/A* | **11.46% 🏆<br>811 / 2,878 MB/s** | **7.89% 🏆<br>14.2 / 2,275 MB/s** |
| **F32 Sine Wave (1 MB)**<br>*(Dynamic $\omega=0.05$ Sine)* | 100.39% ❌<br>16,734 MB/s | 89.56%<br>842 / 1,530 MB/s | 87.44%<br>20.7 / 1,088 MB/s | 106.25% ❌<br>36 / 229 MB/s | 106.25% ❌<br>37 / 230 MB/s | *N/A* | **42.17% 🏆<br>792 / 3,062 MB/s** | **38.17% 🏆<br>10.0 / 1,878 MB/s** |
| **F32 Sine + Noise (1 MB)**<br>*(Sine + Thermal Noise $\pm 2.0$)* | 100.39% ❌<br>18,277 MB/s | 89.76%<br>959 / 1,542 MB/s | 89.79%<br>20.8 / 1,404 MB/s | 106.25% ❌<br>35 / 148 MB/s | 106.25% ❌<br>36 / 232 MB/s | *N/A* | **71.49% 🏆<br>1,787 / 3,211 MB/s** | **70.04% 🏆<br>6.7 / 1,971 MB/s** |
| **F64 Sine Wave (1 MB)**<br>*(Double-Precision IEEE-754)* | 100.39% ❌<br>17,317 MB/s | 94.95%<br>834 / 1,368 MB/s | 94.27%<br>21.6 / 1,422 MB/s | 103.12% ❌<br>37 / 237 MB/s | *N/A* | *N/A* | **68.23% 🏆<br>1,351 / 4,740 MB/s** | **66.93% 🏆<br>18.1 / 3,074 MB/s** |
| **Int16 Sine Wave (1 MB)**<br>*(16-bit Sinusoidal Vibration)* | 97.64%<br>2,286 MB/s | 21.26%<br>504 / 2,192 MB/s | 15.32%<br>6.7 / 1,708 MB/s | *N/A* | *N/A* | 69.99%<br>43 / 329 MB/s | **14.89% 🏆<br>523 / 1,843 MB/s** | **9.03% 🏆<br>8.2 / 1,373 MB/s** |
| **Int32 Ramp (1 MB)**<br>*(32-bit Monotonic Counters)* | 100.39% ❌<br>14,982 MB/s | 72.32%<br>865 / 1,479 MB/s | 23.31%<br>12.0 / 442 MB/s | *N/A* | *N/A* | *N/A* | **0.20% 🏆<br>4,510 / 5,199 MB/s** | **0.02% 🏆<br>320 / 7,905 MB/s** |

### 3. Mixed Industrial SCADA & Structured Buffers
| Dataset & Description | LZ4 (Fast) | Meta Zstd L3 | Meta Zstd L19 | BinPack RT (Fast-Path) | BinPack Default (L19) |
| :--- | :---: | :---: | :---: | :---: | :---: |
| **SCADA PLC Frame (1 MB)**<br>*(20-byte Structs: Timestamp64 + 2xFloat32 + Int16 + Flags)* | 83.30%<br>1,079 / 4,640 MB/s | 59.16%<br>252 / 1,051 MB/s | 37.39%<br>8.8 / 875 MB/s | **10.20% 🏆<br>818 / 2,041 MB/s** | **7.63% 🏆<br>19.2 / 1,785 MB/s** |
| **Kennedy XLS OLE2 (0.98 MB)**<br>*(Binary BIFF8 Spreadsheet)* | 36.03%<br>848 / 5,589 MB/s | 10.86%<br>634 / 1,709 MB/s | 6.30%<br>4.7 / 1,089 MB/s | **10.42% 🏆<br>610 / 1,413 MB/s** | **6.30% 🏆<br>4.5 / 1,085 MB/s** |
| **Astrometry SAO (6.92 MB)**<br>*(Silesia Star Catalog Binary)* | 93.63%<br>858 / 6,903 MB/s | 76.22%<br>202 / 1,091 MB/s | 68.95%<br>5.1 / 1,167 MB/s | **76.22%<br>201 / 1,081 MB/s** | **68.96%<br>5.1 / 1,130 MB/s** |
*(Speed cells show `Compression MB/s / Decompression MB/s`)*

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
