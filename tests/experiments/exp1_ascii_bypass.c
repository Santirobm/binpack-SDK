#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <windows.h>
#include "../../include/binpack_sdk.h"
#include <zstd.h>

typedef struct {
    const char *label;
    const char *path;
    size_t custom_sz;
} text_case_t;

typedef struct {
    const char *label;
    const char *path;
    int is_synthetic;
    const char *expected_type;
} binary_case_t;

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
        arr[i] = 100.0f * (float)sin((double)i * 0.05);
    }
    return (uint8_t*)arr;
}

static uint8_t* create_synthetic_i16_sine(size_t num_elements) {
    int16_t *arr = (int16_t*)malloc(num_elements * sizeof(int16_t));
    for (size_t i = 0; i < num_elements; i++) {
        arr[i] = (int16_t)(16000.0 * sin((double)i * 0.05));
    }
    return (uint8_t*)arr;
}

static uint8_t* create_synthetic_f64_sine(size_t num_elements) {
    double *arr = (double*)malloc(num_elements * sizeof(double));
    for (size_t i = 0; i < num_elements; i++) {
        arr[i] = 100.0 * sin((double)i * 0.05);
    }
    return (uint8_t*)arr;
}

static uint8_t* create_synthetic_i32_ramp(size_t num_elements) {
    int32_t *arr = (int32_t*)malloc(num_elements * sizeof(int32_t));
    for (size_t i = 0; i < num_elements; i++) {
        arr[i] = (int32_t)(i % 65536);
    }
    return (uint8_t*)arr;
}

int main() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    printf("========================================================================================================\n");
    printf("EXPERIMENTO 1: BYPASS ASCII - MEDICION DE LATENCIA, DETECCION Y TASA DE FALSOS POSITIVOS\n");
    printf("========================================================================================================\n\n");

    /* PARTE A: ARCHIVOS DE TEXTO DE 4 KB A 50 MB */
    printf("--- PARTE A: Throughput, Latencia y Costo de Deteccion en Texto (4 KB a 50 MB) ---\n");
    printf("%-18s | %-9s | %-12s | %-20s | %-20s | %-12s\n", 
           "Dataset Texto", "Tamano", "Costo Detect", "Zstd L3 (Lat / MB/s)", "BinPack RT (Lat / MB/s)", "Speedup");
    printf("--------------------------------------------------------------------------------------------------------\n");

    text_case_t text_files[] = {
        {"grammar.lsp", "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/grammar.lsp", 0},
        {"fields.c",    "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/fields.c", 0},
        {"alice29.txt", "C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/alice29.txt", 0},
        {"1MB_sample",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/dickens", 1024 * 1024},
        {"xml",         "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/xml", 0},
        {"dickens",     "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/dickens", 0},
        {"samba",       "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/samba", 0},
        {"webster",     "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/webster", 0}
    };

    ZSTD_CCtx *z_cctx = ZSTD_createCCtx();

    binpack_options_t bp_opt;
    binpack_default_options(&bp_opt);
    bp_opt.clevel = 3;
    bp_opt.mode = BINPACK_MODE_RUNTIME;
    bp_opt.enable_micro_trial = 0;

    for (size_t t = 0; t < sizeof(text_files) / sizeof(text_files[0]); t++) {
        size_t raw_sz = 0;
        uint8_t *raw_data = load_file(text_files[t].path, &raw_sz);
        if (!raw_data) {
            printf("[SKIP] %s no encontrado\n", text_files[t].path);
            continue;
        }
        if (text_files[t].custom_sz > 0 && text_files[t].custom_sz < raw_sz) {
            raw_sz = text_files[t].custom_sz;
        }

        /* 1. Medir costo de deteccion (nanosegundos) */
        uint16_t param = 0;
        int det_iters = raw_sz < (1024 * 1024) ? 10000 : 2000;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        binpack_filter_t filt = BINPACK_FILTER_NONE;
        for (int i = 0; i < det_iters; i++) {
            filt = binpack_auto_select_filter(raw_data, raw_sz, &param);
        }
        QueryPerformanceCounter(&t1);
        double det_ns = ((double)(t1.QuadPart - t0.QuadPart) * 1e9) / ((double)freq.QuadPart * (double)det_iters);

        /* 2. Medir Zstd L3 Directo */
        size_t zbound = ZSTD_compressBound(raw_sz);
        uint8_t *zdst = (uint8_t*)malloc(zbound);
        int z_iters = raw_sz < (1024 * 1024) ? 100 : 15;
        QueryPerformanceCounter(&t0);
        size_t z_compressed_sz = 0;
        for (int i = 0; i < z_iters; i++) {
            ZSTD_CCtx_reset(z_cctx, ZSTD_reset_session_only);
            z_compressed_sz = ZSTD_compressCCtx(z_cctx, zdst, zbound, raw_data, raw_sz, 3);
        }
        QueryPerformanceCounter(&t1);
        double z_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * (double)z_iters);
        double z_mbs = (raw_sz / 1048576.0) / z_sec;
        double z_lat_ms = z_sec * 1000.0;

        /* 3. Medir BINPACK RT con auto-deteccion y bypass */
        size_t bpbound = binpack_compress_bound(raw_sz);
        uint8_t *bpdst = (uint8_t*)malloc(bpbound);
        size_t bp_compressed_sz = 0;
        QueryPerformanceCounter(&t0);
        for (int i = 0; i < z_iters; i++) {
            binpack_compress_into(raw_data, raw_sz, bpdst, bpbound, &bp_compressed_sz, &bp_opt);
        }
        QueryPerformanceCounter(&t1);
        double bp_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * (double)z_iters);
        double bp_mbs = (raw_sz / 1048576.0) / bp_sec;
        double bp_lat_ms = bp_sec * 1000.0;

        double speedup = bp_mbs / z_mbs;

        char sz_str[32];
        if (raw_sz < 1024 * 1024) sprintf(sz_str, "%.1f KB", raw_sz / 1024.0);
        else sprintf(sz_str, "%.2f MB", raw_sz / 1048576.0);

        char det_str[32];
        if (det_ns < 1000.0) sprintf(det_str, "%.1f ns", det_ns);
        else sprintf(det_str, "%.2f us", det_ns / 1000.0);

        char z_str[32], bp_str[32];
        if (z_lat_ms < 1.0) sprintf(z_str, "%.2f us @ %.1f", z_lat_ms * 1000.0, z_mbs);
        else sprintf(z_str, "%.2f ms @ %.1f", z_lat_ms, z_mbs);

        if (bp_lat_ms < 1.0) sprintf(bp_str, "%.2f us @ %.1f", bp_lat_ms * 1000.0, bp_mbs);
        else sprintf(bp_str, "%.2f ms @ %.1f", bp_lat_ms, bp_mbs);

        printf("%-18s | %-9s | %-12s | %-20s | %-20s | %9.2fx\n",
               text_files[t].label, sz_str, det_str, z_str, bp_str, speedup);

        free(zdst);
        free(bpdst);
        free(raw_data);
    }
    ZSTD_freeCCtx(z_cctx);

    /* PARTE B: EVALUACION DE FALSOS POSITIVOS SOBRE BINARIOS */
    printf("\n--- PARTE B: Evaluacion de Falsos Positivos sobre Binarios (Zero-Bypass Guard) ---\n");
    printf("%-24s | %-12s | %-10s | %-22s | %-14s | %-10s\n",
           "Payload Binario", "Tipo / Sensor", "Tamano", "Filtro Seleccionado", "Tiempo Descarte", "Resultado");
    printf("--------------------------------------------------------------------------------------------------------\n");

    struct {
        const char *name;
        const char *type;
        const char *path;
        int is_synth;
        int synth_type; /* 1: F32 ramp, 2: F32 sine, 3: I16 sine, 4: F64 sine, 5: I32 ramp */
    } bin_cases[] = {
        {"x-ray",                  "16b Sensor X-Ray", "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/x-ray", 0, 0},
        {"mr",                     "16b Sensor MRI",   "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/mr", 0, 0},
        {"sao",                    "Astrometria Bin",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/sao", 0, 0},
        {"ooffice",                "ELF x86_64 Exec",  "C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/ooffice", 0, 0},
        {"busybox_armv7l",         "ELF ARM32 Exec",   "C:/Users/sgi.lm/Desktop/Compiler/corpora/busybox_armv7l", 0, 0},
        {"turbofan_float32.bin",   "NASA Turbofan F32","C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_turbofan_float32.bin", 0, 0},
        {"Synthetic_F32_Ramp",     "Matriz Float32",   NULL, 1, 1},
        {"Synthetic_F32_Sine",     "Matriz Float32",   NULL, 1, 2},
        {"Synthetic_I16_Sine",     "Matriz Int16",     NULL, 1, 3},
        {"Synthetic_F64_Sine",     "Matriz Float64",   NULL, 1, 4},
        {"Synthetic_I32_Ramp",     "Matriz Int32",     NULL, 1, 5}
    };

    int total_binaries = 0;
    int false_positives = 0;

    for (size_t b = 0; b < sizeof(bin_cases)/sizeof(bin_cases[0]); b++) {
        uint8_t *bin_data = NULL;
        size_t bin_sz = 0;
        if (bin_cases[b].is_synth) {
            size_t n_elem = 256 * 1024; // 1 MB
            if (bin_cases[b].synth_type == 1) { bin_data = create_synthetic_f32_ramp(n_elem); bin_sz = n_elem * 4; }
            else if (bin_cases[b].synth_type == 2) { bin_data = create_synthetic_f32_sine(n_elem); bin_sz = n_elem * 4; }
            else if (bin_cases[b].synth_type == 3) { bin_data = create_synthetic_i16_sine(n_elem * 2); bin_sz = n_elem * 4; }
            else if (bin_cases[b].synth_type == 4) { bin_data = create_synthetic_f64_sine(n_elem / 2); bin_sz = n_elem * 4; }
            else if (bin_cases[b].synth_type == 5) { bin_data = create_synthetic_i32_ramp(n_elem); bin_sz = n_elem * 4; }
        } else {
            bin_data = load_file(bin_cases[b].path, &bin_sz);
        }

        if (!bin_data) {
            printf("[SKIP] No se pudo cargar %s\n", bin_cases[b].name);
            continue;
        }

        total_binaries++;

        uint16_t param = 0;
        LARGE_INTEGER t0, t1;
        QueryPerformanceCounter(&t0);
        binpack_filter_t filt = binpack_auto_select_filter(bin_data, bin_sz, &param);
        for (int i = 0; i < 5000; i++) {
            filt = binpack_auto_select_filter(bin_data, bin_sz, &param);
        }
        QueryPerformanceCounter(&t1);
        double reject_ns = ((double)(t1.QuadPart - t0.QuadPart) * 1e9) / ((double)freq.QuadPart * 5000.0);

        const char *filt_name = "NONE";
        if (filt == BINPACK_FILTER_TRANSPOSE_DELTA) filt_name = "TRANSPOSE_DELTA";
        else if (filt == BINPACK_FILTER_BITSHUFFLE) filt_name = "BITSHUFFLE";
        else if (filt == BINPACK_FILTER_X86_64) filt_name = "BCJ_X86_64";
        else if (filt == BINPACK_FILTER_ARM32) filt_name = "BCJ_ARM32";
        else if (filt == BINPACK_FILTER_AARCH64) filt_name = "BCJ_AARCH64";
        else if (filt == BINPACK_FILTER_RISCV) filt_name = "BCJ_RISCV";
        else if (filt == BINPACK_FILTER_TEXTURE_BCN) filt_name = "TEXTURE_BCN";

        /* Test específico: ¿El Bypass ASCII clasifica este binario como texto? */
        int ascii_bypass_triggered = 0;
        size_t sample_len = bin_sz > 1024 ? 1024 : bin_sz;
        size_t valid = 0;
        int has_null = 0;
        LARGE_INTEGER tb0, tb1;
        QueryPerformanceCounter(&tb0);
        for (int i = 0; i < 10000; i++) {
            has_null = 0;
            valid = 0;
            for (size_t k = 0; k < sample_len; k++) {
                uint8_t b = bin_data[k];
                if (b == 0x00) { has_null = 1; break; }
                if ((b >= 0x20 && b <= 0x7E) || b == 0x09 || b == 0x0A || b == 0x0D || b >= 0x80) valid++;
            }
            if (!has_null && (valid * 100 / sample_len) >= 98) {
                ascii_bypass_triggered = 1;
            }
        }
        QueryPerformanceCounter(&tb1);
        double ascii_check_ns = ((double)(tb1.QuadPart - tb0.QuadPart) * 1e9) / ((double)freq.QuadPart * 10000.0);

        if (ascii_bypass_triggered) false_positives++;

        char filt_full[32];
        if (param > 0) sprintf(filt_full, "%s (stride=%d)", filt_name, param);
        else sprintf(filt_full, "%s", filt_name);

        char sz_str[32];
        if (bin_sz < 1024 * 1024) sprintf(sz_str, "%.1f KB", bin_sz / 1024.0);
        else sprintf(sz_str, "%.2f MB", bin_sz / 1048576.0);

        char reject_str[32];
        if (ascii_check_ns < 1000.0) sprintf(reject_str, "%.1f ns", ascii_check_ns);
        else sprintf(reject_str, "%.2f us", ascii_check_ns / 1000.0);

        printf("%-24s | %-12s | %-10s | %-22s | %-14s | %s\n",
               bin_cases[b].name, bin_cases[b].type, sz_str, filt_full, reject_str,
               ascii_bypass_triggered ? "[FALLO FP: TEXTO]" : "[CORRECTO: NO-TEXTO]");

        free(bin_data);
    }

    double fp_rate = ((double)false_positives / (double)total_binaries) * 100.0;
    printf("--------------------------------------------------------------------------------------------------------\n");
    printf("TOTAL BINARIOS EVALUADOS: %d | FALSOS POSITIVOS DE TEXTO: %d | TASA DE ERROR (FP): %.2f%%\n",
           total_binaries, false_positives, fp_rate);
    printf("========================================================================================================\n\n");

    return 0;
}
