@echo off
rem BinPack Enterprise SDK - Windows Build Script
rem Compiles Shared DLL, Static Lib, CLI and Tests

setlocal

set INCLUDES=-Iinclude -Ideps/include -Ideps/lzma
set ZSTD_LIB=deps/lib/libzstd_static.lib
set LZMA_LIB=deps/lib/liblzma_static.lib
set PORTABLE_SOURCES=src/binpack_core.c src/binpack_cpu.c src/binpack_eval.c src/binpack_stream.c src/binpack_sdk.c src/binpack_dict.c src/binpack_patch.c src/binpack_archive.c
set AVX2_SOURCE=src/binpack_core_avx2.c

where zig >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Building BinPack Enterprise SDK with Zig CC - Portable Baseline + AVX2 Isolated...
    
    if not exist deps\lib mkdir deps\lib
    if not exist deps\lib\liblzma_static.lib (
        echo [DEP] Compiling LZMA Range Coder Core...
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/Alloc.c -o deps/lzma/Alloc.o
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/LzFind.c -o deps/lzma/LzFind.o
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/LzmaDec.c -o deps/lzma/LzmaDec.o
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/LzmaEnc.c -o deps/lzma/LzmaEnc.o
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/LzmaLib.c -o deps/lzma/LzmaLib.o
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/Bra86.c -o deps/lzma/Bra86.o
        zig cc -O3 -D_7ZIP_ST -Wno-empty-body -Ideps/lzma -c deps/lzma/Bra.c -o deps/lzma/Bra.o
        zig ar rcs deps/lib/liblzma_static.lib deps/lzma/Alloc.o deps/lzma/LzFind.o deps/lzma/LzmaDec.o deps/lzma/LzmaEnc.o deps/lzma/LzmaLib.o deps/lzma/Bra86.o deps/lzma/Bra.o
        if exist deps\lzma\*.o del deps\lzma\*.o
    )

    echo [1/4] Compiling Shared Dynamic Library: binpack.dll
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_core.c -o binpack_core.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_cpu.c -o binpack_cpu.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_eval.c -o binpack_eval.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_stream.c -o binpack_stream.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_sdk.c -o binpack_sdk.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_dict.c -o binpack_dict.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_patch.c -o binpack_patch.obj
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 -c %INCLUDES% src/binpack_archive.c -o binpack_archive.obj
    zig cc -O3 -mavx2 -c %INCLUDES% src/binpack_core_avx2.c -o binpack_core_avx2.obj
    zig cc -shared -DBINPACK_BUILD_DLL binpack_core.obj binpack_cpu.obj binpack_core_avx2.obj binpack_eval.obj binpack_stream.obj binpack_sdk.obj binpack_dict.obj binpack_patch.obj binpack_archive.obj %ZSTD_LIB% %LZMA_LIB% -o binpack.dll
    if errorlevel 1 goto build_err
    
    echo [2/4] Compiling Static Object Library: binpack.lib
    zig ar rcs binpack.lib binpack_core.obj binpack_cpu.obj binpack_core_avx2.obj binpack_eval.obj binpack_stream.obj binpack_sdk.obj binpack_dict.obj binpack_patch.obj binpack_archive.obj
    if exist *.obj del *.obj
    
    echo [3/4] Compiling Standalone Enterprise CLI: binpack-sdk.exe
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 %INCLUDES% cli/binpack_cli.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o binpack-sdk.exe
    if errorlevel 1 goto build_err

    echo [4/5] Compiling Regression Test Suite: test_sdk.exe
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 %INCLUDES% tests/test_sdk.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o test_sdk.exe
    if errorlevel 1 goto build_err

    echo [5/5] Compiling CPU Dispatch Test Suite: test_cpu_dispatch.exe
    zig cc -O3 -march=x86_64 -mno-avx -mno-avx2 %INCLUDES% tests/test_cpu_dispatch.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o test_cpu_dispatch.exe
    if errorlevel 1 goto build_err

    echo.
    echo [SUCCESS] Build completed successfully:
    echo   - Dynamic Library: binpack.dll
    echo   - Static Library:  binpack.lib
    echo   - Enterprise CLI:  binpack-sdk.exe
    echo   - Test Suite:      test_sdk.exe
    echo   - Dispatch Suite:  test_cpu_dispatch.exe
    echo.
    echo Running automated test suite:
    test_sdk.exe
    if errorlevel 1 goto build_err
    test_cpu_dispatch.exe
    exit /b %ERRORLEVEL%
)

where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Building with MSVC cl.exe...
    cl /O2 /W3 /c %INCLUDES% %PORTABLE_SOURCES%
    cl /O2 /arch:AVX2 /W3 /c %INCLUDES% %AVX2_SOURCE% /Fobinpack_core_avx2.obj
    link /DLL /OUT:binpack.dll binpack_core.obj binpack_cpu.obj binpack_core_avx2.obj binpack_eval.obj binpack_stream.obj binpack_sdk.obj binpack_dict.obj binpack_patch.obj binpack_archive.obj %ZSTD_LIB% %LZMA_LIB%
    lib /OUT:binpack.lib binpack_core.obj binpack_cpu.obj binpack_core_avx2.obj binpack_eval.obj binpack_stream.obj binpack_sdk.obj binpack_dict.obj binpack_patch.obj binpack_archive.obj
    cl /O2 /W3 %INCLUDES% cli/binpack_cli.c binpack.lib %ZSTD_LIB% %LZMA_LIB% /Fe:binpack-sdk.exe
    cl /O2 /W3 %INCLUDES% tests/test_sdk.c binpack.lib %ZSTD_LIB% %LZMA_LIB% /Fe:test_sdk.exe
    if exist *.obj del *.obj
    test_sdk.exe
    exit /b %ERRORLEVEL%
)

where gcc >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Building with GCC...
    gcc -O3 -march=x86-64 -mno-avx -mno-avx2 -c %INCLUDES% %PORTABLE_SOURCES%
    gcc -O3 -mavx2 -c %INCLUDES% %AVX2_SOURCE% -o binpack_core_avx2.o
    gcc -shared -DBINPACK_BUILD_DLL binpack_core.o binpack_cpu.o binpack_core_avx2.o binpack_eval.o binpack_stream.o binpack_sdk.o binpack_dict.o binpack_patch.o binpack_archive.o %ZSTD_LIB% %LZMA_LIB% -o binpack.dll
    ar rcs binpack.lib binpack_core.o binpack_cpu.o binpack_core_avx2.o binpack_eval.o binpack_stream.o binpack_sdk.o binpack_dict.o binpack_patch.o binpack_archive.o
    gcc -O3 -march=x86-64 -mno-avx -mno-avx2 %INCLUDES% cli/binpack_cli.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o binpack-sdk.exe
    gcc -O3 -march=x86-64 -mno-avx -mno-avx2 %INCLUDES% tests/test_sdk.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o test_sdk.exe
    if exist *.o del *.o
    test_sdk.exe
    exit /b %ERRORLEVEL%
)

echo [ERROR] No supported compiler found in PATH.
exit /b 1

:build_err
echo [ERROR] Compilation failed.
exit /b 1
