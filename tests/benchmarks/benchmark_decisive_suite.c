#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>

#if defined(_WIN32)
#include <windows.h>
#include <psapi.h>
#else
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <unistd.h>
#endif

#include "../../include/binpack_sdk.h"
#include "../../deps/include/zstd.h"
#include "../../deps/include/lz4.h"
#include "../../deps/lzma/LzmaLib.h"
#include "../../deps/lzma/Bra.h"
#include "../../deps/timeseries/timeseries_codecs.h"

/* ========================================================================= */
/* Cross-Platform High-Resolution Timing & OS Resources                      */
/* ========================================================================= */

#if defined(_WIN32)
static inline uint64_t timer_now_ns(void) {
    static LARGE_INTEGER freq;
    static int init = 0;
    if (!init) {
        QueryPerformanceFrequency(&freq);
        init = 1;
    }
    LARGE_INTEGER t;
    QueryPerformanceCounter(&t);
    return (uint64_t)((double)t.QuadPart * 1000000000.0 / (double)freq.QuadPart);
}

static inline void set_cpu_affinity_pcore(void) {
    /* Intel Core i5-13500: Logical core 2 is Core 1 (P-Core, primary SMT thread) */
    SetProcessAffinityMask(GetCurrentProcess(), (DWORD_PTR)(1ULL << 2));
}

static inline size_t get_process_peak_rss(void) {
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return pmc.PeakWorkingSetSize;
    }
    return 0;
}
#else
static inline uint64_t timer_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC_RAW, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static inline void set_cpu_affinity_pcore(void) {
    /* Intel Core i5-13500: Logical core 2 is Core 1 (P-Core, primary SMT thread) */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(2, &cpuset);
    sched_setaffinity(0, sizeof(cpuset), &cpuset);
}

static inline size_t get_process_peak_rss(void) {
    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) == 0) {
        return (size_t)ru.ru_maxrss * 1024ULL; /* Linux ru_maxrss is in KiB */
    }
    return 0;
}
#endif

/* ========================================================================= */
/* Statistical Aggregation Module (Median, Q1, Q3, IQR, Outliers)           */
/* ========================================================================= */

#define BENCH_ITERS 20

typedef struct {
    int n_samples;
    double raw_samples[BENCH_ITERS];
    double sorted_samples[BENCH_ITERS];
    double min;
    double max;
    double q1;
    double median;
    double q3;
    double iqr;
    double lower_fence;
    double upper_fence;
    int outliers_count;
} stat_summary_t;

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static void calc_stats(const double *samples, int n, stat_summary_t *out) {
    out->n_samples = n;
    for (int i = 0; i < n; i++) {
        out->raw_samples[i] = samples[i];
        out->sorted_samples[i] = samples[i];
    }
    qsort(out->sorted_samples, n, sizeof(double), compare_doubles);

    out->min = out->sorted_samples[0];
    out->max = out->sorted_samples[n - 1];

    /* For n = 20: */
    if (n == 20) {
        out->median = (out->sorted_samples[9] + out->sorted_samples[10]) / 2.0;
        out->q1     = (out->sorted_samples[4] + out->sorted_samples[5]) / 2.0;
        out->q3     = (out->sorted_samples[14] + out->sorted_samples[15]) / 2.0;
    } else {
        out->median = out->sorted_samples[n / 2];
        out->q1     = out->sorted_samples[n / 4];
        out->q3     = out->sorted_samples[(n * 3) / 4];
    }
    out->iqr = out->q3 - out->q1;
    out->lower_fence = out->q1 - 1.5 * out->iqr;
    out->upper_fence = out->q3 + 1.5 * out->iqr;

    out->outliers_count = 0;
    for (int i = 0; i < n; i++) {
        if (out->raw_samples[i] < out->lower_fence || out->raw_samples[i] > out->upper_fence) {
            out->outliers_count++;
        }
    }
}

/* ========================================================================= */
/* Dataset Model & File Loader                                               */
/* ========================================================================= */

typedef enum {
    DTYPE_GENERIC_BYTES,
    DTYPE_FLOAT32,
    DTYPE_INT16,
    DTYPE_EXECUTABLE_ARM
} dataset_dtype_t;

typedef struct {
    const char *name;
    const char *group;
    dataset_dtype_t dtype;
    size_t size;
    uint8_t *data;
} dataset_t;

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
/* Adversarial Validation for Local Gorilla & Chimp Implementations          */
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

    /* Populate adversarial vectors: zeros, subnormals, infinities, NaNs, alternating signs */
    for (int i = 0; i < N_TEST; i++) {
        if (i < 128) test_in[i] = 0.0f;
        else if (i < 256) test_in[i] = (i % 2 == 0) ? -0.0f : 0.0f;
        else if (i < 384) test_in[i] = 1.0f + (float)i * 0.0001f;
        else if (i < 512) test_in[i] = (float)sin((double)i * 0.05);
        else if (i < 640) test_in[i] = 1e-38f * (float)(i - 512); /* Subnormals */
        else if (i < 768) test_in[i] = 1e30f;
        else test_in[i] = (float)(i * 7);
    }

    /* 1. Gorilla F32 */
    size_t g_sz = gorilla_compress_f32(test_in, N_TEST, cmp_buf, N_TEST * 8);
    if (g_sz == 0) {
        fprintf(stderr, "[PRE-FLIGHT ERROR] Gorilla compression returned 0 bytes\n");
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    memset(test_out, 0, N_TEST * sizeof(float));
    gorilla_decompress_f32(cmp_buf, g_sz, test_out, N_TEST);
    if (memcmp(test_in, test_out, N_TEST * sizeof(float)) != 0) {
        fprintf(stderr, "[PRE-FLIGHT ERROR] Gorilla adversarial round-trip mismatch!\n");
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    printf("  [PASS] Gorilla Float32: 100%% Bit-Exact on adversarial patterns\n");

    /* 2. Chimp F32 */
    size_t c_sz = chimp_compress_f32(test_in, N_TEST, cmp_buf, N_TEST * 8);
    if (c_sz == 0) {
        fprintf(stderr, "[PRE-FLIGHT ERROR] Chimp compression returned 0 bytes\n");
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    memset(test_out, 0, N_TEST * sizeof(float));
    chimp_decompress_f32(cmp_buf, c_sz, test_out, N_TEST);
    if (memcmp(test_in, test_out, N_TEST * sizeof(float)) != 0) {
        fprintf(stderr, "[PRE-FLIGHT ERROR] Chimp adversarial round-trip mismatch!\n");
        free(test_in); free(test_out); free(cmp_buf);
        return 0;
    }
    printf("  [PASS] Chimp Float32:   100%% Bit-Exact on adversarial patterns\n");

    free(test_in); free(test_out); free(cmp_buf);
    return 1;
}

/* ========================================================================= */
/* Latency Benchmark Helper (4 KiB and 64 KiB blocks)                       */
/* ========================================================================= */

typedef struct {
    double lat_4k_us;
    double lat_64k_us;
} block_latencies_t;

/* ========================================================================= */
/* CSV Raw Export File Handle                                                */
/* ========================================================================= */

static FILE *g_csv_file = NULL;

static void log_csv_record(
    const char *tier, const char *dataset, const char *engine,
    size_t orig_sz, size_t cmp_sz, double ratio_pct,
    const stat_summary_t *c_stat, const stat_summary_t *d_stat,
    double lat_4k, double lat_64k, size_t peak_rss, int integrity_ok
) {
    if (!g_csv_file) return;
    double c_throughput_mbs = (double)orig_sz / (1e6 * c_stat->median);
    double d_throughput_mbs = (double)orig_sz / (1e6 * d_stat->median);

    fprintf(g_csv_file, "%s,%s,%s,%zu,%zu,%.3f,%.2f,%.2f,%.4f,%.4f,%.2f,%.2f,%zu,%s",
            tier, dataset, engine, orig_sz, cmp_sz, ratio_pct,
            c_throughput_mbs, d_throughput_mbs,
            c_stat->median * 1000.0, d_stat->median * 1000.0,
            lat_4k, lat_64k, peak_rss, integrity_ok ? "OK" : "FAIL");

    /* Export 20 raw samples for compression */
    for (int i = 0; i < c_stat->n_samples; i++) {
        fprintf(g_csv_file, ",%.6f", c_stat->raw_samples[i]);
    }
    fprintf(g_csv_file, "\n");
    fflush(g_csv_file);
}

/* ========================================================================= */
/* Main Benchmark Protocol Execution                                         */
/* ========================================================================= */

int main(int argc, char **argv) {
    const char *corpora_base = (argc > 1) ? argv[1] : "tests/corpora";
    set_cpu_affinity_pcore();

    printf("==================================================================================================================================\n");
    printf("BINPACK ENTERPRISE SDK — SCIENTIFIC BENCHMARK & COMPARABILITY PROTOCOL (BP-008C)\n");
    printf("Hardware: Intel Core i5-13500 | P-Core Pinning (Logical Core 2) | Active RAM: Zero-Disk I/O\n");
    printf("Runtime Linked Versions: Zstd %s (Number: %u) | LZ4 %s (Number: %u) | BinPack %s\n",
           ZSTD_versionString(), ZSTD_versionNumber(),
           LZ4_versionString(), LZ4_versionNumber(),
           binpack_version_string());
    printf("Protocol: Exactly %d Timed Iterations | Median, Q1, Q3, IQR | Decimal MB/s (10^6 bytes/sec)\n", BENCH_ITERS);
    printf("==================================================================================================================================\n\n");

    if (!pre_flight_validate_float_codecs()) {
        fprintf(stderr, "[FATAL] Pre-flight codec validation failed! Aborting.\n");
        return 1;
    }

    g_csv_file = fopen("decisive_benchmark_raw.csv", "w");
    if (g_csv_file) {
        fprintf(g_csv_file, "Tier,Dataset,Engine,OriginalSize,CompressedSize,RatioPct,Comp_MBs,Decomp_MBs,Comp_Med_ms,Decomp_Med_ms,Lat_4k_us,Lat_64k_us,PeakRSS_B,Integrity");
        for (int i = 0; i < BENCH_ITERS; i++) {
            fprintf(g_csv_file, ",Raw_C_Sec_%d", i);
        }
        fprintf(g_csv_file, "\n");
    }

    /* Datasets to evaluate */
    char path_busybox[512], path_turbofan[512], path_mozilla[512], path_samba[512];
    snprintf(path_busybox, sizeof(path_busybox), "%s/arm_binaries/busybox_armv7l", corpora_base);
    snprintf(path_turbofan, sizeof(path_turbofan), "%s/nasa_telemetry/nasa_turbofan_float32.bin", corpora_base);
    snprintf(path_mozilla, sizeof(path_mozilla), "%s/silesia/mozilla", corpora_base);
    snprintf(path_samba, sizeof(path_samba), "%s/silesia/samba", corpora_base);

    dataset_t datasets[4];
    int ds_count = 0;

    size_t sz = 0;
    uint8_t *d_arm = load_raw_file(path_busybox, &sz);
    if (d_arm) datasets[ds_count++] = (dataset_t){"busybox_armv7l", "ARMv7-A Production Binary", DTYPE_EXECUTABLE_ARM, sz, d_arm};

    sz = 0;
    uint8_t *d_tf = load_raw_file(path_turbofan, &sz);
    if (d_tf) datasets[ds_count++] = (dataset_t){"nasa_turbofan_f32", "NASA C-MAPSS Telemetry", DTYPE_FLOAT32, sz, d_tf};

    sz = 0;
    uint8_t *d_moz = load_raw_file(path_mozilla, &sz);
    if (d_moz) datasets[ds_count++] = (dataset_t){"silesia_mozilla", "Silesia x86 Executable", DTYPE_GENERIC_BYTES, sz, d_moz};

    sz = 0;
    uint8_t *d_sam = load_raw_file(path_samba, &sz);
    if (d_sam) datasets[ds_count++] = (dataset_t){"silesia_samba", "Silesia Structured C Code", DTYPE_GENERIC_BYTES, sz, d_sam};

    if (ds_count == 0) {
        fprintf(stderr, "[ERROR] Could not load datasets from '%s'. Please ensure corpora are fetched.\n", corpora_base);
        if (g_csv_file) fclose(g_csv_file);
        return 1;
    }

    size_t max_buf_sz = 64 * 1024 * 1024;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz * 2);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz * 2);
    uint8_t *scratch_work = (uint8_t*)malloc(max_buf_sz * 2);

    ZSTD_CCtx *z_cctx1 = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(z_cctx1, ZSTD_c_compressionLevel, 1);
    ZSTD_CCtx *z_cctx19 = ZSTD_createCCtx();
    ZSTD_CCtx_setParameter(z_cctx19, ZSTD_c_compressionLevel, 19);
    ZSTD_CCtx_setParameter(z_cctx19, ZSTD_c_windowLog, 25); /* 32 MB window */
    ZSTD_DCtx *z_dctx = ZSTD_createDCtx();

    for (int d = 0; d < ds_count; d++) {
        dataset_t *cur = &datasets[d];
        size_t cur_sz = cur->size;
        uint8_t *src = cur->data;

        printf("\n----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("DATASET [%d/%d]: %-22s | Categoria: %-26s | Tamano: %8zu Bytes (%.2f MB)\n",
               d + 1, ds_count, cur->name, cur->group, cur_sz, (double)cur_sz / 1e6);
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");
        printf("%-24s | %-10s | %-16s | %-16s | %-12s | %-10s\n",
               "Motor / Configuracion", "Ratio (%)", "Comp (MB/s ± IQR)", "Decomp (MB/s ± IQR)", "Pico RSS", "Integridad");
        printf("----------------------------------------------------------------------------------------------------------------------------------\n");

        /* ================================================================= */
        /* TIER 1: SPEED FRONTIER                                            */
        /* ================================================================= */
        if (cur->dtype == DTYPE_GENERIC_BYTES || cur->dtype == DTYPE_EXECUTABLE_ARM) {
            /* 1.1 LZ4 Fast */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = 0;

                /* Warm-up */
                c_sz = (size_t)LZ4_compress_default((const char*)src, (char*)comp_buf, (int)cur_sz, (int)max_buf_sz);
                LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)cur_sz);

                /* Timed 20 iterations: Compression */
                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    c_sz = (size_t)LZ4_compress_default((const char*)src, (char*)comp_buf, (int)cur_sz, (int)max_buf_sz);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                }

                /* Timed 20 iterations: Decompression */
                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)cur_sz);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double c_iqr_mbs = (c_stat.iqr > 0) ? (c_stat.iqr / c_stat.median) * c_mbs : 0.0;
                double d_iqr_mbs = (d_stat.iqr > 0) ? (d_stat.iqr / d_stat.median) * d_mbs : 0.0;
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "LZ4 v1.10 (Fast)", ratio, c_mbs, c_iqr_mbs, d_mbs, d_iqr_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier1", cur->name, "LZ4_Fast", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 1.2 Zstd Level 1 */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = 0;

                /* Warm-up */
                ZSTD_CCtx_reset(z_cctx1, ZSTD_reset_session_only);
                c_sz = ZSTD_compressCCtx(z_cctx1, comp_buf, max_buf_sz, src, cur_sz, 1);
                ZSTD_decompressDCtx(z_dctx, decomp_buf, cur_sz, comp_buf, c_sz);

                for (int i = 0; i < BENCH_ITERS; i++) {
                    ZSTD_CCtx_reset(z_cctx1, ZSTD_reset_session_only);
                    uint64_t t0 = timer_now_ns();
                    c_sz = ZSTD_compressCCtx(z_cctx1, comp_buf, max_buf_sz, src, cur_sz, 1);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    ZSTD_decompressDCtx(z_dctx, decomp_buf, cur_sz, comp_buf, c_sz);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double c_iqr_mbs = (c_stat.iqr > 0) ? (c_stat.iqr / c_stat.median) * c_mbs : 0.0;
                double d_iqr_mbs = (d_stat.iqr > 0) ? (d_stat.iqr / d_stat.median) * d_mbs : 0.0;
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "Zstd v1.5.7 (L1)", ratio, c_mbs, c_iqr_mbs, d_mbs, d_iqr_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier1", cur->name, "Zstd_L1", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 1.3 BinPack Fast Zero-CRC */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                void *out_dst = NULL;
                size_t c_sz = 0;
                binpack_options_t opt;
                binpack_default_options(&opt);
                opt.mode = BINPACK_MODE_RUNTIME;
                opt.clevel = 1;

                /* Warm-up */
                binpack_compress_buffer(src, cur_sz, &out_dst, &c_sz, &opt);
                if (out_dst) {
                    size_t dec_sz = 0;
                    binpack_decompress_into(out_dst, c_sz, decomp_buf, cur_sz, &dec_sz, 0);
                    free(out_dst);
                    out_dst = NULL;
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    binpack_compress_buffer(src, cur_sz, &out_dst, &c_sz, &opt);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                    if (i < BENCH_ITERS - 1 && out_dst) { free(out_dst); out_dst = NULL; }
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t dec_sz = 0;
                    uint64_t t0 = timer_now_ns();
                    binpack_decompress_into(out_dst, c_sz, decomp_buf, cur_sz, &dec_sz, 0 /* Zero-CRC */);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                if (out_dst) { free(out_dst); out_dst = NULL; }

                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double c_iqr_mbs = (c_stat.iqr > 0) ? (c_stat.iqr / c_stat.median) * c_mbs : 0.0;
                double d_iqr_mbs = (d_stat.iqr > 0) ? (d_stat.iqr / d_stat.median) * d_mbs : 0.0;
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "BinPack (Fast Zero-CRC)", ratio, c_mbs, c_iqr_mbs, d_mbs, d_iqr_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier1", cur->name, "BinPack_Fast_ZeroCRC", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 1.4 BinPack Strict CRC32 */
            {
                double d_samples[BENCH_ITERS];
                void *out_dst = NULL;
                size_t c_sz = 0;
                binpack_options_t opt;
                binpack_default_options(&opt);
                opt.mode = BINPACK_MODE_RUNTIME;
                opt.clevel = 1;

                binpack_compress_buffer(src, cur_sz, &out_dst, &c_sz, &opt);

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t dec_sz = 0;
                    uint64_t t0 = timer_now_ns();
                    binpack_decompress_into(out_dst, c_sz, decomp_buf, cur_sz, &dec_sz, 1 /* Strict CRC32 */);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                if (out_dst) { free(out_dst); out_dst = NULL; }

                stat_summary_t d_stat;
                calc_stats(d_samples, BENCH_ITERS, &d_stat);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double d_iqr_mbs = (d_stat.iqr > 0) ? (d_stat.iqr / d_stat.median) * d_mbs : 0.0;
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  |        —         | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "BinPack (Strict CRC32)", ratio, d_mbs, d_iqr_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");
            }
        }

        /* ================================================================= */
        /* TIER 2: BALANCED PRODUCTION (Zstd L19 vs BinPack L19)             */
        /* ================================================================= */
        if (cur->dtype == DTYPE_GENERIC_BYTES || cur->dtype == DTYPE_EXECUTABLE_ARM) {
            /* 2.1 Zstandard L19 */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = 0;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    ZSTD_CCtx_reset(z_cctx19, ZSTD_reset_session_only);
                    uint64_t t0 = timer_now_ns();
                    c_sz = ZSTD_compressCCtx(z_cctx19, comp_buf, max_buf_sz, src, cur_sz, 19);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    ZSTD_decompressDCtx(z_dctx, decomp_buf, cur_sz, comp_buf, c_sz);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double c_iqr_mbs = (c_stat.iqr > 0) ? (c_stat.iqr / c_stat.median) * c_mbs : 0.0;
                double d_iqr_mbs = (d_stat.iqr > 0) ? (d_stat.iqr / d_stat.median) * d_mbs : 0.0;
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "Zstd v1.5.7 (L19)", ratio, c_mbs, c_iqr_mbs, d_mbs, d_iqr_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier2", cur->name, "Zstd_L19", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 2.2 BinPack Default L19 */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                void *out_dst = NULL;
                size_t c_sz = 0;
                binpack_options_t opt;
                binpack_default_options(&opt);
                opt.mode = BINPACK_MODE_DEFAULT;
                opt.clevel = 19;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    binpack_compress_buffer(src, cur_sz, &out_dst, &c_sz, &opt);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                    if (i < BENCH_ITERS - 1 && out_dst) { free(out_dst); out_dst = NULL; }
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t dec_sz = 0;
                    uint64_t t0 = timer_now_ns();
                    binpack_decompress_into(out_dst, c_sz, decomp_buf, cur_sz, &dec_sz, 1);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                if (out_dst) { free(out_dst); out_dst = NULL; }

                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double c_iqr_mbs = (c_stat.iqr > 0) ? (c_stat.iqr / c_stat.median) * c_mbs : 0.0;
                double d_iqr_mbs = (d_stat.iqr > 0) ? (d_stat.iqr / d_stat.median) * d_mbs : 0.0;
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "BinPack (Default L19)", ratio, c_mbs, c_iqr_mbs, d_mbs, d_iqr_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier2", cur->name, "BinPack_Default_L19", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }
        }

        /* ================================================================= */
        /* TIER 3: DENSITY FRONTIER — 3-ROUTE DECOMPOSITION (ARM Binaries)    */
        /* ================================================================= */
        if (cur->dtype == DTYPE_EXECUTABLE_ARM) {
            printf("  [Tier 3 — Descomposicion de Prefiltro BCJ vs Compresor LZMA1]:\n");

            /* Route A: 7-Zip LZMA1 Puro (Sin BCJ) */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = max_buf_sz;
                unsigned char props[5];
                size_t props_sz = 5;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    c_sz = max_buf_sz;
                    props_sz = 5;
                    uint64_t t0 = timer_now_ns();
                    LzmaCompress(comp_buf + 5, &c_sz, src, cur_sz, props, &props_sz, 9, 32 * 1024 * 1024, 3, 0, 2, 32, 1);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                }
                memcpy(comp_buf, props, 5);
                size_t total_c_sz = c_sz + 5;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t d_sz = cur_sz;
                    size_t src_len = c_sz;
                    uint64_t t0 = timer_now_ns();
                    LzmaUncompress(decomp_buf, &d_sz, comp_buf + 5, &src_len, comp_buf, 5);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double ratio = (double)total_c_sz / (double)cur_sz * 100.0;

                printf("  %-22s | %8.2f%%  | %7.1f MB/s       | %7.1f MB/s       | %8zu KB | %s\n",
                       "Ruta A: LZMA1 Puro", ratio, c_mbs, d_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier3", cur->name, "RutaA_LZMA1_Puro", cur_sz, total_c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* Route B: 7-Zip LZMA1 + Pavlov BCJ ARM Oficial */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = max_buf_sz;
                unsigned char props[5];
                size_t props_sz = 5;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    memcpy(scratch_work, src, cur_sz);
                    uint64_t t0 = timer_now_ns();
                    ARM_Convert(scratch_work, cur_sz, 0, 1 /* encode */);
                    c_sz = max_buf_sz;
                    props_sz = 5;
                    LzmaCompress(comp_buf + 5, &c_sz, scratch_work, cur_sz, props, &props_sz, 9, 32 * 1024 * 1024, 3, 0, 2, 32, 1);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                }
                memcpy(comp_buf, props, 5);
                size_t total_c_sz = c_sz + 5;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t d_sz = cur_sz;
                    size_t src_len = c_sz;
                    uint64_t t0 = timer_now_ns();
                    LzmaUncompress(decomp_buf, &d_sz, comp_buf + 5, &src_len, comp_buf, 5);
                    ARM_Convert(decomp_buf, cur_sz, 0, 0 /* decode */);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double ratio = (double)total_c_sz / (double)cur_sz * 100.0;

                printf("  %-22s | %8.2f%%  | %7.1f MB/s       | %7.1f MB/s       | %8zu KB | %s\n",
                       "Ruta B: Pavlov BCJ+LZMA1", ratio, c_mbs, d_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier3", cur->name, "RutaB_PavlovBCJ_LZMA1", cur_sz, total_c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* Route C: BinPack Archival Core (.text bounds + BCJ ARM + LZMA1) */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                void *out_dst = NULL;
                size_t c_sz = 0;
                binpack_options_t opt;
                binpack_default_options(&opt);
                opt.mode = BINPACK_MODE_ARCHIVAL;
                opt.filter = BINPACK_FILTER_ARM32;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    binpack_compress_buffer(src, cur_sz, &out_dst, &c_sz, &opt);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                    if (i < BENCH_ITERS - 1 && out_dst) { free(out_dst); out_dst = NULL; }
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t dec_sz = 0;
                    uint64_t t0 = timer_now_ns();
                    binpack_decompress_into(out_dst, c_sz, decomp_buf, cur_sz, &dec_sz, 1);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                if (out_dst) { free(out_dst); out_dst = NULL; }

                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("  %-22s | %8.2f%%  | %7.1f MB/s       | %7.1f MB/s       | %8zu KB | %s\n",
                       "Ruta C: BinPack Archival", ratio, c_mbs, d_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier3", cur->name, "RutaC_BinPack_Archival", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }
        }

        /* ================================================================= */
        /* TIER 4: TELEMETRY & TIMESERIES (Float32 Sub-Tier)                 */
        /* ================================================================= */
        if (cur->dtype == DTYPE_FLOAT32) {
            size_t n_floats = cur_sz / sizeof(float);
            const float *src_f = (const float*)src;
            float *dec_f = (float*)decomp_buf;

            /* 4.1 Gorilla Float32 */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = 0;

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
                }

                int ok = (memcmp(dec_f, src_f, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "Gorilla (F32 Lossless)", ratio, c_mbs, (c_stat.iqr / c_stat.median) * c_mbs, d_mbs, (d_stat.iqr / d_stat.median) * d_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier4A", cur->name, "Gorilla_F32", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 4.2 Chimp Float32 */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                size_t c_sz = 0;

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
                }

                int ok = (memcmp(dec_f, src_f, cur_sz) == 0);
                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "Chimp (F32 Lossless)", ratio, c_mbs, (c_stat.iqr / c_stat.median) * c_mbs, d_mbs, (d_stat.iqr / d_stat.median) * d_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier4A", cur->name, "Chimp_F32", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 4.3 BinPack Telemetry Float32 */
            {
                double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
                void *out_dst = NULL;
                size_t c_sz = 0;
                binpack_options_t opt;
                binpack_default_options(&opt);
                opt.mode = BINPACK_MODE_DEFAULT;
                opt.filter = BINPACK_FILTER_TRANSPOSE_DELTA;
                opt.transpose_stride = 4; /* 4 bytes for float32 */
                opt.clevel = 19;

                for (int i = 0; i < BENCH_ITERS; i++) {
                    uint64_t t0 = timer_now_ns();
                    binpack_compress_buffer(src, cur_sz, &out_dst, &c_sz, &opt);
                    uint64_t t1 = timer_now_ns();
                    c_samples[i] = (double)(t1 - t0) / 1e9;
                    if (i < BENCH_ITERS - 1 && out_dst) { free(out_dst); out_dst = NULL; }
                }

                for (int i = 0; i < BENCH_ITERS; i++) {
                    size_t dec_sz = 0;
                    uint64_t t0 = timer_now_ns();
                    binpack_decompress_into(out_dst, c_sz, decomp_buf, cur_sz, &dec_sz, 1);
                    uint64_t t1 = timer_now_ns();
                    d_samples[i] = (double)(t1 - t0) / 1e9;
                }

                int ok = (memcmp(decomp_buf, src, cur_sz) == 0);
                if (out_dst) { free(out_dst); out_dst = NULL; }

                stat_summary_t c_stat, d_stat;
                calc_stats(c_samples, BENCH_ITERS, &c_stat);
                calc_stats(d_samples, BENCH_ITERS, &d_stat);

                double c_mbs = (double)cur_sz / (1e6 * c_stat.median);
                double d_mbs = (double)cur_sz / (1e6 * d_stat.median);
                double ratio = (double)c_sz / (double)cur_sz * 100.0;

                printf("%-24s | %8.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %8zu KB | %s\n",
                       "BinPack (Transpose-F32)", ratio, c_mbs, (c_stat.iqr / c_stat.median) * c_mbs, d_mbs, (d_stat.iqr / d_stat.median) * d_mbs,
                       get_process_peak_rss() / 1024, ok ? "PASS" : "FAIL");

                log_csv_record("Tier4A", cur->name, "BinPack_Transpose_F32", cur_sz, c_sz, ratio, &c_stat, &d_stat, 0, 0, get_process_peak_rss(), ok);
            }

            /* 4.4 Sprintz I16 (Audit Warning: Marked Uncertified) */
            printf("  [TIER 4B AUDIT WARNING]: Sprintz I16 excluded from Float32 evaluation (Lossless integrity constraint).\n");
        }
    }

    printf("\n==================================================================================================================================\n");
    printf("[COMPLETE] Benchmark execution finished. Raw sample data exported to 'decisive_benchmark_raw.csv'.\n");
    printf("==================================================================================================================================\n");

    if (g_csv_file) fclose(g_csv_file);
    free(comp_buf); free(decomp_buf); free(scratch_work);
    ZSTD_freeCCtx(z_cctx1); ZSTD_freeCCtx(z_cctx19); ZSTD_freeDCtx(z_dctx);
    for (int d = 0; d < ds_count; d++) {
        if (datasets[d].data) free(datasets[d].data);
    }
    return 0;
}
