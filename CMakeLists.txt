cmake_minimum_required(VERSION 3.16)
project(binpack_sdk VERSION 2.2.0 LANGUAGES C CXX)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Options
option(BINPACK_BUILD_SHARED "Build shared library (binpack.dll / libbinpack.so)" ON)
option(BINPACK_BUILD_STATIC "Build static library (binpack.lib / libbinpack.a)" ON)
option(BINPACK_BUILD_CLI "Build standalone command-line tool" ON)
option(BINPACK_BUILD_TESTS "Build regression test suite" ON)

include_directories(include deps/include deps/lzma)
add_compile_definitions(_7ZIP_ST)

if(CMAKE_SYSTEM_PROCESSOR MATCHES "x86_64|AMD64")
    if(MSVC)
        add_compile_options(/arch:AVX2)
    else()
        add_compile_options(-mavx2)
    endif()
endif()

set(BINPACK_SOURCES
    src/binpack_core.c
    src/binpack_eval.c
    src/binpack_stream.c
    src/binpack_sdk.c
    src/binpack_dict.c
    src/binpack_patch.c
    src/binpack_archive.c
    deps/lzma/Alloc.c
    deps/lzma/LzFind.c
    deps/lzma/LzmaDec.c
    deps/lzma/LzmaEnc.c
    deps/lzma/LzmaLib.c
    deps/lzma/Bra86.c
    deps/lzma/Bra.c
)

# External Zstandard linkage
find_library(ZSTD_LIBRARY NAMES zstd libzstd_static libzstd PATHS deps/lib)

# Shared Library
if(BINPACK_BUILD_SHARED)
    add_library(binpack SHARED ${BINPACK_SOURCES})
    target_compile_definitions(binpack PRIVATE BINPACK_BUILD_DLL)
    if(ZSTD_LIBRARY)
        target_link_libraries(binpack PRIVATE ${ZSTD_LIBRARY})
    else()
        target_link_libraries(binpack PRIVATE -lzstd)
    endif()
    set_target_properties(binpack PROPERTIES
        OUTPUT_NAME "binpack"
        VERSION ${PROJECT_VERSION}
        SOVERSION 2
    )
endif()

# Static Library
if(BINPACK_BUILD_STATIC)
    add_library(binpack_static STATIC ${BINPACK_SOURCES})
    if(ZSTD_LIBRARY)
        target_link_libraries(binpack_static PRIVATE ${ZSTD_LIBRARY})
    else()
        target_link_libraries(binpack_static PRIVATE -lzstd)
    endif()
    set_target_properties(binpack_static PROPERTIES
        OUTPUT_NAME "binpack"
    )
endif()

# CLI Tool
if(BINPACK_BUILD_CLI)
    add_executable(binpack-sdk cli/binpack_cli.c)
    if(BINPACK_BUILD_STATIC)
        target_link_libraries(binpack-sdk PRIVATE binpack_static ${ZSTD_LIBRARY})
    elseif(BINPACK_BUILD_SHARED)
        target_link_libraries(binpack-sdk PRIVATE binpack ${ZSTD_LIBRARY})
    endif()
endif()

# Tests
if(BINPACK_BUILD_TESTS)
    enable_testing()
    add_executable(test_sdk tests/test_sdk.c)
    if(BINPACK_BUILD_STATIC)
        target_link_libraries(test_sdk PRIVATE binpack_static ${ZSTD_LIBRARY})
    elseif(BINPACK_BUILD_SHARED)
        target_link_libraries(test_sdk PRIVATE binpack ${ZSTD_LIBRARY})
    endif()
    add_test(NAME RegressionTests COMMAND test_sdk)
endif()
