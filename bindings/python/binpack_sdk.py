"""
BinPack Enterprise SDK - Python High-Performance C-ABI Bindings
Copyright (c) 2026 Santiago Robledo. All Rights Reserved.
"""

import ctypes
import os
import sys
from typing import Optional, Dict, Any

class BinPackStatus:
    OK = 0
    ERR_INVALID_PARAM = -1
    ERR_OUT_OF_MEMORY = -2
    ERR_COMPRESSION_FAILED = -3
    ERR_DECOMPRESSION_FAILED = -4
    ERR_CORRUPT_DATA = -5
    ERR_CHECKSUM_MISMATCH = -6
    ERR_IO = -7
    ERR_UNSUPPORTED_VERSION = -8
    ERR_BUFFER_TOO_SMALL = -9

class BinPackFilter:
    AUTO = 0
    NONE = 1
    X86_64 = 2
    ARM32 = 3
    AARCH64 = 4
    TRANSPOSE_DELTA = 5
    BITSHUFFLE = 6
    TEXTURE_BCN = 7

class BinPackEngineMode:
    DEFAULT = 0
    RUNTIME = 1
    ARCHIVAL = 2

class _BinPackOptions(ctypes.Structure):
    _fields_ = [
        ("clevel", ctypes.c_int),
        ("nb_workers", ctypes.c_int),
        ("chunk_size", ctypes.c_int),
        ("enable_micro_trial", ctypes.c_int),
        ("filter", ctypes.c_int),
        ("transpose_stride", ctypes.c_uint16),
        ("mode", ctypes.c_int),
    ]

class _BinPackFileInfo(ctypes.Structure):
    _fields_ = [
        ("version", ctypes.c_uint32),
        ("uncompressed_size", ctypes.c_uint64),
        ("compressed_size", ctypes.c_uint64),
        ("chunk_count", ctypes.c_uint32),
        ("original_crc32", ctypes.c_uint32),
        ("primary_filter", ctypes.c_uint8),
        ("flags", ctypes.c_uint8),
    ]

def _load_library():
    # Search locations
    cur_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(cur_dir, "binpack.dll"),
        os.path.join(cur_dir, "..", "..", "binpack.dll"),
        os.path.join(cur_dir, "..", "..", "bin", "binpack.dll"),
        os.path.join(cur_dir, "libbinpack.so"),
        os.path.join(cur_dir, "..", "..", "libbinpack.so"),
        "binpack.dll",
        "libbinpack.so",
    ]
    for c in candidates:
        if os.path.exists(c):
            try:
                return ctypes.CDLL(c)
            except Exception:
                pass
    try:
        if sys.platform == "win32":
            return ctypes.CDLL("binpack.dll")
        else:
            return ctypes.CDLL("libbinpack.so")
    except Exception:
        return None

_LIB = _load_library()

if _LIB:
    _LIB.binpack_version_string.restype = ctypes.c_char_p
    _LIB.binpack_error_string.argtypes = [ctypes.c_int]
    _LIB.binpack_error_string.restype = ctypes.c_char_p

    _LIB.binpack_default_options.argtypes = [ctypes.POINTER(_BinPackOptions)]
    _LIB.binpack_default_options.restype = None

    _LIB.binpack_compress_buffer.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(_BinPackOptions)
    ]
    _LIB.binpack_compress_buffer.restype = ctypes.c_int

    _LIB.binpack_decompress_buffer.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)
    ]
    _LIB.binpack_decompress_buffer.restype = ctypes.c_int

    _LIB.binpack_free.argtypes = [ctypes.c_void_p]
    _LIB.binpack_free.restype = None

    _LIB.binpack_compress_file.argtypes = [
        ctypes.c_char_p, ctypes.c_char_p,
        ctypes.POINTER(_BinPackOptions),
        ctypes.c_void_p, ctypes.c_void_p
    ]
    _LIB.binpack_compress_file.restype = ctypes.c_int

    _LIB.binpack_decompress_file.argtypes = [
        ctypes.c_char_p, ctypes.c_char_p,
        ctypes.c_void_p, ctypes.c_void_p
    ]
    _LIB.binpack_decompress_file.restype = ctypes.c_int

    _LIB.binpack_query_file_info.argtypes = [
        ctypes.c_char_p, ctypes.POINTER(_BinPackFileInfo)
    ]
    _LIB.binpack_query_file_info.restype = ctypes.c_int

class BinPackException(Exception):
    pass

def version() -> str:
    if not _LIB: return "2.0.0 (unloaded)"
    return _LIB.binpack_version_string().decode("utf-8")

def compress(data: bytes, level: int = 19, threads: int = 0, 
             chunk_size_mb: int = 16, micro_trial: bool = True, 
             filter_mode: int = BinPackFilter.AUTO, stride: int = 4) -> bytes:
    """Compress in-memory bytes into BinPack V2 stream."""
    if not _LIB:
        raise RuntimeError("BinPack shared library (binpack.dll / libbinpack.so) could not be loaded.")
    
    opts = _BinPackOptions()
    _LIB.binpack_default_options(ctypes.byref(opts))
    opts.clevel = level
    opts.nb_workers = threads
    opts.chunk_size = chunk_size_mb * 1024 * 1024
    opts.enable_micro_trial = 1 if micro_trial else 0
    opts.filter = filter_mode
    opts.transpose_stride = stride

    dst_ptr = ctypes.c_void_p()
    dst_sz = ctypes.c_size_t()

    st = _LIB.binpack_compress_buffer(data, len(data), ctypes.byref(dst_ptr), ctypes.byref(dst_sz), ctypes.byref(opts))
    if st != BinPackStatus.OK:
        err_msg = _LIB.binpack_error_string(st).decode("utf-8")
        raise BinPackException(f"Compression error ({st}): {err_msg}")

    try:
        out_buf = ctypes.string_at(dst_ptr, dst_sz.value)
        return out_buf
    finally:
        _LIB.binpack_free(dst_ptr)

def decompress(compressed_data: bytes) -> bytes:
    """Decompress in-memory BinPack bytes."""
    if not _LIB:
        raise RuntimeError("BinPack shared library could not be loaded.")

    dst_ptr = ctypes.c_void_p()
    dst_sz = ctypes.c_size_t()

    st = _LIB.binpack_decompress_buffer(compressed_data, len(compressed_data), ctypes.byref(dst_ptr), ctypes.byref(dst_sz))
    if st != BinPackStatus.OK:
        err_msg = _LIB.binpack_error_string(st).decode("utf-8")
        raise BinPackException(f"Decompression error ({st}): {err_msg}")

    try:
        out_buf = ctypes.string_at(dst_ptr, dst_sz.value)
        return out_buf
    finally:
        _LIB.binpack_free(dst_ptr)

def compress_file(in_path: str, out_path: str, level: int = 19, threads: int = 0, chunk_size_mb: int = 16) -> None:
    if not _LIB: raise RuntimeError("BinPack library missing")
    opts = _BinPackOptions()
    _LIB.binpack_default_options(ctypes.byref(opts))
    opts.clevel = level
    opts.nb_workers = threads
    opts.chunk_size = chunk_size_mb * 1024 * 1024

    st = _LIB.binpack_compress_file(in_path.encode("utf-8"), out_path.encode("utf-8"), ctypes.byref(opts), None, None)
    if st != BinPackStatus.OK:
        err_msg = _LIB.binpack_error_string(st).decode("utf-8")
        raise BinPackException(f"File compression failed ({st}): {err_msg}")

def decompress_file(in_path: str, out_path: str) -> None:
    if not _LIB: raise RuntimeError("BinPack library missing")
    st = _LIB.binpack_decompress_file(in_path.encode("utf-8"), out_path.encode("utf-8"), None, None)
    if st != BinPackStatus.OK:
        err_msg = _LIB.binpack_error_string(st).decode("utf-8")
        raise BinPackException(f"File decompression failed ({st}): {err_msg}")

def query_info(archive_path: str) -> Dict[str, Any]:
    if not _LIB: raise RuntimeError("BinPack library missing")
    info = _BinPackFileInfo()
    st = _LIB.binpack_query_file_info(archive_path.encode("utf-8"), ctypes.byref(info))
    if st != BinPackStatus.OK:
        raise BinPackException(f"Query failed ({st})")
    return {
        "version": info.version,
        "uncompressed_size": info.uncompressed_size,
        "compressed_size": info.compressed_size,
        "chunk_count": info.chunk_count,
        "crc32": hex(info.original_crc32),
        "primary_filter": info.primary_filter,
    }
