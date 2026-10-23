#!/usr/bin/env python3
"""
BinPack Enterprise SDK - Automated Multi-Engine Comparative Benchmark
Evaluates BinPack SDK against Meta Zstandard, 7-Zip/XZ (LZMA2 + BCJ), and Gzip/Deflate.
"""

import os
import sys
import time
import gzip
import lzma
import zstandard as zstd
import subprocess

FILES = [
    ("ooffice", "C:/Users/sgi.lm/Desktop/Compiler/ooffice", "x86 ELF Executable (Silesia Corpus)"),
    ("mozilla", "C:/Users/sgi.lm/Desktop/Compiler/mozilla", "Shared Executable Binaries (Silesia Corpus)"),
    ("vcruntime140.dll", "C:/Users/sgi.lm/Desktop/Compiler/vcruntime140.dll", "Windows PE x64 System Runtime DLL"),
    ("sensor_data.raw", "C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", "IEEE 754 Float32 Telemetry Array"),
    ("arm_binary.elf", "C:/Users/sgi.lm/Desktop/Compiler/arm_binary.elf", "ARM32 Thumb-2 Embedded Firmware ELF"),
    ("mr", "C:/Users/sgi.lm/Desktop/Compiler/mr", "Medical MRI Imaging Array (Silesia Corpus)")
]

BINPACK_SDK_EXE = os.path.abspath("binpack-sdk.exe")
if not os.path.exists(BINPACK_SDK_EXE):
    BINPACK_SDK_EXE = os.path.abspath("C:/Users/sgi.lm/Desktop/binpack-SDK/binpack-sdk.exe")

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

def bench_binpack_sdk(path, level=19, filter_mode=None, mode=None):
    out_bpk = path + ".tmp.bench.bpk2"
    out_rest = path + ".tmp.bench.rest"
    cmd = [BINPACK_SDK_EXE, "-c", path, out_bpk, "-l", str(level)]
    if filter_mode:
        cmd += ["-f", filter_mode]
    if mode:
        cmd += ["-m", mode]
    t0 = time.perf_counter()
    subprocess.run(cmd, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t1 = time.perf_counter()
    c_size = os.path.getsize(out_bpk)

    cmd_d = [BINPACK_SDK_EXE, "-d", out_bpk, out_rest]
    t2 = time.perf_counter()
    subprocess.run(cmd_d, check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    t3 = time.perf_counter()

    with open(path, "rb") as f1, open(out_rest, "rb") as f2:
        assert f1.read() == f2.read()

    if os.path.exists(out_bpk): os.remove(out_bpk)
    if os.path.exists(out_rest): os.remove(out_rest)
    return c_size, (t1 - t0), (t3 - t2)

def run_benchmark():
    print("=" * 115)
    print("BINPACK ENTERPRISE SDK - MULTI-ENGINE INDUSTRY BENCHMARK SUITE")
    print("=" * 115)

    all_results = []

    for name, path, desc in FILES:
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        orig_sz = len(data)
        print(f"\n[BENCHMARK] Payload: {name} ({desc}) | Raw Size: {orig_sz:,} bytes ({orig_sz/1048576:.2f} MB)")
        print("-" * 115)
        print(f"{'Engine / Preset':<32} | {'Compressed Size':<16} | {'Ratio':<8} | {'Comp Time':<10} | {'Decomp Speed':<14}")
        print("-" * 115)

        file_res = {
            "name": name,
            "desc": desc,
            "orig_sz": orig_sz,
            "rows": []
        }

        # 1. Gzip -9
        gz_sz, gz_ct, gz_dt = bench_gzip(data)
        gz_speed = (orig_sz / 1048576) / (gz_dt if gz_dt > 0 else 0.001)
        print(f"{'Gzip (Deflate -9)':<32} | {gz_sz:>8,} bytes | {gz_sz*100/orig_sz:>6.2f}% | {gz_ct:>8.3f}s | {gz_speed:>9.1f} MB/s")
        file_res["rows"].append(("Gzip (Deflate -9)", gz_sz, gz_sz*100/orig_sz, gz_ct, gz_speed))

        # 2. Zstandard Level 19
        z19_sz, z19_ct, z19_dt = bench_zstd(data, level=19)
        z19_speed = (orig_sz / 1048576) / (z19_dt if z19_dt > 0 else 0.001)
        print(f"{'Zstandard (Level 19)':<32} | {z19_sz:>8,} bytes | {z19_sz*100/orig_sz:>6.2f}% | {z19_ct:>8.3f}s | {z19_speed:>9.1f} MB/s")
        file_res["rows"].append(("Zstandard (Level 19)", z19_sz, z19_sz*100/orig_sz, z19_ct, z19_speed))

        # 3. Zstandard Level 22 (Ultra)
        z22_sz, z22_ct, z22_dt = bench_zstd(data, level=22)
        z22_speed = (orig_sz / 1048576) / (z22_dt if z22_dt > 0 else 0.001)
        print(f"{'Zstandard (Level 22 Ultra)':<32} | {z22_sz:>8,} bytes | {z22_sz*100/orig_sz:>6.2f}% | {z22_ct:>8.3f}s | {z22_speed:>9.1f} MB/s")
        file_res["rows"].append(("Zstandard (Level 22 Ultra)", z22_sz, z22_sz*100/orig_sz, z22_ct, z22_speed))

        # 4. 7-Zip / XZ (LZMA2 -9)
        if orig_sz < 20 * 1024 * 1024:
            xz_sz, xz_ct, xz_dt = bench_xz_lzma2(data)
            xz_speed = (orig_sz / 1048576) / (xz_dt if xz_dt > 0 else 0.001)
            print(f"{'7-Zip / XZ (LZMA2 -9)':<32} | {xz_sz:>8,} bytes | {xz_sz*100/orig_sz:>6.2f}% | {xz_ct:>8.3f}s | {xz_speed:>9.1f} MB/s")
            file_res["rows"].append(("7-Zip / XZ (LZMA2 -9)", xz_sz, xz_sz*100/orig_sz, xz_ct, xz_speed))

            is_arm = "arm" in name.lower()
            bcj_sz, bcj_ct, bcj_dt = bench_xz_bcj(data, is_arm=is_arm)
            if bcj_sz:
                bcj_speed = (orig_sz / 1048576) / (bcj_dt if bcj_dt > 0 else 0.001)
                print(f"{'7-Zip / XZ (BCJ + LZMA2 -9)':<32} | {bcj_sz:>8,} bytes | {bcj_sz*100/orig_sz:>6.2f}% | {bcj_ct:>8.3f}s | {bcj_speed:>9.1f} MB/s")
                file_res["rows"].append(("7-Zip / XZ (BCJ + LZMA2 -9)", bcj_sz, bcj_sz*100/orig_sz, bcj_ct, bcj_speed))

        # 5. BinPack SDK Level 19 (Default / Balanced)
        fmode = "bitshuffle" if ("sensor" in name or "mr" in name) else None
        bp19_sz, bp19_ct, bp19_dt = bench_binpack_sdk(path, level=19, filter_mode=fmode, mode="default")
        bp19_speed = (orig_sz / 1048576) / (bp19_dt if bp19_dt > 0 else 0.001)
        print(f"{'BinPack SDK v2.1 (Default)':<32} | {bp19_sz:>8,} bytes | {bp19_sz*100/orig_sz:>6.2f}% | {bp19_ct:>8.3f}s | {bp19_speed:>9.1f} MB/s")
        file_res["rows"].append(("BinPack SDK v2.1 (Default L19)", bp19_sz, bp19_sz*100/orig_sz, bp19_ct, bp19_speed))

        # 6. BinPack SDK Archival Mode (Ultra btultra2)
        bp_arch_sz, bp_arch_ct, bp_arch_dt = bench_binpack_sdk(path, level=22, filter_mode=fmode, mode="archival")
        bp_arch_speed = (orig_sz / 1048576) / (bp_arch_dt if bp_arch_dt > 0 else 0.001)
        print(f"{'BinPack SDK v2.1 (Archival)':<32} | {bp_arch_sz:>8,} bytes | {bp_arch_sz*100/orig_sz:>6.2f}% | {bp_arch_ct:>8.3f}s | {bp_arch_speed:>9.1f} MB/s")
        file_res["rows"].append(("BinPack SDK v2.1 (Archival L22)", bp_arch_sz, bp_arch_sz*100/orig_sz, bp_arch_ct, bp_arch_speed))

        all_results.append(file_res)

    print("=" * 115)
    return all_results

if __name__ == "__main__":
    results = run_benchmark()
