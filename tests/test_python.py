"""
Verification test for Python ctypes wrapper over binpack.dll
"""

import sys
import os

# Add bindings/python to path
sys.path.insert(0, os.path.abspath("bindings/python"))

import binpack_sdk

def main():
    print("Testing Python SDK Bindings...")
    print(f"Library Version: {binpack_sdk.version()}")

    # 1. Compress test
    data = b"Hello BinPack Enterprise SDK! " * 1000
    compressed = binpack_sdk.compress(data, level=5)
    print(f"Original size: {len(data)} bytes | Compressed: {len(compressed)} bytes")
    assert len(compressed) < len(data)

    # 2. Decompress test
    restored = binpack_sdk.decompress(compressed)
    assert restored == data
    print("In-memory Buffer Roundtrip: PASSED")

    # 3. File test
    test_in = "py_test.tmp"
    test_bpk = "py_test.tmp.bpk2"
    test_out = "py_test.restored.tmp"

    with open(test_in, "wb") as f:
        f.write(data)

    binpack_sdk.compress_file(test_in, test_bpk, level=5)
    info = binpack_sdk.query_info(test_bpk)
    print(f"Archive info: {info}")
    assert info["uncompressed_size"] == len(data)

    binpack_sdk.decompress_file(test_bpk, test_out)
    with open(test_out, "rb") as f:
        restored_file = f.read()

    assert restored_file == data
    print("File Streaming Roundtrip: PASSED")

    # Cleanup
    for p in (test_in, test_bpk, test_out):
        if os.path.exists(p):
            os.remove(p)

    print("\n[SUCCESS] Python Bindings Verified 100% OK!")

if __name__ == "__main__":
    main()
