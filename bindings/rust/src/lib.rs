//! Safe Rust bindings for BinPack Enterprise SDK
//! Copyright (c) 2026 Santiago Robledo. All Rights Reserved.

use std::ffi::{CStr, CString};
use std::os::raw::{c_char, c_int, c_void};

#[repr(C)]
#[derive(Debug, Copy, Clone, PartialEq, Eq)]
pub enum BinPackStatus {
    Ok = 0,
    InvalidParam = -1,
    OutOfMemory = -2,
    CompressionFailed = -3,
    DecompressionFailed = -4,
    CorruptData = -5,
    ChecksumMismatch = -6,
    IOError = -7,
    UnsupportedVersion = -8,
    BufferTooSmall = -9,
    PatchMismatch = -10,
}

#[repr(C)]
#[derive(Debug, Copy, Clone)]
pub struct BinPackOptions {
    pub clevel: c_int,
    pub nb_workers: c_int,
    pub chunk_size: c_int,
    pub enable_micro_trial: c_int,
    pub filter: c_int,
    pub transpose_stride: u16,
    pub mode: c_int,
}

extern "C" {
    fn binpack_version_string() -> *const c_char;
    fn binpack_error_string(status: c_int) -> *const c_char;
    fn binpack_default_options(opts: *mut BinPackOptions);
    fn binpack_compress_buffer(
        src: *const c_void, src_sz: usize,
        dst: *mut *mut c_void, dst_sz: *mut usize,
        opts: *const BinPackOptions,
    ) -> c_int;
    fn binpack_decompress_buffer(
        src: *const c_void, src_sz: usize,
        dst: *mut *mut c_void, dst_sz: *mut usize,
    ) -> c_int;
    fn binpack_free(ptr: *mut c_void);
}

pub fn version() -> String {
    unsafe {
        let ptr = binpack_version_string();
        CStr::from_ptr(ptr).to_string_lossy().into_owned()
    }
}

pub fn default_options() -> BinPackOptions {
    let mut opts = BinPackOptions {
        clevel: 19,
        nb_workers: 0,
        chunk_size: 16 * 1024 * 1024,
        enable_micro_trial: 1,
        filter: 0,
        transpose_stride: 4,
        mode: 0,
    };
    unsafe { binpack_default_options(&mut opts) };
    opts
}

pub fn compress(data: &[u8], options: Option<&BinPackOptions>) -> Result<Vec<u8>, String> {
    let mut dst_ptr: *mut c_void = std::ptr::null_mut();
    let mut dst_sz: usize = 0;

    let opt_ptr = match options {
        Some(o) => o as *const BinPackOptions,
        None => std::ptr::null(),
    };

    let st = unsafe {
        binpack_compress_buffer(
            data.as_ptr() as *const c_void,
            data.len(),
            &mut dst_ptr,
            &mut dst_sz,
            opt_ptr,
        )
    };

    if st != 0 {
        let err_ptr = unsafe { binpack_error_string(st) };
        let msg = unsafe { CStr::from_ptr(err_ptr).to_string_lossy().into_owned() };
        return Err(format!("Compression failed ({}): {}", st, msg));
    }

    let slice = unsafe { std::slice::from_raw_parts(dst_ptr as *const u8, dst_sz) };
    let result = slice.to_vec();
    unsafe { binpack_free(dst_ptr) };
    Ok(result)
}

pub fn decompress(compressed: &[u8]) -> Result<Vec<u8>, String> {
    let mut dst_ptr: *mut c_void = std::ptr::null_mut();
    let mut dst_sz: usize = 0;

    let st = unsafe {
        binpack_decompress_buffer(
            compressed.as_ptr() as *const c_void,
            compressed.len(),
            &mut dst_ptr,
            &mut dst_sz,
        )
    };

    if st != 0 {
        let err_ptr = unsafe { binpack_error_string(st) };
        let msg = unsafe { CStr::from_ptr(err_ptr).to_string_lossy().into_owned() };
        return Err(format!("Decompression failed ({}): {}", st, msg));
    }

    let slice = unsafe { std::slice::from_raw_parts(dst_ptr as *const u8, dst_sz) };
    let result = slice.to_vec();
    unsafe { binpack_free(dst_ptr) };
    Ok(result)
}
