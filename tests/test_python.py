"""
Verification test for Python ctypes wrapper over binpack.dll
"""

import sys
import os

# Add bindings/python to path
sys.path.insert(0, os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "bindings", "python")))

import binpack_sdk

def main():
    print("Testing Python SDK Bindings...")
    print(f"Library Version: {binpack_sdk.version()}")

    # 1. In-memory roundtrip
    data = b"Hello BinPack Enterprise SDK! " * 1000
    compressed = binpack_sdk.compress(data, level=5)
    print(f"Original size: {len(data)} bytes | Compressed: {len(compressed)} bytes")
    assert len(compressed) < len(data)

    restored = binpack_sdk.decompress(compressed)
    assert restored == data
    print("In-memory Buffer Roundtrip: PASSED")

    # 2. File test & Seekable Archive
    test_in = "py_test.tmp"
    test_bpk = "py_test.tmp.bpk2"
    test_out = "py_test.restored.tmp"

    big_data = (b"ChunkDataPayloadMarker_" * 50000) # ~1.15 MB
    with open(test_in, "wb") as f:
        f.write(big_data)

    binpack_sdk.compress_file(test_in, test_bpk, level=5, chunk_size_mb=1)
    info = binpack_sdk.query_info(test_bpk)
    print(f"Archive info: {info}")
    assert info["uncompressed_size"] == len(big_data)

    # Test Seekable Archive random-access
    with binpack_sdk.Archive(test_bpk) as arch:
        print(f"Archive chunks: {arch.chunk_count}, total size: {arch.total_uncompressed_size}")
        assert arch.chunk_count >= 1
        chunk0 = arch.decompress_chunk(0)
        assert len(chunk0) > 0
        assert chunk0 == big_data[:len(chunk0)]
    print("Seekable Archive Random-Access: PASSED")

    binpack_sdk.decompress_file(test_bpk, test_out)
    with open(test_out, "rb") as f:
        restored_file = f.read()

    assert restored_file == big_data
    print("File Streaming Roundtrip: PASSED")

    # 3. Dictionary Engine Test (Micro-Telemetry)
    samples = [b"CAN_FRAME_ID_0x18FEF100_SENSOR_DATA_" + bytes([i % 256, (i * 3) % 256]) for i in range(100)]
    dict_obj = binpack_sdk.Dictionary.train(samples, dict_capacity=16384)
    assert len(dict_obj.to_bytes()) > 0
    micro_pkt = samples[0]
    cmp_pkt = dict_obj.compress(micro_pkt)
    print(f"Micro-packet 64B compressed with dictionary: {len(cmp_pkt)} bytes")
    assert len(cmp_pkt) < 25
    dec_pkt = dict_obj.decompress(cmp_pkt, expected_max_sz=128)
    assert dec_pkt == micro_pkt
    print("Dictionary Micro-Telemetry Roundtrip: PASSED")

    # 4. Binary Differential Patch Test (OTA Updates)
    old_firmware = b"ARM64_FIRMWARE_HEADER_BOOT_CODE_SECTION_" * 1000
    new_firmware = old_firmware[:5000] + b"PATCHED_SECURITY_FIX_V2_" + old_firmware[5025:]
    patch = binpack_sdk.create_patch(old_firmware, new_firmware)
    print(f"Differential patch size for 40KB binary: {len(patch)} bytes")
    assert len(patch) < 400
    restored_firmware = binpack_sdk.apply_patch(old_firmware, patch)
    assert restored_firmware == new_firmware
    print("Binary Differential Patch (OTA) Roundtrip: PASSED")

    # Cleanup
    for p in (test_in, test_bpk, test_out):
        if os.path.exists(p):
            os.remove(p)

    print("\n[SUCCESS] All Python Bindings Verified 100% Flawlessly!")

if __name__ == "__main__":
    main()
