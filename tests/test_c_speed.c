#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <windows.h>
#include "../include/binpack_sdk.h"
#include <zstd.h>

int main() {
    FILE *f = fopen("C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/etth1_transformer.csv", "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *data = (uint8_t*)malloc(sz);
    fread(data, 1, sz, f);
    fclose(f);

    LARGE_INTEGER freq, t0, t1;
    QueryPerformanceFrequency(&freq);

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.clevel = 3;
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.enable_micro_trial = 0;

    void *dst = NULL;
    size_t dst_sz = 0;
    size_t bound = binpack_compress_bound(sz);
    uint8_t *prealloc_dst = (uint8_t*)malloc(bound);

    int iters = 100;

    /* 1. Pure C ZSTD CCtx Benchmark (pre-allocated) */
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    size_t zbound = ZSTD_compressBound(sz);
    uint8_t *zdst = (uint8_t*)malloc(zbound);

    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iters; i++) {
        ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
        ZSTD_compressCCtx(cctx, zdst, zbound, data, sz, 3);
    }
    QueryPerformanceCounter(&t1);
    double sec_z = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart / (double)iters;
    double mbs_z = (sz / 1048576.0) / sec_z;

    /* 2. BinPack binpack_compress_into (pre-allocated) */
    size_t comp_sz = 0;
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iters; i++) {
        binpack_compress_into(data, sz, prealloc_dst, bound, &comp_sz, &opt);
    }
    QueryPerformanceCounter(&t1);
    double sec_into = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart / (double)iters;
    double mbs_into = (sz / 1048576.0) / sec_into;

    /* 3. BinPack binpack_compress_buffer (allocating dst) */
    QueryPerformanceCounter(&t0);
    for (int i = 0; i < iters; i++) {
        binpack_compress_buffer(data, sz, &dst, &dst_sz, &opt);
        binpack_free(dst);
    }
    QueryPerformanceCounter(&t1);
    double sec_bp = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart / (double)iters;
    double mbs_bp = (sz / 1048576.0) / sec_bp;

    printf("Pure C Native Performance (etth1_transformer.csv, %.2f MB):\n", sz / 1048576.0);
    printf("  Zstd L3 pure C (pre-allocated):            %.1f MB/s (%.3f ms)\n", mbs_z, sec_z * 1000.0);
    printf("  BinPack RT compress_into (pre-allocated):   %.1f MB/s (%.3f ms)\n", mbs_into, sec_into * 1000.0);
    printf("  BinPack RT compress_buffer (dynamic alloc): %.1f MB/s (%.3f ms)\n", mbs_bp, sec_bp * 1000.0);

    return 0;
}
