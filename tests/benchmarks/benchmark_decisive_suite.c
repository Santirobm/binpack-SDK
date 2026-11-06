#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <errno.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cpuid.h>
#endif

#include "../../include/binpack_sdk.h"
#include "../../deps/include/zstd.h"
#include "../../deps/include/lz4.h"
#include "../../deps/lzma/LzmaLib.h"
#include "../../deps/lzma/Bra.h"
#include "../../deps/timeseries/timeseries_codecs.h"

/* ========================================================================= */
/* Topologia de Hardware & Afinidad Determinista de CPU                      */
/* ========================================================================= */

typedef struct {
    int logical_id;
    int core_id;
    int is_pcore;
    char description[128];
} cpu_affinity_info_t;

static int detect_and_set_pcore_affinity(cpu_affinity_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->logical_id = 2; /* Target Core 1 / Logical Core 2 */
    info->core_id = 1;
    info->is_pcore = 1;

#if defined(__linux__)
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", info->logical_id);
    FILE *f = fopen(path, "r");
    if (f) {
        if (fscanf(f, "%d", &info->core_id) != 1) {
            info->core_id = 1;
        }
        fclose(f);
    }

    unsigned int eax = 0, ebx = 0, ecx = 0, edx = 0;
    __cpuid_count(0x1a, 0, eax, ebx, ecx, edx);
    unsigned int core_type = (eax >> 24) & 0xff;
    if (core_type == 0x40) {
        info->is_pcore = 1;
        snprintf(info->description, sizeof(info->description),
                 "Intel P-Core (Raptor Cove, Native CPUID Core Type 0x40)");
    } else if (core_type == 0x20) {
        info->is_pcore = 0;
        snprintf(info->description, sizeof(info->description),
                 "Intel E-Core (Gracemont, Native CPUID Core Type 0x20)");
    } else {
        if (info->logical_id < 12) {
            info->is_pcore = 1;
            snprintf(info->description, sizeof(info->description),
                     "Intel Core i5-13500 Performance Core (SMT Core %d, Logical %d)",
                     info->core_id, info->logical_id);
        } else {
            info->is_pcore = 0;
            snprintf(info->description, sizeof(info->description),
                     "Intel Core i5-13500 Efficient Core (Logical %d)", info->logical_id);
        }
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(info->logical_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        fprintf(stderr, "[FATAL] sched_setaffinity failed on logical CPU %d: %s\n",
                info->logical_id, strerror(errno));
        return -1;
    }

    cpu_set_t verify_set;
    CPU_ZERO(&verify_set);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &verify_set) != 0) {
        fprintf(stderr, "[FATAL] sched_getaffinity failed: %s\n", strerror(errno));
        return -1;
    }
    if (!CPU_ISSET(info->logical_id, &verify_set)) {
        fprintf(stderr, "[FATAL] Affinity verification failed: CPU %d not set\n", info->logical_id);
        return -1;
    }
    return 0;
#elif defined(_WIN32)
    DWORD_PTR mask = ((DWORD_PTR)1 << info->logical_id);
    if (!SetProcessAffinityMask(GetCurrentProcess(), mask)) {
        fprintf(stderr, "[FATAL] SetProcessAffinityMask failed on CPU %d: %lu\n",
                info->logical_id, GetLastError());
        return -1;
    }
    snprintf(info->description, sizeof(info->description),
             "Win32 Performance Core (Logical %d, Mask 0x%llX)",
             info->logical_id, (unsigned long long)mask);
    return 0;
#endif
}

/* ========================================================================= */
/* Temporizacion de Alta Precision Monotonica con Verificacion de Error      */
/* ========================================================================= */

static inline uint64_t timer_now_ns(void) {
#if defined(__linux__)
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
        fprintf(stderr, "[FATAL] clock_gettime(CLOCK_MONOTONIC_RAW) failed: %s\n", strerror(errno));
        abort();
    }
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
#elif defined(_WIN32)
    static LARGE_INTEGER freq;
    static int freq_init = 0;
    if (!freq_init) {
        if (!QueryPerformanceFrequency(&freq)) {
            fprintf(stderr, "[FATAL] QueryPerformanceFrequency failed\n");
            abort();
        }
        freq_init = 1;
    }
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter)) {
        fprintf(stderr, "[FATAL] QueryPerformanceCounter failed\n");
        abort();
    }
    return (uint64_t)((counter.QuadPart * 1000000000ULL) / freq.QuadPart);
#endif
}

/* ========================================================================= */
/* Modulo Estadistico Riguroso (Muestras Raw + Regla de Tukey Aplicada)       */
/* ========================================================================= */

#define BENCH_ITERS 20

typedef struct {
    int n_samples;
    double raw_samples[BENCH_ITERS];
    double sorted_samples[BENCH_ITERS];

    /* Raw Statistics */
    double raw_min;
    double raw_max;
    double raw_mean;
    double raw_median;
    double raw_q1;
    double raw_q3;
    double raw_iqr;
    double lower_fence;
    double upper_fence;
    int outliers_count;

    /* Filtered Statistics (Tukey's Rule Applied) */
    int n_filtered;
    double filtered_samples[BENCH_ITERS];
    double filtered_mean;
    double filtered_median;
    double filtered_stddev;
} stat_summary_t;

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static void calc_stats(const double *samples, int n, stat_summary_t *out) {
    memset(out, 0, sizeof(*out));
    out->n_samples = n;
    double sum_raw = 0.0;
    for (int i = 0; i < n; i++) {
        out->raw_samples[i] = samples[i];
        out->sorted_samples[i] = samples[i];
        sum_raw += samples[i];
    }
    qsort(out->sorted_samples, n, sizeof(double), compare_doubles);

    out->raw_min = out->sorted_samples[0];
    out->raw_max = out->sorted_samples[n - 1];
    out->raw_mean = sum_raw / (double)n;

    if (n == 20) {
        out->raw_median = (out->sorted_samples[9] + out->sorted_samples[10]) / 2.0;
        out->raw_q1     = (out->sorted_samples[4] + out->sorted_samples[5]) / 2.0;
        out->raw_q3     = (out->sorted_samples[14] + out->sorted_samples[15]) / 2.0;
    } else {
        out->raw_median = out->sorted_samples[n / 2];
        out->raw_q1     = out->sorted_samples[n / 4];
        out->raw_q3     = out->sorted_samples[(n * 3) / 4];
    }
    out->raw_iqr = out->raw_q3 - out->raw_q1;
    out->lower_fence = out->raw_q1 - 1.5 * out->raw_iqr;
    out->upper_fence = out->raw_q3 + 1.5 * out->raw_iqr;

    /* Apply Tukey's Rule: keep only values inside [lower_fence, upper_fence] */
    out->n_filtered = 0;
    out->outliers_count = 0;
    double sum_filt = 0.0;
    for (int i = 0; i < n; i++) {
        double val = out->raw_samples[i];
        if (val >= out->lower_fence && val <= out->upper_fence) {
            out->filtered_samples[out->n_filtered++] = val;
            sum_filt += val;
        } else {
            out->outliers_count++;
        }
    }

    if (out->n_filtered > 0) {
        out->filtered_mean = sum_filt / (double)out->n_filtered;
        double sorted_filt[BENCH_ITERS];
        memcpy(sorted_filt, out->filtered_samples, out->n_filtered * sizeof(double));
        qsort(sorted_filt, out->n_filtered, sizeof(double), compare_doubles);
        if (out->n_filtered % 2 == 0) {
            out->filtered_median = (sorted_filt[out->n_filtered / 2 - 1] + sorted_filt[out->n_filtered / 2]) / 2.0;
        } else {
            out->filtered_median = sorted_filt[out->n_filtered / 2];
        }
        double sum_sq = 0.0;
        for (int i = 0; i < out->n_filtered; i++) {
            double d = out->filtered_samples[i] - out->filtered_mean;
            sum_sq += d * d;
        }
        out->filtered_stddev = sqrt(sum_sq / (double)out->n_filtered);
    } else {
        out->filtered_mean = out->raw_mean;
        out->filtered_median = out->raw_median;
        out->filtered_stddev = 0.0;
    }
}

/* ========================================================================= */
/* Generador Pseudoaleatorio & Aleatorizacion de Fisher-Yates                 */
/* ========================================================================= */

typedef struct {
    uint32_t state;
} rng_t;

static inline void rng_init(rng_t *rng, uint32_t seed) {
    rng->state = seed ? seed : 20261106u;
}

static inline uint32_t rng_next(rng_t *rng) {
    rng->state = rng->state * 1664525u + 1013904223u;
    return rng->state;
}

static inline int rng_next_bounded(rng_t *rng, int bound) {
    return (int)(rng_next(rng) % (uint32_t)bound);
}

/* ========================================================================= */
/* Modelo de Resultados IPC (Memoria Aislada por Subproceso)                 */
/* ========================================================================= */

typedef struct {
    size_t orig_sz;
    size_t cmp_sz;
    double ratio_pct;
    int integrity_all_pass;

    stat_summary_t c_stat;
    stat_summary_t d_stat;

    double lat_4k_comp_us;
    double lat_4k_decomp_us;
    double lat_64k_comp_us;
    double lat_64k_decomp_us;

    size_t peak_rss_kb;
} bench_result_t;

/* ========================================================================= */
/* Prototipo de Evaluador de Motor                                           */
/* ========================================================================= */

typedef int (*engine_run_fn)(const uint8_t *src, size_t src_sz, bench_result_t *res);

typedef struct {
    const char *tier;
    const char *engine_id;
    const char *display_name;
    const char *contract_desc;
    engine_run_fn run_fn;
} engine_config_t;

/* ========================================================================= */
/* Cargador de Archivos Crudos                                               */
/* ========================================================================= */

static uint8_t* load_raw_file(const char *path, size_t *out_sz) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    if (sz <= 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    uint8_t *buf = (uint8_t*)malloc((size_t)sz);
    if (!buf) { fclose(f); return NULL; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) {
        free(buf);
        return NULL;
    }
    *out_sz = (size_t)sz;
    return buf;
}

/* ========================================================================= */
/* Pre-flight Adversarial para Gorilla y Chimp                               */
/* ========================================================================= */

static int pre_flight_validate_float_codecs(void) {
    printf("[PRE-FLIGHT] Validating Gorilla and Chimp on Adversarial Test Vectors...\n");
    const int N_TEST = 1024;
    float *test_in = (float*)malloc(N_TEST * sizeof(float));
    float *test_out = (float*)malloc(N_TEST * sizeof(float));
    uint8_t *cmp_buf = (uint8_t*)malloc(N_TEST * sizeof(float) * 2);

    if (!test_in || !test_out || !cmp_buf) {
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }

    for (int i = 0; i < N_TEST; i++) {
        if (i < 128) test_in[i] = 0.0f;
        else if (i < 256) test_in[i] = (i % 2 == 0) ? -0.0f : 0.0f;
        else if (i < 384) test_in[i] = 1.0f + (float)i * 0.0001f;
        else if (i < 512) test_in[i] = (float)sin((double)i * 0.05);
        else if (i < 640) test_in[i] = 1e-38f * (float)(i - 512);
        else if (i < 768) test_in[i] = 1e30f;
        else test_in[i] = (float)(i * 7);
    }

    size_t g_sz = gorilla_compress_f32(test_in, N_TEST, cmp_buf, N_TEST * 8);
    if (g_sz == 0) {
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    memset(test_out, 0, N_TEST * sizeof(float));
    gorilla_decompress_f32(cmp_buf, g_sz, test_out, N_TEST);
    if (memcmp(test_in, test_out, N_TEST * sizeof(float)) != 0) {
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    printf("  [PASS] Gorilla Float32: 100%% Bit-Exact on adversarial patterns\n");

    size_t c_sz = chimp_compress_f32(test_in, N_TEST, cmp_buf, N_TEST * 8);
    if (c_sz == 0) {
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    memset(test_out, 0, N_TEST * sizeof(float));
    chimp_decompress_f32(cmp_buf, c_sz, test_out, N_TEST);
    if (memcmp(test_in, test_out, N_TEST * sizeof(float)) != 0) {
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    printf("  [PASS] Chimp Float32:   100%% Bit-Exact on adversarial patterns\n");

    free(test_in); free(test_out); free(cmp_buf);
    return 1;
}

/* ========================================================================= */
/* Medicion de Latencias de Bloques (4 KiB y 64 KiB)                         */
/* ========================================================================= */

static void measure_latencies_generic(
    const uint8_t *src, size_t src_sz,
    size_t block_sz,
    int is_zstd, int zstd_level,
    int is_binpack, const binpack_options_t *bp_opt,
    double *out_comp_us, double *out_decomp_us
) {
    if (src_sz < block_sz) {
        *out_comp_us = 0.0;
        *out_decomp_us = 0.0;
        return;
    }

    int n_blocks = (int)(src_sz / block_sz);
    if (n_blocks > 32) n_blocks = 32; /* Limit to 32 representative blocks */
    if (n_blocks <= 0) return;

    uint8_t *cbuf = (uint8_t*)malloc(block_sz * 2 + 65536);
    uint8_t *dbuf = (uint8_t*)malloc(block_sz * 2 + 65536);
    double c_times[32], d_times[32];

    ZSTD_CCtx *zcctx = NULL;
    ZSTD_DCtx *zdctx = NULL;
    if (is_zstd) {
        zcctx = ZSTD_createCCtx();
        zdctx = ZSTD_createDCtx();
    }

    for (int b = 0; b < n_blocks; b++) {
        const uint8_t *blk = src + (b * block_sz);
        size_t c_sz = 0;

        uint64_t t0 = timer_now_ns();
        if (is_zstd) {
            ZSTD_CCtx_reset(zcctx, ZSTD_reset_session_only);
            c_sz = ZSTD_compressCCtx(zcctx, cbuf, block_sz * 2, blk, block_sz, zstd_level);
        } else if (is_binpack) {
            binpack_compress_into(blk, block_sz, cbuf, block_sz * 2, &c_sz, bp_opt);
        } else {
            /* LZ4 */
            c_sz = (size_t)LZ4_compress_default((const char*)blk, (char*)cbuf, (int)block_sz, (int)(block_sz * 2));
        }
        uint64_t t1 = timer_now_ns();
        c_times[b] = (double)(t1 - t0) / 1000.0; /* microseconds */

        size_t dec_sz = 0;
        uint64_t t2 = timer_now_ns();
        if (is_zstd) {
            ZSTD_decompressDCtx(zdctx, dbuf, block_sz, cbuf, c_sz);
        } else if (is_binpack) {
            binpack_decompress_into(cbuf, c_sz, dbuf, block_sz, &dec_sz, 0);
        } else {
            LZ4_decompress_safe((const char*)cbuf, (char*)dbuf, (int)c_sz, (int)block_sz);
        }
        uint64_t t3 = timer_now_ns();
        d_times[b] = (double)(t3 - t2) / 1000.0; /* microseconds */
    }

    qsort(c_times, n_blocks, sizeof(double), compare_doubles);
    qsort(d_times, n_blocks, sizeof(double), compare_doubles);

    *out_comp_us = c_times[n_blocks / 2];
    *out_decomp_us = d_times[n_blocks / 2];

    if (zcctx) ZSTD_freeCCtx(zcctx);
    if (zdctx) ZSTD_freeDCtx(zdctx);
    free(cbuf); free(dbuf);
}

/* ========================================================================= */
/* Implementaciones de Evaluadores por Motor                                 */
/* ========================================================================= */

/* 1. LZ4 Fast (Raw Block API, preallocated buffers) */
static int run_lz4_fast(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    /* Uniform Warm-up: 1 un-timed round */
    c_sz = (size_t)LZ4_compress_default((const char*)src, (char*)comp_buf, (int)src_sz, (int)max_buf_sz);
    LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)src_sz);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    /* 20 Timed Compression Iterations */
    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        c_sz = (size_t)LZ4_compress_default((const char*)src, (char*)comp_buf, (int)src_sz, (int)max_buf_sz);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    /* 20 Timed Decompression Iterations with per-iteration memcmp verification */
    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)src_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    measure_latencies_generic(src, src_sz, 4096, 0, 0, 0, NULL, &res->lat_4k_comp_us, &res->lat_4k_decomp_us);
    measure_latencies_generic(src, src_sz, 65536, 0, 0, 0, NULL, &res->lat_64k_comp_us, &res->lat_64k_decomp_us);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 2. Zstandard Level 1 (Framed Container, preallocated buffers) */
static int run_zstd_l1(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    ZSTD_DCtx *dctx = ZSTD_createDCtx();

    /* Uniform Warm-up */
    ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 1);
    ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
        uint64_t t0 = timer_now_ns();
        c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 1);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    measure_latencies_generic(src, src_sz, 4096, 1, 1, 0, NULL, &res->lat_4k_comp_us, &res->lat_4k_decomp_us);
    measure_latencies_generic(src, src_sz, 65536, 1, 1, 0, NULL, &res->lat_64k_comp_us, &res->lat_64k_decomp_us);

    ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 3. BinPack Fast Zero-CRC (Algorithmic Core, preallocated buffer) */
static int run_binpack_fast_zerocrc(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    /* Uniform Warm-up */
    binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    size_t dec_sz = 0;
    binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 0);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 0 /* Zero-CRC */);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    measure_latencies_generic(src, src_sz, 4096, 0, 0, 1, &opt, &res->lat_4k_comp_us, &res->lat_4k_decomp_us);
    measure_latencies_generic(src, src_sz, 65536, 0, 0, 1, &opt, &res->lat_64k_comp_us, &res->lat_64k_decomp_us);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 4. BinPack Strict CRC32 (Decompression Verification Path) */
static int run_binpack_strict_crc32(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    /* Produce payload once */
    binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);

    /* Warm-up */
    size_t dec_sz = 0;
    binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    /* Measure decompression under CRC32 verification */
    for (int i = 0; i < BENCH_ITERS; i++) {
        c_samples[i] = 0.001; /* Sentinel */
        uint64_t t0 = timer_now_ns();
        binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1 /* Strict CRC32 */);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 5. BinPack Convenience API (Measures allocation overhead of binpack_compress_buffer) */
static int run_binpack_convenience_api(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    void *out_dst = NULL;
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    /* Warm-up */
    binpack_compress_buffer(src, src_sz, &out_dst, &c_sz, &opt);
    if (out_dst) {
        size_t dec_sz = 0;
        binpack_decompress_into(out_dst, c_sz, decomp_buf, src_sz, &dec_sz, 0);
        if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;
        free(out_dst); out_dst = NULL;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_compress_buffer(src, src_sz, &out_dst, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (i < BENCH_ITERS - 1 && out_dst) { free(out_dst); out_dst = NULL; }
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        size_t dec_sz = 0;
        uint64_t t0 = timer_now_ns();
        binpack_decompress_into(out_dst, c_sz, decomp_buf, src_sz, &dec_sz, 0);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    if (out_dst) free(out_dst);
    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 6. Zstandard Level 19 (Tier 2 Balanced Density) */
static int run_zstd_l19(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 19);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 25);
    ZSTD_DCtx *dctx = ZSTD_createDCtx();

    /* Warm-up */
    ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 19);
    ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
        uint64_t t0 = timer_now_ns();
        c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 19);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 7. BinPack Default Level 19 (Tier 2 Balanced Density) */
static int run_binpack_default_l19(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_DEFAULT;
    opt.clevel = 19;

    /* Warm-up */
    binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    size_t dec_sz = 0;
    binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 8. Tier 3: Ruta A — 7-Zip LZMA1 Puro */
static int run_tier3_ruta_a(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    size_t props_sz = LZMA_PROPS_SIZE;
    uint8_t props[LZMA_PROPS_SIZE];

    /* Warm-up */
    size_t dst_len = max_buf_sz - LZMA_PROPS_SIZE;
    LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, src, src_sz,
                 props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
    c_sz = dst_len + LZMA_PROPS_SIZE;
    size_t uncmp_len = src_sz;
    size_t src_processed = dst_len;
    LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                   props, props_sz);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        dst_len = max_buf_sz - LZMA_PROPS_SIZE;
        props_sz = LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, src, src_sz,
                     props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        c_sz = dst_len + LZMA_PROPS_SIZE;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uncmp_len = src_sz;
        src_processed = c_sz - LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                       props, props_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 9. Tier 3: Ruta B — Pavlov BCJ ARM Filter + 7-Zip LZMA1 */
static int run_tier3_ruta_b(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *bcj_work = (uint8_t*)malloc(max_buf_sz);
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    size_t props_sz = LZMA_PROPS_SIZE;
    uint8_t props[LZMA_PROPS_SIZE];

    /* Warm-up */
    memcpy(bcj_work, src, src_sz);
    ARM_Convert(bcj_work, src_sz, 0, 1 /* forward encoding */);
    size_t dst_len = max_buf_sz - LZMA_PROPS_SIZE;
    LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, bcj_work, src_sz,
                 props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
    c_sz = dst_len + LZMA_PROPS_SIZE;
    size_t uncmp_len = src_sz;
    size_t src_processed = dst_len;
    LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                   props, props_sz);
    ARM_Convert(decomp_buf, src_sz, 0, 0 /* inverse decoding */);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        memcpy(bcj_work, src, src_sz);
        dst_len = max_buf_sz - LZMA_PROPS_SIZE;
        props_sz = LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        ARM_Convert(bcj_work, src_sz, 0, 1);
        LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, bcj_work, src_sz,
                     props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        c_sz = dst_len + LZMA_PROPS_SIZE;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uncmp_len = src_sz;
        src_processed = c_sz - LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                       props, props_sz);
        ARM_Convert(decomp_buf, src_sz, 0, 0);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(bcj_work); free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 10. Tier 3: Ruta C — BinPack Archival Core (BINPACK_MODE_ARCHIVAL) */
static int run_tier3_ruta_c(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_ARCHIVAL;
    opt.clevel = 19;

    /* Warm-up */
    binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    size_t dec_sz = 0;
    binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 11. Tier 4A: Gorilla Float32 */
static int run_gorilla_f32(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t n_floats = src_sz / sizeof(float);
    const float *src_f = (const float*)src;
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    float *dec_f = (float*)malloc(n_floats * sizeof(float));
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    /* Warm-up */
    c_sz = gorilla_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
    gorilla_decompress_f32(comp_buf, c_sz, dec_f, n_floats);
    if (memcmp(dec_f, src_f, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        c_sz = gorilla_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        gorilla_decompress_f32(comp_buf, c_sz, dec_f, n_floats);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(dec_f, src_f, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(dec_f);
    return all_pass ? 0 : -1;
}

/* 12. Tier 4A: Chimp Float32 */
static int run_chimp_f32(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t n_floats = src_sz / sizeof(float);
    const float *src_f = (const float*)src;
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    float *dec_f = (float*)malloc(n_floats * sizeof(float));
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    /* Warm-up */
    c_sz = chimp_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
    chimp_decompress_f32(comp_buf, c_sz, dec_f, n_floats);
    if (memcmp(dec_f, src_f, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        c_sz = chimp_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        chimp_decompress_f32(comp_buf, c_sz, dec_f, n_floats);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(dec_f, src_f, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(dec_f);
    return all_pass ? 0 : -1;
}

/* 13. Tier 4A: BinPack Transpose-F32 */
static int run_binpack_transpose_f32(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.mode = BINPACK_MODE_DEFAULT;
    opt.filter = BINPACK_FILTER_TRANSPOSE_DELTA;
    opt.transpose_stride = 4;
    opt.clevel = 19;

    /* Warm-up */
    binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    size_t dec_sz = 0;
    binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* ========================================================================= */
/* Ejecucion Aislada de Motor por Subproceso (fork & wait4)                  */
/* ========================================================================= */

static int run_engine_isolated(
    const engine_config_t *cfg,
    const uint8_t *src, size_t src_sz,
    const cpu_affinity_info_t *aff,
    bench_result_t *out_res
) {
#if defined(__linux__)
    bench_result_t *shm = (bench_result_t*)mmap(NULL, sizeof(bench_result_t),
        PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    if (shm == MAP_FAILED) {
        fprintf(stderr, "[ERROR] mmap failed for IPC: %s\n", strerror(errno));
        return -1;
    }
    memset(shm, 0, sizeof(bench_result_t));

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[ERROR] fork failed: %s\n", strerror(errno));
        munmap(shm, sizeof(bench_result_t));
        return -1;
    }

    if (pid == 0) {
        /* Child Process: Set CPU Affinity */
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(aff->logical_id, &cpuset);
        sched_setaffinity(0, sizeof(cpu_set_t), &cpuset);

        /* Execute benchmark function */
        cfg->run_fn(src, src_sz, shm);
        _exit(0);
    }

    /* Parent Process: wait for child and capture isolated resource usage */
    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    wait4(pid, &status, 0, &ru);

    shm->peak_rss_kb = (size_t)ru.ru_maxrss; /* Kernel-measured isolated RSS */
    memcpy(out_res, shm, sizeof(bench_result_t));
    munmap(shm, sizeof(bench_result_t));
    return 0;
#else
    /* Fallback on non-Linux platforms */
    cfg->run_fn(src, src_sz, out_res);
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        out_res->peak_rss_kb = pmc.PeakWorkingSetSize / 1024;
    }
    return 0;
#endif
}

/* ========================================================================= */
/* Registro en Archivo CSV Crudo (BP-008C-R1)                                */
/* ========================================================================= */

static FILE *g_csv_file = NULL;

static void log_csv_record_r1(
    const char *tier, const char *dataset, const char *engine,
    const bench_result_t *res, uint32_t seed
) {
    if (!g_csv_file) return;

    double c_raw_mbs  = (double)res->orig_sz / (1e6 * res->c_stat.raw_median);
    double c_filt_mbs = (double)res->orig_sz / (1e6 * res->c_stat.filtered_median);
    double c_iqr_mbs  = (res->c_stat.raw_iqr > 0) ? (res->c_stat.raw_iqr / res->c_stat.raw_median) * c_raw_mbs : 0.0;

    double d_raw_mbs  = (double)res->orig_sz / (1e6 * res->d_stat.raw_median);
    double d_filt_mbs = (double)res->orig_sz / (1e6 * res->d_stat.filtered_median);
    double d_iqr_mbs  = (res->d_stat.raw_iqr > 0) ? (res->d_stat.raw_iqr / res->d_stat.raw_median) * d_raw_mbs : 0.0;

    fprintf(g_csv_file,
            "%s,%s,%s,%zu,%zu,%.3f,"
            "%.2f,%.2f,%.2f,%d,"
            "%.2f,%.2f,%.2f,%d,"
            "%.4f,%.4f,"
            "%.2f,%.2f,%.2f,%.2f,"
            "%zu,%s,%u",
            tier, dataset, engine, res->orig_sz, res->cmp_sz, res->ratio_pct,
            c_raw_mbs, c_filt_mbs, c_iqr_mbs, res->c_stat.outliers_count,
            d_raw_mbs, d_filt_mbs, d_iqr_mbs, res->d_stat.outliers_count,
            res->c_stat.filtered_median * 1000.0, res->d_stat.filtered_median * 1000.0,
            res->lat_4k_comp_us, res->lat_4k_decomp_us,
            res->lat_64k_comp_us, res->lat_64k_decomp_us,
            res->peak_rss_kb, res->integrity_all_pass ? "PASS" : "FAIL", seed);

    /* Export 20 raw samples for compression */
    for (int i = 0; i < BENCH_ITERS; i++) {
        fprintf(g_csv_file, ",%.6f", res->c_stat.raw_samples[i]);
    }
    /* Export 20 raw samples for decompression */
    for (int i = 0; i < BENCH_ITERS; i++) {
        fprintf(g_csv_file, ",%.6f", res->d_stat.raw_samples[i]);
    }
    fprintf(g_csv_file, "\n");
    fflush(g_csv_file);
}

/* ========================================================================= */
/* Punto de Entrada Principal (BP-008C-R1)                                   */
/* ========================================================================= */

int main(int argc, char **argv) {
    const char *corpora_base = (argc > 1) ? argv[1] : "tests/corpora";
    uint32_t seed = 20261106u;
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            seed = (uint32_t)strtoul(argv[i + 1], NULL, 10);
        }
    }

    cpu_affinity_info_t aff;
    if (detect_and_set_pcore_affinity(&aff) != 0) {
        fprintf(stderr, "[FATAL] CPU pinning failed. Aborting benchmark.\n");
        return 1;
    }

    printf("==================================================================================================================================\n");
    printf("BINPACK ENTERPRISE SDK — SCIENTIFIC BENCHMARK & COMPARABILITY PROTOCOL (BP-008C-R1)\n");
    printf("Hardware Topology: Logical Core %d (Core ID %d) | %s\n",
           aff.logical_id, aff.core_id, aff.description);
    printf("Execution Seed: %u (Fisher-Yates Randomized Suite) | Isolated Memory per Subprocess (fork/wait4)\n", seed);
    printf("Runtime Linked Versions: Zstd %s (Num: %u) | LZ4 %s (Num: %u) | BinPack %s\n",
           ZSTD_versionString(), ZSTD_versionNumber(),
           LZ4_versionString(), LZ4_versionNumber(),
           binpack_version_string());
    printf("Protocol: 1 Warm-up + 20 Timed Iterations | Tukey's Rule Outlier Removal | Decimal MB/s (10^6 B/s)\n");
    printf("==================================================================================================================================\n\n");
    fflush(stdout);

    if (!pre_flight_validate_float_codecs()) {
        fprintf(stderr, "[FATAL] Pre-flight codec validation failed! Aborting.\n");
        return 1;
    }

    g_csv_file = fopen("docs/audit/decisive_benchmark_raw_r1.csv", "w");
    if (!g_csv_file) {
        g_csv_file = fopen("decisive_benchmark_raw_r1.csv", "w");
    }
    if (g_csv_file) {
        fprintf(g_csv_file,
                "Tier,Dataset,Engine,OriginalSize,CompressedSize,RatioPct,"
                "Comp_Raw_MBs,Comp_Filt_MBs,Comp_IQR_MBs,Comp_Outliers,"
                "Decomp_Raw_MBs,Decomp_Filt_MBs,Decomp_IQR_MBs,Decomp_Outliers,"
                "Comp_Med_ms,Decomp_Med_ms,"
                "Lat_4k_C_us,Lat_4k_D_us,Lat_64k_C_us,Lat_64k_D_us,"
                "PeakRSS_KB,Integrity,Seed");
        for (int i = 0; i < BENCH_ITERS; i++) fprintf(g_csv_file, ",Raw_C_Sec_%d", i);
        for (int i = 0; i < BENCH_ITERS; i++) fprintf(g_csv_file, ",Raw_D_Sec_%d", i);
        fprintf(g_csv_file, "\n");
        fflush(g_csv_file);
    }

    /* Load Corpora */
    char path_busybox[512], path_turbofan[512], path_mozilla[512], path_samba[512];
    snprintf(path_busybox, sizeof(path_busybox), "%s/arm_binaries/busybox_armv7l", corpora_base);
    snprintf(path_turbofan, sizeof(path_turbofan), "%s/nasa_telemetry/nasa_turbofan_float32.bin", corpora_base);
    snprintf(path_mozilla, sizeof(path_mozilla), "%s/silesia/mozilla", corpora_base);
    snprintf(path_samba, sizeof(path_samba), "%s/silesia/samba", corpora_base);

    size_t sz_arm = 0, sz_tf = 0, sz_moz = 0, sz_sam = 0;
    uint8_t *d_arm = load_raw_file(path_busybox, &sz_arm);
    uint8_t *d_tf  = load_raw_file(path_turbofan, &sz_tf);
    uint8_t *d_moz = load_raw_file(path_mozilla, &sz_moz);
    uint8_t *d_sam = load_raw_file(path_samba, &sz_sam);

    if (!d_arm || !d_tf || !d_moz || !d_sam) {
        fprintf(stderr, "[FATAL] Failed to load required benchmark datasets from '%s'.\n", corpora_base);
        if (g_csv_file) fclose(g_csv_file);
        return 1;
    }

    /* Also extract a single sensor channel (Channel 0, de-interleaved) from NASA telemetry */
    size_t n_total_floats = sz_tf / sizeof(float);
    size_t n_ch_floats = n_total_floats / 24;
    float *d_tf_ch0 = (float*)malloc(n_ch_floats * sizeof(float));
    const float *all_floats = (const float*)d_tf;
    for (size_t i = 0; i < n_ch_floats; i++) {
        d_tf_ch0[i] = all_floats[i * 24]; /* Sensor 0 */
    }
    size_t sz_tf_ch0 = n_ch_floats * sizeof(float);

    rng_t rng;
    rng_init(&rng, seed);

    /* ===================================================================== */
    /* DATASET 1: busybox_armv7l (Tier 1, Tier 2, Tier 3)                    */
    /* ===================================================================== */
    {
        printf("\n----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("DATASET [1/4]: busybox_armv7l         | Categoria: ARMv7-A Production Binary  | Tamano: %8zu Bytes (%.2f MB)\n",
               sz_arm, (double)sz_arm / 1e6);
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("%-26s | %-9s | %-16s | %-16s | %-10s | %-8s | %-9s\n",
               "Motor / Configuracion", "Ratio (%)", "Comp (MB/s ± IQR)", "Decomp (MB/s ± IQR)", "Lat 4K (C/D)", "Pico RSS", "Integridad");
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        fflush(stdout);

        engine_config_t engines_arm[] = {
            {"Tier1", "LZ4_RawBlock", "LZ4 v1.10 (RawBlock)", "Block API, 0-frame", run_lz4_fast},
            {"Tier1", "Zstd_Frame_L1", "Zstd v1.5.7 (L1 Frame)", "Standard Frame L1", run_zstd_l1},
            {"Tier1", "BinPack_Fast_ZeroCRC", "BinPack (Fast Zero-CRC)", "Preallocated, 0-CRC", run_binpack_fast_zerocrc},
            {"Tier1", "BinPack_Strict_CRC32", "BinPack (Strict CRC32)", "Preallocated, CRC32", run_binpack_strict_crc32},
            {"Tier1", "BinPack_Convenience_API", "BinPack (Convenience API)", "API with Malloc Overhead", run_binpack_convenience_api},
            {"Tier2", "Zstd_Frame_L19", "Zstd v1.5.7 (L19 Frame)", "Standard Frame L19, 32M Dict", run_zstd_l19},
            {"Tier2", "BinPack_Default_L19", "BinPack (Default L19)", "Default Engine L19", run_binpack_default_l19},
            {"Tier3", "RutaA_LZMA1_Puro", "Tier3: Ruta A (LZMA1 Puro)", "7-Zip LZMA1 SDK v19.00 Puro", run_tier3_ruta_a},
            {"Tier3", "RutaB_PavlovBCJ_LZMA1", "Tier3: Ruta B (BCJ+LZMA1)", "Pavlov BCJ ARM + LZMA1", run_tier3_ruta_b},
            {"Tier3", "RutaC_BinPack_Archival", "Tier3: Ruta C (BinPack Archival)", "BinPack Archival Core (LZMA1)", run_tier3_ruta_c}
        };
        int n_arm = sizeof(engines_arm) / sizeof(engines_arm[0]);

        /* Shuffle engines */
        engine_config_t *shuffled[16];
        for (int i = 0; i < n_arm; i++) shuffled[i] = &engines_arm[i];
        for (int i = n_arm - 1; i > 0; i--) {
            int j = rng_next_bounded(&rng, i + 1);
            engine_config_t *tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }

        for (int e = 0; e < n_arm; e++) {
            engine_config_t *cur_e = shuffled[e];
            bench_result_t res;
            run_engine_isolated(cur_e, d_arm, sz_arm, &aff, &res);

            double c_filt_mbs = (double)sz_arm / (1e6 * res.c_stat.filtered_median);
            double d_filt_mbs = (double)sz_arm / (1e6 * res.d_stat.filtered_median);
            double c_iqr_mbs  = (res.c_stat.raw_iqr > 0) ? (res.c_stat.raw_iqr / res.c_stat.raw_median) * c_filt_mbs : 0.0;
            double d_iqr_mbs  = (res.d_stat.raw_iqr > 0) ? (res.d_stat.raw_iqr / res.d_stat.raw_median) * d_filt_mbs : 0.0;

            char lat_str[32];
            snprintf(lat_str, sizeof(lat_str), "%.1f / %.1f", res.lat_4k_comp_us, res.lat_4k_decomp_us);

            printf("%-26s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   c_filt_mbs, c_iqr_mbs, d_filt_mbs, d_iqr_mbs,
                   lat_str, res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r1(cur_e->tier, "busybox_armv7l", cur_e->engine_id, &res, seed);
        }
    }

    /* ===================================================================== */
    /* DATASET 2: nasa_turbofan_f32 (Tier 4A Telemetry)                       */
    /* ===================================================================== */
    {
        printf("\n----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("DATASET [2/4]: nasa_turbofan_f32      | Categoria: NASA C-MAPSS Telemetry     | Tamano: %8zu Bytes (%.2f MB)\n",
               sz_tf, (double)sz_tf / 1e6);
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("%-26s | %-9s | %-16s | %-16s | %-10s | %-8s | %-9s\n",
               "Motor / Configuracion", "Ratio (%)", "Comp (MB/s ± IQR)", "Decomp (MB/s ± IQR)", "Lat 4K (C/D)", "Pico RSS", "Integridad");
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        fflush(stdout);

        /* A. Interleaved 24-channel evaluation */
        engine_config_t engines_tf[] = {
            {"Tier4A", "Gorilla_F32_Interleaved", "Gorilla (24-ch Interleaved)", "1D Delta-XOR on 24 channels", run_gorilla_f32},
            {"Tier4A", "Chimp_F32_Interleaved", "Chimp (24-ch Interleaved)", "1D Delta-XOR Leading Zeros", run_chimp_f32},
            {"Tier4A", "BinPack_Transpose_F32", "BinPack (Transpose-F32)", "4-Way Byte Stride Transpose", run_binpack_transpose_f32}
        };
        int n_tf = sizeof(engines_tf) / sizeof(engines_tf[0]);
        for (int e = 0; e < n_tf; e++) {
            engine_config_t *cur_e = &engines_tf[e];
            bench_result_t res;
            run_engine_isolated(cur_e, d_tf, sz_tf, &aff, &res);

            double c_filt_mbs = (double)sz_tf / (1e6 * res.c_stat.filtered_median);
            double d_filt_mbs = (double)sz_tf / (1e6 * res.d_stat.filtered_median);
            double c_iqr_mbs  = (res.c_stat.raw_iqr > 0) ? (res.c_stat.raw_iqr / res.c_stat.raw_median) * c_filt_mbs : 0.0;
            double d_iqr_mbs  = (res.d_stat.raw_iqr > 0) ? (res.d_stat.raw_iqr / res.d_stat.raw_median) * d_filt_mbs : 0.0;

            printf("%-26s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   c_filt_mbs, c_iqr_mbs, d_filt_mbs, d_iqr_mbs,
                   "—", res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r1(cur_e->tier, "nasa_turbofan_f32", cur_e->engine_id, &res, seed);
        }

        /* B. Single De-interleaved Channel 0 (Fair evaluation for 1D time-series codecs) */
        printf("  [TIER 4A FAIR COMPARISON — CANAL INDIVIDUAL DESENTRELAZADO (Sensor 0, %zu Bytes)]:\n", sz_tf_ch0);
        engine_config_t engines_tf_ch0[] = {
            {"Tier4A_Fair", "Gorilla_F32_SingleCh", "Gorilla (Single-Ch 0)", "1D Pure Univariate", run_gorilla_f32},
            {"Tier4A_Fair", "Chimp_F32_SingleCh", "Chimp (Single-Ch 0)", "1D Pure Univariate", run_chimp_f32},
            {"Tier4A_Fair", "BinPack_F32_SingleCh", "BinPack (Single-Ch 0)", "Transpose-F32 on Univariate", run_binpack_transpose_f32}
        };
        for (int e = 0; e < 3; e++) {
            engine_config_t *cur_e = &engines_tf_ch0[e];
            bench_result_t res;
            run_engine_isolated(cur_e, (const uint8_t*)d_tf_ch0, sz_tf_ch0, &aff, &res);

            double c_filt_mbs = (double)sz_tf_ch0 / (1e6 * res.c_stat.filtered_median);
            double d_filt_mbs = (double)sz_tf_ch0 / (1e6 * res.d_stat.filtered_median);
            double c_iqr_mbs  = (res.c_stat.raw_iqr > 0) ? (res.c_stat.raw_iqr / res.c_stat.raw_median) * c_filt_mbs : 0.0;
            double d_iqr_mbs  = (res.d_stat.raw_iqr > 0) ? (res.d_stat.raw_iqr / res.d_stat.raw_median) * d_filt_mbs : 0.0;

            printf("  %-24s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   c_filt_mbs, c_iqr_mbs, d_filt_mbs, d_iqr_mbs,
                   "—", res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r1(cur_e->tier, "nasa_turbofan_ch0", cur_e->engine_id, &res, seed);
        }

        printf("  [TIER 4B AUDIT NOTICE]: Sprintz I16 excluded from production claims (Mathematical UB quarantine).\n");
        fflush(stdout);
    }

    /* ===================================================================== */
    /* DATASET 3: silesia_mozilla (51.22 MB — x86 Executable)                */
    /* ===================================================================== */
    {
        printf("\n----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("DATASET [3/4]: silesia_mozilla        | Categoria: Silesia x86 Executable     | Tamano: %8zu Bytes (%.2f MB)\n",
               sz_moz, (double)sz_moz / 1e6);
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("%-26s | %-9s | %-16s | %-16s | %-10s | %-8s | %-9s\n",
               "Motor / Configuracion", "Ratio (%)", "Comp (MB/s ± IQR)", "Decomp (MB/s ± IQR)", "Lat 4K (C/D)", "Pico RSS", "Integridad");
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        fflush(stdout);

        engine_config_t engines_moz[] = {
            {"Tier1", "LZ4_RawBlock", "LZ4 v1.10 (RawBlock)", "Block API, 0-frame", run_lz4_fast},
            {"Tier1", "Zstd_Frame_L1", "Zstd v1.5.7 (L1 Frame)", "Standard Frame L1", run_zstd_l1},
            {"Tier1", "BinPack_Fast_ZeroCRC", "BinPack (Fast Zero-CRC)", "Preallocated, 0-CRC", run_binpack_fast_zerocrc},
            {"Tier1", "BinPack_Strict_CRC32", "BinPack (Strict CRC32)", "Preallocated, CRC32", run_binpack_strict_crc32},
            {"Tier2", "Zstd_Frame_L19", "Zstd v1.5.7 (L19 Frame)", "Standard Frame L19, 32M Dict", run_zstd_l19},
            {"Tier2", "BinPack_Default_L19", "BinPack (Default L19)", "Default Engine L19", run_binpack_default_l19}
        };
        int n_moz = sizeof(engines_moz) / sizeof(engines_moz[0]);

        engine_config_t *shuffled[8];
        for (int i = 0; i < n_moz; i++) shuffled[i] = &engines_moz[i];
        for (int i = n_moz - 1; i > 0; i--) {
            int j = rng_next_bounded(&rng, i + 1);
            engine_config_t *tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }

        for (int e = 0; e < n_moz; e++) {
            engine_config_t *cur_e = shuffled[e];
            bench_result_t res;
            run_engine_isolated(cur_e, d_moz, sz_moz, &aff, &res);

            double c_filt_mbs = (double)sz_moz / (1e6 * res.c_stat.filtered_median);
            double d_filt_mbs = (double)sz_moz / (1e6 * res.d_stat.filtered_median);
            double c_iqr_mbs  = (res.c_stat.raw_iqr > 0) ? (res.c_stat.raw_iqr / res.c_stat.raw_median) * c_filt_mbs : 0.0;
            double d_iqr_mbs  = (res.d_stat.raw_iqr > 0) ? (res.d_stat.raw_iqr / res.d_stat.raw_median) * d_filt_mbs : 0.0;

            char lat_str[32];
            snprintf(lat_str, sizeof(lat_str), "%.1f / %.1f", res.lat_4k_comp_us, res.lat_4k_decomp_us);

            printf("%-26s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   c_filt_mbs, c_iqr_mbs, d_filt_mbs, d_iqr_mbs,
                   lat_str, res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r1(cur_e->tier, "silesia_mozilla", cur_e->engine_id, &res, seed);
        }
    }

    /* ===================================================================== */
    /* DATASET 4: silesia_samba (21.61 MB — Structured C Code)               */
    /* ===================================================================== */
    {
        printf("\n----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("DATASET [4/4]: silesia_samba          | Categoria: Silesia Structured C Code  | Tamano: %8zu Bytes (%.2f MB)\n",
               sz_sam, (double)sz_sam / 1e6);
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("%-26s | %-9s | %-16s | %-16s | %-10s | %-8s | %-9s\n",
               "Motor / Configuracion", "Ratio (%)", "Comp (MB/s ± IQR)", "Decomp (MB/s ± IQR)", "Lat 4K (C/D)", "Pico RSS", "Integridad");
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        fflush(stdout);

        engine_config_t engines_sam[] = {
            {"Tier1", "LZ4_RawBlock", "LZ4 v1.10 (RawBlock)", "Block API, 0-frame", run_lz4_fast},
            {"Tier1", "Zstd_Frame_L1", "Zstd v1.5.7 (L1 Frame)", "Standard Frame L1", run_zstd_l1},
            {"Tier1", "BinPack_Fast_ZeroCRC", "BinPack (Fast Zero-CRC)", "Preallocated, 0-CRC", run_binpack_fast_zerocrc},
            {"Tier1", "BinPack_Strict_CRC32", "BinPack (Strict CRC32)", "Preallocated, CRC32", run_binpack_strict_crc32},
            {"Tier2", "Zstd_Frame_L19", "Zstd v1.5.7 (L19 Frame)", "Standard Frame L19, 32M Dict", run_zstd_l19},
            {"Tier2", "BinPack_Default_L19", "BinPack (Default L19)", "Default Engine L19", run_binpack_default_l19}
        };
        int n_sam = sizeof(engines_sam) / sizeof(engines_sam[0]);

        engine_config_t *shuffled[8];
        for (int i = 0; i < n_sam; i++) shuffled[i] = &engines_sam[i];
        for (int i = n_sam - 1; i > 0; i--) {
            int j = rng_next_bounded(&rng, i + 1);
            engine_config_t *tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }

        for (int e = 0; e < n_sam; e++) {
            engine_config_t *cur_e = shuffled[e];
            bench_result_t res;
            run_engine_isolated(cur_e, d_sam, sz_sam, &aff, &res);

            double c_filt_mbs = (double)sz_sam / (1e6 * res.c_stat.filtered_median);
            double d_filt_mbs = (double)sz_sam / (1e6 * res.d_stat.filtered_median);
            double c_iqr_mbs  = (res.c_stat.raw_iqr > 0) ? (res.c_stat.raw_iqr / res.c_stat.raw_median) * c_filt_mbs : 0.0;
            double d_iqr_mbs  = (res.d_stat.raw_iqr > 0) ? (res.d_stat.raw_iqr / res.d_stat.raw_median) * d_filt_mbs : 0.0;

            char lat_str[32];
            snprintf(lat_str, sizeof(lat_str), "%.1f / %.1f", res.lat_4k_comp_us, res.lat_4k_decomp_us);

            printf("%-26s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   c_filt_mbs, c_iqr_mbs, d_filt_mbs, d_iqr_mbs,
                   lat_str, res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r1(cur_e->tier, "silesia_samba", cur_e->engine_id, &res, seed);
        }
    }

    printf("\n==================================================================================================================================\n");
    printf("[COMPLETE] Benchmark execution finished cleanly. Raw sample data exported to 'decisive_benchmark_raw_r1.csv'.\n");
    printf("==================================================================================================================================\n");
    fflush(stdout);

    if (g_csv_file) fclose(g_csv_file);
    free(d_arm); free(d_tf); free(d_tf_ch0); free(d_moz); free(d_sam);
    return 0;
}
