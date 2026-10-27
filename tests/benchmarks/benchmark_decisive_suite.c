#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <windows.h>
#include <psapi.h>

#include "../../include/binpack_sdk.h"
#include "../../deps/include/zstd.h"
#include "../../deps/include/lz4.h"
#include "../../deps/timeseries/timeseries_codecs.h"

/* ========================================================================= */
/* Dataset Types and Generators                                              */
/* ========================================================================= */

typedef enum {
    DTYPE_GENERIC_BYTES,
    DTYPE_FLOAT32,
    DTYPE_FLOAT64,
    DTYPE_INT16,
    DTYPE_INT32,
    DTYPE_MIXED_SCADA
} dataset_dtype_t;

typedef struct {
    const char *name;
    const char *group;
    dataset_dtype_t dtype;
    size_t size;
    uint8_t *data;
} dataset_t;

#pragma pack(push, 1)
typedef struct {
    uint64_t timestamp_us;
    float    pressure_psi;
    float    temperature_c;
    int16_t  motor_current_ma;
    uint8_t  valve_state;
    uint8_t  alarm_flag;
} scada_packet_t; /* 20 bytes per frame */
#pragma pack(pop)

static uint8_t* load_raw_file(const char *path, size_t *out_sz) {
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

static dataset_t create_synth_f32_sine(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_FLOAT32;
    d.size = count * sizeof(float);
    d.data = (uint8_t*)malloc(d.size);
    float *p = (float*)d.data;
    for (size_t i = 0; i < count; i++) {
        p[i] = 100.0f * (float)sin((double)i * 0.05);
    }
    return d;
}

static dataset_t create_synth_f32_sine_smooth(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_FLOAT32;
    d.size = count * sizeof(float);
    d.data = (uint8_t*)malloc(d.size);
    float *p = (float*)d.data;
    for (size_t i = 0; i < count; i++) {
        p[i] = 100.0f * (float)sin((double)i * (50.0 * 3.141592653589793 / (double)count));
    }
    return d;
}

static dataset_t create_synth_f32_ramp(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_FLOAT32;
    d.size = count * sizeof(float);
    d.data = (uint8_t*)malloc(d.size);
    float *p = (float*)d.data;
    for (size_t i = 0; i < count; i++) {
        p[i] = (float)i * 0.025f;
    }
    return d;
}

static dataset_t create_synth_f32_sine_noise(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_FLOAT32;
    d.size = count * sizeof(float);
    d.data = (uint8_t*)malloc(d.size);
    float *p = (float*)d.data;
    uint32_t lcg = 1234567;
    for (size_t i = 0; i < count; i++) {
        lcg = lcg * 1664525u + 1013904223u;
        float noise = (((float)(lcg >> 16) / 32768.0f) - 1.0f) * 2.0f;
        p[i] = 100.0f * (float)sin((double)i * 0.05) + noise;
    }
    return d;
}

static dataset_t create_synth_f64_sine(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_FLOAT64;
    d.size = count * sizeof(double);
    d.data = (uint8_t*)malloc(d.size);
    double *p = (double*)d.data;
    for (size_t i = 0; i < count; i++) {
        p[i] = 100.0 * sin((double)i * 0.05);
    }
    return d;
}

static dataset_t create_synth_i16_sine(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_INT16;
    d.size = count * sizeof(int16_t);
    d.data = (uint8_t*)malloc(d.size);
    int16_t *p = (int16_t*)d.data;
    for (size_t i = 0; i < count; i++) {
        p[i] = (int16_t)(16000.0 * sin((double)i * 0.05));
    }
    return d;
}

static dataset_t create_synth_i32_ramp(const char *name, const char *group, size_t count) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_INT32;
    d.size = count * sizeof(int32_t);
    d.data = (uint8_t*)malloc(d.size);
    int32_t *p = (int32_t*)d.data;
    for (size_t i = 0; i < count; i++) {
        p[i] = (int32_t)(i * 7);
    }
    return d;
}

static dataset_t create_synth_scada_stream(const char *name, const char *group, size_t num_packets) {
    dataset_t d;
    d.name = name;
    d.group = group;
    d.dtype = DTYPE_MIXED_SCADA;
    d.size = num_packets * sizeof(scada_packet_t);
    d.data = (uint8_t*)malloc(d.size);
    scada_packet_t *p = (scada_packet_t*)d.data;
    uint64_t t = 1700000000000000ULL;
    for (size_t i = 0; i < num_packets; i++) {
        t += 10000; /* 100 Hz = 10ms */
        p[i].timestamp_us = t;
        p[i].pressure_psi = 120.0f + 5.0f * (float)sin((double)i * 0.01);
        p[i].temperature_c = 65.0f + 2.0f * (float)cos((double)i * 0.005);
        p[i].motor_current_ma = (int16_t)(4500 + 300 * sin((double)i * 0.02));
        p[i].valve_state = (uint8_t)((i / 500) % 2);
        p[i].alarm_flag = (p[i].pressure_psi > 124.5f) ? 1 : 0;
    }
    return d;
}

/* ========================================================================= */
/* Benchmark Runner                                                          */
/* ========================================================================= */

int main() {
    LARGE_INTEGER freq;
    QueryPerformanceFrequency(&freq);

    printf("==================================================================================================================================\n");
    printf("THE DECISIVE BENCHMARK: BINPACK vs ZSTD (L3/L19), LZ4, GORILLA, CHIMP & SPRINTZ\n");
    printf("Hardware: Intel Core i5-13500 (14 Cores / 20 Threads) | Windows 10 Pro 64-bit | Zig Clang AVX2 -O3\n");
    printf("Methodology: Bit-Exact Lossless Verification (memcmp == 0) | Exact Same Machine & Memory Conditions\n");
    printf("Scope: 1. Real Industrial Telemetry | 2. Controlled Numerical Series | 3. Mixed Industrial SCADA Buffers\n");
    printf("==================================================================================================================================\n\n");

    /* Assemble Datasets */
    dataset_t datasets[16];
    int ds_count = 0;

    /* Grupo 1: Telemetria Real */
    size_t tf_sz = 0;
    uint8_t *tf_data = load_raw_file("C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_turbofan_float32.bin", &tf_sz);
    if (tf_data) {
        datasets[ds_count++] = (dataset_t){"NASA_Turbofan_F32", "1. Telemetria Real", DTYPE_FLOAT32, tf_sz, tf_data};
    }

    size_t ims_sz = 0;
    uint8_t *ims_data = load_raw_file("C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv", &ims_sz);
    if (ims_data) {
        datasets[ds_count++] = (dataset_t){"NASA_Bearing_Vib", "1. Telemetria Real", DTYPE_GENERIC_BYTES, ims_sz, ims_data};
    }

    size_t etth_sz = 0;
    uint8_t *etth_data = load_raw_file("C:/Users/sgi.lm/Desktop/Compiler/corpora/nasa_telemetry/etth1_transformer.csv", &etth_sz);
    if (etth_data) {
        datasets[ds_count++] = (dataset_t){"ETTh1_Transformer", "1. Telemetria Real", DTYPE_GENERIC_BYTES, etth_sz, etth_data};
    }

    size_t sens_sz = 0;
    uint8_t *sens_data = load_raw_file("C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", &sens_sz);
    if (sens_data) {
        datasets[ds_count++] = (dataset_t){"Sensor_Matrix_16b", "1. Telemetria Real", DTYPE_INT16, sens_sz, sens_data};
    }

    /* Grupo 2: Series Numericas Controladas */
    datasets[ds_count++] = create_synth_f32_ramp("F32_Ramp_1MB",                "2. Series Numericas", 256 * 1024);
    datasets[ds_count++] = create_synth_f32_sine_smooth("F32_Sine_Smooth_1MB",  "2. Series Numericas", 256 * 1024);
    datasets[ds_count++] = create_synth_f32_sine("F32_Sine_Wave_1MB",           "2. Series Numericas", 256 * 1024);
    datasets[ds_count++] = create_synth_f32_sine_noise("F32_Sine_Noise",        "2. Series Numericas", 256 * 1024);
    datasets[ds_count++] = create_synth_f64_sine("F64_Sine_Wave_1MB",           "2. Series Numericas", 128 * 1024);
    datasets[ds_count++] = create_synth_i16_sine("Int16_Sine_1MB",              "2. Series Numericas", 512 * 1024);
    datasets[ds_count++] = create_synth_i32_ramp("Int32_Ramp_1MB",              "2. Series Numericas", 256 * 1024);

    /* Grupo 3: Buffers Industriales Mixtos */
    datasets[ds_count++] = create_synth_scada_stream("SCADA_PLC_Frame_1MB", "3. Mixtos SCADA", 50000); /* 1 MB */

    size_t sao_sz = 0;
    uint8_t *sao_data = load_raw_file("C:/Users/sgi.lm/Desktop/Compiler/corpora/silesia/sao", &sao_sz);
    if (sao_data) {
        datasets[ds_count++] = (dataset_t){"Astrometry_Sao_Bin", "3. Mixtos SCADA", DTYPE_GENERIC_BYTES, sao_sz, sao_data};
    }

    size_t xls_sz = 0;
    uint8_t *xls_data = load_raw_file("C:/Users/sgi.lm/Desktop/Compiler/corpora/canterbury/kennedy.xls", &xls_sz);
    if (xls_data) {
        datasets[ds_count++] = (dataset_t){"Kennedy_Xls_OLE2", "3. Mixtos SCADA", DTYPE_GENERIC_BYTES, xls_sz, xls_data};
    }

    /* Working Buffers */
    size_t max_buf_sz = 16 * 1024 * 1024;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz * 2);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz * 2);

    /* Compression Contexts */
    ZSTD_CCtx *z_cctx3 = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(z_cctx3, ZSTD_c_compressionLevel, 3);
    ZSTD_CCtx *z_cctx19 = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(z_cctx19, ZSTD_c_compressionLevel, 19);
    ZSTD_DCtx *z_dctx = ZSTD_createDCtx();

    binpack_options_t bp_rt_opt;
    binpack_default_options(&bp_rt_opt);
    bp_rt_opt.clevel = 3;
    bp_rt_opt.mode = BINPACK_MODE_RUNTIME;
    bp_rt_opt.nb_workers = 1;
    bp_rt_opt.enable_micro_trial = 0;

    binpack_options_t bp_def_opt;
    binpack_default_options(&bp_def_opt);
    bp_def_opt.clevel = 19;
    bp_def_opt.mode = BINPACK_MODE_DEFAULT;
    bp_def_opt.nb_workers = 1;

    for (int d = 0; d < ds_count; d++) {
        dataset_t *cur = &datasets[d];
        size_t sz = cur->size;
        uint8_t *src = cur->data;

        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("DATASET: %-22s | Grupo: %-20s | Tamano: %6.2f MB (%zu bytes)\n",
               cur->name, cur->group, sz / 1048576.0, sz);
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("%-18s | %-10s | %-14s | %-14s | %-14s | %-14s | %-10s\n",
               "Motor Evaluado", "Ratio (%)", "Comp (MB/s)", "Decomp (MB/s)", "Latencia 4KB", "Latencia 64KB", "Integridad");
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");

        LARGE_INTEGER t0, t1;
        int iters = (sz <= 2 * 1024 * 1024) ? 15 : 6;
        size_t block_4k = sz > 4096 ? 4096 : sz;
        size_t block_64k = sz > 65536 ? 65536 : sz;

        /* ========================================================================= */
        /* 1. LZ4                                                                    */
        /* ========================================================================= */
        {
            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                c_sz = LZ4_compress_default((const char*)src, (char*)comp_buf, (int)sz, (int)max_buf_sz);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)sz);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            /* Latencia 4KB & 64KB */
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 3000; i++) {
                LZ4_compress_default((const char*)src, (char*)comp_buf, (int)block_4k, 8192);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 3000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 1000; i++) {
                LZ4_compress_default((const char*)src, (char*)comp_buf, (int)block_64k, 131072);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 1000.0);

            int ok = (memcmp(decomp_buf, src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "LZ4 (Fast)", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        }

        /* ========================================================================= */
        /* 2. Zstandard L3                                                           */
        /* ========================================================================= */
        {
            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                ZSTD_CCtx_reset(z_cctx3, ZSTD_reset_session_only);
                c_sz = ZSTD_compressCCtx(z_cctx3, comp_buf, max_buf_sz, src, sz, 3);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                ZSTD_decompressDCtx(z_dctx, decomp_buf, sz, comp_buf, c_sz);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            /* Latencia 4KB & 64KB */
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 2000; i++) {
                ZSTD_CCtx_reset(z_cctx3, ZSTD_reset_session_only);
                ZSTD_compressCCtx(z_cctx3, comp_buf, 8192, src, block_4k, 3);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 2000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 500; i++) {
                ZSTD_CCtx_reset(z_cctx3, ZSTD_reset_session_only);
                ZSTD_compressCCtx(z_cctx3, comp_buf, 131072, src, block_64k, 3);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 500.0);

            int ok = (memcmp(decomp_buf, src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "Meta Zstd L3", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        }

        /* ========================================================================= */
        /* 3. Zstandard L19                                                          */
        /* ========================================================================= */
        {
            size_t c_sz = 0;
            int l19_iters = iters > 4 ? 4 : iters;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < l19_iters; i++) {
                ZSTD_CCtx_reset(z_cctx19, ZSTD_reset_session_only);
                c_sz = ZSTD_compressCCtx(z_cctx19, comp_buf, max_buf_sz, src, sz, 19);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * l19_iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < l19_iters; i++) {
                ZSTD_decompressDCtx(z_dctx, decomp_buf, sz, comp_buf, c_sz);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * l19_iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            /* Latencia 4KB & 64KB */
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 200; i++) {
                ZSTD_CCtx_reset(z_cctx19, ZSTD_reset_session_only);
                ZSTD_compressCCtx(z_cctx19, comp_buf, 8192, src, block_4k, 19);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 200.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 50; i++) {
                ZSTD_CCtx_reset(z_cctx19, ZSTD_reset_session_only);
                ZSTD_compressCCtx(z_cctx19, comp_buf, 131072, src, block_64k, 19);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 50.0);

            int ok = (memcmp(decomp_buf, src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "Meta Zstd L19", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        }

        /* ========================================================================= */
        /* 4. Facebook Gorilla (Float32 & Float64)                                   */
        /* ========================================================================= */
        if (cur->dtype == DTYPE_FLOAT32) {
            size_t count = sz / 4;
            const float *f_src = (const float*)src;
            float *f_dec = (float*)decomp_buf;

            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                c_sz = gorilla_compress_f32(f_src, count, comp_buf, max_buf_sz);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                gorilla_decompress_f32(comp_buf, c_sz, f_dec, count);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            /* Latencia 4KB & 64KB */
            size_t block_elem4k = count > 1024 ? 1024 : count;
            size_t block_elem64k = count > 16384 ? 16384 : count;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 2000; i++) {
                gorilla_compress_f32(f_src, block_elem4k, comp_buf, 8192);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 2000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 500; i++) {
                gorilla_compress_f32(f_src, block_elem64k, comp_buf, 131072);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 500.0);

            int ok = (memcmp(f_dec, f_src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "Gorilla (FB '15)", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        } else if (cur->dtype == DTYPE_FLOAT64) {
            size_t count = sz / 8;
            const double *d_src = (const double*)src;
            double *d_dec = (double*)decomp_buf;

            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                c_sz = gorilla_compress_f64(d_src, count, comp_buf, max_buf_sz);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                gorilla_decompress_f64(comp_buf, c_sz, d_dec, count);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            size_t block_elem4k = count > 512 ? 512 : count;
            size_t block_elem64k = count > 8192 ? 8192 : count;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 2000; i++) {
                gorilla_compress_f64(d_src, block_elem4k, comp_buf, 8192);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 2000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 500; i++) {
                gorilla_compress_f64(d_src, block_elem64k, comp_buf, 131072);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 500.0);

            int ok = (memcmp(d_dec, d_src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "Gorilla (FB '15)", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        } else {
            printf("%-18s | %-10s | %-14s | %-14s | %-14s | %-14s | N/A (Solo Floats)\n",
                   "Gorilla (FB '15)", "N/A", "-", "-", "-", "-");
        }

        /* ========================================================================= */
        /* 5. Chimp (VLDB 2022 Floating Point Codec)                                */
        /* ========================================================================= */
        if (cur->dtype == DTYPE_FLOAT32) {
            size_t count = sz / 4;
            const float *f_src = (const float*)src;
            float *f_dec = (float*)decomp_buf;

            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                c_sz = chimp_compress_f32(f_src, count, comp_buf, max_buf_sz);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                chimp_decompress_f32(comp_buf, c_sz, f_dec, count);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            size_t block_elem4k = count > 1024 ? 1024 : count;
            size_t block_elem64k = count > 16384 ? 16384 : count;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 2000; i++) {
                chimp_compress_f32(f_src, block_elem4k, comp_buf, 8192);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 2000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 500; i++) {
                chimp_compress_f32(f_src, block_elem64k, comp_buf, 131072);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 500.0);

            int ok = (memcmp(f_dec, f_src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "Chimp (VLDB '22)", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        } else {
            printf("%-18s | %-10s | %-14s | %-14s | %-14s | %-14s | N/A (Solo Floats)\n",
                   "Chimp (VLDB '22)", "N/A", "-", "-", "-", "-");
        }

        /* ========================================================================= */
        /* 6. Sprintz (VLDB 2018 IoT Integer Codec)                                  */
        /* ========================================================================= */
        if (cur->dtype == DTYPE_INT16) {
            size_t count = sz / 2;
            const int16_t *i_src = (const int16_t*)src;
            int16_t *i_dec = (int16_t*)decomp_buf;

            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                c_sz = sprintz_compress_i16(i_src, count, comp_buf, max_buf_sz);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                sprintz_decompress_i16(comp_buf, c_sz, i_dec, count);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            size_t block_elem4k = count > 2048 ? 2048 : count;
            size_t block_elem64k = count > 32768 ? 32768 : count;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 2000; i++) {
                sprintz_compress_i16(i_src, block_elem4k, comp_buf, 8192);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 2000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 500; i++) {
                sprintz_compress_i16(i_src, block_elem64k, comp_buf, 131072);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 500.0);

            int ok = (memcmp(i_dec, i_src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "Sprintz (IoT '18)", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        } else {
            printf("%-18s | %-10s | %-14s | %-14s | %-14s | %-14s | N/A (Solo Enteros)\n",
                   "Sprintz (IoT '18)", "N/A", "-", "-", "-", "-");
        }

        /* ========================================================================= */
        /* 7. BinPack RT (Fast-Path level 3, 1 thread)                               */
        /* ========================================================================= */
        {
            size_t c_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                binpack_compress_into(src, sz, comp_buf, max_buf_sz, &c_sz, &bp_rt_opt);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            size_t dec_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < iters; i++) {
                binpack_decompress_into(comp_buf, c_sz, decomp_buf, sz + 4096, &dec_sz, 0);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            /* Latencia 4KB & 64KB */
            size_t lat_csz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 2000; i++) {
                binpack_compress_into(src, block_4k, comp_buf, 8192, &lat_csz, &bp_rt_opt);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 2000.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 500; i++) {
                binpack_compress_into(src, block_64k, comp_buf, 131072, &lat_csz, &bp_rt_opt);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 500.0);

            int ok = (dec_sz == sz && memcmp(decomp_buf, src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "BINPACK RT", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        }

        /* ========================================================================= */
        /* 8. BinPack Default (Balanced level 19, 1 thread)                          */
        /* ========================================================================= */
        {
            size_t c_sz = 0;
            int def_iters = iters > 4 ? 4 : iters;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < def_iters; i++) {
                binpack_compress_into(src, sz, comp_buf, max_buf_sz, &c_sz, &bp_def_opt);
            }
            QueryPerformanceCounter(&t1);
            double c_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * def_iters);
            double c_mbs = (sz / 1048576.0) / c_sec;

            size_t dec_sz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < def_iters; i++) {
                binpack_decompress_into(comp_buf, c_sz, decomp_buf, sz + 4096, &dec_sz, 0);
            }
            QueryPerformanceCounter(&t1);
            double d_sec = (double)(t1.QuadPart - t0.QuadPart) / ((double)freq.QuadPart * def_iters);
            double d_mbs = (sz / 1048576.0) / d_sec;

            /* Latencia 4KB & 64KB */
            size_t lat_csz = 0;
            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 300; i++) {
                binpack_compress_into(src, block_4k, comp_buf, 8192, &lat_csz, &bp_def_opt);
            }
            QueryPerformanceCounter(&t1);
            double lat_4k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 300.0);

            QueryPerformanceCounter(&t0);
            for (int i = 0; i < 50; i++) {
                binpack_compress_into(src, block_64k, comp_buf, 131072, &lat_csz, &bp_def_opt);
            }
            QueryPerformanceCounter(&t1);
            double lat_64k_us = ((double)(t1.QuadPart - t0.QuadPart) * 1000000.0) / ((double)freq.QuadPart * 50.0);

            int ok = (dec_sz == sz && memcmp(decomp_buf, src, sz) == 0);
            printf("%-18s | %8.2f%%  | %10.1f M   | %10.1f M   | %10.2f us  | %10.2f us  | %s\n",
                   "BINPACK Default", (double)c_sz / sz * 100.0, c_mbs, d_mbs, lat_4k_us, lat_64k_us, ok ? "BIT-EXACT" : "FALLO");
        }
    }

    printf("==================================================================================================================================\n\n");

    ZSTD_freeCCtx(z_cctx3);
    ZSTD_freeCCtx(z_cctx19);
    ZSTD_freeDCtx(z_dctx);
    free(comp_buf);
    free(decomp_buf);

    return 0;
}
