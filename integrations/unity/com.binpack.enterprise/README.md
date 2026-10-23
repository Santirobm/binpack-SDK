# BinPack Enterprise SDK for Unity
> **High-Throughput Compression & Bounded Streaming for Video Games**  
> *Native C / AVX2 Accelerated Plugin for Unity 2020.3+ / 2021 / 2022 / Unity 6*

---

## Features for Game Developers

* **>900 to 1,060 MB/s Decompression Throughput**: Decompresses level geometry, textures, and asset bundles in RAM faster than Meta Zstandard L19.
* **GPU Texture Decoupling (BC1–BC7)**: Specialized preconditioning separates 4x4 block endpoints from interpolation indices, unlocking 30–45% higher compression on DDS/DXT textures.
* **Asynchronous Main-Thread Safe API**: Native non-blocking decompression via `await BinPackEngine.DecompressAsync()`.
* **Bounded-Memory Streaming**: Stream 50+ GB package files using strictly $\le 64\text{ MB}$ RAM, avoiding Out-Of-Memory crashes on mobile or console hardware (Nintendo Switch).

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

        // 2. Decompress asynchronously with Zero-Copy (>900 MB/s)
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
// Processes arbitrary file sizes with <= 64 MB RAM
BinPackEngine.CompressFile("RawGameData.pak", "CompressedGameData.bpk2");
BinPackEngine.DecompressFile("CompressedGameData.bpk2", "RestoredGameData.pak");
```

---

## License & Support
Available under Dual-Licensing (Community / Indie & Commercial Enterprise Production).  
Contact: Santiago Robledo (<santiagorobledo198@gmail.com>)
