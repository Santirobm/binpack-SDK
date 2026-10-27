#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <windows.h>
#include "../../include/binpack_sdk.h"
#include <zstd.h>

typedef struct {
    const char *name;
    const char *category;
    const char *path;
    int is_synth;
    int synth_type; /* 1: F32 ramp, 2: F32 sine */
    binpack_filter_t filter;
    uint16_t stride;
} exp4_case_t;

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

static uint8_t* create_synthetic_f32_ramp(size_t num_elements) {
    float *arr = (float*)malloc(num_elements * sizeof(float));
    for (size_t i = 0; i < num_elements; i++) {
        arr[i] = (float)i * 0.1f;
    }
    return (uint8_t*)arr;
}

static uint8_t* create_synthetic_f32_sine(size_t num_elements) {
    float *arr = (float*)malloc(num_elements * sizeof(float));
    for (size_t i = 0; i < num_elements; i++) {
        arr[i] = 100.0f * (float)sin((double)i * (50.0 * 3.141592653589793 / (double)num_elements));
    }
    return (uint8_t*)arr;
}

int main() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    printf("========================================================================================================================\n");
    printf("EXPERIMENTO 4: PREFILTRO AISLADO - TRANSFORMACION MATEMATICA vs OPTIMIZACIONES DE EJECUCION\n");
    printf("========================================================================================================================\n\n");

    exp4_case_t cases[] = {
        {"F32_Ramp_8MB",        "Matriz Float32",    NULL, 1, 1, BINPACK_FILTER_TRANSPOSE_DELTA, 4},
        {"F32_Sine_8MB",        "Matriz Float32",    NULL, 1, 2, BINPACK_FILTER_TRANSPOSE_DELTA, 4},
        {"turbofan_float32",    "NASA Turbofan F32", "C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_turbofan_float32.bin", 0, 0, BINPACK_FILTER_TRANSPOSE_DELTA, 96},
        {"x-ray",               "16b Sensor X-Ray",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/x-ray", 0, 0, BINPACK_FILTER_TRANSPOSE_DELTA, 2},
        {"mr",                  "16b Sensor MRI",    "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/mr", 0, 0, BINPACK_FILTER_TRANSPOSE_DELTA, 2},
        {"busybox_armv7l",      "ELF ARM32 Firmware","C:/Users/sgi.lm/Desktop/Compiler/corpora/busybox_armv7l", 0, 0, BINPACK_FILTER_ARM32, 0},
        {"ooffice",             "ELF x86_64 Exec",   "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/ooffice", 0, 0, BINPACK_FILTER_X86_64, 0},
        {"xml",                 "Silesia XML Texto", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/xml", 0, 0, BINPACK_FILTER_NONE, 0}
    };

    printf("%-18s | %-8s | %-16s | %-20s | %-20s | %-16s\n",
           "Dataset", "Tamano", "1. Zstd L3 Solo", "2. Prefiltro + Zstd", "3. BinPack RT Comp", "Ganancia Prefiltro");
    printf("------------------------------------------------------------------------------------------------------------------------\n");

    ZSTD_CCtx *cctx = ZSTD_createCCtx();

    binpack_options_t bp_opt;
    binpack_default_options(&bp_opt);
    bp_opt.clevel = 3;
    bp_opt.mode = BINPACK_MODE_RUNTIME;
    bp_opt.enable_micro_trial = 0;

    for (size_t c = 0; c < sizeof(cases)/sizeof(cases[0]); c++) {
        uint8_t *raw_data = NULL;
        size_t raw_sz = 0;
        if (cases[c].is_synth) {
            size_t n_elem = 2 * 1024 * 1024; // 8 MB Float32
            if (cases[c].synth_type == 1) raw_data = create_synthetic_f32_ramp(n_elem);
            else if (cases[c].synth_type == 2) raw_data = create_synthetic_f32_sine(n_elem);
            raw_sz = n_elem * sizeof(float);
        } else {
            raw_data = load_file(cases[c].path, &raw_sz);
        }

        if (!raw_data) {
            printf("[SKIP] No se pudo cargar %s\n", cases[c].name);
            continue;
        }

        size_t bound = ZSTD_compressBound(raw_sz);
        uint8_t *zdst = (uint8_t*)malloc(bound);
        size_t bpbound = binpack_compress_bound(raw_sz);
        uint8_t *bpdst = (uint8_t*)malloc(bpbound);
        uint8_t *filtered = (uint8_t*)malloc(raw_sz);

        int iters = (raw_sz <= 2 * 1024 * 1024) ? 30 : 10;
        LARGE_INTEGER t0, t1;

        /* ========================================================================= */
        /* 1. ZSTD L3 DIRECTO (Sin prefiltro)                                        */
        /* ========================================================================= */
        size_t z_sz = 0;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < iters; i++) {
            ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
            z_sz = ZSTD_compressCCtx(cctx, zdst, bound, raw_data, raw_sz, 3);
        }
        QueryPerformanceCounter(&t1);
        double z_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * (double)iters);
        double z_mbs = (raw_sz / 1048576.0) / z_sec;
        double z_ratio = ((double)z_sz / (double)raw_sz) * 100.0;

        /* ========================================================================= */
        /* 2. PREFILTRO AISLADO + ZSTD L3                                            */
        /* ========================================================================= */
        size_t fz_sz = 0;
        double filt_math_sec = 0.0;
        double fz_total_sec = 0.0;

        QueryPerformanceCounter(&t0);
        for (int i = 0; i < iters; i++) {
            memcpy(filtered, raw_data, raw_sz);

            LARGE_INTEGER mf0, mf1;
            QueryPerformanceCounter(&mf0);
            if (cases[c].filter == BINPACK_FILTER_TRANSPOSE_DELTA) {
                binpack_transpose_delta_filter(filtered, raw_sz, cases[c].stride);
            } else if (cases[c].filter == BINPACK_FILTER_X86_64) {
                binpack_bcj_x86_filter(filtered, raw_sz);
            } else if (cases[c].filter == BINPACK_FILTER_ARM32) {
                binpack_bcj_arm32_filter(filtered, raw_sz);
            }
            QueryPerformanceCounter(&mf1);
            filt_math_sec += (double)(mf1.QuadPart - mf0.QuadPart) / (double)freq.QuadPart;

            ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
            fz_sz = ZSTD_compressCCtx(cctx, zdst, bound, filtered, raw_sz, 3);
        }
        QueryPerformanceCounter(&t1);
        fz_total_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * (double)iters);
        filt_math_sec /= (double)iters;
        double fz_mbs = (raw_sz / 1048576.0) / fz_total_sec;
        double fz_ratio = ((double)fz_sz / (double)raw_sz) * 100.0;

        /* ========================================================================= */
        /* 3. BINPACK RT COMPLETO (Auto-detect + Prefiltro + Headers BPK2 + FastPath)  */
        /* ========================================================================= */
        size_t bp_sz = 0;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < iters; i++) {
            binpack_compress_into(raw_data, raw_sz, bpdst, bpbound, &bp_sz, &bp_opt);
        }
        QueryPerformanceCounter(&t1);
        double bp_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * (double)iters);
        double bp_mbs = (raw_sz / 1048576.0) / bp_sec;
        double bp_ratio = ((double)bp_sz / (double)raw_sz) * 100.0;

        /* Calculo de impacto exacto */
        double ratio_gain = z_ratio - fz_ratio; /* Puntos porcentuales ganados por el prefiltro */
        double framing_overhead_bytes = (double)bp_sz - (double)fz_sz;

        char sz_str[32];
        if (raw_sz < 1024 * 1024) sprintf(sz_str, "%.1f KB", raw_sz / 1024.0);
        else sprintf(sz_str, "%.2f MB", raw_sz / 1048576.0);

        char z_str[32], fz_str[32], bp_str[32];
        sprintf(z_str, "%5.2f%% @ %5.1fM", z_ratio, z_mbs);
        sprintf(fz_str, "%5.2f%% @ %5.1fM", fz_ratio, fz_mbs);
        sprintf(bp_str, "%5.2f%% @ %5.1fM", bp_ratio, bp_mbs);

        char gain_str[32];
        if (ratio_gain > 0.05) {
            sprintf(gain_str, "-%5.2f%% (+%zu B)", ratio_gain, (size_t)(z_sz - fz_sz));
        } else {
            sprintf(gain_str, "0.00%% (Bypass)");
        }

        printf("%-18s | %-8s | %-16s | %-20s | %-20s | %-16s\n",
               cases[c].name, sz_str, z_str, fz_str, bp_str, gain_str);

        free(zdst);
        free(bpdst);
        free(filtered);
        free(raw_data);
    }
    ZSTD_freeCCtx(cctx);

    printf("========================================================================================================================\n\n");
    return 0;
}
