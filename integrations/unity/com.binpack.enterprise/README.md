# BinPack Enterprise SDK for Unity
> **High-Throughput Compression & Bounded Streaming for Video Games**  
> *Native C / AVX2 Accelerated Plugin for Unity 2020.3+ / 2021 / 2022 / Unity 6*

---

## Features for Game Developers

* **>900 to 1,060 MB/s Decompression Throughput**: Decompresses level geometry, textures, and asset bundles in RAM faster than Meta Zstandard L19 in Fast-Path mode.
* **Strict Integrity Mode**: Built-in 32-bit per-chunk CRC verification delivering 480–700 MB/s for network streaming and patch validation.
* **GPU Texture Decoupling (BC1–BC7)**: Specialized preconditioning separates 4x4 block endpoints from interpolation indices, unlocking 30–45% higher compression on DDS/DXT textures.
* **Asynchronous Main-Thread Safe API**: Native non-blocking decompression via `await BinPackEngine.DecompressAsync()`.
* **Bounded-Memory Streaming**: Stream 50+ GB package files using strictly $\le 64\text{ MB}$ streaming chunk RAM, avoiding Out-Of-Memory crashes on mobile or console hardware (Nintendo Switch).

---

## Platform Support Matrix

| Platform | Architecture | Status in Package | Binary Location | Build / Setup Instructions |
| :--- | :---: | :---: | :--- | :--- |
| **Windows** | x86_64 | **Included (Precompiled)** | `Plugins/x86_64/binpack.dll` | Ready out-of-the-box (Clang / AVX2 optimized). |
| **Linux Standalone**| x86_64 | Source / Build Script | `Plugins/Linux/x86_64/libbinpack.so` | Build via `make` or `zig cc -shared -O3 src/*.c -o libbinpack.so`. |
| **macOS** | Universal (ARM64 + x86_64) | Source / Build Script | `Plugins/macOS/libbinpack.dylib` | Build with `clang -shared -O3 -arch arm64 -arch x86_64`. |
| **Android** | ARM64-v8a / armeabi-v7a | NDK Build Guide | `Plugins/Android/libs/arm64-v8a/libbinpack.so` | Build using standard Android NDK `CMakeLists.txt`. |
| **iOS** | ARM64 | CMake / Xcode | `Plugins/iOS/libbinpack.a` | Build static library target for Xcode archive. |

---

## Installation

### Method 1: Unity Package Manager via Git URL (Recommended)
1. In Unity, open **Window** $\rightarrow$ **Package Manager**.
2. Click the `+` button in the top left and select **Add package from git URL...**
3. Paste:
   ```
   https://github.com/Santirobm/binpack-SDK.git?path=/integrations/unity/com.binpack.enterprise
   ```

### Method 2: Local Copy
Copy the `com.binpack.enterprise` folder directly into your Unity project's `Packages/` or `Assets/Plugins/` directory.

---

## Quickstart Code Example

### 1. In-Memory Asset Compression & Async Decompression
```csharp
using UnityEngine;
using BinPack.Enterprise;

public class AssetLoader : MonoBehaviour
{
    async void Start()
    {
        byte[] rawAsset = LoadRawAssetFromDisk();

        // 1. Compress using Fast Runtime profile
        byte[] compressed = BinPackEngine.Compress(rawAsset, BinPackOptions.FastRuntime);

        // 2. Decompress asynchronously with Zero-Copy Fast-Path (>900 MB/s)
        byte[] restored = await BinPackEngine.DecompressAsync(compressed, verifyChecksum: false);

        Debug.Log($"Restored {restored.Length} bytes without main-thread stutter!");
    }
}
```

### 2. GPU Texture Decompression
```csharp
// Direct raw texture reconstruction
Texture2D tex = BinPackEngine.DecompressTexture(compressedTextureBytes, 2048, 2048, TextureFormat.BC7);
```

### 3. Bounded-Memory Package File Streaming
```csharp
// Processes arbitrary file sizes with <= 64 MB streaming chunk RAM
BinPackEngine.CompressFile("RawGameData.pak", "CompressedGameData.bpk2");
BinPackEngine.DecompressFile("CompressedGameData.bpk2", "RestoredGameData.pak");
```

---

## Licensing Terms for Unity Developers

* **Commercial Royalty-Free Distribution**: Purchasers of BinPack Enterprise SDK via the official Unity Asset Store receive a perpetual, worldwide, royalty-free license to bundle the native libraries inside compiled commercial games and interactive applications in accordance with the Unity Asset Store EULA. No per-title royalties or additional off-store contracts required.
* **Third-Party Attributions**: Includes components under permissible licenses (Meta Zstandard BSD-3-Clause, Igor Pavlov LZMA Public Domain). See `THIRD_PARTY_NOTICES.md` in the SDK repository.
* **Direct Commercial Licensing & Inquiries**: Santiago Robledo (<santiagorobledo198@gmail.com>)
