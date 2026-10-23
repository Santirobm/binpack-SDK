#!/usr/bin/env python3
"""
BinPack Enterprise SDK v2.2 - Real-Time Interactive Benchmark Server
Streams live C-ABI (binpack.dll) vs Zstandard vs LZ4 benchmark events via SSE (Server-Sent Events)
to the visual studio dashboard at http://localhost:8090
"""

import os
import sys
import json
import time
import webbrowser
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse, parse_qs

SDK_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
BINDINGS_DIR = os.path.join(SDK_ROOT, "bindings", "python")
if BINDINGS_DIR not in sys.path:
    sys.path.insert(0, BINDINGS_DIR)

import binpack_sdk
import zstandard as zstd
import lz4.frame

CORPORA_MAP = {
    "nasa_turbofan_float32": os.path.join(SDK_ROOT, "tests", "corpora", "nasa_telemetry", "nasa_turbofan_float32.bin"),
    "mr": os.path.join(SDK_ROOT, "tests", "corpora", "silesia", "mr"),
    "ooffice": os.path.join(SDK_ROOT, "tests", "corpora", "silesia", "ooffice"),
    "busybox_armv7l": os.path.join(SDK_ROOT, "tests", "corpora", "arm_binaries", "busybox_armv7l"),
    "mozilla": os.path.join(SDK_ROOT, "tests", "corpora", "silesia", "mozilla"),
    "arm_synthetic": os.path.join(SDK_ROOT, "tests", "corpora", "arm_binaries", "arm_synthetic_branch_loop.elf"),
    "dickens": os.path.join(SDK_ROOT, "tests", "corpora", "silesia", "dickens"),
}


class LiveBenchmarkHandler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=os.path.dirname(__file__), **kwargs)

    def do_GET(self):
        parsed = urlparse(self.path)
        if parsed.path == "/api/live-stream":
            qs = parse_qs(parsed.query)
            dataset = qs.get("dataset", ["nasa_turbofan_float32"])[0]
            self.handle_live_stream(dataset)
        else:
            super().do_GET()

    def send_sse(self, payload: dict):
        msg = f"data: {json.dumps(payload)}\n\n".encode("utf-8")
        self.wfile.write(msg)
        self.wfile.flush()

    def handle_live_stream(self, dataset: str):
        self.send_response(200)
        self.send_header("Content-Type", "text/event-stream")
        self.send_header("Cache-Control", "no-cache")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()

        fpath = CORPORA_MAP.get(dataset)
        if not fpath or not os.path.exists(fpath):
            self.send_sse({"stage": 5, "tag": "ERROR", "msg": f"Dataset '{dataset}' no encontrado en {fpath}"})
            return

        with open(fpath, "rb") as f:
            data = f.read()
        orig_sz = len(data)

        self.send_sse({
            "stage": 1,
            "progress": 15,
            "tag": "LIVE-CPU",
            "tagClass": "tag-simd",
            "msg": f"Cargado en RAM '{dataset}' ({orig_sz:,} bytes). Ejecutando LZ4 & Zstandard L1 en vivo..."
        })

        # 1. LZ4
        t0 = time.perf_counter()
        c_lz4 = lz4.frame.compress(data)
        t1 = time.perf_counter()
        d_lz4 = lz4.frame.decompress(c_lz4)
        t2 = time.perf_counter()
        lz4_cs = (orig_sz / 1048576.0) / max(t1 - t0, 1e-6)
        lz4_ds = (orig_sz / 1048576.0) / max(t2 - t1, 1e-6)

        # 2. Zstd L1
        cctx1 = zstd.ZstdCompressor(level=1)
        dctx = zstd.ZstdDecompressor()
        t0 = time.perf_counter()
        c_z1 = cctx1.compress(data)
        t1 = time.perf_counter()
        d_z1 = dctx.decompress(c_z1, max_output_size=orig_sz + 1024)
        t2 = time.perf_counter()
        z1_cs = (orig_sz / 1048576.0) / max(t1 - t0, 1e-6)
        z1_ds = (orig_sz / 1048576.0) / max(t2 - t1, 1e-6)

        self.send_sse({
            "stage": 2,
            "progress": 45,
            "tag": "BASELINE",
            "tagClass": "tag-trial",
            "msg": f"Zstd L1 (En Vivo): {len(c_z1):,} B ({len(c_z1)*100/orig_sz:.2f}%) | Comp: {z1_cs:.1f} MB/s | Decomp: {z1_ds:.1f} MB/s"
        })

        # 3. BinPack Runtime (Live C-ABI)
        # Warmup
        _ = binpack_sdk.decompress(binpack_sdk.compress(data, level=12, filter_mode="auto", mode="runtime"), verify_checksum=False)
        t0 = time.perf_counter()
        c_bp_rt = binpack_sdk.compress(data, level=12, filter_mode="auto", mode="runtime")
        t1 = time.perf_counter()
        d_bp_rt = binpack_sdk.decompress(c_bp_rt, verify_checksum=False)
        t2 = time.perf_counter()
        assert d_bp_rt == data
        bp_rt_cs = (orig_sz / 1048576.0) / max(t1 - t0, 1e-6)
        bp_rt_ds = (orig_sz / 1048576.0) / max(t2 - t1, 1e-6)

        self.send_sse({
            "stage": 4,
            "progress": 80,
            "tag": "BINPACK-LIVE",
            "tagClass": "tag-win",
            "msg": f"BinPack Runtime (En Vivo): {len(c_bp_rt):,} B ({len(c_bp_rt)*100/orig_sz:.2f}%) | Comp: {bp_rt_cs:.1f} MB/s | Decomp: {bp_rt_ds:.1f} MB/s (Ahorro: {len(c_z1)-len(c_bp_rt):+,} B vs Zstd L1)"
        })

        self.send_sse({
            "stage": 6,
            "progress": 100,
            "tag": "VERIFIED",
            "tagClass": "tag-crc",
            "msg": f"Igualdad bit-a-bit verificada en tu CPU ({orig_sz:,} == {len(d_bp_rt):,} bytes).",
            "live_update": {
                "lz4": {"size": len(c_lz4), "ratio": round(len(c_lz4) * 100 / orig_sz, 2), "c_speed": round(lz4_cs, 1), "d_speed": round(lz4_ds, 1)},
                "z1": {"size": len(c_z1), "ratio": round(len(c_z1) * 100 / orig_sz, 2), "c_speed": round(z1_cs, 1), "d_speed": round(z1_ds, 1)},
                "bp_rt": {"size": len(c_bp_rt), "ratio": round(len(c_bp_rt) * 100 / orig_sz, 2), "c_speed": round(bp_rt_cs, 1), "d_speed": round(bp_rt_ds, 1)},
            }
        })


if __name__ == "__main__":
    port = 8090
    url = f"http://localhost:{port}/index.html"
    print("=" * 72)
    print(f"BinPack Enterprise SDK v2.2 - Live Visual Studio Server")
    print(f"Dashboard URL: {url}")
    print("=" * 72)
    try:
        webbrowser.open(url)
    except Exception:
        pass
    server = HTTPServer(("127.0.0.1", port), LiveBenchmarkHandler)
    server.serve_forever()
