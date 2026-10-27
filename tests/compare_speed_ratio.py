import os
import sys
import time
import zstandard as zstd

SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BINDINGS_PATH = os.path.join(SDK_ROOT, "bindings", "python")
if BINDINGS_PATH not in sys.path:
    sys.path.insert(0, BINDINGS_PATH)

import binpack_sdk

FILES = [
    ("ooffice (x86 ELF)", "C:/Users/sgi.lm/Desktop/Compiler/ooffice", "x86"),
    ("x-ray (16-bit Medical)", "C:/Users/sgi.lm/Desktop/Compiler/x-ray", "auto"),
    ("mr (16-bit MRI)", "C:/Users/sgi.lm/Desktop/Compiler/mr", "auto"),
    ("sensor_data.raw (64-bit Floats)", "C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", "transpose"),
    ("arm_binary.elf (ARM32 Code)", "C:/Users/sgi.lm/Desktop/Compiler/arm_binary.elf", "arm32"),
]

def bench(name, fn_comp, fn_decomp, data, iters=3):
    sz = len(data)
    # Warmup
    c = fn_comp(data)
    d = fn_decomp(c)
    assert d == data, f"{name}: Roundtrip failed!"

    c_times = []
    d_times = []
    for _ in range(iters):
        t0 = time.perf_counter()
        c = fn_comp(data)
        t1 = time.perf_counter()
        c_times.append(t1 - t0)

        t2 = time.perf_counter()
        d = fn_decomp(c)
        t3 = time.perf_counter()
        d_times.append(t3 - t2)

    c_med = min(c_times)
    d_med = min(d_times)
    c_sz = len(c)
    ratio = (c_sz / sz) * 100.0
    c_mbs = (sz / 1048576.0) / (c_med if c_med > 0 else 1e-6)
    d_mbs = (sz / 1048576.0) / (d_med if d_med > 0 else 1e-6)

    return {
        "engine": name,
        "size": c_sz,
        "ratio": ratio,
        "c_time_ms": c_med * 1000.0,
        "c_speed_mbs": c_mbs,
        "d_speed_mbs": d_mbs
    }

print("RUNNING BENCHMARKS...")
for label, path, filter_mode in FILES:
    if not os.path.exists(path):
        continue
    with open(path, "rb") as f:
        data = f.read()
    sz = len(data)
    print(f"\n==========================================================================================")
    print(f"PAYLOAD: {label} ({sz:,} bytes / {sz/1048576:.2f} MB)")
    print(f"==========================================================================================")
    print(f"{'Engine / Preset':<30} | {'Size (B)':<10} | {'Ratio':<7} | {'Comp Time':<10} | {'Comp MB/s':<10} | {'Decomp MB/s':<10}")
    print(f"-" * 90)

    # 1. Zstd Level 1 (Single thread)
    z1_c = zstd.ZstdCompressor(level=1)
    z_d = zstd.ZstdDecompressor()
    r_z1 = bench("Meta Zstandard L1 (Single)", lambda d: z1_c.compress(d), lambda c: z_d.decompress(c, max_output_size=sz+1024), data)
    print(f"{r_z1['engine']:<30} | {r_z1['size']:>10,} | {r_z1['ratio']:>6.2f}% | {r_z1['c_time_ms']:>7.2f} ms | {r_z1['c_speed_mbs']:>8.1f} | {r_z1['d_speed_mbs']:>8.1f}")

    # 2. Zstd Level 3 (Default)
    z3_c = zstd.ZstdCompressor(level=3)
    r_z3 = bench("Meta Zstandard L3 (Default)", lambda d: z3_c.compress(d), lambda c: z_d.decompress(c, max_output_size=sz+1024), data)
    print(f"{r_z3['engine']:<30} | {r_z3['size']:>10,} | {r_z3['ratio']:>6.2f}% | {r_z3['c_time_ms']:>7.2f} ms | {r_z3['c_speed_mbs']:>8.1f} | {r_z3['d_speed_mbs']:>8.1f}")

    # 3. Zstd Level 19 (Ultra)
    z19_c = zstd.ZstdCompressor(level=19)
    r_z19 = bench("Meta Zstandard L19 (Ultra)", lambda d: z19_c.compress(d), lambda c: z_d.decompress(c, max_output_size=sz+1024), data)
    print(f"{r_z19['engine']:<30} | {r_z19['size']:>10,} | {r_z19['ratio']:>6.2f}% | {r_z19['c_time_ms']:>7.2f} ms | {r_z19['c_speed_mbs']:>8.1f} | {r_z19['d_speed_mbs']:>8.1f}")

    # 4. BinPack Runtime Fast
    r_bp_rt = bench("BinPack v2.2 (Runtime Fast)", lambda d: binpack_sdk.compress(d, level=4, filter_mode=filter_mode, mode="runtime"), lambda c: binpack_sdk.decompress(c, verify_checksum=False), data)
    print(f"{r_bp_rt['engine']:<30} | {r_bp_rt['size']:>10,} | {r_bp_rt['ratio']:>6.2f}% | {r_bp_rt['c_time_ms']:>7.2f} ms | {r_bp_rt['c_speed_mbs']:>8.1f} | {r_bp_rt['d_speed_mbs']:>8.1f}")

    # 5. BinPack Default
    r_bp_def = bench("BinPack v2.2 (Default Balanced)", lambda d: binpack_sdk.compress(d, level=19, filter_mode=filter_mode, mode="default"), lambda c: binpack_sdk.decompress(c, verify_checksum=False), data)
    print(f"{r_bp_def['engine']:<30} | {r_bp_def['size']:>10,} | {r_bp_def['ratio']:>6.2f}% | {r_bp_def['c_time_ms']:>7.2f} ms | {r_bp_def['c_speed_mbs']:>8.1f} | {r_bp_def['d_speed_mbs']:>8.1f}")
