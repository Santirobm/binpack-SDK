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
    size_t bound = ZSTD_compressBound(sz);
    uint8_t *dst = (uint8_t*)malloc(bound);

    double *cold_times = (double*)malloc(N * sizeof(double));
    double *warm_times = (double*)malloc(N * sizeof(double));

    /* 1. MODO FRIO: Crear y destruir contexto en CADA iteracion */
    for (int i = 0; i < N; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        ZSTD_CCtx *cctx = ZSTD_createCCtx();
        size_t c_sz = ZSTD_compressCCtx(cctx, dst, bound, src, sz, 3);
        ZSTD_freeCCtx(cctx);

        QueryPerformanceCounter(&t1);
        cold_times[i] = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / (double)freq.QuadPart;
    }

    /* 2. MODO CALIENTE: Reutilizar contexto con ZSTD_reset_session_only */
    ZSTD_CCtx *warm_cctx = NULL;
    for (int i = 0; i < N; i++) {
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);

        if (!warm_cctx) {
            warm_cctx = ZSTD_createCCtx();
        } else {
            ZSTD_CCtx_reset(warm_cctx, ZSTD_reset_session_only);
        }
        size_t c_sz = ZSTD_compressCCtx(warm_cctx, dst, bound, src, sz, 3);

        QueryPerformanceCounter(&t1);
        warm_times[i] = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / (double)freq.QuadPart;
    }
    if (warm_cctx) ZSTD_freeCCtx(warm_cctx);

    /* Extraer Run #1 (Cold Start) */
    double cold_first = cold_times[0];
    double warm_first = warm_times[0];

    /* Ordenar las corridas 2..N para calcular p50 y p95 */
    int tail_count = N - 1;
    double *cold_tail = (double*)malloc(tail_count * sizeof(double));
    double *warm_tail = (double*)malloc(tail_count * sizeof(double));
    memcpy(cold_tail, cold_times + 1, tail_count * sizeof(double));
    memcpy(warm_tail, warm_times + 1, tail_count * sizeof(double));

    qsort(cold_tail, tail_count, sizeof(double), cmp_double);
    qsort(warm_tail, tail_count, sizeof(double), cmp_double);

    int idx_p50 = (int)(tail_count * 0.50);
    int idx_p95 = (int)(tail_count * 0.95);
    int idx_p99 = (int)(tail_count * 0.99);

    double cold_p50 = cold_tail[idx_p50];
    double cold_p95 = cold_tail[idx_p95];
    double cold_p99 = cold_tail[idx_p99];

    double warm_p50 = warm_tail[idx_p50];
    double warm_p95 = warm_tail[idx_p95];
    double warm_p99 = warm_tail[idx_p99];

    double speedup_p50 = cold_p50 / warm_p50;
    double speedup_p95 = cold_p95 / warm_p95;

    printf("\n>>> ESCALA: N = %d operaciones (Payload: %.1f KB) <<<\n", N, sz / 1024.0);
    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("%-20s | %-14s | %-14s | %-14s | %-14s\n",
           "Metrica de Latencia", "Contexto Frio", "Contexto Caliente", "Aceleracion", "Reduccion Tiempo");
    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("%-20s | %10.2f us   | %10.2f us   | %10.2fx     | %10.2f us\n", 
           "Run #1 (Cold Start)", cold_first, warm_first, cold_first / warm_first, cold_first - warm_first);
    printf("%-20s | %10.2f us   | %10.2f us   | %10.2fx     | %10.2f us\n", 
           "Mediana (p50)", cold_p50, warm_p50, speedup_p50, cold_p50 - warm_p50);
    printf("%-20s | %10.2f us   | %10.2f us   | %10.2fx     | %10.2f us\n", 
           "Percentil 95 (p95)", cold_p95, warm_p95, speedup_p95, cold_p95 - warm_p95);
    printf("%-20s | %10.2f us   | %10.2f us   | %10.2fx     | %10.2f us\n", 
           "Percentil 99 (p99)", cold_p99, warm_p99, cold_p99 / warm_p99, cold_p99 - warm_p99);

    double ops_sec_cold = 1000000.0 / cold_p50;
    double ops_sec_warm = 1000000.0 / warm_p50;
    printf("%-20s | %8.0f ops/s | %8.0f ops/s | %10.2fx     |\n",
           "Throughput (p50)", ops_sec_cold, ops_sec_warm, ops_sec_warm / ops_sec_cold);
    printf("--------------------------------------------------------------------------------------------------------\n");

    free(cold_times);
    free(warm_times);
    free(cold_tail);
    free(warm_tail);
    free(dst);
}

int main() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    printf("========================================================================================================\n");
    printf("EXPERIMENTO 3: IMPACTO DE GESTION DE CONTEXTO (FRIO vs CALIENTE) EN 100, 1.000 Y 10.000 CORRIDAS\n");
    printf("========================================================================================================\n");

    /* Payload de telemetria / microservicio representativo: 64 KB */
    size_t payload_sz = 64 * 1024;
    uint8_t *sample_payload = (uint8_t*)malloc(payload_sz);
    for (size_t i = 0; i < payload_sz; i++) {
        sample_payload[i] = (uint8_t)((i * 7 + 13) % 256);
    }

    run_scale(sample_payload, payload_sz, 100, freq);
    run_scale(sample_payload, payload_sz, 1000, freq);
    run_scale(sample_payload, payload_sz, 10000, freq);

    printf("\n========================================================================================================\n\n");
    free(sample_payload);
    return 0;
}
