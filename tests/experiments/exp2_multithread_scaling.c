#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include <psapi.h>
#include "../../include/binpack_sdk.h"
#include <zstd.h>

static uint8_t* load_file(const char *path, size_t *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc(sz);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, sz, f);
    fclose(f);
    *out_sz = sz;
    return buf;
}

static uint64_t filetime_to_u64(FILETIME ft) {
    ULARGE_INTEGER li;
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    return li.QuadPart;
}

int main() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    const char *test_path = "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/mozilla";
    size_t raw_sz = 0;
    uint8_t *raw_data = load_file(test_path, &raw_sz);
    if (!raw_data) {
        test_path = "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/webster";
        raw_data = load_file(test_path, &raw_sz);
    }
    if (!raw_data) {
        printf("[ERROR] No se pudo cargar dataset de prueba\n");
        return 1;
    }

    printf("========================================================================================================\n");
    printf("EXPERIMENTO 2: ESCALABILIDAD DE HILOS (1, 2, 4, 8 THREADS) - ZSTD L3 vs BINPACK RT\n");
    printf("Dataset: %s (%.2f MB / %zu bytes)\n", test_path, raw_sz / 1048576.0, raw_sz);
    printf("========================================================================================================\n\n");

    int thread_counts[] = {1, 2, 4, 8};
    int iters = 5;

    size_t zbound = ZSTD_compressBound(raw_sz);
    uint8_t *zdst = (uint8_t*)malloc(zbound);

    size_t bpbound = binpack_compress_bound(raw_sz);
    uint8_t *bpdst = (uint8_t*)malloc(bpbound);

    printf("%-14s | %-6s | %-12s | %-10s | %-12s | %-14s | %-10s\n",
           "Motor", "Hilos", "Throughput", "CPU Uso", "Peak RAM", "Tamano Final", "Ratio");
    printf("--------------------------------------------------------------------------------------------------------\n");

    /* 1. Meta Zstd L3 across 1, 2, 4, 8 threads */
    for (size_t i = 0; i < sizeof(thread_counts)/sizeof(thread_counts[0]); i++) {
        int th = thread_counts[i];
        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 3);
        ZSTD_CCtx_setParameter(cctx, ZSTD_c_nbWorkers, th);

        size_t c_sz = 0;
        if (th > 1) {
            ZSTD_inBuffer in = { raw_data, raw_sz, 0 };
            ZSTD_outBuffer out = { zdst, zbound, 0 };
            ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end);
            c_sz = out.pos;
        } else {
            c_sz = ZSTD_compressCCtx(cctx, zdst, zbound, raw_data, raw_sz, 3);
        }

        FILETIME c0, e0, k0, u0;
        FILETIME c1, e1, k1, u1;
        LARGE_INTEGER t0, t1;

        GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
        QueryPerformanceCounter(&t0);

        for (int it = 0; it < iters; it++) {
            ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
            if (th > 1) {
                ZSTD_inBuffer in = { raw_data, raw_sz, 0 };
                ZSTD_outBuffer out = { zdst, zbound, 0 };
                ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end);
                c_sz = out.pos;
            } else {
                c_sz = ZSTD_compressCCtx(cctx, zdst, zbound, raw_data, raw_sz, 3);
            }
        }

        QueryPerformanceCounter(&t1);
        GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);

        double wall_sec = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        double wall_per_iter = wall_sec / (double)iters;
        double mbs = (raw_sz / 1048576.0) / wall_per_iter;

        uint64_t cpu_k = filetime_to_u64(k1) - filetime_to_u64(k0);
        uint64_t cpu_u = filetime_to_u64(u1) - filetime_to_u64(u0);
        double total_cpu_sec = (double)(cpu_k + cpu_u) / 10000000.0;
        double cpu_pct = (total_cpu_sec / wall_sec) * 100.0;

        PROCESS_MEMORY_COUNTERS pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        double peak_ram_mb = (double)pmc.PeakWorkingSetSize / 1048576.0;

        double ratio = ((double)c_sz / (double)raw_sz) * 100.0;

        printf("%-14s | %-6d | %8.1f MB/s | %8.1f%% | %9.1f MB | %10zu B | %7.2f%%\n",
               "Meta Zstd L3", th, mbs, cpu_pct, peak_ram_mb, c_sz, ratio);

        ZSTD_freeCCtx(cctx);
    }

    printf("--------------------------------------------------------------------------------------------------------\n");

    /* 2. BinPack RT across 1, 2, 4, 8 threads */
    for (size_t i = 0; i < sizeof(thread_counts)/sizeof(thread_counts[0]); i++) {
        int th = thread_counts[i];

        binpack_options_t bp_opt;
        binpack_default_options(&bp_opt);
        bp_opt.clevel = 3;
        bp_opt.nb_workers = th;
        bp_opt.mode = BINPACK_MODE_RUNTIME;
        bp_opt.enable_micro_trial = 0;

        size_t c_sz = 0;
        /* Warmup run */
        binpack_compress_into(raw_data, raw_sz, bpdst, bpbound, &c_sz, &bp_opt);

        FILETIME c0, e0, k0, u0;
        FILETIME c1, e1, k1, u1;
        LARGE_INTEGER t0, t1;

        GetProcessTimes(GetCurrentProcess(), &c0, &e0, &k0, &u0);
        QueryPerformanceCounter(&t0);

        for (int it = 0; it < iters; it++) {
            binpack_compress_into(raw_data, raw_sz, bpdst, bpbound, &c_sz, &bp_opt);
        }

        QueryPerformanceCounter(&t1);
        GetProcessTimes(GetCurrentProcess(), &c1, &e1, &k1, &u1);

        double wall_sec = (double)(t1.QuadPart - t0.QuadPart) / (double)freq.QuadPart;
        double wall_per_iter = wall_sec / (double)iters;
        double mbs = (raw_sz / 1048576.0) / wall_per_iter;

        uint64_t cpu_k = filetime_to_u64(k1) - filetime_to_u64(k0);
        uint64_t cpu_u = filetime_to_u64(u1) - filetime_to_u64(u0);
        double total_cpu_sec = (double)(cpu_k + cpu_u) / 10000000.0;
        double cpu_pct = (total_cpu_sec / wall_sec) * 100.0;

        PROCESS_MEMORY_COUNTERS pmc;
        GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc));
        double peak_ram_mb = (double)pmc.PeakWorkingSetSize / 1048576.0;

        double ratio = ((double)c_sz / (double)raw_sz) * 100.0;

        printf("%-14s | %-6d | %8.1f MB/s | %8.1f%% | %9.1f MB | %10zu B | %7.2f%%\n",
               "BinPack RT", th, mbs, cpu_pct, peak_ram_mb, c_sz, ratio);
    }

    printf("========================================================================================================\n\n");

    free(zdst);
    free(bpdst);
    free(raw_data);
    return 0;
}
