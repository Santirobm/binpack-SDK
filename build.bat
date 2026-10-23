@echo off
rem BinPack Enterprise SDK - Windows Build Script
rem Compiles Shared DLL, Static Lib, CLI and Tests

setlocal

set INCLUDES=-Iinclude -Ideps/include -Ideps/lzma
set ZSTD_LIB=deps/lib/libzstd_static.lib
set LZMA_LIB=deps/lib/liblzma_static.lib
set SRC_FILES=src/binpack_core.c src/binpack_eval.c src/binpack_stream.c src/binpack_sdk.c

where zig >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Building BinPack Enterprise SDK with Zig CC AVX2 Vectorized...
    
    if not exist deps\lib\liblzma_static.lib (
        echo [DEP] Compiling LZMA Range Coder Core...
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/Alloc.c -o deps/lzma/Alloc.o
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/LzFind.c -o deps/lzma/LzFind.o
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/LzmaDec.c -o deps/lzma/LzmaDec.o
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/LzmaEnc.c -o deps/lzma/LzmaEnc.o
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/LzmaLib.c -o deps/lzma/LzmaLib.o
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/Bra86.c -o deps/lzma/Bra86.o
        zig cc -O3 -D_7ZIP_ST -Ideps/lzma -c deps/lzma/Bra.c -o deps/lzma/Bra.o
        zig ar rcs deps/lib/liblzma_static.lib deps/lzma/Alloc.o deps/lzma/LzFind.o deps/lzma/LzmaDec.o deps/lzma/LzmaEnc.o deps/lzma/LzmaLib.o deps/lzma/Bra86.o deps/lzma/Bra.o
        if exist deps\lzma\*.o del deps\lzma\*.o
    )

    echo [1/4] Compiling Shared Dynamic Library: binpack.dll
    zig cc -O3 -mavx2 -shared -DBINPACK_BUILD_DLL %INCLUDES% %SRC_FILES% %ZSTD_LIB% %LZMA_LIB% -o binpack.dll
    if errorlevel 1 goto build_err
    
    echo [2/4] Compiling Static Object Library: binpack.lib
    zig cc -O3 -mavx2 -c %INCLUDES% src/binpack_core.c -o binpack_core.obj
    zig cc -O3 -mavx2 -c %INCLUDES% src/binpack_eval.c -o binpack_eval.obj
    zig cc -O3 -mavx2 -c %INCLUDES% src/binpack_stream.c -o binpack_stream.obj
    zig cc -O3 -mavx2 -c %INCLUDES% src/binpack_sdk.c -o binpack_sdk.obj
    zig ar rcs binpack.lib binpack_core.obj binpack_eval.obj binpack_stream.obj binpack_sdk.obj
    if exist *.obj del *.obj
    
    echo [3/4] Compiling Standalone Enterprise CLI: binpack-sdk.exe
    zig cc -O3 -mavx2 %INCLUDES% cli/binpack_cli.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o binpack-sdk.exe
    if errorlevel 1 goto build_err

    echo [4/4] Compiling Regression Test Suite: test_sdk.exe
    zig cc -O3 -mavx2 %INCLUDES% tests/test_sdk.c binpack.lib %ZSTD_LIB% %LZMA_LIB% -o test_sdk.exe
    if errorlevel 1 goto build_err

    echo.
    echo [SUCCESS] Build completed successfully:
    echo   - Dynamic Library: binpack.dll
    echo   - Static Library:  binpack.lib
    echo   - Enterprise CLI:  binpack-sdk.exe
    echo   - Test Suite:      test_sdk.exe
    echo.
    echo Running automated test suite:
    test_sdk.exe
    exit /b %ERRORLEVEL%
)

where cl >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Building with MSVC cl.exe...
    cl /O2 /arch:AVX2 /W3 /DBINPACK_BUILD_DLL %INCLUDES% %SRC_FILES% %ZSTD_LIB% %LZMA_LIB% /LD /Fe:binpack.dll
    cl /O2 /arch:AVX2 /W3 /c %INCLUDES% %SRC_FILES%
    lib /OUT:binpack.lib binpack_core.obj binpack_eval.obj binpack_stream.obj binpack_sdk.obj
    cl /O2 /arch:AVX2 /W3 %INCLUDES% cli/binpack_cli.c binpack.lib %ZSTD_LIB% %LZMA_LIB% /Fe:binpack-sdk.exe
    cl /O2 /arch:AVX2 /W3 %INCLUDES% tests/test_sdk.c binpack.lib %ZSTD_LIB% %LZMA_LIB% /Fe:test_sdk.exe
    if exist *.obj del *.obj
    test_sdk.exe
    exit /b %ERRORLEVEL%
)

where gcc >nul 2>nul
if %ERRORLEVEL% equ 0 (
    echo [BUILD] Building with GCC...
    gcc -O3 -mavx2 -shared -DBINPACK_BUILD_DLL %INCLUDES% %SRC_FILES% %ZSTD_LIB% %LZMA_LIB% -o binpack.dll
    gcc -O3 -mavx2 %INCLUDES% cli/binpack_cli.c %SRC_FILES% %ZSTD_LIB% %LZMA_LIB% -o binpack-sdk.exe
    gcc -O3 -mavx2 %INCLUDES% tests/test_sdk.c %SRC_FILES% %ZSTD_LIB% %LZMA_LIB% -o test_sdk.exe
    test_sdk.exe
    exit /b %ERRORLEVEL%
)

echo [ERROR] No supported compiler found in PATH.
exit /b 1

:build_err
echo [ERROR] Compilation failed.
exit /b 1
