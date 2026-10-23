#!/usr/bin/env python3
"""
BinPack Enterprise SDK - Automated Multi-Corpus Downloader & Integrity Verifier
Downloads and verifies standard benchmark corpora:
  - Silesia Corpus (Full 12 files)
  - Canterbury Corpus (Standard academic set)
  - NASA C-MAPSS Turbofan Telemetry (Raw text and packed IEEE 754 Float32)
  - Real-World Production ARM Binary (BusyBox ARMv7-A)
"""

import os
import sys
import urllib.request
import zipfile
import tarfile
import hashlib
import struct

BASE_DIR = os.path.abspath(os.path.join(os.path.dirname(__file__), "corpora"))

DATASETS = {
    "silesia": {
        "url": "https://mattmahoney.net/dc/silesia.zip",
        "archive": "silesia.zip",
        "type": "zip",
        "target_dir": os.path.join(BASE_DIR, "silesia")
    },
    "canterbury": {
        "url": "http://corpus.canterbury.ac.nz/resources/cantrbry.tar.gz",
        "archive": "cantrbry.tar.gz",
        "type": "tar.gz",
        "target_dir": os.path.join(BASE_DIR, "canterbury")
    },
    "nasa_turbofan": {
        "url": "https://raw.githubusercontent.com/datacubeR/cmapps/time-series/assets/CMAPSSData/train_FD001.txt",
        "archive": "nasa_turbofan_ascii.txt",
        "type": "direct_nasa",
        "target_dir": os.path.join(BASE_DIR, "nasa_telemetry")
    },
    "busybox_arm": {
        "url": "https://busybox.net/downloads/binaries/1.21.1/busybox-armv7l",
        "archive": "busybox_armv7l",
        "type": "direct",
        "target_dir": os.path.join(BASE_DIR, "arm_binaries")
    }
}

EXPECTED_HASHES = {
    "arm_binaries/arm_synthetic_branch_loop.elf": "076324ff9f0f850e06c73e6f50947cadb0358721ea1bf106bf5a979142268c48",
    "arm_binaries/busybox_armv7l": "8fceda57d83418bb288de0aef3c6df5e9421344ed0515c90fa5206f15c50f7e4",
    "canterbury/alice29.txt": "7467306ee0feed4971260f3c87421154a05be571d944e9cb021a5713700c38f0",
    "canterbury/asyoulik.txt": "eaa3526fe53859f34ecdf255712f9ecf0b2c903451d4755b2edaa2e2599cb0fc",
    "canterbury/cp.html": "e0cd21cef5b6c4069461e949be100080c3ce887de6f1dd8626c480528efaaf61",
    "canterbury/fields.c": "85d73e354cc50cec76cb5a50537cf8dc035f8cbb8480f9e1cbe2f7d6c23393c7",
    "canterbury/grammar.lsp": "1b0805dfc0ae706b35aac2bb4e15f02485efd24dda5dbd29de7b2f84d1a88c15",
    "canterbury/kennedy.xls": "9af47239ca29dfe20e633f80bbbb9a4cc9783d0803d7b2b5626f42e4c3790420",
    "canterbury/lcet10.txt": "5314ba1dbb03f471df88bec6cd120a938ef60d0fd3511c5c1dce61bf7463245f",
    "canterbury/plrabn12.txt": "07e2e0b461af78c7c647cb53dab39de560198e16f799b4516eccf0fbd69f764c",
    "canterbury/ptt5": "0ec3a75089bb52342813496b17e51377bc9eba3cb519a444d67025354841d650",
    "canterbury/sum": "ee5733cd76ecc2f9d8ff156adc3c02a7a851051dcf43a2d56ff4ee4ff606bdb3",
    "canterbury/xargs.1": "c58aeb5d2d1e12751d47e7412b45784405fc30a5671b03d480fa05776e183619",
    "nasa_telemetry/nasa_turbofan_ascii.txt": "963b5e22825b34d8b21c69e1aeb4af3e647050eb672ee8834ba4b5d91d2de0f8",
    "silesia/dickens": "b24c37886142e11d0ee687db6ab06f936207aa7f2ea1fd1d9a36763c7a507e6a",
    "silesia/mozilla": "657fc3764b0c75ac9de9623125705831ebbfbe08fed248df73bc2dc66e2a963b",
    "silesia/mr": "68637ed52e3e4860174ed2dc0840ac77d5f1a60abbcb13770d5754e3774d53e6",
    "silesia/nci": "fc63a31770947b8c2062d3b19ca94c00485a232bb91b502021948fee983e1635",
    "silesia/ooffice": "e7ee013880d34dd5208283d0d3d91b07f442e067454276095ded14f322a656eb",
    "silesia/osdb": "60f027179302ca3ad87c58ac90b6be72ec23588aaa7a3b7fe8ecc0f11def3fa3",
    "silesia/reymont": "0eac0114a3dfe6e2ee1f345a0f79d653cb26c3bc9f0ed79238af4933422b7578",
    "silesia/samba": "93ba07bc44d8267789c1d911992f40b089ffa2140b4a160fac11ccae9a40e7b2",
    "silesia/sao": "c2d0ea2cc59d4c21b7fe43a71499342a00cbe530a1d5548770e91ecd6214adcc",
    "silesia/webster": "6a68f69b26daf09f9dd84f7470368553194a0b294fcfa80f1604efb11143a383",
    "silesia/x-ray": "7de9fce1405dc44ae5e6813ed21cd5751e761bd4265655a005d39b9685d1c9ad",
    "silesia/xml": "0e82e54e695c1938e4193448022543845b33020c8be6bf3bf3ead2224903e08c",
}

def sha256_file(path):
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(65536):
            h.update(chunk)
    return h.hexdigest()

def fetch_all():
    os.makedirs(BASE_DIR, exist_ok=True)
    print("=" * 80)
    print("BinPack Benchmark Corpora Fetcher & Hash Verifier")
    print("=" * 80)

    # 1. Silesia
    sil_info = DATASETS["silesia"]
    os.makedirs(sil_info["target_dir"], exist_ok=True)
    local_zip = "C:/Users/sgi.lm/Desktop/Compiler/silesia.zip"
    sil_archive = os.path.join(BASE_DIR, sil_info["archive"])

    if not os.path.exists(sil_archive):
        if os.path.exists(local_zip):
            print(f"[COPY] Found local Silesia archive: {local_zip}")
            import shutil
            shutil.copy(local_zip, sil_archive)
        else:
            print(f"[DOWNLOAD] Fetching Silesia Corpus from {sil_info['url']}...")
            urllib.request.urlretrieve(sil_info["url"], sil_archive)

    if not os.path.exists(os.path.join(sil_info["target_dir"], "dickens")):
        print("[EXTRACT] Extracting Silesia Corpus...")
        with zipfile.ZipFile(sil_archive, "r") as z:
            z.extractall(sil_info["target_dir"])
    else:
        print("[CACHE] Silesia Corpus already extracted.")

    # 2. Canterbury
    cant_info = DATASETS["canterbury"]
    os.makedirs(cant_info["target_dir"], exist_ok=True)
    cant_archive = os.path.join(BASE_DIR, cant_info["archive"])
    local_cant = "C:/Users/sgi.lm/Desktop/Compiler/corpora/cantrbry.tar.gz"

    if not os.path.exists(cant_archive):
        if os.path.exists(local_cant):
            print(f"[COPY] Found local Canterbury archive: {local_cant}")
            import shutil
            shutil.copy(local_cant, cant_archive)
        else:
            print(f"[DOWNLOAD] Fetching Canterbury Corpus from {cant_info['url']}...")
            req = urllib.request.Request(cant_info["url"], headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req) as resp, open(cant_archive, "wb") as f:
                f.write(resp.read())

    if not os.path.exists(os.path.join(cant_info["target_dir"], "alice29.txt")):
        print("[EXTRACT] Extracting Canterbury Corpus...")
        with tarfile.open(cant_archive, "r:gz") as tar:
            tar.extractall(cant_info["target_dir"])
    else:
        print("[CACHE] Canterbury Corpus already extracted.")

    # 3. NASA C-MAPSS
    nasa_info = DATASETS["nasa_turbofan"]
    os.makedirs(nasa_info["target_dir"], exist_ok=True)
    nasa_txt = os.path.join(nasa_info["target_dir"], "nasa_turbofan_ascii.txt")
    nasa_bin = os.path.join(nasa_info["target_dir"], "nasa_turbofan_float32.bin")
    local_nasa_txt = "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_turbofan_ascii.txt"

    if not os.path.exists(nasa_txt):
        if os.path.exists(local_nasa_txt):
            import shutil
            shutil.copy(local_nasa_txt, nasa_txt)
        else:
            print(f"[DOWNLOAD] Fetching NASA C-MAPSS Telemetry from {nasa_info['url']}...")
            req = urllib.request.Request(nasa_info["url"], headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req) as resp, open(nasa_txt, "wb") as f:
                f.write(resp.read())

    print("[PROCESS] Generating NASA Turbofan Float32 Binary Array...")
    with open(nasa_txt, "r", encoding="utf-8") as f:
        floats = []
        for line in f:
            parts = line.strip().split()
            if len(parts) >= 26:
                for val in parts[2:]:
                    floats.append(float(val))

    with open(nasa_bin, "wb") as f:
        f.write(struct.pack(f"{len(floats)}f", *floats))

    # 4. BusyBox ARM
    arm_info = DATASETS["busybox_arm"]
    os.makedirs(arm_info["target_dir"], exist_ok=True)
    arm_bin = os.path.join(arm_info["target_dir"], "busybox_armv7l")
    local_arm = "C:/Users/sgi.lm/Desktop/Compiler/corpora/busybox_armv7l"

    if not os.path.exists(arm_bin):
        if os.path.exists(local_arm):
            import shutil
            shutil.copy(local_arm, arm_bin)
        else:
            print(f"[DOWNLOAD] Fetching BusyBox ARMv7-A from {arm_info['url']}...")
            req = urllib.request.Request(arm_info["url"], headers={"User-Agent": "Mozilla/5.0"})
            with urllib.request.urlopen(req) as resp, open(arm_bin, "wb") as f:
                f.write(resp.read())

    # 5. Synthetic ARM Micro-benchmark loop
    local_synth_arm = "C:/Users/sgi.lm/Desktop/Compiler/arm_binary.elf"
    synth_arm_target = os.path.join(arm_info["target_dir"], "arm_synthetic_branch_loop.elf")
    if os.path.exists(local_synth_arm):
        import shutil
        shutil.copy(local_synth_arm, synth_arm_target)

    # 6. Verify Hashes
    print("\n[VERIFY] Validating Cryptographic SHA-256 Checksums:")
    print("-" * 80)
    all_ok = True
    for rel_path, expected_hash in EXPECTED_HASHES.items():
        full_path = os.path.join(BASE_DIR, rel_path)
        if not os.path.exists(full_path):
            print(f"  {rel_path:<40} : MISSING")
            all_ok = False
            continue
        actual_hash = sha256_file(full_path)
        if actual_hash.lower() == expected_hash.lower():
            print(f"  {rel_path:<40} : OK (SHA-256 match)")
        else:
            print(f"  {rel_path:<40} : MISMATCH (got {actual_hash[:8]}...)")
            all_ok = False

    print("-" * 80)
    if all_ok:
        print("[SUCCESS] All corpora verified and ready for reproducible benchmarking.")
    else:
        print("[WARNING] Some files failed validation. Please inspect downloads.")

if __name__ == "__main__":
    fetch_all()
