#!/usr/bin/env python3
"""
Comprehensive Real-World & Industrial Datasets Benchmark
Benchmarks:
  1. Silesia Corpus (all 12 files)
  2. NASA IMS Bearings (Test 1, Test 2, Test 3)
  3. NASA PCoE C-MAPSS Turbofan (Float32 Telemetry & ASCII Text)
  4. IEEE IES Industrial AI Hub (ETTh1, ETTh2 Transformer Temperature)
  5. Embedded Architecture / Sensor Raw (ARM Cortex ELF, IEEE Float64 Raw)
Across:
  - Meta Zstandard Level 3 (Default)
  - Meta Zstandard Level 19 (Ultra)
  - BinPack v2.2 Runtime Fast (level=4, mode="runtime")
  - BinPack v2.2 Default Balanced (level=19, mode="default")
Reports both Ratio (%) AND Comp Speed (MB/s) AND Decomp Speed (MB/s).
"""

import os
import sys
import time
import json
import zstandard as zstd

SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
sys.path.insert(0, os.path.join(SDK_ROOT, "bindings", "python"))
import binpack_sdk

def bench_dataset(name, path, category, description):
    if not os.path.exists(path):
        return None
    with open(path, "rb") as f:
        data = f.read()
    sz = len(data)
    if sz == 0:
        return None

    # 1. Meta Zstd Level 3
    cctx3 = zstd.ZstdCompressor(level=3)
    dctx = zstd.ZstdDecompressor()
    t0 = time.perf_counter()
    c3 = cctx3.compress(data)
    t_c3 = time.perf_counter() - t0
    t0 = time.perf_counter()
    d3 = dctx.decompress(c3, max_output_size=sz + 4096)
    t_d3 = time.perf_counter() - t0
    assert d3 == data

    # 2. Meta Zstd Level 19 (Multithreaded for speed)
    cctx19 = zstd.ZstdCompressor(level=19, threads=-1)
    t0 = time.perf_counter()
    c19 = cctx19.compress(data)
    t_c19 = time.perf_counter() - t0
    t0 = time.perf_counter()
    d19 = dctx.decompress(c19, max_output_size=sz + 4096)
    t_d19 = time.perf_counter() - t0
    assert d19 == data

    # 3. BinPack Runtime Fast
    t0 = time.perf_counter()
    c_bp_rt = binpack_sdk.compress(data, level=4, filter_mode="auto", mode="runtime")
    t_c_rt = time.perf_counter() - t0
    t0 = time.perf_counter()
    d_bp_rt = binpack_sdk.decompress(c_bp_rt, verify_checksum=False)
    t_d_rt = time.perf_counter() - t0
    assert d_bp_rt == data

    # 4. BinPack Default Balanced (level=19)
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

    record = {
        "id": f"real_{name}",
        "name": f"{name} ({description})",
        "category": category,
        "type": category,
        "pattern": "real_world",
        "noise": "real",
        "size_label": f"{sz / 1048576.0:.2f} MB",
        "raw_bytes": sz,
        "models": {
            "zstd3": {
                "name": "Meta Zstd L3",
                "ratio": round(r_z3, 2),
                "comp_speed_mbs": round(cs_z3, 1),
                "decomp_speed_mbs": round(ds_z3, 1),
                "comp_time_ms": round(t_c3 * 1000.0, 1),
                "compressed_bytes": len(c3),
                "color": "#3b82f6"
            },
            "zstd19": {
                "name": "Meta Zstd L19",
                "ratio": round(r_z19, 2),
                "comp_speed_mbs": round(cs_z19, 1),
                "decomp_speed_mbs": round(ds_z19, 1),
                "comp_time_ms": round(t_c19 * 1000.0, 1),
                "compressed_bytes": len(c19),
                "color": "#a855f7"
            },
            "binpack_rt": {
                "name": "BinPack v2.2 Runtime",
                "ratio": round(r_rt, 2),
                "comp_speed_mbs": round(cs_rt, 1),
                "decomp_speed_mbs": round(ds_rt, 1),
                "comp_time_ms": round(t_c_rt * 1000.0, 1),
                "compressed_bytes": len(c_bp_rt),
                "color": "#00f5ff"
            },
            "binpack_def": {
                "name": "BinPack v2.2 Default",
                "ratio": round(r_def, 2),
                "comp_speed_mbs": round(cs_def, 1),
                "decomp_speed_mbs": round(ds_def, 1),
                "comp_time_ms": round(t_c_def * 1000.0, 1),
                "compressed_bytes": len(c_bp_def),
                "color": "#10b981"
            }
        }
    }
    return record

def main():
    silesia_dir = "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia"
    nasa_dir = "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry"

    datasets = [
        # --- 1. Silesia Corpus (Full 12 files) ---
        ("ooffice", os.path.join(silesia_dir, "ooffice"), "Silesia (x86 ELF)", "OpenOffice 1.0.1 Executable (6.15 MB)"),
        ("x-ray", os.path.join(silesia_dir, "x-ray"), "Silesia (Medical 16b)", "16-bit Medical X-Ray Scans (8.47 MB)"),
        ("mr", os.path.join(silesia_dir, "mr"), "Silesia (Medical 16b)", "16-bit Magnetic Resonance Imaging (9.97 MB)"),
        ("sao", os.path.join(silesia_dir, "sao"), "Silesia (Astrometry)", "Smithsonian Astrophysical Star Catalog (7.25 MB)"),
        ("mozilla", os.path.join(silesia_dir, "mozilla"), "Silesia (Shared Libs)", "Mozilla Browser Binary Tar (51.22 MB)"),
        ("dickens", os.path.join(silesia_dir, "dickens"), "Silesia (Literature)", "Charles Dickens Collected Works (10.19 MB)"),
        ("nci", os.path.join(silesia_dir, "nci"), "Silesia (Chemical DB)", "National Cancer Institute Molecule DB (33.55 MB)"),
        ("osdb", os.path.join(silesia_dir, "osdb"), "Silesia (Postgres DB)", "Open Source Database Benchmark (10.09 MB)"),
        ("reymont", os.path.join(silesia_dir, "reymont"), "Silesia (Polish Text)", "Wladyslaw Reymont Novel Chlopi (6.63 MB)"),
        ("samba", os.path.join(silesia_dir, "samba"), "Silesia (Source Code)", "Samba 2-2-3 Source Code Tarball (21.61 MB)"),
        ("webster", os.path.join(silesia_dir, "webster"), "Silesia (HTML Dict)", "1913 Webster English Dictionary HTML (41.46 MB)"),
        ("xml", os.path.join(silesia_dir, "xml"), "Silesia (XML Code)", "Structured XML Data and DTD Schemas (5.35 MB)"),

        # --- 2. NASA IMS Bearings (data.nasa.gov) ---
        ("nasa_bearing_ims_test1", os.path.join(nasa_dir, "nasa_bearing_ims_test1.csv"), "NASA IMS Bearing", "Run-to-failure 4-Ch Accelerometer Test 1 (1.91 MB)"),
        ("nasa_bearing_ims_test2", os.path.join(nasa_dir, "nasa_bearing_ims_test2.csv"), "NASA IMS Bearing", "Run-to-failure 4-Ch Accelerometer Test 2 (0.46 MB)"),
        ("nasa_bearing_ims_test3", os.path.join(nasa_dir, "nasa_bearing_ims_test3.csv"), "NASA IMS Bearing", "Run-to-failure 4-Ch Accelerometer Test 3 (2.97 MB)"),

        # --- 3. NASA PCoE C-MAPSS Turbofan ---
        ("nasa_turbofan_float32", os.path.join(nasa_dir, "nasa_turbofan_float32.bin"), "NASA Turbofan", "C-MAPSS 21-Sensor Binary Float32 Array (1.98 MB)"),
        ("nasa_turbofan_ascii", os.path.join(nasa_dir, "nasa_turbofan_ascii.txt"), "NASA Turbofan", "C-MAPSS Degradation Trajectory Text (3.52 MB)"),

        # --- 4. IEEE IES Industrial AI Hub (ETTh1 / ETTh2) ---
        ("etth1_transformer", os.path.join(nasa_dir, "etth1_transformer.csv"), "IEEE Industrial", "Electricity Transformer Oil Temp 1-Hour (2.59 MB)"),
        ("etth2_transformer", os.path.join(nasa_dir, "etth2_transformer.csv"), "IEEE Industrial", "Electricity Transformer Oil Temp 1-Hour (2.42 MB)"),

        # --- 5. Microcontrollers & Sensors ---
        ("sensor_data.raw", "C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", "Sensor Matrix", "IEEE 754 Float64 Multichannel Matrix (256 KB)"),
        ("arm_binary.elf", "C:/Users/sgi.lm/Desktop/Compiler/arm_binary.elf", "Embedded ARM", "ARM Cortex Thumb-2 Microcontroller Firmware (100 KB)")
    ]

    print("=" * 148, flush=True)
    print(f"{'Dataset / Corpus':<28} | {'Type':<18} | {'Size':<8} | {'Meta Zstd L3 (R / MB/s)':<22} | {'Meta Zstd L19 (R / MB/s)':<22} | {'BinPack RT (R / MB/s)':<22} | {'BinPack Def (R / MB/s)':<22}", flush=True)
    print("=" * 148, flush=True)

    results = []
    for name, path, cat, desc in datasets:
        rec = bench_dataset(name, path, cat, desc)
        if not rec:
            continue
        results.append(rec)
        m = rec["models"]
        z3_str = f"{m['zstd3']['ratio']:>5.2f}% @ {m['zstd3']['comp_speed_mbs']:>6.1f} MB/s"
        z19_str = f"{m['zstd19']['ratio']:>5.2f}% @ {m['zstd19']['comp_speed_mbs']:>6.1f} MB/s"
        rt_str = f"{m['binpack_rt']['ratio']:>5.2f}% @ {m['binpack_rt']['comp_speed_mbs']:>6.1f} MB/s"
        def_str = f"{m['binpack_def']['ratio']:>5.2f}% @ {m['binpack_def']['comp_speed_mbs']:>6.1f} MB/s"
        print(f"{name:<28} | {cat:<18} | {rec['size_label']:<8} | {z3_str:<22} | {z19_str:<22} | {rt_str:<22} | {def_str:<22}", flush=True)

    print("=" * 148, flush=True)

    # Export to real_datasets.json
    out_dir = os.path.join(SDK_ROOT, "dashboard")
    os.makedirs(out_dir, exist_ok=True)
    out_json = os.path.join(out_dir, "real_datasets.json")
    with open(out_json, "w", encoding="utf-8") as f:
        json.dump(results, f, indent=2)
    print(f"\n[OK] Benchmarked {len(results)} real datasets. Saved to {out_json}", flush=True)

if __name__ == "__main__":
    main()
