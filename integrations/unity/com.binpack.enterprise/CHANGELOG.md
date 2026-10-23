# Changelog - BinPack Enterprise SDK for Unity

## [2.2.0] - 2026-10-23
### Added
- **O(1) Seekable Random-Access Archive Engine (`BinPackArchive`)**: Instant chunk index lookup via `BIDX` footer table and thread-safe `DecompressChunkAsync` for virtual texturing and open-world streaming.
- **Zero-Allocation API (`CompressInto` / `DecompressInto`)**: Pre-allocated buffer compression and decompression for zero GC pressure in 60/120 FPS game loops.
- **Binary Differential Patching (`CreatePatch` / `ApplyPatch`)**: Sub-kilobyte OTA binary updates.
- **Pre-Trained Micro-Packet Dictionaries (`BinPackDictionary`)**: High-ratio compression for 64–512 byte network/telemetry packets.
- **Multi-Architecture Prefilters**: Added `AArch64` (`B`/`BL`), `RISC-V` (`JAL`), SIMD-fused `Bitshuffle`, and auto-stride `Transpose+Delta`.

### Improved
- **Runtime Mode Compression Speed**: 5x–9.8x faster runtime compression (`120–220 MB/s`) via native `ZSTD_compress2` + `ZSTD_dfast`.
- **64-bit Large File Support**: Full support for archives exceeding 2 GB across all desktop and mobile platforms.
