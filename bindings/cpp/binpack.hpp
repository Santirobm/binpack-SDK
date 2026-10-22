/**
 * @file binpack.hpp
 * @brief BinPack Enterprise SDK - Modern C++ RAII Wrapper
 * @version 2.0.0
 * @copyright (c) 2026 Santiago Robledo. All Rights Reserved.
 */

#ifndef BINPACK_HPP
#define BINPACK_HPP

#include "../../include/binpack_sdk.h"
#include <vector>
#include <string>
#include <stdexcept>
#include <cstdint>
#include <memory>

namespace binpack {

class Exception : public std::runtime_error {
public:
    binpack_status_t status;
    explicit Exception(binpack_status_t st)
        : std::runtime_error(binpack_error_string(st)), status(st) {}
    Exception(binpack_status_t st, const std::string &msg)
        : std::runtime_error(msg + ": " + binpack_error_string(st)), status(st) {}
};

inline std::string version() {
    return binpack_version_string();
}

inline std::vector<uint8_t> compress(
    const uint8_t *data, size_t size,
    const binpack_options_t *options = nullptr
) {
    if (!data && size > 0) throw Exception(BINPACK_ERR_INVALID_PARAM);

    void *dst = nullptr;
    size_t dst_sz = 0;
    binpack_status_t st = binpack_compress_buffer(data, size, &dst, &dst_sz, options);
    if (st != BINPACK_OK) {
        throw Exception(st, "Compression failed");
    }

    std::vector<uint8_t> result((const uint8_t*)dst, (const uint8_t*)dst + dst_sz);
    binpack_free(dst);
    return result;
}

inline std::vector<uint8_t> compress(
    const std::vector<uint8_t> &data,
    const binpack_options_t *options = nullptr
) {
    return compress(data.data(), data.size(), options);
}

inline std::vector<uint8_t> decompress(const uint8_t *data, size_t size) {
    if (!data && size > 0) throw Exception(BINPACK_ERR_INVALID_PARAM);

    void *dst = nullptr;
    size_t dst_sz = 0;
    binpack_status_t st = binpack_decompress_buffer(data, size, &dst, &dst_sz);
    if (st != BINPACK_OK) {
        throw Exception(st, "Decompression failed");
    }

    std::vector<uint8_t> result((const uint8_t*)dst, (const uint8_t*)dst + dst_sz);
    binpack_free(dst);
    return result;
}

inline std::vector<uint8_t> decompress(const std::vector<uint8_t> &data) {
    return decompress(data.data(), data.size());
}

inline void compress_file(
    const std::string &in_path,
    const std::string &out_path,
    const binpack_options_t *options = nullptr
) {
    binpack_status_t st = binpack_compress_file(in_path.c_str(), out_path.c_str(), options, nullptr, nullptr);
    if (st != BINPACK_OK) {
        throw Exception(st, "File compression failed: " + in_path);
    }
}

inline void decompress_file(
    const std::string &in_path,
    const std::string &out_path
) {
    binpack_status_t st = binpack_decompress_file(in_path.c_str(), out_path.c_str(), nullptr, nullptr);
    if (st != BINPACK_OK) {
        throw Exception(st, "File decompression failed: " + in_path);
    }
}

inline binpack_file_info_t query_file_info(const std::string &path) {
    binpack_file_info_t info;
    binpack_status_t st = binpack_query_file_info(path.c_str(), &info);
    if (st != BINPACK_OK) {
        throw Exception(st, "Query file info failed: " + path);
    }
    return info;
}

} // namespace binpack

#endif // BINPACK_HPP
