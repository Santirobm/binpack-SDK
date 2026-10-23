"""
BinPack Enterprise SDK - Python High-Performance C-ABI Bindings
Copyright (c) 2026 Santiago Robledo. All Rights Reserved.
"""

import ctypes
import os
import sys
from typing import Optional, Dict, Any, List

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
    ERR_PATCH_MISMATCH = -10

class BinPackFilter:
    AUTO = 0
    NONE = 1
    X86_64 = 2
    ARM32 = 3
    AARCH64 = 4
    TRANSPOSE_DELTA = 5
    BITSHUFFLE = 6
    TEXTURE_BCN = 7
    RISCV = 8

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

class _BinPackChunkInfo(ctypes.Structure):
    _fields_ = [
        ("chunk_index", ctypes.c_uint32),
        ("file_offset", ctypes.c_uint64),
        ("uncompressed_sz", ctypes.c_uint32),
        ("compressed_sz", ctypes.c_uint32),
        ("filter_id", ctypes.c_uint8),
        ("filter_flags", ctypes.c_uint8),
        ("filter_param", ctypes.c_uint16),
        ("code_offset", ctypes.c_uint32),
        ("code_length", ctypes.c_uint32),
        ("chunk_crc32", ctypes.c_uint32),
    ]

def _load_library():
    cur_dir = os.path.dirname(os.path.abspath(__file__))
    candidates = [
        os.path.join(cur_dir, "binpack.dll"),
        os.path.join(cur_dir, "..", "..", "binpack.dll"),
        os.path.join(cur_dir, "..", "..", "bin", "binpack.dll"),
        os.path.join(cur_dir, "libbinpack.so"),
        os.path.join(cur_dir, "..", "..", "libbinpack.so"),
        os.path.join(cur_dir, "libbinpack.dylib"),
        os.path.join(cur_dir, "..", "..", "libbinpack.dylib"),
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

    _LIB.binpack_decompress_buffer_ex.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
        ctypes.c_int
    ]
    _LIB.binpack_decompress_buffer_ex.restype = ctypes.c_int

    _LIB.binpack_compress_bound.argtypes = [ctypes.c_size_t]
    _LIB.binpack_compress_bound.restype = ctypes.c_size_t

    _LIB.binpack_compress_into.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(_BinPackOptions)
    ]
    _LIB.binpack_compress_into.restype = ctypes.c_int

    _LIB.binpack_decompress_into.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t),
        ctypes.c_int
    ]
    _LIB.binpack_decompress_into.restype = ctypes.c_int

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

    # Dictionary API
    _LIB.binpack_train_dict.argtypes = [
        ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t), ctypes.c_size_t, ctypes.c_size_t
    ]
    _LIB.binpack_train_dict.restype = ctypes.c_void_p

    _LIB.binpack_create_dict.argtypes = [ctypes.c_void_p, ctypes.c_size_t, ctypes.c_int]
    _LIB.binpack_create_dict.restype = ctypes.c_void_p

    _LIB.binpack_get_dict_buffer.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_size_t)]
    _LIB.binpack_get_dict_buffer.restype = ctypes.c_void_p

    _LIB.binpack_compress_dict.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p
    ]
    _LIB.binpack_compress_dict.restype = ctypes.c_int

    _LIB.binpack_decompress_dict.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t), ctypes.c_void_p
    ]
    _LIB.binpack_decompress_dict.restype = ctypes.c_int

    _LIB.binpack_free_dict.argtypes = [ctypes.c_void_p]
    _LIB.binpack_free_dict.restype = None

    # Patch API
    _LIB.binpack_create_patch.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t),
        ctypes.POINTER(_BinPackOptions)
    ]
    _LIB.binpack_create_patch.restype = ctypes.c_int

    _LIB.binpack_apply_patch.argtypes = [
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(ctypes.c_size_t)
    ]
    _LIB.binpack_apply_patch.restype = ctypes.c_int

    # Archive API
    _LIB.binpack_archive_open.argtypes = [ctypes.c_char_p]
    _LIB.binpack_archive_open.restype = ctypes.c_void_p

    _LIB.binpack_archive_get_chunk_count.argtypes = [ctypes.c_void_p]
    _LIB.binpack_archive_get_chunk_count.restype = ctypes.c_uint32

    _LIB.binpack_archive_get_total_uncompressed_size.argtypes = [ctypes.c_void_p]
    _LIB.binpack_archive_get_total_uncompressed_size.restype = ctypes.c_uint64

    _LIB.binpack_archive_get_chunk_info.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32, ctypes.POINTER(_BinPackChunkInfo)
    ]
    _LIB.binpack_archive_get_chunk_info.restype = ctypes.c_int

    _LIB.binpack_archive_decompress_chunk.argtypes = [
        ctypes.c_void_p, ctypes.c_uint32,
        ctypes.c_void_p, ctypes.c_size_t,
        ctypes.POINTER(ctypes.c_size_t)
    ]
    _LIB.binpack_archive_decompress_chunk.restype = ctypes.c_int

    _LIB.binpack_archive_close.argtypes = [ctypes.c_void_p]
    _LIB.binpack_archive_close.restype = None

class BinPackException(Exception):
    pass

def version() -> str:
    if not _LIB: raise RuntimeError("BinPack library missing")
    return _LIB.binpack_version_string().decode("utf-8")

def compress(
    data: bytes, 
    level: int = 19, 
    filter_mode: str = "auto", 
    stride: int = 4, 
    threads: int = 0,
    mode: str = "default"
) -> bytes:
    if not _LIB: raise RuntimeError("BinPack shared library could not be loaded.")

    opts = _BinPackOptions()
    _LIB.binpack_default_options(ctypes.byref(opts))
    opts.clevel = level
    opts.nb_workers = threads
    opts.transpose_stride = stride

    f_map = {
        "auto": BinPackFilter.AUTO,
        "none": BinPackFilter.NONE,
        "x86": BinPackFilter.X86_64,
        "arm": BinPackFilter.ARM32,
        "arm64": BinPackFilter.AARCH64,
        "riscv": BinPackFilter.RISCV,
        "delta": BinPackFilter.TRANSPOSE_DELTA,
        "bitshuffle": BinPackFilter.BITSHUFFLE,
        "texture": BinPackFilter.TEXTURE_BCN,
    }
    opts.filter = f_map.get(filter_mode.lower(), BinPackFilter.AUTO)

    m_map = {
        "default": BinPackEngineMode.DEFAULT,
        "runtime": BinPackEngineMode.RUNTIME,
        "archival": BinPackEngineMode.ARCHIVAL,
    }
    opts.mode = m_map.get(mode.lower(), BinPackEngineMode.DEFAULT)

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

def decompress(compressed_data: bytes, verify_checksum: bool = True) -> bytes:
    if not _LIB: raise RuntimeError("BinPack shared library could not be loaded.")

    dst_ptr = ctypes.c_void_p()
    dst_sz = ctypes.c_size_t()

    st = _LIB.binpack_decompress_buffer_ex(
        compressed_data, len(compressed_data), 
        ctypes.byref(dst_ptr), ctypes.byref(dst_sz), 
        1 if verify_checksum else 0
    )
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

# =============================================================================
# Pre-trained Dictionary API
# =============================================================================

class Dictionary:
    def __init__(self, handle: ctypes.c_void_p):
        self._handle = handle

    @classmethod
    def train(cls, samples: List[bytes], dict_capacity: int = 32768) -> "Dictionary":
        if not _LIB: raise RuntimeError("BinPack library missing")
        flat = b"".join(samples)
        sizes = (ctypes.c_size_t * len(samples))(*[len(s) for s in samples])
        handle = _LIB.binpack_train_dict(flat, sizes, len(samples), dict_capacity)
        if not handle:
            raise BinPackException("Failed to train dictionary from samples")
        return cls(handle)

    @classmethod
    def load(cls, dict_bytes: bytes, level: int = 19) -> "Dictionary":
        if not _LIB: raise RuntimeError("BinPack library missing")
        handle = _LIB.binpack_create_dict(dict_bytes, len(dict_bytes), level)
        if not handle:
            raise BinPackException("Failed to load dictionary from buffer")
        return cls(handle)

    def to_bytes(self) -> bytes:
        sz = ctypes.c_size_t()
        buf = _LIB.binpack_get_dict_buffer(self._handle, ctypes.byref(sz))
        return ctypes.string_at(buf, sz.value)

    def compress(self, payload: bytes) -> bytes:
        dst_cap = len(payload) + 512
        dst = (ctypes.c_ubyte * dst_cap)()
        cmp_sz = ctypes.c_size_t()
        st = _LIB.binpack_compress_dict(payload, len(payload), dst, dst_cap, ctypes.byref(cmp_sz), self._handle)
        if st != BinPackStatus.OK:
            raise BinPackException(f"Dictionary compression failed ({st})")
        return bytes(dst[:cmp_sz.value])

    def decompress(self, compressed_payload: bytes, expected_max_sz: int = 65536) -> bytes:
        dst = (ctypes.c_ubyte * expected_max_sz)()
        dec_sz = ctypes.c_size_t()
        st = _LIB.binpack_decompress_dict(compressed_payload, len(compressed_payload), dst, expected_max_sz, ctypes.byref(dec_sz), self._handle)
        if st != BinPackStatus.OK:
            raise BinPackException(f"Dictionary decompression failed ({st})")
        return bytes(dst[:dec_sz.value])

    def __del__(self):
        if hasattr(self, "_handle") and self._handle and _LIB:
            _LIB.binpack_free_dict(self._handle)
            self._handle = None

# =============================================================================
# Binary Differential Patch API
# =============================================================================

def create_patch(old_bytes: bytes, new_bytes: bytes, level: int = 19) -> bytes:
    if not _LIB: raise RuntimeError("BinPack library missing")
    opts = _BinPackOptions()
    _LIB.binpack_default_options(ctypes.byref(opts))
    opts.clevel = level

    patch_ptr = ctypes.c_void_p()
    patch_sz = ctypes.c_size_t()
    st = _LIB.binpack_create_patch(
        old_bytes, len(old_bytes),
        new_bytes, len(new_bytes),
        ctypes.byref(patch_ptr), ctypes.byref(patch_sz),
        ctypes.byref(opts)
    )
    if st != BinPackStatus.OK:
        raise BinPackException(f"Patch creation failed ({st})")
    try:
        return ctypes.string_at(patch_ptr, patch_sz.value)
    finally:
        _LIB.binpack_free(patch_ptr)

def apply_patch(old_bytes: bytes, patch_bytes: bytes) -> bytes:
    if not _LIB: raise RuntimeError("BinPack library missing")
    new_ptr = ctypes.c_void_p()
    new_sz = ctypes.c_size_t()
    st = _LIB.binpack_apply_patch(
        old_bytes, len(old_bytes),
        patch_bytes, len(patch_bytes),
        ctypes.byref(new_ptr), ctypes.byref(new_sz)
    )
    if st != BinPackStatus.OK:
        raise BinPackException(f"Patch application failed ({st})")
    try:
        return ctypes.string_at(new_ptr, new_sz.value)
    finally:
        _LIB.binpack_free(new_ptr)

# =============================================================================
# Seekable Random-Access Archive API
# =============================================================================

class Archive:
    def __init__(self, filepath: str):
        if not _LIB: raise RuntimeError("BinPack library missing")
        self._handle = _LIB.binpack_archive_open(filepath.encode("utf-8"))
        if not self._handle:
            raise BinPackException(f"Failed to open archive: {filepath}")

    @property
    def chunk_count(self) -> int:
        return _LIB.binpack_archive_get_chunk_count(self._handle)

    @property
    def total_uncompressed_size(self) -> int:
        return _LIB.binpack_archive_get_total_uncompressed_size(self._handle)

    def get_chunk_info(self, index: int) -> Dict[str, Any]:
        info = _BinPackChunkInfo()
        st = _LIB.binpack_archive_get_chunk_info(self._handle, index, ctypes.byref(info))
        if st != BinPackStatus.OK:
            raise BinPackException(f"Failed to get chunk info ({st})")
        return {
            "chunk_index": info.chunk_index,
            "file_offset": info.file_offset,
            "uncompressed_sz": info.uncompressed_sz,
            "compressed_sz": info.compressed_sz,
            "filter_id": info.filter_id,
            "filter_flags": info.filter_flags,
            "chunk_crc32": hex(info.chunk_crc32)
        }

    def decompress_chunk(self, index: int) -> bytes:
        info = self.get_chunk_info(index)
        dst = (ctypes.c_ubyte * info["uncompressed_sz"])()
        dec_sz = ctypes.c_size_t()
        st = _LIB.binpack_archive_decompress_chunk(
            self._handle, index, dst, info["uncompressed_sz"], ctypes.byref(dec_sz)
        )
        if st != BinPackStatus.OK:
            raise BinPackException(f"Failed to decompress chunk ({st})")
        return bytes(dst[:dec_sz.value])

    def close(self):
        if hasattr(self, "_handle") and self._handle and _LIB:
            _LIB.binpack_archive_close(self._handle)
            self._handle = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()

    def __del__(self):
        self.close()
