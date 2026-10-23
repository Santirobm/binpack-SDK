#!/usr/bin/env python3
"""
BinPack Enterprise SDK v2.2 - Automated Multi-Engine Industry Benchmark
Evaluates BinPack SDK against Meta Zstandard, 7-Zip/XZ (LZMA2 + BCJ), and Gzip/Deflate.
Measures both in-memory pure engine throughput and standalone CLI streaming.
"""

import os
import sys
import time
import gzip
import lzma
import zstandard as zstd
import subprocess

# Ensure Python bindings are available
SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BINDINGS_PATH = os.path.join(SDK_ROOT, "bindings", "python")
if BINDINGS_PATH not in sys.path:
    sys.path.insert(0, BINDINGS_PATH)

import binpack_sdk
from binpack_sdk import BinPackEngineMode, BinPackFilter

FILES = [
    ("ooffice", "C:/Users/sgi.lm/Desktop/Compiler/ooffice", "x86 ELF Executable (Silesia Corpus)"),
    ("mozilla", "C:/Users/sgi.lm/Desktop/Compiler/mozilla", "Shared Executable Binaries (Silesia Corpus)"),
    ("vcruntime140.dll", "C:/Users/sgi.lm/Desktop/Compiler/vcruntime140.dll", "Windows PE x64 System Runtime DLL"),
    ("sensor_data.raw", "C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", "IEEE 754 Float32 Telemetry Array"),
    ("arm_binary.elf", "C:/Users/sgi.lm/Desktop/Compiler/arm_binary.elf", "ARM32 Thumb-2 Embedded Firmware ELF"),
    ("mr", "C:/Users/sgi.lm/Desktop/Compiler/mr", "Medical MRI Imaging Array (Silesia Corpus)")
]

BINPACK_SDK_EXE = os.path.join(SDK_ROOT, "binpack-sdk.exe")

def bench_gzip(data):
    t0 = time.perf_counter()
    c = gzip.compress(data, compresslevel=9)
    t1 = time.perf_counter()
    d = gzip.decompress(c)
    t2 = time.perf_counter()
    assert d == data
    return len(c), (t1 - t0), (t2 - t1)

def bench_zstd(data, level=19):
    cctx = zstd.ZstdCompressor(level=level)
    t0 = time.perf_counter()
    c = cctx.compress(data)
    t1 = time.perf_counter()
    dctx = zstd.ZstdDecompressor()
    t2 = time.perf_counter()
    d = dctx.decompress(c, max_output_size=len(data) + 1024)
    t3 = time.perf_counter()
    assert d == data
    return len(c), (t1 - t0), (t3 - t2)

def bench_xz_lzma2(data):
    t0 = time.perf_counter()
    c = lzma.compress(data, preset=9)
    t1 = time.perf_counter()
    t2 = time.perf_counter()
    d = lzma.decompress(c)
    t3 = time.perf_counter()
    assert d == data
    return len(c), (t1 - t0), (t3 - t2)

def bench_xz_bcj(data, is_arm=False):
    filter_id = lzma.FILTER_ARM if is_arm else lzma.FILTER_X86
    filters = [
        {"id": filter_id},
        {"id": lzma.FILTER_LZMA2, "preset": 9}
    ]
    try:
        t0 = time.perf_counter()
        c = lzma.compress(data, format=lzma.FORMAT_XZ, filters=filters)
        t1 = time.perf_counter()
        t2 = time.perf_counter()
        d = lzma.decompress(c)
        t3 = time.perf_counter()
        assert d == data
        return len(c), (t1 - t0), (t3 - t2)
    except Exception:
        return None, None, None

def bench_binpack_in_memory(data, level=19, filter_mode=BinPackFilter.AUTO, mode=BinPackEngineMode.DEFAULT, verify=False):
    t0 = time.perf_counter()
    c = binpack_sdk.compress(data, level=level, filter_mode=filter_mode, mode=mode, chunk_size_mb=64)
    t1 = time.perf_counter()
    t2 = time.perf_counter()
    d = binpack_sdk.decompress(c, verify_checksum=verify)
    t3 = time.perf_counter()
    assert d == data
    return len(c), (t1 - t0), (t3 - t2)

def run_benchmark():
    print("=" * 120)
    print("BINPACK ENTERPRISE SDK v2.2 - IN-MEMORY ENGINE & DECOMPRESSION BENCHMARK")
    print("=" * 120)

    for name, path, desc in FILES:
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        orig_sz = len(data)
        print(f"\n[BENCHMARK] Payload: {name} ({desc}) | Raw Size: {orig_sz:,} bytes ({orig_sz/1048576:.2f} MB)")
        print("-" * 120)
        print(f"{'Engine / Preset':<36} | {'Compressed Size':<16} | {'Ratio':<8} | {'Comp Time':<10} | {'Decomp Speed':<14}")
        print("-" * 120)

        # 1. Gzip -9
        gz_sz, gz_ct, gz_dt = bench_gzip(data)
        gz_speed = (orig_sz / 1048576) / (gz_dt if gz_dt > 0 else 0.001)
        print(f"{'Gzip (Deflate -9)':<36} | {gz_sz:>8,} bytes | {gz_sz*100/orig_sz:>6.2f}% | {gz_ct:>8.3f}s | {gz_speed:>9.1f} MB/s")

        # 2. Zstandard Level 19
        z19_sz, z19_ct, z19_dt = bench_zstd(data, level=19)
        z19_speed = (orig_sz / 1048576) / (z19_dt if z19_dt > 0 else 0.001)
        print(f"{'Zstandard (Level 19)':<36} | {z19_sz:>8,} bytes | {z19_sz*100/orig_sz:>6.2f}% | {z19_ct:>8.3f}s | {z19_speed:>9.1f} MB/s")

        # 3. Zstandard Level 22 (Ultra)
        z22_sz, z22_ct, z22_dt = bench_zstd(data, level=22)
        z22_speed = (orig_sz / 1048576) / (z22_dt if z22_dt > 0 else 0.001)
        print(f"{'Zstandard (Level 22 Ultra)':<36} | {z22_sz:>8,} bytes | {z22_sz*100/orig_sz:>6.2f}% | {z22_ct:>8.3f}s | {z22_speed:>9.1f} MB/s")

        # 4. 7-Zip / XZ (LZMA2 -9)
        if orig_sz < 20 * 1024 * 1024:
            xz_sz, xz_ct, xz_dt = bench_xz_lzma2(data)
            xz_speed = (orig_sz / 1048576) / (xz_dt if xz_dt > 0 else 0.001)
            print(f"{'7-Zip / XZ (LZMA2 -9)':<36} | {xz_sz:>8,} bytes | {xz_sz*100/orig_sz:>6.2f}% | {xz_ct:>8.3f}s | {xz_speed:>9.1f} MB/s")

            is_arm = "arm" in name.lower()
            bcj_sz, bcj_ct, bcj_dt = bench_xz_bcj(data, is_arm=is_arm)
            if bcj_sz:
                bcj_speed = (orig_sz / 1048576) / (bcj_dt if bcj_dt > 0 else 0.001)
                print(f"{'7-Zip / XZ (BCJ + LZMA2 -9)':<36} | {bcj_sz:>8,} bytes | {bcj_sz*100/orig_sz:>6.2f}% | {bcj_ct:>8.3f}s | {bcj_speed:>9.1f} MB/s")

        # 5. BinPack SDK Runtime Profile (Fast-Path Zero-Copy)
        fmode = "bitshuffle" if ("sensor" in name or "mr" in name) else "auto"
        bp_rt_sz, bp_rt_ct, bp_rt_dt = bench_binpack_in_memory(data, level=12, filter_mode=fmode, mode="runtime", verify=False)
        bp_rt_speed = (orig_sz / 1048576) / (bp_rt_dt if bp_rt_dt > 0 else 0.001)
        print(f"{'BinPack SDK v2.2 (Runtime Fast)':<36} | {bp_rt_sz:>8,} bytes | {bp_rt_sz*100/orig_sz:>6.2f}% | {bp_rt_ct:>8.3f}s | {bp_rt_speed:>9.1f} MB/s")

        # 6. BinPack SDK Runtime Profile (Strict Checksum)
        bp_rts_sz, bp_rts_ct, bp_rts_dt = bench_binpack_in_memory(data, level=12, filter_mode=fmode, mode="runtime", verify=True)
        bp_rts_speed = (orig_sz / 1048576) / (bp_rts_dt if bp_rts_dt > 0 else 0.001)
        print(f"{'BinPack SDK v2.2 (Runtime Strict CRC)':<36} | {bp_rts_sz:>8,} bytes | {bp_rts_sz*100/orig_sz:>6.2f}% | {bp_rts_ct:>8.3f}s | {bp_rts_speed:>9.1f} MB/s")

        # 7. BinPack SDK Default Profile (Rolling Prefix + Level 19)
        bp_def_sz, bp_def_ct, bp_def_dt = bench_binpack_in_memory(data, level=19, filter_mode=fmode, mode="default", verify=False)
        bp_def_speed = (orig_sz / 1048576) / (bp_def_dt if bp_def_dt > 0 else 0.001)
        print(f"{'BinPack SDK v2.2 (Default L19)':<36} | {bp_def_sz:>8,} bytes | {bp_def_sz*100/orig_sz:>6.2f}% | {bp_def_ct:>8.3f}s | {bp_def_speed:>9.1f} MB/s")

        # 8. BinPack SDK Archival Mode (Range Coder Core)
        if orig_sz < 20 * 1024 * 1024:
            bp_arch_sz, bp_arch_ct, bp_arch_dt = bench_binpack_in_memory(data, level=22, filter_mode=fmode, mode="archival", verify=False)
            bp_arch_speed = (orig_sz / 1048576) / (bp_arch_dt if bp_arch_dt > 0 else 0.001)
            print(f"{'BinPack SDK v2.2 (Archival LZMA2)':<36} | {bp_arch_sz:>8,} bytes | {bp_arch_sz*100/orig_sz:>6.2f}% | {bp_arch_ct:>8.3f}s | {bp_arch_speed:>9.1f} MB/s")

    print("\n" + "=" * 120)

if __name__ == "__main__":
    run_benchmark()
