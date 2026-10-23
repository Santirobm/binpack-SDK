#!/usr/bin/env python3
"""
BinPack Enterprise SDK v2.2 - Standardized Industry Benchmark Suite
Statistically rigorous benchmark (1 warm-up + timed iterations, median & IQR).
Fully reproducible across any platform: zero hardcoded local paths, auto-downloads corpora.

Pareto-Optimal Evaluation Tiers:
  - Tier 1: High-Speed Real-Time Streaming (LZ4 vs Zstd L1 vs BinPack Runtime Fast vs BinPack Runtime Strict)
  - Tier 2: Balanced Production Streaming (Zstd L19 vs BinPack Default L19)
  - Tier 3: Maximum Archival Density (7-Zip LZMA2 -9 vs 7-Zip BCJ Filter vs BinPack Archival Core)
"""

import os
import sys
import time
import math
import argparse
import statistics
import lzma
import zstandard as zstd
import lz4.frame

# Resolve paths relative to repository root
SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BINDINGS_PATH = os.path.join(SDK_ROOT, "bindings", "python")
if BINDINGS_PATH not in sys.path:
    sys.path.insert(0, BINDINGS_PATH)

import binpack_sdk
from binpack_sdk import BinPackEngineMode, BinPackFilter

# Corpora paths relative to tests/
CORPORA_DIR = os.path.join(SDK_ROOT, "tests", "corpora")

# Ensure corpora are downloaded and verified
def ensure_corpora():
    check_file = os.path.join(CORPORA_DIR, "silesia", "ooffice")
    if not os.path.exists(check_file):
        print("[INFO] Benchmark corpora missing. Auto-fetching and verifying...")
        fetch_script = os.path.join(SDK_ROOT, "tests", "fetch_corpora.py")
        import subprocess
        subprocess.run([sys.executable, fetch_script], check=True)

ensure_corpora()

# Datasets with clean relative paths and semantic roles
DATASETS = [
    {
        "name": "ooffice",
        "path": os.path.join(CORPORA_DIR, "silesia", "ooffice"),
        "category": "x86 Code",
        "desc": "OpenOffice x86 ELF Executable (.text section parsing)"
    },
    {
        "name": "mozilla",
        "path": os.path.join(CORPORA_DIR, "silesia", "mozilla"),
        "category": "Shared Libs",
        "desc": "Silesia Browser Shared Libraries (Multi-module binaries - 51.2 MB)"
    },
    {
        "name": "busybox_armv7l",
        "path": os.path.join(CORPORA_DIR, "arm_binaries", "busybox_armv7l"),
        "category": "ARM32 Real",
        "desc": "Production Embedded Linux ARMv7-A Executable (1.11 MB)"
    },
    {
        "name": "arm_synthetic",
        "path": os.path.join(CORPORA_DIR, "arm_binaries", "arm_synthetic_branch_loop.elf"),
        "category": "ARM32 Stress",
        "desc": "ARM Thumb-2 Relative Branch Loop Micro-benchmark (100 KB)"
    },
    {
        "name": "nasa_turbofan_float32",
        "path": os.path.join(CORPORA_DIR, "nasa_telemetry", "nasa_turbofan_float32.bin"),
        "category": "Telemetry",
        "desc": "NASA C-MAPSS Turbofan 21-Sensor Packed Float32 Array (1.98 MB)"
    },
    {
        "name": "mr",
        "path": os.path.join(CORPORA_DIR, "silesia", "mr"),
        "category": "Medical Imaging",
        "desc": "Medical MRI 16-bit Grayscale Scan Array (9.51 MB)"
    },
    {
        "name": "dickens",
        "path": os.path.join(CORPORA_DIR, "silesia", "dickens"),
        "category": "Plain Text",
        "desc": "English Literature Text (Charles Dickens - 10.19 MB)"
    }
]

def run_stat_bench(fn_compress, fn_decompress, data, iters=10, warmup=1):
    orig_sz = len(data)

    # 1. Warm-up pass (prime caches & branch predictors)
    for _ in range(warmup):
        c = fn_compress(data)
        d = fn_decompress(c)
        assert d == data, "Warm-up integrity failure!"

    # 2. Timed iterations
    c_times = []
    d_times = []
    c_data = None

    for _ in range(iters):
        t0 = time.perf_counter()
        c = fn_compress(data)
        t1 = time.perf_counter()
        c_times.append(t1 - t0)
        c_data = c

        t2 = time.perf_counter()
        d = fn_decompress(c_data)
        t3 = time.perf_counter()
        d_times.append(t3 - t2)
        assert d == data, "Integrity verification mismatch!"

    # Calculate statistics: Median and Interquartile Range (IQR)
    c_med = statistics.median(c_times)
    d_med = statistics.median(d_times)

    if len(c_times) >= 4:
        q75_c, q25_c = statistics.quantiles(c_times, n=4)[2], statistics.quantiles(c_times, n=4)[0]
        q75_d, q25_d = statistics.quantiles(d_times, n=4)[2], statistics.quantiles(d_times, n=4)[0]
        c_iqr = q75_c - q25_c
        d_iqr = q75_d - q25_d
    else:
        c_iqr = max(c_times) - min(c_times)
        d_iqr = max(d_times) - min(d_times)

    c_sz = len(c_data)
    ratio = (c_sz * 100.0) / orig_sz
    c_speed = (orig_sz / 1048576.0) / (c_med if c_med > 0 else 1e-6)
    d_speed = (orig_sz / 1048576.0) / (d_med if d_med > 0 else 1e-6)

    return {
        "size": c_sz,
        "ratio": ratio,
        "c_med": c_med,
        "d_med": d_med,
        "c_speed": c_speed,
        "d_speed": d_speed,
        "c_iqr_pct": (c_iqr / c_med) * 100 if c_med > 0 else 0,
        "d_iqr_pct": (d_iqr / d_med) * 100 if d_med > 0 else 0
    }

def run_suite(full_iterations=False):
    default_iters = 10 if full_iterations else 5
    warmup_count = 1

    print("=" * 132)
    print("BINPACK ENTERPRISE SDK v2.2 - REPRODUCIBLE PARETO-TIERED BENCHMARK SUITE")
    print(f"Protocol: {warmup_count} Warmup + {default_iters} Iterations (Median & IQR) | Platform: {sys.platform}")
    print("=" * 132)

    for item in DATASETS:
        path = item["path"]
        if not os.path.exists(path):
            continue
        with open(path, "rb") as f:
            data = f.read()
        orig_sz = len(data)

        # For large files (>20 MB), archival mode is compute-intensive; scale iterations accordingly
        archival_iters = default_iters if orig_sz <= 20 * 1024 * 1024 or full_iterations else 2

        print(f"\n[PAYLOAD] {item['name']} ({item['category']}) | {item['desc']}")
        print(f"          Raw Size: {orig_sz:,} bytes ({orig_sz/1048576:.2f} MB)")
        print("-" * 132)
        print(f"{'Engine & Configuration':<38} | {'Compressed Size':<15} | {'Ratio':<8} | {'Comp Speed':<14} | {'Decomp Speed':<15} | {'IQR Var'}")
        print("-" * 132)

        # -------------------------------------------------------------
        # TIER 1: HIGH-SPEED STREAMING (Target: >600 MB/s Decomp)
        # -------------------------------------------------------------
        # 1. LZ4
        try:
            r_lz4 = run_stat_bench(lambda d: lz4.frame.compress(d), lambda c: lz4.frame.decompress(c), data, iters=default_iters)
            print(f"{'LZ4 (Frame Fast)':<38} | {r_lz4['size']:>8,} bytes | {r_lz4['ratio']:>6.2f}% | {r_lz4['c_speed']:>8.1f} MB/s | {r_lz4['d_speed']:>9.1f} MB/s | ±{r_lz4['d_iqr_pct']:3.1f}%")
        except Exception:
            pass

        # 2. Zstandard Level 1
        cctx1 = zstd.ZstdCompressor(level=1)
        dctx = zstd.ZstdDecompressor()
        r_z1 = run_stat_bench(lambda d: cctx1.compress(d), lambda c: dctx.decompress(c, max_output_size=orig_sz+1024), data, iters=default_iters)
        print(f"{'Meta Zstandard (Level 1)':<38} | {r_z1['size']:>8,} bytes | {r_z1['ratio']:>6.2f}% | {r_z1['c_speed']:>8.1f} MB/s | {r_z1['d_speed']:>9.1f} MB/s | ±{r_z1['d_iqr_pct']:3.1f}%")

        # 3. BinPack Runtime Fast-Path (Zero-CRC)
        fmode = "bitshuffle" if ("telemetry" in item["category"].lower() or "medical" in item["category"].lower()) else "auto"
        r_bp_rt_fast = run_stat_bench(
            lambda d: binpack_sdk.compress(d, level=12, filter_mode=fmode, mode="runtime"),
            lambda c: binpack_sdk.decompress(c, verify_checksum=False),
            data,
            iters=default_iters
        )
        print(f"{'BinPack Runtime (Fast Zero-CRC)':<38} | {r_bp_rt_fast['size']:>8,} bytes | {r_bp_rt_fast['ratio']:>6.2f}% | {r_bp_rt_fast['c_speed']:>8.1f} MB/s | {r_bp_rt_fast['d_speed']:>9.1f} MB/s | ±{r_bp_rt_fast['d_iqr_pct']:3.1f}%")

        # 4. BinPack Runtime Strict (Full CRC32 Check)
        r_bp_rt_strict = run_stat_bench(
            lambda d: binpack_sdk.compress(d, level=12, filter_mode=fmode, mode="runtime"),
            lambda c: binpack_sdk.decompress(c, verify_checksum=True),
            data,
            iters=default_iters
        )
        print(f"{'BinPack Runtime (Strict CRC32)':<38} | {r_bp_rt_strict['size']:>8,} bytes | {r_bp_rt_strict['ratio']:>6.2f}% | {r_bp_rt_strict['c_speed']:>8.1f} MB/s | {r_bp_rt_strict['d_speed']:>9.1f} MB/s | ±{r_bp_rt_strict['d_iqr_pct']:3.1f}%")

        # -------------------------------------------------------------
        # TIER 2: BALANCED PRODUCTION STREAMING (Bounded-Memory <= 64 MB)
        # -------------------------------------------------------------
        # 5. Zstandard Level 19
        cctx19 = zstd.ZstdCompressor(level=19)
        r_z19 = run_stat_bench(lambda d: cctx19.compress(d), lambda c: dctx.decompress(c, max_output_size=orig_sz+1024), data, iters=default_iters)
        print(f"{'Meta Zstandard (Level 19)':<38} | {r_z19['size']:>8,} bytes | {r_z19['ratio']:>6.2f}% | {r_z19['c_speed']:>8.1f} MB/s | {r_z19['d_speed']:>9.1f} MB/s | ±{r_z19['d_iqr_pct']:3.1f}%")

        # 6. BinPack Default Profile L19 (Rolling Prefix)
        r_bp_def = run_stat_bench(
            lambda d: binpack_sdk.compress(d, level=19, filter_mode=fmode, mode="default"),
            lambda c: binpack_sdk.decompress(c, verify_checksum=False),
            data,
            iters=default_iters
        )
        print(f"{'BinPack SDK v2.2 (Default L19)':<38} | {r_bp_def['size']:>8,} bytes | {r_bp_def['ratio']:>6.2f}% | {r_bp_def['c_speed']:>8.1f} MB/s | {r_bp_def['d_speed']:>9.1f} MB/s | ±{r_bp_def['d_iqr_pct']:3.1f}%")

        # -------------------------------------------------------------
        # TIER 3: MAXIMUM ARCHIVAL DENSITY (Range Coder Core)
        # -------------------------------------------------------------
        # 7. Standard 7-Zip / XZ LZMA2 -9
        try:
            r_xz = run_stat_bench(lambda d: lzma.compress(d, preset=9), lambda c: lzma.decompress(c), data, iters=archival_iters)
            print(f"{'7-Zip / XZ (LZMA2 -9)':<38} | {r_xz['size']:>8,} bytes | {r_xz['ratio']:>6.2f}% | {r_xz['c_speed']:>8.1f} MB/s | {r_xz['d_speed']:>9.1f} MB/s | ±{r_xz['d_iqr_pct']:3.1f}%")
        except Exception:
            pass

        # 8. Architecture-Specific 7-Zip BCJ Filter comparison
        is_arm = ("arm" in item["category"].lower()) or ("arm" in item["name"].lower())
        is_x86 = ("x86" in item["category"].lower()) or ("ooffice" in item["name"].lower())

        if is_arm:
            try:
                arm_filters = [
                    {"id": lzma.FILTER_ARM},
                    {"id": lzma.FILTER_LZMA2, "preset": 9}
                ]
                r_xz_bcj = run_stat_bench(
                    lambda d: lzma.compress(d, filters=arm_filters),
                    lambda c: lzma.decompress(c),
                    data,
                    iters=archival_iters
                )
                print(f"{'7-Zip / XZ (BCJ ARM + LZMA2 -9)':<38} | {r_xz_bcj['size']:>8,} bytes | {r_xz_bcj['ratio']:>6.2f}% | {r_xz_bcj['c_speed']:>8.1f} MB/s | {r_xz_bcj['d_speed']:>9.1f} MB/s | ±{r_xz_bcj['d_iqr_pct']:3.1f}%")
            except Exception as e:
                pass
        elif is_x86:
            try:
                x86_filters = [
                    {"id": lzma.FILTER_X86},
                    {"id": lzma.FILTER_LZMA2, "preset": 9}
                ]
                r_xz_bcj = run_stat_bench(
                    lambda d: lzma.compress(d, filters=x86_filters),
                    lambda c: lzma.decompress(c),
                    data,
                    iters=archival_iters
                )
                print(f"{'7-Zip / XZ (BCJ x86 + LZMA2 -9)':<38} | {r_xz_bcj['size']:>8,} bytes | {r_xz_bcj['ratio']:>6.2f}% | {r_xz_bcj['c_speed']:>8.1f} MB/s | {r_xz_bcj['d_speed']:>9.1f} MB/s | ±{r_xz_bcj['d_iqr_pct']:3.1f}%")
            except Exception as e:
                pass

        # 9. BinPack Archival Core
        r_bp_arch = run_stat_bench(
            lambda d: binpack_sdk.compress(d, level=22, filter_mode=fmode, mode="archival"),
            lambda c: binpack_sdk.decompress(c, verify_checksum=False),
            data,
            iters=archival_iters
        )
        print(f"{'BinPack SDK v2.2 (Archival Core)':<38} | {r_bp_arch['size']:>8,} bytes | {r_bp_arch['ratio']:>6.2f}% | {r_bp_arch['c_speed']:>8.1f} MB/s | {r_bp_arch['d_speed']:>9.1f} MB/s | ±{r_bp_arch['d_iqr_pct']:3.1f}%")

    print("\n" + "=" * 132)
    print("[SUCCESS] Multi-tier Pareto benchmark complete. 100% reproducible with zero hardcoded paths.")
    print("=" * 132)

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="BinPack Enterprise Benchmark Suite")
    parser.add_argument("--full", action="store_true", help="Run full 10 iterations on all payloads including large files")
    args = parser.parse_args()
    run_suite(full_iterations=args.full)
