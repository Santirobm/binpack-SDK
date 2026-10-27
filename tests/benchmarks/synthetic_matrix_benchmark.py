#!/usr/bin/env python3
"""
BinPack Enterprise SDK - Controlled Synthetic Matrix Scientific Benchmark
Evaluates structural preconditioning performance across controlled mathematical patterns,
data types, noise levels, and scaling sizes (256 KB to 128 MB).

Aims to scientifically answer:
  - Exactly when and why does BinPack outperform standard LZ77/FSE?
  - What is the breakdown between entropy gain vs. throughput across memory sizes?
  - How does noise degrade stride transpose and delta normalization?
"""

import os
import sys
import time
import argparse
import numpy as np
import zstandard as zstd

SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
BINDINGS_PATH = os.path.join(SDK_ROOT, "bindings", "python")
if BINDINGS_PATH not in sys.path:
    sys.path.insert(0, BINDINGS_PATH)

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

PATTERNS = ["ramp", "sine", "random_walk", "repeated", "pure_noise"]
NOISE_LEVELS = ["clean", "low_noise", "high_noise"]

def generate_pattern(pattern_name, dtype, num_elements, noise_level="clean"):
    np.random.seed(42)  # Deterministic repeatability

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

    elif pattern_name == "repeated":
        block_sz = 16
        num_blocks = num_elements // block_sz
        if np.issubdtype(dtype, np.floating):
            vals = np.random.uniform(10.0, 500.0, num_blocks).astype(dtype)
        else:
            vals = np.random.randint(100, 30000, num_blocks).astype(dtype)
        arr = np.repeat(vals, block_sz).astype(dtype)
        if len(arr) < num_elements:
            arr = np.pad(arr, (0, num_elements - len(arr)), mode='edge')

    elif pattern_name == "pure_noise":
        if np.issubdtype(dtype, np.floating):
            arr = np.random.uniform(-1000.0, 1000.0, num_elements).astype(dtype)
        else:
            arr = np.random.randint(-32000, 32000, num_elements).astype(dtype)

    else:
        raise ValueError(f"Unknown pattern: {pattern_name}")

    # Noise Injection
    if noise_level != "clean" and pattern_name != "pure_noise":
        if noise_level == "low_noise":
            # Noise impacting only the lower bits of precision
            if np.issubdtype(dtype, np.floating):
                noise = np.random.normal(0, 0.0001, num_elements).astype(dtype)
            else:
                noise = np.random.choice([-1, 0, 1], num_elements).astype(dtype)
        elif noise_level == "high_noise":
            # Significant noise across the whole dynamic range
            if np.issubdtype(dtype, np.floating):
                noise = np.random.normal(0, 10.0, num_elements).astype(dtype)
            else:
                noise = np.random.randint(-500, 500, num_elements).astype(dtype)
        arr = arr + noise

    return arr.tobytes()

def bench_case(data, iters=2):
    sz = len(data)
    results = {}

    # 1. Meta Zstd Level 3 (Default)
    cctx3 = zstd.ZstdCompressor(level=3)
    dctx = zstd.ZstdDecompressor()
    c3 = cctx3.compress(data)
    d3 = dctx.decompress(c3, max_output_size=sz + 1024)
    assert d3 == data

    t_c3 = []
    t_d3 = []
    for _ in range(iters):
        t0 = time.perf_counter()
        c3 = cctx3.compress(data)
        t_c3.append(time.perf_counter() - t0)
        t0 = time.perf_counter()
        dctx.decompress(c3, max_output_size=sz + 1024)
        t_d3.append(time.perf_counter() - t0)

    time_c3 = min(t_c3)
    time_d3 = min(t_d3)
    results["zstd3"] = {
        "size": len(c3),
        "ratio": (len(c3) / sz) * 100.0,
        "c_mbs": (sz / 1048576.0) / (time_c3 if time_c3 > 0 else 1e-6),
        "d_mbs": (sz / 1048576.0) / (time_d3 if time_d3 > 0 else 1e-6),
        "time_ms": time_c3 * 1000.0
    }

    # 2. Meta Zstd Level 19 (Ultra)
    cctx19 = zstd.ZstdCompressor(level=19)
    c19 = cctx19.compress(data)
    t_c19 = []
    t_d19 = []
    for _ in range(iters):
        t0 = time.perf_counter()
        c19 = cctx19.compress(data)
        t_c19.append(time.perf_counter() - t0)
        t0 = time.perf_counter()
        dctx.decompress(c19, max_output_size=sz + 1024)
        t_d19.append(time.perf_counter() - t0)
    time_c19 = min(t_c19)
    time_d19 = min(t_d19)
    results["zstd19"] = {
        "size": len(c19),
        "ratio": (len(c19) / sz) * 100.0,
        "c_mbs": (sz / 1048576.0) / (time_c19 if time_c19 > 0 else 1e-6),
        "d_mbs": (sz / 1048576.0) / (time_d19 if time_d19 > 0 else 1e-6),
        "time_ms": time_c19 * 1000.0
    }

    # 3. BinPack Runtime Fast (level=4, mode="runtime")
    c_bp_rt = binpack_sdk.compress(data, level=4, filter_mode="auto", mode="runtime")
    d_bp_rt = binpack_sdk.decompress(c_bp_rt, verify_checksum=False)
    assert d_bp_rt == data

    t_c_bp = []
    t_d_bp = []
    for _ in range(iters):
        t0 = time.perf_counter()
        c_bp_rt = binpack_sdk.compress(data, level=4, filter_mode="auto", mode="runtime")
        t_c_bp.append(time.perf_counter() - t0)
        t0 = time.perf_counter()
        binpack_sdk.decompress(c_bp_rt, verify_checksum=False)
        t_d_bp.append(time.perf_counter() - t0)

    time_c_bp = min(t_c_bp)
    time_d_bp = min(t_d_bp)
    results["binpack_rt"] = {
        "size": len(c_bp_rt),
        "ratio": (len(c_bp_rt) / sz) * 100.0,
        "c_mbs": (sz / 1048576.0) / (time_c_bp if time_c_bp > 0 else 1e-6),
        "d_mbs": (sz / 1048576.0) / (time_d_bp if time_d_bp > 0 else 1e-6),
        "time_ms": time_c_bp * 1000.0
    }

    # 4. BinPack Default Balanced (level=19, mode="default")
    c_bp_def = binpack_sdk.compress(data, level=19, filter_mode="auto", mode="default")
    d_bp_def = binpack_sdk.decompress(c_bp_def, verify_checksum=False)
    assert d_bp_def == data

    t_c_def = []
    t_d_def = []
    for _ in range(iters):
        t0 = time.perf_counter()
        c_bp_def = binpack_sdk.compress(data, level=19, filter_mode="auto", mode="default")
        t_c_def.append(time.perf_counter() - t0)
        t0 = time.perf_counter()
        binpack_sdk.decompress(c_bp_def, verify_checksum=False)
        t_d_def.append(time.perf_counter() - t0)
    time_c_def = min(t_c_def)
    time_d_def = min(t_d_def)
    results["binpack_def"] = {
        "size": len(c_bp_def),
        "ratio": (len(c_bp_def) / sz) * 100.0,
        "c_mbs": (sz / 1048576.0) / (time_c_def if time_c_def > 0 else 1e-6),
        "d_mbs": (sz / 1048576.0) / (time_d_def if time_d_def > 0 else 1e-6),
        "time_ms": time_c_def * 1000.0
    }

    return results

def run_controlled_matrix(size_keys, quick=False):
    import json
    print("=" * 155)
    print("BINPACK CONTROLLED SYNTHETIC BENCHMARK MATRIX — COMPLETE RATIO & SPEED COMPARISON")
    print(f"Sizes Evaluated: {', '.join(size_keys)} | Data Types: Float32, Float64, Int16, Int32")
    print("Patterns: Ramp, Sine Wave, Random Walk (Brownian), Repeated Blocks, Pure Noise")
    print("Noise Levels: Clean (0%), Low Noise (Least Significant Bits), High Noise")
    print("=" * 155)

    header = (
        f"{'Type':<8} | {'Pattern':<11} | {'Noise':<9} | {'Size':<6} | "
        f"{'Zstd L3 (R / MB/s)':<22} | {'Zstd L19 (R / MB/s)':<23} | "
        f"{'BinPack RT (R / MB/s)':<24} | {'BinPack Def (R / MB/s)':<24} | {'Speedup RT'}"
    )
    print(header)
    print("-" * 155)

    summary_rows = []
    dashboard_payloads = []

    for sz_key in size_keys:
        target_bytes = SIZES[sz_key]

        for dtype_name, (dtype, itemsize) in DTYPES.items():
            num_elem = target_bytes // itemsize

            if sz_key in ["32MB", "128MB"]:
                patterns_to_test = ["ramp", "sine", "random_walk"]
                noise_levels = ["clean"]
                cur_iters = 1
            elif sz_key == "8MB":
                patterns_to_test = ["ramp", "sine", "random_walk"]
                noise_levels = ["clean", "low_noise"]
                cur_iters = 1
            else:
                patterns_to_test = ["ramp", "sine", "random_walk"] if quick else PATTERNS
                noise_levels = ["clean", "low_noise"] if quick else NOISE_LEVELS
                cur_iters = 1 if target_bytes >= 32*1024*1024 else 2

            for pat in patterns_to_test:
                for noise in noise_levels:
                    if pat == "pure_noise" and noise != "clean":
                        continue  # Noise on noise is redundant

                    data = generate_pattern(pat, dtype, num_elem, noise)
                    res = bench_case(data, iters=cur_iters)

                    z3_r = res["zstd3"]["ratio"]
                    z3_s = res["zstd3"]["c_mbs"]
                    z19_r = res["zstd19"]["ratio"]
                    z19_s = res["zstd19"]["c_mbs"]
                    bp_rt_r = res["binpack_rt"]["ratio"]
                    bp_rt_s = res["binpack_rt"]["c_mbs"]
                    bp_def_r = res["binpack_def"]["ratio"]
                    bp_def_s = res["binpack_def"]["c_mbs"]

                    z19_time = res["zstd19"]["time_ms"]
                    bp_time = res["binpack_rt"]["time_ms"]
                    speedup = (z19_time / bp_time) if bp_time > 0 else 1.0

                    z3_cell = f"{z3_r:>5.1f}% @ {z3_s:>6.1f} MB/s"
                    z19_cell = f"{z19_r:>5.1f}% @ {z19_s:>6.1f} MB/s"
                    bp_rt_cell = f"{bp_rt_r:>5.1f}% @ {bp_rt_s:>6.1f} MB/s"
                    bp_def_cell = f"{bp_def_r:>5.1f}% @ {bp_def_s:>6.1f} MB/s"

                    row_str = (
                        f"{dtype_name:<8} | {pat:<11} | {noise:<9} | {sz_key:<6} | "
                        f"{z3_cell:<22} | {z19_cell:<23} | "
                        f"{bp_rt_cell:<24} | {bp_def_cell:<24} | {speedup:>9.1f}x"
                    )
                    print(row_str)

                    item_record = {
                        "id": f"{dtype_name}_{pat}_{noise}_{sz_key}",
                        "name": f"{dtype_name} {pat} ({noise}, {sz_key})",
                        "type": dtype_name,
                        "pattern": pat,
                        "noise": noise,
                        "size_label": sz_key,
                        "raw_bytes": target_bytes,
                        "models": {
                            "zstd3": {
                                "name": "Meta Zstd L3 (Default)",
                                "compressed_bytes": res["zstd3"]["size"],
                                "ratio": z3_r,
                                "comp_speed_mbs": z3_s,
                                "decomp_speed_mbs": res["zstd3"]["d_mbs"],
                                "comp_time_ms": res["zstd3"]["time_ms"],
                                "color": "#3b82f6"
                            },
                            "zstd19": {
                                "name": "Meta Zstd L19 (Ultra)",
                                "compressed_bytes": res["zstd19"]["size"],
                                "ratio": z19_r,
                                "comp_speed_mbs": z19_s,
                                "decomp_speed_mbs": res["zstd19"]["d_mbs"],
                                "comp_time_ms": res["zstd19"]["time_ms"],
                                "color": "#a855f7"
                            },
                            "binpack_rt": {
                                "name": "BinPack v2.2 Runtime Fast",
                                "compressed_bytes": res["binpack_rt"]["size"],
                                "ratio": bp_rt_r,
                                "comp_speed_mbs": bp_rt_s,
                                "decomp_speed_mbs": res["binpack_rt"]["d_mbs"],
                                "comp_time_ms": res["binpack_rt"]["time_ms"],
                                "color": "#00f5ff"
                            },
                            "binpack_def": {
                                "name": "BinPack v2.2 Default Balanced",
                                "compressed_bytes": res["binpack_def"]["size"],
                                "ratio": bp_def_r,
                                "comp_speed_mbs": bp_def_s,
                                "decomp_speed_mbs": res["binpack_def"]["d_mbs"],
                                "comp_time_ms": res["binpack_def"]["time_ms"],
                                "color": "#10b981"
                            }
                        }
                    }
                    dashboard_payloads.append(item_record)

    # Complete Real Datasets from Silesia & NASA
    silesia_dir = "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia"
    nasa_dir = "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry"

    real_files = [
        # Silesia Corpus (Full 12 files)
        ("ooffice", os.path.join(silesia_dir, "ooffice"), "x86 Code", "Silesia x86 ELF Executable (6.15 MB)"),
        ("x-ray", os.path.join(silesia_dir, "x-ray"), "Medical 16b", "Silesia 16-bit Medical Image (8.47 MB)"),
        ("mr", os.path.join(silesia_dir, "mr"), "Medical 16b", "Silesia 16-bit MRI Resonance (9.97 MB)"),
        ("sao", os.path.join(silesia_dir, "sao"), "Astro Star", "Silesia Astrometric Star Catalog (7.25 MB)"),
        ("mozilla", os.path.join(silesia_dir, "mozilla"), "Shared Libs", "Silesia Browser Shared Libs (51.22 MB)"),
        ("dickens", os.path.join(silesia_dir, "dickens"), "Text", "Silesia English Literature (10.19 MB)"),
        ("nci", os.path.join(silesia_dir, "nci"), "Chemical DB", "Silesia Chemical Structure DB (33.55 MB)"),
        ("osdb", os.path.join(silesia_dir, "osdb"), "Database", "Silesia Postgres DB Dump (10.09 MB)"),
        ("reymont", os.path.join(silesia_dir, "reymont"), "Polish Text", "Silesia Polish Literature (6.63 MB)"),
        ("samba", os.path.join(silesia_dir, "samba"), "Source Tar", "Silesia Source Code Tarball (21.61 MB)"),
        ("webster", os.path.join(silesia_dir, "webster"), "HTML Dict", "Silesia HTML English Dictionary (41.46 MB)"),
        ("xml", os.path.join(silesia_dir, "xml"), "Structured XML", "Silesia XML Schema & Code (5.35 MB)"),

        # NASA Industrial Telemetry & Bearing Vibrations (NASA PCoE & IMS)
        ("nasa_bearing_ims_test1", os.path.join(nasa_dir, "nasa_bearing_ims_test1.csv"), "Bearing IMS", "NASA IMS Bearing 4-Ch Accelerometer Test 1 (1.91 MB)"),
        ("nasa_bearing_ims_test2", os.path.join(nasa_dir, "nasa_bearing_ims_test2.csv"), "Bearing IMS", "NASA IMS Bearing 4-Ch Accelerometer Test 2 (0.46 MB)"),
        ("nasa_bearing_ims_test3", os.path.join(nasa_dir, "nasa_bearing_ims_test3.csv"), "Bearing IMS", "NASA IMS Bearing 4-Ch Accelerometer Test 3 (2.97 MB)"),
        ("nasa_turbofan_float32", os.path.join(nasa_dir, "nasa_turbofan_float32.bin"), "Float32 Telemetry", "NASA C-MAPSS 21-Sensor Floats (1.98 MB)"),
        ("nasa_turbofan_ascii", os.path.join(nasa_dir, "nasa_turbofan_ascii.txt"), "Turbofan ASCII", "NASA C-MAPSS Turbofan RUL Text (3.52 MB)"),
        ("sensor_data.raw", "C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", "Float64 Matrix", "IEEE 754 Float64 Telemetry Matrix (256 KB)"),
        ("arm_binary.elf", "C:/Users/sgi.lm/Desktop/Compiler/arm_binary.elf", "ARM Code", "ARM Cortex-M Thumb-2 Executable (100 KB)")
    ]

    print("\n" + "=" * 155)
    print("REAL-WORLD DATASETS BENCHMARK (SILESIA & NASA INDUSTRIAL TELEMETRY)")
    print("=" * 155)
    print(header)
    print("-" * 155)

    for r_name, r_path, r_type, r_desc in real_files:
        if not os.path.exists(r_path):
            continue
        with open(r_path, "rb") as f:
            data = f.read()
        target_bytes = len(data)
        res = bench_case(data, iters=1 if target_bytes >= 32*1024*1024 else 2)

        z3_r = res["zstd3"]["ratio"]
        z3_s = res["zstd3"]["c_mbs"]
        z19_r = res["zstd19"]["ratio"]
        z19_s = res["zstd19"]["c_mbs"]
        bp_rt_r = res["binpack_rt"]["ratio"]
        bp_rt_s = res["binpack_rt"]["c_mbs"]
        bp_def_r = res["binpack_def"]["ratio"]
        bp_def_s = res["binpack_def"]["c_mbs"]

        z19_time = res["zstd19"]["time_ms"]
        bp_time = res["binpack_rt"]["time_ms"]
        speedup = (z19_time / bp_time) if bp_time > 0 else 1.0

        z3_cell = f"{z3_r:>5.1f}% @ {z3_s:>6.1f} MB/s"
        z19_cell = f"{z19_r:>5.1f}% @ {z19_s:>6.1f} MB/s"
        bp_rt_cell = f"{bp_rt_r:>5.1f}% @ {bp_rt_s:>6.1f} MB/s"
        bp_def_cell = f"{bp_def_r:>5.1f}% @ {bp_def_s:>6.1f} MB/s"

        row_str = (
            f"{r_type:<8} | {r_name:<11} | {'real':<9} | {target_bytes/1048576:>4.1f}MB | "
            f"{z3_cell:<22} | {z19_cell:<23} | "
            f"{bp_rt_cell:<24} | {bp_def_cell:<24} | {speedup:>9.1f}x"
        )
        print(row_str)

        item_record = {
            "id": f"real_{r_name}",
            "name": f"{r_name} ({r_desc})",
            "type": r_type,
            "pattern": "real_data",
            "noise": "real",
            "size_label": f"{target_bytes/1048576:.2f} MB",
            "raw_bytes": target_bytes,
            "models": {
                "zstd3": {
                    "name": "Meta Zstd L3 (Default)",
                    "compressed_bytes": res["zstd3"]["size"],
                    "ratio": z3_r,
                    "comp_speed_mbs": z3_s,
                    "decomp_speed_mbs": res["zstd3"]["d_mbs"],
                    "comp_time_ms": res["zstd3"]["time_ms"],
                    "color": "#3b82f6"
                },
                "zstd19": {
                    "name": "Meta Zstd L19 (Ultra)",
                    "compressed_bytes": res["zstd19"]["size"],
                    "ratio": z19_r,
                    "comp_speed_mbs": z19_s,
                    "decomp_speed_mbs": res["zstd19"]["d_mbs"],
                    "comp_time_ms": res["zstd19"]["time_ms"],
                    "color": "#a855f7"
                },
                "binpack_rt": {
                    "name": "BinPack v2.2 Runtime Fast",
                    "compressed_bytes": res["binpack_rt"]["size"],
                    "ratio": bp_rt_r,
                    "comp_speed_mbs": bp_rt_s,
                    "decomp_speed_mbs": res["binpack_rt"]["d_mbs"],
                    "comp_time_ms": res["binpack_rt"]["time_ms"],
                    "color": "#00f5ff"
                },
                "binpack_def": {
                    "name": "BinPack v2.2 Default Balanced",
                    "compressed_bytes": res["binpack_def"]["size"],
                    "ratio": bp_def_r,
                    "comp_speed_mbs": bp_def_s,
                    "decomp_speed_mbs": res["binpack_def"]["d_mbs"],
                    "comp_time_ms": res["binpack_def"]["time_ms"],
                    "color": "#10b981"
                }
            }
        }
        dashboard_payloads.append(item_record)

    # Save to dashboard json
    dashboard_dir = os.path.join(SDK_ROOT, "dashboard")
    os.makedirs(dashboard_dir, exist_ok=True)
    json_path = os.path.join(dashboard_dir, "black_hole_data.json")
    with open(json_path, "w", encoding="utf-8") as f:
        json.dump(dashboard_payloads, f, indent=2)
    print(f"\n[OK] Complete structured benchmark data exported to: {json_path}")

    return dashboard_payloads

if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="Synthetic Matrix Benchmark Runner")
    parser.add_argument("--sizes", nargs="+", default=["256KB", "1MB", "8MB"], help="Sizes to run (e.g. 256KB 1MB 8MB 32MB 128MB)")
    parser.add_argument("--quick", action="store_true", help="Run quick representative subset")
    args = parser.parse_args()

    run_controlled_matrix(args.sizes, quick=args.quick)
