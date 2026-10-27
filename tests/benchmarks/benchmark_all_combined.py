#!/usr/bin/env python3
"""
Complete Unified Benchmark Suite:
- Controlled Synthetic Arrays (256 KB, 1 MB, 8 MB, 32 MB, 128 MB)
  DataTypes: Float32, Float64, Int16, Int32
  Patterns: Ramp, Sine Wave, Random Walk (Brownian)
- Silesia Corpus (all 12 files)
- NASA IMS Bearings (Test 1, Test 2, Test 3)
- NASA PCoE C-MAPSS Turbofan (Float32 Binary & ASCII Text)
- IEEE IES Industrial AI Hub (ETTh1, ETTh2 Transformer Temperature)
- Architecture & Microcontroller Raw (ARM ELF, IEEE 754 Raw Matrix)

Always reports:
  - Compressed Ratio (%)
  - Compression Speed (MB/s)
  - Decompression Speed (MB/s)
For:
  1. Meta Zstd Level 3 (Default)
  2. Meta Zstd Level 19 (Ultra MT)
  3. BinPack v2.2 Runtime Fast (level=4, mode="runtime")
  4. BinPack v2.2 Default Balanced (level=19, mode="default")
"""

import os
import sys
import time
import json
import numpy as np
import zstandard as zstd

SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(SDK_ROOT, "bindings", "python"))
import binpack_sdk

SIZES = {
    "256KB": 256 * 1024,
    "1MB":   1 * 1024 * 1024,
    "8MB":   8 * 1024 * 1024,
    "32MB":  32 * 1024 * 1024,
    "128MB": 128 * 1024 * 1024,
}

DTYPES = {
    "Float32": (np.float32, 4),
    "Float64": (np.float64, 8),
    "Int16":   (np.int16, 2),
    "Int32":   (np.int32, 4)
}

def generate_pattern(pattern_name, dtype, num_elements, noise_level="clean"):
    np.random.seed(42)
    if pattern_name == "ramp":
        if np.issubdtype(dtype, np.floating):
            arr = np.linspace(0.0, 1000.0, num_elements, dtype=dtype)
        else:
            arr = (np.arange(num_elements) % 32767).astype(dtype)
    elif pattern_name == "sine":
        t = np.linspace(0, 50 * np.pi, num_elements)
        if np.issubdtype(dtype, np.floating):
            arr = (100.0 * np.sin(t)).astype(dtype)
        else:
            scale = 16000.0 if dtype == np.int16 else 1000000.0
            arr = (scale * np.sin(t)).astype(dtype)
    elif pattern_name == "random_walk":
        if np.issubdtype(dtype, np.floating):
            steps = np.random.normal(0, 0.05, num_elements).astype(dtype)
        else:
            steps = np.random.choice([-1, 0, 1], num_elements).astype(dtype)
        arr = np.cumsum(steps).astype(dtype)
    else:
        raise ValueError(f"Unknown pattern: {pattern_name}")

    if noise_level == "low_noise":
        if np.issubdtype(dtype, np.floating):
            noise = np.random.normal(0, 0.0001, num_elements).astype(dtype)
        else:
            noise = np.random.choice([-1, 0, 1], num_elements).astype(dtype)
        arr = arr + noise

    return arr.tobytes()

def bench_case(data):
    sz = len(data)
    # 1. Zstd 3
    cctx3 = zstd.ZstdCompressor(level=3)
    dctx = zstd.ZstdDecompressor()
    t0 = time.perf_counter()
    c3 = cctx3.compress(data)
    t_c3 = time.perf_counter() - t0
    t0 = time.perf_counter()
    d3 = dctx.decompress(c3, max_output_size=sz + 4096)
    t_d3 = time.perf_counter() - t0
    assert d3 == data

    # 2. Zstd 19 MT
    cctx19 = zstd.ZstdCompressor(level=19, threads=-1)
    t0 = time.perf_counter()
    c19 = cctx19.compress(data)
    t_c19 = time.perf_counter() - t0
    t0 = time.perf_counter()
    d19 = dctx.decompress(c19, max_output_size=sz + 4096)
    t_d19 = time.perf_counter() - t0
    assert d19 == data

    # 3. BinPack RT
    t0 = time.perf_counter()
    c_bp_rt = binpack_sdk.compress(data, level=4, filter_mode="auto", mode="runtime")
    t_c_rt = time.perf_counter() - t0
    t0 = time.perf_counter()
    d_bp_rt = binpack_sdk.decompress(c_bp_rt, verify_checksum=False)
    t_d_rt = time.perf_counter() - t0
    assert d_bp_rt == data

    # 4. BinPack Def
    t0 = time.perf_counter()
    c_bp_def = binpack_sdk.compress(data, level=19, filter_mode="auto", mode="default")
    t_c_def = time.perf_counter() - t0
    t0 = time.perf_counter()
    d_bp_def = binpack_sdk.decompress(c_bp_def, verify_checksum=False)
    t_d_def = time.perf_counter() - t0
    assert d_bp_def == data

    r_z3 = (len(c3) / sz) * 100.0
    r_z19 = (len(c19) / sz) * 100.0
    r_rt = (len(c_bp_rt) / sz) * 100.0
    r_def = (len(c_bp_def) / sz) * 100.0

    cs_z3 = (sz / 1048576.0) / max(t_c3, 1e-6)
    cs_z19 = (sz / 1048576.0) / max(t_c19, 1e-6)
    cs_rt = (sz / 1048576.0) / max(t_c_rt, 1e-6)
    cs_def = (sz / 1048576.0) / max(t_c_def, 1e-6)

    ds_z3 = (sz / 1048576.0) / max(t_d3, 1e-6)
    ds_z19 = (sz / 1048576.0) / max(t_d19, 1e-6)
    ds_rt = (sz / 1048576.0) / max(t_d_rt, 1e-6)
    ds_def = (sz / 1048576.0) / max(t_d_def, 1e-6)

    return {
        "zstd3": { "size": len(c3), "ratio": round(r_z3, 2), "c_mbs": round(cs_z3, 1), "d_mbs": round(ds_z3, 1), "time_ms": round(t_c3*1000.0, 1), "color": "#3b82f6" },
        "zstd19": { "size": len(c19), "ratio": round(r_z19, 2), "c_mbs": round(cs_z19, 1), "d_mbs": round(ds_z19, 1), "time_ms": round(t_c19*1000.0, 1), "color": "#a855f7" },
        "binpack_rt": { "size": len(c_bp_rt), "ratio": round(r_rt, 2), "c_mbs": round(cs_rt, 1), "d_mbs": round(ds_rt, 1), "time_ms": round(t_c_rt*1000.0, 1), "color": "#00f5ff" },
        "binpack_def": { "size": len(c_bp_def), "ratio": round(r_def, 2), "c_mbs": round(cs_def, 1), "d_mbs": round(ds_def, 1), "time_ms": round(t_c_def*1000.0, 1), "color": "#10b981" }
    }

def run():
    dashboard_dir = os.path.join(SDK_ROOT, "dashboard")
    real_json = os.path.join(dashboard_dir, "real_datasets.json")
    all_records = []

    # 1. Load Real Datasets
    if os.path.exists(real_json):
        with open(real_json, "r", encoding="utf-8") as f:
            all_records.extend(json.load(f))
        print(f"[OK] Loaded {len(all_records)} real datasets from cache.", flush=True)

    # 2. Benchmark Synthetic Matrix across all 5 sizes
    print("=" * 148, flush=True)
    print("RUNNING CONTROLLED SYNTHETIC SCALES (256 KB, 1 MB, 8 MB, 32 MB, 128 MB)", flush=True)
    print("=" * 148, flush=True)

    header = f"{'Type':<8} | {'Pattern':<12} | {'Noise':<9} | {'Size':<6} | {'Meta Zstd L3 (R / MB/s)':<22} | {'Meta Zstd L19 (R / MB/s)':<22} | {'BinPack RT (R / MB/s)':<22} | {'BinPack Def (R / MB/s)':<22} | {'Speedup RT'}"
    print(header, flush=True)
    print("-" * 148, flush=True)

    synthetic_matrix = [
        # (size_key, [(dtype, pattern, noise)])
        ("256KB", [("Float32", "ramp", "clean"), ("Float32", "sine", "clean"), ("Int16", "sine", "clean"), ("Int32", "ramp", "clean")]),
        ("1MB",   [("Float32", "ramp", "clean"), ("Float32", "sine", "clean"), ("Float32", "sine", "low_noise"), ("Int16", "sine", "clean"), ("Int32", "ramp", "clean"), ("Float64", "sine", "clean")]),
        ("8MB",   [("Float32", "ramp", "clean"), ("Float32", "sine", "clean"), ("Float32", "random_walk", "clean"), ("Int16", "sine", "clean"), ("Int32", "ramp", "clean")]),
        ("32MB",  [("Float32", "ramp", "clean"), ("Float32", "sine", "clean"), ("Int16", "sine", "clean"), ("Int32", "ramp", "clean")]),
        ("128MB", [("Float32", "ramp", "clean"), ("Float32", "sine", "clean"), ("Int16", "sine", "clean"), ("Int32", "ramp", "clean")]),
    ]

    for sz_key, test_cases in synthetic_matrix:
        target_bytes = SIZES[sz_key]
        for dtype_name, pat, noise in test_cases:
            dtype, itemsize = DTYPES[dtype_name]
            num_elem = target_bytes // itemsize
            data = generate_pattern(pat, dtype, num_elem, noise)
            res = bench_case(data)

            z3 = res["zstd3"]
            z19 = res["zstd19"]
            rt = res["binpack_rt"]
            df = res["binpack_def"]
            speedup = (z19["time_ms"] / rt["time_ms"]) if rt["time_ms"] > 0 else 1.0

            z3_s = f"{z3['ratio']:>5.2f}% @ {z3['c_mbs']:>6.1f} MB/s"
            z19_s = f"{z19['ratio']:>5.2f}% @ {z19['c_mbs']:>6.1f} MB/s"
            rt_s = f"{rt['ratio']:>5.2f}% @ {rt['c_mbs']:>6.1f} MB/s"
            df_s = f"{df['ratio']:>5.2f}% @ {df['c_mbs']:>6.1f} MB/s"

            print(f"{dtype_name:<8} | {pat:<12} | {noise:<9} | {sz_key:<6} | {z3_s:<22} | {z19_s:<22} | {rt_s:<22} | {df_s:<22} | {speedup:>9.1f}x", flush=True)

            record = {
                "id": f"syn_{dtype_name}_{pat}_{noise}_{sz_key}",
                "name": f"Synthetic {dtype_name} {pat} ({noise}, {sz_key})",
                "category": f"Synthetic {sz_key}",
                "type": dtype_name,
                "pattern": pat,
                "noise": noise,
                "size_label": sz_key,
                "raw_bytes": target_bytes,
                "models": {
                    "zstd3": { "name": "Meta Zstd L3", "ratio": z3["ratio"], "comp_speed_mbs": z3["c_mbs"], "decomp_speed_mbs": z3["d_mbs"], "comp_time_ms": z3["time_ms"], "compressed_bytes": z3["size"], "color": "#3b82f6" },
                    "zstd19": { "name": "Meta Zstd L19", "ratio": z19["ratio"], "comp_speed_mbs": z19["c_mbs"], "decomp_speed_mbs": z19["d_mbs"], "comp_time_ms": z19["time_ms"], "compressed_bytes": z19["size"], "color": "#a855f7" },
                    "binpack_rt": { "name": "BinPack v2.2 Runtime", "ratio": rt["ratio"], "comp_speed_mbs": rt["c_mbs"], "decomp_speed_mbs": rt["d_mbs"], "comp_time_ms": rt["time_ms"], "compressed_bytes": rt["size"], "color": "#00f5ff" },
                    "binpack_def": { "name": "BinPack v2.2 Default", "ratio": df["ratio"], "comp_speed_mbs": df["c_mbs"], "decomp_speed_mbs": df["d_mbs"], "comp_time_ms": df["time_ms"], "compressed_bytes": df["size"], "color": "#10b981" }
                }
            }
            all_records.append(record)

    # 3. Export complete unified JSON and JS
    full_json_path = os.path.join(dashboard_dir, "black_hole_data.json")
    with open(full_json_path, "w", encoding="utf-8") as f:
        json.dump(all_records, f, indent=2)

    full_js_path = os.path.join(dashboard_dir, "black_hole_data.js")
    with open(full_js_path, "w", encoding="utf-8") as f:
        f.write("window.BLACK_HOLE_BENCHMARK_DATA = ")
        json.dump(all_records, f, indent=2)
        f.write(";\n")

    print(f"\n[OK] Exported {len(all_records)} total benchmark scenarios to {full_json_path} and {full_js_path}!", flush=True)

if __name__ == "__main__":
    run()
