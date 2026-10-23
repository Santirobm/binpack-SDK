# BinPack Enterprise SDK - 2-Minute Technical Evaluation Guide
> **For Technical Directors, Principal Firmware Engineers, and System Architects**  
> *Fast, reproducible benchmark evaluation on your own proprietary payloads.*

---

## 1. Quick Evaluation (Zero-Installation CLI)

You do **not** need to install compilers, runtimes, or external package managers to evaluate BinPack. The standalone executable [`binpack-sdk.exe`](binpack-sdk.exe) is precompiled and statically links all core preconditions.

Place `binpack-sdk.exe` and `binpack.dll` in any directory and run:

```cmd
binpack-sdk.exe -b <path_to_your_file>
```

### What this command does:
1. Automatically profiles your file across all 3 engine modes:
   * **Runtime Profile (`-m runtime`)**: Fast-path zero-copy decompression (>650–1,060 MB/s).
   * **Default Profile (`-m default`)**: Production streaming with rolling prefix dictionary (Level 19).
   * **Archival Profile (`-m archival`)**: Bit-level Range Coder core for maximum compression density.
2. Applies domain-aware heuristics (x86/x64 `.text` BCJ, ARM32 Thumb-2, Float32 Bitshuffle).
3. Verifies bit-for-bit integrity with internal CRC32 checksums.
4. Prints a comparative table showing compressed size, compression ratio, wall-clock compression time, and decompression throughput.

---

## 2. Industry-Specific Benchmark Reference

All numbers measured on native x64 hardware (Intel Core i5-13500) and reproducible via `python tests/benchmark_suite.py`:

### Domain A: Video Game Development (Unity / Unreal Engine)
* **Problem**: Large executables, level geometry, and BC1–BC7 GPU textures cause long download times and loading screen stutter.

| Engine / Profile | Payload: `mozilla` (51.2 MB Executables) | Payload: `ooffice` (6.15 MB x86 Code) | Decompression Throughput |
| :--- | :---: | :---: | :---: |
| **Meta Zstandard (Level 19)** | 15,065,760 B (29.41%) | 2,595,003 B (42.18%) | ~616–788 MB/s |
| **BinPack Runtime (Fast-Path)**| 16,540,945 B (32.29%) | **2,436,630 B (39.61%)** | **652–911 MB/s (+122 MB/s vs Zstd)** |
| **BinPack Archival Mode** | **13,570,015 B (26.49%)** | **2,082,993 B (33.86%)** | **Beats 7-Zip & Zstd L22** |

* **Unity Package**: Ready-to-import Unity Package Manager (UPM) plugin available in [`integrations/unity/com.binpack.enterprise`](integrations/unity/com.binpack.enterprise).
* **RAM Guarantee**: Bounded streaming processes 50+ GB package files using strictly $\le 64\text{ MB}$ RAM.

---

### Domain B: Automotive ECUs & IoT Firmware (ARM Cortex-M / Thumb-2)
* **Problem**: Cellular OTA data transfer (LTE-M, NB-IoT) is expensive; microcontrollers have limited flash memory.

| Engine / Profile | Payload: `arm_binary.elf` (100 KB Thumb-2 Firmware) | Net Reduction vs Zstandard |
| :--- | :---: | :---: |
| **Gzip / zlib (Deflate -9)** | 34,688 bytes (34.67%) | Baseline |
| **Meta Zstandard (Level 19)** | 22,680 bytes (22.67%) | -34.6% vs Gzip |
| **7-Zip (LZMA2 + BCJ ARM)** | 172 bytes (0.17%) | -99.2% vs Zstd |
| **BinPack SDK v2.2** | **110 bytes (0.11%)** | **-99.5% reduction (110 bytes total)** |

* **Why it wins**: BinPack's Thumb-2 branch normalizer resolves relative 24-bit branch offsets (`BL/B.W`) into identical absolute targets, eliminating branch entropy before entropy coding.

---

### Domain C: Scientific Telemetry & Sensor Arrays (IEEE 754 Floats)
* **Problem**: Sensor streams (temperature, IMU, GPS, vibration) contain floating-point mantissa noise that defeats standard LZ/FSE compressors.

| Engine / Profile | Payload: `sensor_data.raw` (262 KB Float32 Telemetry) | Space Saved | Decompression Speed |
| :--- | :---: | :---: | :---: |
| **Gzip (Deflate -9)** | 233,894 bytes (89.22%) | 10.78% | 204.7 MB/s |
| **Meta Zstandard (Level 19)** | 226,242 bytes (86.30%) | 13.70% | 592.3 MB/s |
| **BinPack SDK (Runtime)** | **187,419 bytes (71.49%)** | **28.51%** | **634.2 MB/s (Beats Zstd)** |
| **BinPack SDK (Archival)**| **177,696 bytes (67.79%)** | **32.21%** | **Over 2x more savings than Zstd** |

* **Why it wins**: AVX2 Bitshuffle transposes the $8 \times 8$ bit matrix across adjacent 32-bit floats, grouping sign and exponent bits into continuous runs of zeros.

---

## 3. Integration in 3 Lines of Code

### C# / .NET 8 / Unity
```csharp
using BinPack.Enterprise;

// High-throughput async decompression (>900 MB/s in RAM)
byte[] restored = await BinPackEngine.DecompressAsync(compressedBytes, verifyChecksum: false);
```

### Python
```python
import binpack_sdk

# Compress using fast runtime profile
compressed = binpack_sdk.compress(raw_bytes, mode="runtime")
restored = binpack_sdk.decompress(compressed, verify_checksum=False)
```

### Modern C++ (C++17 / C++20)
```cpp
#include "binpack.hpp"

std::vector<uint8_t> compressed = binpack::compress(payload, binpack::Profile::Runtime);
std::vector<uint8_t> restored = binpack::decompress(compressed);
```

---

## 4. Evaluation Licensing & Commercial Inquiries

BinPack Enterprise SDK is dual-licensed:
* **Community / Evaluation**: Free for evaluation, research, and non-commercial projects.
* **Commercial / Production**: Royalty-free per-seat or per-product licensing for proprietary games, automotive ECUs, and commercial IoT platforms.

For evaluation licenses, custom format adapters, or dedicated SLAs:
* **Lead Architect**: Santiago Robledo (<santiagorobledo198@gmail.com>)
* **Official Repository**: [https://github.com/Santirobm/binpack-SDK](https://github.com/Santirobm/binpack-SDK)
