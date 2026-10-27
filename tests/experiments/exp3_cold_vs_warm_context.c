#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <windows.h>
#include "../../include/binpack_sdk.h"
#include <zstd.h>

static int cmp_double(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static void run_scale(const uint8_t *src, size_t sz, int N, LARGE_INTEGER freq) {
    size_t zbound = ZSTD_compressBound(sz);
    uint8_t *zdst = (uint8_t*)malloc(zbound);
    size_t bpbound = binpack_compress_bound(sz);
    uint8_t *bpdst = (uint8_t*)malloc(bpbound);

    binpack_options_t bp_opt;
    binpack_default_options(&bp_opt);
    bp_opt.clevel = 3;
    bp_opt.mode = BINPACK_MODE_RUNTIME;
    bp_opt.nb_workers = 1;
    bp_opt.enable_micro_trial = 0;

    double *times_cold = (double*)malloc(N * sizeof(double));
    double *times_zstd_warm = (double*)malloc(N * sizeof(double));
    double *times_bp_warm = (double*)malloc(N * sizeof(double));

    /* 1. VARIANTE 1: Zstd L3 Frio (Create + Compress + Free en cada llamada) */
    for (int i = 0; i < N; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        size_t c_sz = ZSTD_compressCCtx(cctx, zdst, zbound, src, sz, 3);
        ZSTD_freeCCtx(cctx);

        QueryPerformanceCounter(&t1);
        times_cold[i] = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / (double)freq.QuadPart;
    }

    /* 2. VARIANTE 2: Zstd L3 Caliente (Reutiliza CCtx con ZSTD_reset_session_only) */
    ZSTD_CCtx *warm_zstd_cctx = ZSTD_createCCtx();
    for (int i = 0; i < N; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        ZSTD_CCtx_reset(warm_zstd_cctx, ZSTD_reset_session_only);
        size_t c_sz = ZSTD_compressCCtx(warm_zstd_cctx, zdst, zbound, src, sz, 3);

        QueryPerformanceCounter(&t1);
        times_zstd_warm[i] = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / (double)freq.QuadPart;
    }
    ZSTD_freeCCtx(warm_zstd_cctx);

    /* 3. VARIANTE 3: BinPack RT Caliente (s_fast_cctx en Thread-Local Storage) */
    for (int i = 0; i < N; i++) {
        LARGE_INTEGER t0, t1;
        size_t c_sz = 0;
        QueryPerformanceCounter(&t0);

        binpack_compress_into(src, sz, bpdst, bpbound, &c_sz, &bp_opt);

        QueryPerformanceCounter(&t1);
        times_bp_warm[i] = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / (double)freq.QuadPart;
    }

    /* Extraer Run #1 (Cold Start) */
    double cold_first = times_cold[0];
    double z_warm_first = times_zstd_warm[0];
    double bp_warm_first = times_bp_warm[0];

    /* Ordenar las corridas 2..N para calcular p50, p95 y p99 */
    int tail_count = N - 1;
    double *tail_cold = (double*)malloc(tail_count * sizeof(double));
    double *tail_zwarm = (double*)malloc(tail_count * sizeof(double));
    double *tail_bpwarm = (double*)malloc(tail_count * sizeof(double));

    memcpy(tail_cold, times_cold + 1, tail_count * sizeof(double));
    memcpy(tail_zwarm, times_zstd_warm + 1, tail_count * sizeof(double));
    memcpy(tail_bpwarm, times_bp_warm + 1, tail_count * sizeof(double));

    qsort(tail_cold, tail_count, sizeof(double), cmp_double);
    qsort(tail_zwarm, tail_count, sizeof(double), cmp_double);
    qsort(tail_bpwarm, tail_count, sizeof(double), cmp_double);

    int idx_p50 = (int)(tail_count * 0.50);
    int idx_p95 = (int)(tail_count * 0.95);
    int idx_p99 = (int)(tail_count * 0.99);

    /* 4. MEDIDA REAL: Ejecutar durante 1.0 segundo sostenido para contar ops reales completadas */
    LARGE_INTEGER t_start, t_now;
    long long real_ops_cold = 0;
    QueryPerformanceCounter(&t_start);
    do {
        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        ZSTD_compressCCtx(cctx, zdst, zbound, src, sz, 3);
        ZSTD_freeCCtx(cctx);
        real_ops_cold++;
        QueryPerformanceCounter(&t_now);
    } while (((double)(t_now.QuadPart - t_start.QuadPart) / (double)freq.QuadPart) < 1.0);

    long long real_ops_zwarm = 0;
    ZSTD_CCtx *bench_cctx = ZSTD_createCCtx();
    QueryPerformanceCounter(&t_start);
    do {
        ZSTD_CCtx_reset(bench_cctx, ZSTD_reset_session_only);
        ZSTD_compressCCtx(bench_cctx, zdst, zbound, src, sz, 3);
        real_ops_zwarm++;
        QueryPerformanceCounter(&t_now);
    } while (((double)(t_now.QuadPart - t_start.QuadPart) / (double)freq.QuadPart) < 1.0);
    ZSTD_freeCCtx(bench_cctx);

    long long real_ops_bpwarm = 0;
    size_t dummy_sz = 0;
    QueryPerformanceCounter(&t_start);
    do {
        binpack_compress_into(src, sz, bpdst, bpbound, &dummy_sz, &bp_opt);
        real_ops_bpwarm++;
        QueryPerformanceCounter(&t_now);
    } while (((double)(t_now.QuadPart - t_start.QuadPart) / (double)freq.QuadPart) < 1.0);

    printf("\n>>> ESCALA N = %d operaciones (Payload: %.1f KB) <<<\n", N, sz / 1024.0);
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    printf("%-24s | %-16s | %-16s | %-16s | %-16s\n",
           "Metrica Evaluada", "1. Zstd L3 Frio", "2. Zstd Caliente", "3. BinPack RT Cal", "Delta (1 vs 3)");
    printf("------------------------------------------------------------------------------------------------------------------------\n");
    printf("%-24s | %10.2f us     | %10.2f us     | %10.2f us     | %8.2fx menos\n",
           "Run #1 (Cold Start)", cold_first, z_warm_first, bp_warm_first, cold_first / bp_warm_first);
    printf("%-24s | %10.2f us     | %10.2f us     | %10.2f us     | %8.2fx veloz\n",
           "Mediana (p50)", tail_cold[idx_p50], tail_zwarm[idx_p50], tail_bpwarm[idx_p50], tail_cold[idx_p50] / tail_bpwarm[idx_p50]);
    printf("%-24s | %10.2f us     | %10.2f us     | %10.2f us     | %8.2fx veloz\n",
           "Percentil 95 (p95)", tail_cold[idx_p95], tail_zwarm[idx_p95], tail_bpwarm[idx_p95], tail_cold[idx_p95] / tail_bpwarm[idx_p95]);
    printf("%-24s | %10.2f us     | %10.2f us     | %10.2f us     | %8.2fx veloz\n",
           "Percentil 99 (p99)", tail_cold[idx_p99], tail_zwarm[idx_p99], tail_bpwarm[idx_p99], tail_cold[idx_p99] / tail_bpwarm[idx_p99]);
    printf("%-24s | %10.2f us     | %10.2f us     | %10.2f us     | %8.2f us\n",
           "Overhead asignacion", tail_cold[idx_p50] - tail_zwarm[idx_p50], 0.0, tail_bpwarm[idx_p50] - tail_zwarm[idx_p50], tail_cold[idx_p50] - tail_bpwarm[idx_p50]);
    printf("%-24s | %8lld ops/s    | %8lld ops/s    | %8lld ops/s    | +%-7lld ops/s\n",
           "Ops Reales en 1.0s", real_ops_cold, real_ops_zwarm, real_ops_bpwarm, real_ops_bpwarm - real_ops_cold);
    printf("------------------------------------------------------------------------------------------------------------------------\n");

    free(times_cold);
    free(times_zstd_warm);
    free(times_bp_warm);
    free(tail_cold);
    free(tail_zwarm);
    free(tail_bpwarm);
    free(zdst);
    free(bpdst);
}

int main() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    printf("========================================================================================================================\n");
    printf("EXPERIMENTO 3: METODOLOGIA DE 3 VIAS - ASIGNACION/INICIALIZACION vs COMPRESION (FRIO vs CALIENTE)\n");
    printf("Condiciones: Intel Core i5-13500, Windows 10 Pro, Zig Clang AVX2 -O3, Zstd v1.5.7\n");
    printf("========================================================================================================================\n");

    size_t payload_sz = 64 * 1024; // 64 KB telemetria
    uint8_t *sample_payload = (uint8_t*)malloc(payload_sz);
    for (size_t i = 0; i < payload_sz; i++) {
        sample_payload[i] = (uint8_t)((i * 7 + 13) % 256);
    }

    run_scale(sample_payload, payload_sz, 100, freq);
    run_scale(sample_payload, payload_sz, 1000, freq);
    run_scale(sample_payload, payload_sz, 10000, freq);

    printf("\n========================================================================================================================\n\n");
    free(sample_payload);
    return 0;
}
