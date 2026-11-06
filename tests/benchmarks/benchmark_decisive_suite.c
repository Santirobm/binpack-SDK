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
#include <fcntl.h>
#include <io.h>
#include <sys/stat.h>
#if defined(__GNUC__) || defined(__clang__)
#include <cpuid.h>
#elif defined(_MSC_VER)
#include <intrin.h>
#define __cpuid(level, a, b, c, d) do { int _ci[4]; __cpuid(_ci, level); a = _ci[0]; b = _ci[1]; c = _ci[2]; d = _ci[3]; } while(0)
#define __cpuid_count(level, count, a, b, c, d) do { int _ci[4]; __cpuidex(_ci, level, count); a = _ci[0]; b = _ci[1]; c = _ci[2]; d = _ci[3]; } while(0)
#endif
#else
#include <time.h>
#include <sched.h>
#include <sys/resource.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cpuid.h>
#include <fcntl.h>
#include <sys/stat.h>
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
    int core_id;                   /* -1 if unknown / unavailable */
    int physical_core_verified;    /* 1 only if native CPUID Leaf 0x1A returns 0x40 */
    int topology_gate_passed;      /* 1 if affinity pinned and execution gate passed */
    char description[128];
} cpu_affinity_info_t;

static int detect_and_set_pcore_affinity(cpu_affinity_info_t *info) {
    memset(info, 0, sizeof(*info));
    info->logical_id = 2; /* Target Core 1 / Logical Core 2 */
    info->core_id = -1;
    info->physical_core_verified = 0;
    info->topology_gate_passed = 0;

#if defined(__linux__)
    /* 1. Fijar primero la afinidad al CPU logico 2 */
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(info->logical_id, &cpuset);
    if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
        fprintf(stderr, "[FATAL] sched_setaffinity failed on logical CPU %d: %s\n",
                info->logical_id, strerror(errno));
        return -1;
    }

    /* 2. Verificar que la mascara efectiva sea la esperada */
    cpu_set_t verify_set;
    CPU_ZERO(&verify_set);
    if (sched_getaffinity(0, sizeof(cpu_set_t), &verify_set) != 0) {
        fprintf(stderr, "[FATAL] sched_getaffinity failed: %s\n", strerror(errno));
        return -1;
    }
    if (!CPU_ISSET(info->logical_id, &verify_set) || CPU_COUNT(&verify_set) != 1) {
        fprintf(stderr, "[FATAL] Affinity verification failed: CPU %d not strictly isolated in mask\n", info->logical_id);
        return -1;
    }

    /* Forzar migracion efectiva al nucleo asignado */
    sched_yield();

    /* Leer topologia fisica sysfs (R5-03: rechazar valores negativos o fallos de lectura) */
    char path[128];
    snprintf(path, sizeof(path), "/sys/devices/system/cpu/cpu%d/topology/core_id", info->logical_id);
    FILE *f = fopen(path, "r");
    if (f) {
        int parsed_id = -1;
        if (fscanf(f, "%d", &parsed_id) == 1 && parsed_id >= 0) {
            info->core_id = parsed_id;
        } else {
            info->core_id = -1;
        }
        fclose(f);
    }
#elif defined(_WIN32)
    DWORD_PTR mask = ((DWORD_PTR)1 << info->logical_id);
    if (!SetProcessAffinityMask(GetCurrentProcess(), mask)) {
        fprintf(stderr, "[FATAL] SetProcessAffinityMask failed on CPU %d: %lu\n",
                info->logical_id, GetLastError());
        return -1;
    }
    DWORD_PTR proc_mask = 0, sys_mask = 0;
    if (!GetProcessAffinityMask(GetCurrentProcess(), &proc_mask, &sys_mask) || proc_mask != mask) {
        fprintf(stderr, "[FATAL] Affinity verification failed: Mask 0x%llX != expected 0x%llX\n",
                (unsigned long long)proc_mask, (unsigned long long)mask);
        return -1;
    }
    Sleep(10);
#endif

    /* 3. Validar disponibilidad basica de CPUID (Leaf 0) */
    unsigned int max_basic_leaf = 0, b0 = 0, c0 = 0, d0 = 0;
    __cpuid(0, max_basic_leaf, b0, c0, d0);
    if (max_basic_leaf < 1) {
        fprintf(stderr, "[FATAL] CPUID basic leaves unsupported (max basic leaf < 1). Aborting.\n");
        return -1;
    }

    /* 4. REGLA R5-01 / R6: Deteccion de hipervisor ANTES de cualquier certificacion fisica
     * Se consulta CPUID.1:ECX[31]. Si esta activo, NUNCA se otorga physical_core_verified = 1,
     * garantizando que ningun entorno virtualizado obtenga certificacion fisica por exponer Leaf 0x1A. */
    unsigned int h_eax = 0, h_ebx = 0, h_ecx = 0, h_edx = 0;
    __cpuid(1, h_eax, h_ebx, h_ecx, h_edx);
    int is_hypervisor = (h_ecx >> 31) & 1;

    if (is_hypervisor) {
        info->physical_core_verified = 0;
        info->topology_gate_passed = 1;

        /* R5-02 / R6: Validar que el hipervisor soporte y exponga la hoja 0x40000000 */
        char hv_sig[13];
        memset(hv_sig, 0, sizeof(hv_sig));
        __cpuid(0x40000000, h_eax, h_ebx, h_ecx, h_edx);

        const char *hv_vendor = "Generic Virtual Guest / Hypervisor";
        if (h_eax >= 0x40000000 && h_eax < 0x40010000) {
            memcpy(hv_sig + 0, &h_ebx, 4);
            memcpy(hv_sig + 4, &h_ecx, 4);
            memcpy(hv_sig + 8, &h_edx, 4);

            if (memcmp(hv_sig, "Microsoft Hv", 12) == 0) {
                hv_vendor = "Microsoft Hyper-V Virtual Guest (WSL2/Hyper-V)";
            } else if (memcmp(hv_sig, "KVMKVMKVM\0\0\0", 12) == 0 || memcmp(hv_sig, "KVMKVMKVM", 9) == 0) {
                hv_vendor = "KVM Virtual Guest";
            } else if (memcmp(hv_sig, "VMwareVMware", 12) == 0) {
                hv_vendor = "VMware Virtual Guest";
            }
        } else {
            hv_vendor = "Generic Virtual Guest / Hypervisor (Incomplete Leaf Range)";
        }

        if (info->core_id >= 0) {
            snprintf(info->description, sizeof(info->description),
                     "%s (vCPU %d, Sysfs Core ID %d, Hypervisor Topology Abstraction)",
                     hv_vendor, info->logical_id, info->core_id);
        } else {
            snprintf(info->description, sizeof(info->description),
                     "%s (vCPU %d, Sysfs Core ID Unknown, Hypervisor Topology Abstraction)",
                     hv_vendor, info->logical_id);
        }
        return 0;
    }

    /* 5. Evaluacion exclusiva en Silicio Nativo (Bare-Metal, !is_hypervisor)
     * Validar que la hoja 0x1A este dentro del rango maximo basico anunciado */
    if (max_basic_leaf < 0x1a) {
        fprintf(stderr, "[FATAL] Bare-metal CPU does not support CPUID Leaf 0x1A (max basic leaf 0x%02X < 0x1A). Aborting.\n",
                max_basic_leaf);
        return -1;
    }

    unsigned int eax_1a = 0, ebx_1a = 0, ecx_1a = 0, edx_1a = 0;
    __cpuid_count(0x1a, 0, eax_1a, ebx_1a, ecx_1a, edx_1a);
    unsigned int core_type = (eax_1a >> 24) & 0xff;

    if (core_type == 0x40) {
        info->physical_core_verified = 1;
        info->topology_gate_passed = 1;
        if (info->core_id >= 0) {
            snprintf(info->description, sizeof(info->description),
                     "Intel P-Core (Raptor Cove, Native CPUID 0x1A Core Type 0x40, Logical %d, Core ID %d)",
                     info->logical_id, info->core_id);
        } else {
            snprintf(info->description, sizeof(info->description),
                     "Intel P-Core (Raptor Cove, Native CPUID 0x1A Core Type 0x40, Logical %d, Core ID Unknown)",
                     info->logical_id);
        }
        return 0;
    } else if (core_type == 0x20) {
        fprintf(stderr, "[FATAL] Target CPU %d is an Intel E-Core (CPUID 0x1A: 0x20). Benchmark requires P-Core.\n",
                info->logical_id);
        return -1;
    }

    fprintf(stderr, "[FATAL] Core %d could not be verified as an Intel P-Core on bare-metal (CPUID 0x1A returned 0x%02X, expected 0x40). Aborting.\n",
            info->logical_id, core_type);
    return -1;
}

/* ========================================================================= */
/* Temporizacion Monotonica de Alta Precision & Libre de Desbordamiento      */
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
    static volatile LONG freq_init = 0;
    if (InterlockedCompareExchange(&freq_init, 1, 0) == 0) {
        if (!QueryPerformanceFrequency(&freq)) {
            fprintf(stderr, "[FATAL] QueryPerformanceFrequency failed\n");
            abort();
        }
    } else {
        while (freq.QuadPart == 0) {
            Sleep(0);
        }
    }
    LARGE_INTEGER counter;
    if (!QueryPerformanceCounter(&counter)) {
        fprintf(stderr, "[FATAL] QueryPerformanceCounter failed\n");
        abort();
    }
    /* Overflow-safe decomposition: counter * 1e9 can overflow 64-bit integer when uptime > 30 mins */
    uint64_t whole_sec = (uint64_t)counter.QuadPart / (uint64_t)freq.QuadPart;
    uint64_t remainder = (uint64_t)counter.QuadPart % (uint64_t)freq.QuadPart;
    return whole_sec * 1000000000ULL + (remainder * 1000000000ULL) / (uint64_t)freq.QuadPart;
#endif
}

/* ========================================================================= */
/* Modulo Estadistico Riguroso (Calculo Directo de Throughput + Tukey)       */
/* ========================================================================= */

#define BENCH_ITERS 20

typedef struct {
    int n_samples;
    double raw_samples[BENCH_ITERS]; /* execution time in seconds */
    double raw_tp_mbs[BENCH_ITERS];  /* sample throughput in MB/s */

    /* Raw Statistics */
    double raw_min_sec;
    double raw_max_sec;
    double raw_mean_sec;
    double raw_median_sec;
    double raw_q1_sec;
    double raw_q3_sec;
    double raw_iqr_sec;
    double lower_fence_sec;
    double upper_fence_sec;
    int outliers_count;

    /* Raw Throughput Statistics (Calculated directly from sample throughputs!) */
    double raw_median_tp_mbs;
    double raw_q1_tp_mbs;
    double raw_q3_tp_mbs;
    double raw_iqr_tp_mbs;

    /* Filtered Statistics (Tukey's Rule Applied) */
    int n_filtered;
    double filtered_samples[BENCH_ITERS];
    double filtered_tp_mbs[BENCH_ITERS];
    double filtered_mean_sec;
    double filtered_median_sec;
    double filtered_stddev_sec;

    /* Filtered Throughput Statistics (Calculated directly from filtered samples!) */
    double filtered_median_tp_mbs;
    double filtered_q1_tp_mbs;
    double filtered_q3_tp_mbs;
    double filtered_iqr_tp_mbs;
} stat_summary_t;

static int compare_doubles(const void *a, const void *b) {
    double da = *(const double*)a;
    double db = *(const double*)b;
    if (da < db) return -1;
    if (da > db) return 1;
    return 0;
}

static void calc_stats(const double *samples, int n, size_t orig_sz, stat_summary_t *out) {
    memset(out, 0, sizeof(*out));
    if (n <= 0) return;
    out->n_samples = n;

    double sum_raw = 0.0;
    double sorted_time[BENCH_ITERS];
    double sorted_tp[BENCH_ITERS];

    for (int i = 0; i < n; i++) {
        out->raw_samples[i] = samples[i];
        sorted_time[i] = samples[i];
        sum_raw += samples[i];

        if (samples[i] > 0.0) {
            out->raw_tp_mbs[i] = (double)orig_sz / (1e6 * samples[i]);
        } else {
            out->raw_tp_mbs[i] = 0.0;
        }
        sorted_tp[i] = out->raw_tp_mbs[i];
    }

    qsort(sorted_time, n, sizeof(double), compare_doubles);
    qsort(sorted_tp, n, sizeof(double), compare_doubles);

    out->raw_min_sec = sorted_time[0];
    out->raw_max_sec = sorted_time[n - 1];
    out->raw_mean_sec = sum_raw / (double)n;

    if (n == 20) {
        out->raw_median_sec = (sorted_time[9] + sorted_time[10]) / 2.0;
        out->raw_q1_sec     = (sorted_time[4] + sorted_time[5]) / 2.0;
        out->raw_q3_sec     = (sorted_time[14] + sorted_time[15]) / 2.0;

        out->raw_median_tp_mbs = (sorted_tp[9] + sorted_tp[10]) / 2.0;
        out->raw_q1_tp_mbs     = (sorted_tp[4] + sorted_tp[5]) / 2.0;
        out->raw_q3_tp_mbs     = (sorted_tp[14] + sorted_tp[15]) / 2.0;
    } else {
        out->raw_median_sec = sorted_time[n / 2];
        out->raw_q1_sec     = sorted_time[n / 4];
        out->raw_q3_sec     = sorted_time[(n * 3) / 4];

        out->raw_median_tp_mbs = sorted_tp[n / 2];
        out->raw_q1_tp_mbs     = sorted_tp[n / 4];
        out->raw_q3_tp_mbs     = sorted_tp[(n * 3) / 4];
    }
    out->raw_iqr_sec = out->raw_q3_sec - out->raw_q1_sec;
    out->raw_iqr_tp_mbs = out->raw_q3_tp_mbs - out->raw_q1_tp_mbs;
    out->lower_fence_sec = out->raw_q1_sec - 1.5 * out->raw_iqr_sec;
    out->upper_fence_sec = out->raw_q3_sec + 1.5 * out->raw_iqr_sec;

    /* Apply Tukey's Rule on raw time samples */
    out->n_filtered = 0;
    out->outliers_count = 0;
    double sum_filt = 0.0;
    for (int i = 0; i < n; i++) {
        double val = out->raw_samples[i];
        if (val >= out->lower_fence_sec && val <= out->upper_fence_sec) {
            out->filtered_samples[out->n_filtered] = val;
            out->filtered_tp_mbs[out->n_filtered] = out->raw_tp_mbs[i];
            sum_filt += val;
            out->n_filtered++;
        } else {
            out->outliers_count++;
        }
    }

    if (out->n_filtered > 0) {
        out->filtered_mean_sec = sum_filt / (double)out->n_filtered;

        double sorted_filt_time[BENCH_ITERS];
        double sorted_filt_tp[BENCH_ITERS];
        memcpy(sorted_filt_time, out->filtered_samples, out->n_filtered * sizeof(double));
        memcpy(sorted_filt_tp, out->filtered_tp_mbs, out->n_filtered * sizeof(double));
        qsort(sorted_filt_time, out->n_filtered, sizeof(double), compare_doubles);
        qsort(sorted_filt_tp, out->n_filtered, sizeof(double), compare_doubles);

        int nf = out->n_filtered;
        if (nf % 2 == 0) {
            out->filtered_median_sec    = (sorted_filt_time[nf / 2 - 1] + sorted_filt_time[nf / 2]) / 2.0;
            out->filtered_median_tp_mbs = (sorted_filt_tp[nf / 2 - 1] + sorted_filt_tp[nf / 2]) / 2.0;
        } else {
            out->filtered_median_sec    = sorted_filt_time[nf / 2];
            out->filtered_median_tp_mbs = sorted_filt_tp[nf / 2];
        }

        out->filtered_q1_tp_mbs = sorted_filt_tp[nf / 4];
        out->filtered_q3_tp_mbs = sorted_filt_tp[(nf * 3) / 4];
        out->filtered_iqr_tp_mbs = out->filtered_q3_tp_mbs - out->filtered_q1_tp_mbs;

        double sum_sq = 0.0;
        for (int i = 0; i < out->n_filtered; i++) {
            double d = out->filtered_samples[i] - out->filtered_mean_sec;
            sum_sq += d * d;
        }
        out->filtered_stddev_sec = sqrt(sum_sq / (double)out->n_filtered);
    } else {
        out->filtered_mean_sec = out->raw_mean_sec;
        out->filtered_median_sec = out->raw_median_sec;
        out->filtered_median_tp_mbs = out->raw_median_tp_mbs;
        out->filtered_iqr_tp_mbs = out->raw_iqr_tp_mbs;
        out->filtered_stddev_sec = 0.0;
    }
}

/* ========================================================================= */
/* Generador Pseudoaleatorio & Aleatorizacion de Fisher-Yates (Lemire Unbiased)*/
/* ========================================================================= */

typedef struct {
    uint32_t state;
} rng_t;

static rng_t g_suite_rng;

static inline void rng_init(rng_t *rng, uint32_t seed) {
    rng->state = seed;
}

static inline uint32_t rng_next(rng_t *rng) {
    rng->state = rng->state * 1664525u + 1013904223u;
    return rng->state;
}

/* Lemire's nearly divisionless unbiased bounded integer reduction.
 * ACM Transactions on Mathematical Software, 2019.
 * Uses 64-bit multiplication: (uint64_t)x * bound, taking upper 32 bits (m >> 32).
 * Rejection sampling on the residual interval t = (-bound) % bound eliminates modulo bias,
 * ensuring each integer in [0, bound-1] has an identical preimage count under uniform input.
 * Mathematical note on LCG properties:
 * 1. State Periodicity: The 32-bit state possesses a full period of 2^32 under the Hull-Dobell Theorem,
 *    meaning any seed (including seed 0) visits all 2^32 states.
 * 2. Low-bit Periodicity: Individual low bits exhibit short periods (bit k has period at most 2^(k+1)),
 *    with the LSB alternating strictly with period 2.
 * 3. Role of Lemire Reduction: By extracting (m >> 32) rather than taking (x % bound), Lemire avoids
 *    using the LSB directly as the output index and eliminates division modulo bias. However, because
 *    multiplication propagates low-order bits into high-order bits via carries, Lemire does NOT eliminate
 *    the underlying statistical correlations of the LCG. Absence of reduction bias != statistical independence.
 */
static inline int rng_next_unbiased_lemire(rng_t *rng, int bound) {
    if (bound <= 1) return 0;
    uint32_t b = (uint32_t)bound;
    uint32_t x = rng_next(rng);
    uint64_t m = (uint64_t)x * (uint64_t)b;
    uint32_t l = (uint32_t)m;
    if (l < b) {
        uint32_t t = -b % b;
        while (l < t) {
            x = rng_next(rng);
            m = (uint64_t)x * (uint64_t)b;
            l = (uint32_t)m;
        }
    }
    return (int)(m >> 32);
}

/* ========================================================================= */
/* Modelo de Resultados IPC (Memoria Aislada por Subproceso)                 */
/* ========================================================================= */

typedef struct {
    size_t orig_sz;
    size_t cmp_sz;
    double ratio_pct;
    int has_comp_measurement; /* 1 if compression was timed, 0 for Strict CRC32 */
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
/* Pre-flight Checksum Rejection para Zstd Strict Checksum                   */
/* ========================================================================= */

static int pre_flight_validate_zstd_checksum_rejection(void) {
    printf("[PRE-FLIGHT] Validating Zstd Strict Checksum Flag and Corrupted Frame Rejection...\n");
    const size_t test_sz = 16384;
    uint8_t *src = (uint8_t*)malloc(test_sz);
    uint8_t *cmp = (uint8_t*)malloc(test_sz * 2 + 65536);
    uint8_t *dec = (uint8_t*)malloc(test_sz);
    if (!src || !cmp || !dec) {
        free(src); free(cmp); free(dec);
        return 0;
    }
    for (size_t i = 0; i < test_sz; i++) {
        src[i] = (uint8_t)((i * 37 + 13) & 0xFF);
    }

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!cctx || !dctx) {
        if (cctx) ZSTD_freeCCtx(cctx);
        if (dctx) ZSTD_freeDCtx(dctx);
        free(src); free(cmp); free(dec);
        return 0;
    }

    ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);
    size_t c_sz = ZSTD_compressCCtx(cctx, cmp, test_sz * 2 + 65536, src, test_sz, 1);
    if (ZSTD_isError(c_sz) || c_sz < 5) {
        ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
        free(src); free(cmp); free(dec);
        return 0;
    }

    /* Verify Content_Checksum_flag in Frame_Header_Descriptor (Byte 4, bit 2) */
    if ((cmp[4] & 0x04) == 0) {
        fprintf(stderr, "  [FAIL] Zstd Frame header missing Content_Checksum_flag (byte 4 = 0x%02X)\n", cmp[4]);
        ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
        free(src); free(cmp); free(dec);
        return 0;
    }

    /* Normal decompression succeeds */
    size_t dec_sz = ZSTD_decompressDCtx(dctx, dec, test_sz, cmp, c_sz);
    if (ZSTD_isError(dec_sz) || dec_sz != test_sz || memcmp(dec, src, test_sz) != 0) {
        fprintf(stderr, "  [FAIL] Zstd decomp failed on valid stream\n");
        ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
        free(src); free(cmp); free(dec);
        return 0;
    }

    /* Corrupt the 4-byte xxHash64 Content_Checksum at the frame trailer (last byte of stream).
     * This keeps the compressed payload blocks intact while forcing the frame epilogue
     * verification to fail specifically with ZSTD_error_checksum_wrong.
     */
    cmp[c_sz - 1] ^= 0x55;

    size_t corrupt_ret = ZSTD_decompressDCtx(dctx, dec, test_sz, cmp, c_sz);
    if (!ZSTD_isError(corrupt_ret)) {
        fprintf(stderr, "  [FAIL] Zstd silently accepted corrupted payload with checksumFlag=1!\n");
        ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
        free(src); free(cmp); free(dec);
        return 0;
    }

    if (ZSTD_getErrorCode(corrupt_ret) != ZSTD_error_checksum_wrong) {
        fprintf(stderr, "  [FAIL] Zstd failed with error '%s' instead of expected ZSTD_error_checksum_wrong!\n",
                ZSTD_getErrorName(corrupt_ret));
        ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
        free(src); free(cmp); free(dec);
        return 0;
    }

    printf("  [PASS] Zstd Strict Checksum: Frame flag 0x04 verified, corrupted frame specifically rejected with ZSTD_error_checksum_wrong (%s)\n",
           ZSTD_getErrorName(corrupt_ret));

    ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
    free(src); free(cmp); free(dec);
    return 1;
}

/* ========================================================================= */
/* Medicion de Latencias de Bloques (4 KiB y 64 KiB) con Chequeo de Errores  */
/* ========================================================================= */

static int measure_latencies_generic(
    const uint8_t *src, size_t src_sz,
    size_t block_sz,
    int is_zstd, int zstd_level,
    int is_binpack, const binpack_options_t *bp_opt,
    double *out_comp_us, double *out_decomp_us
) {
    if (src_sz < block_sz) {
        *out_comp_us = 0.0;
        *out_decomp_us = 0.0;
        return 0;
    }

    int n_blocks = (int)(src_sz / block_sz);
    if (n_blocks > 32) n_blocks = 32;
    if (n_blocks <= 0) return 0;

    uint8_t *cbuf = (uint8_t*)malloc(block_sz * 2 + 65536);
    uint8_t *dbuf = (uint8_t*)malloc(block_sz * 2 + 65536);
    if (!cbuf || !dbuf) {
        free(cbuf); free(dbuf);
        return -1;
    }
    double c_times[32], d_times[32];

    ZSTD_CCtx *zcctx = NULL;
    ZSTD_DCtx *zdctx = NULL;
    if (is_zstd) {
        zcctx = ZSTD_createCCtx();
        zdctx = ZSTD_createDCtx();
        if (!zcctx || !zdctx) {
            if (zcctx) ZSTD_freeCCtx(zcctx);
            if (zdctx) ZSTD_freeDCtx(zdctx);
            free(cbuf); free(dbuf);
            return -1;
        }
        ZSTD_CCtx_setParameter(zcctx, ZSTD_c_checksumFlag, 1);
    }

    int success = 1;
    for (int b = 0; b < n_blocks; b++) {
        const uint8_t *blk = src + (b * block_sz);
        size_t c_sz = 0;

        uint64_t t0 = timer_now_ns();
        if (is_zstd) {
            size_t rst = ZSTD_CCtx_reset(zcctx, ZSTD_reset_session_only);
            if (ZSTD_isError(rst)) { success = 0; break; }
            c_sz = ZSTD_compressCCtx(zcctx, cbuf, block_sz * 2, blk, block_sz, zstd_level);
            if (ZSTD_isError(c_sz)) { success = 0; break; }
        } else if (is_binpack) {
            binpack_status_t st = binpack_compress_into(blk, block_sz, cbuf, block_sz * 2, &c_sz, bp_opt);
            if (st != BINPACK_OK || c_sz == 0) { success = 0; break; }
        } else {
            /* LZ4 */
            c_sz = (size_t)LZ4_compress_default((const char*)blk, (char*)cbuf, (int)block_sz, (int)(block_sz * 2));
            if (c_sz == 0) { success = 0; break; }
        }
        uint64_t t1 = timer_now_ns();
        c_times[b] = (double)(t1 - t0) / 1000.0;

        size_t dec_sz = 0;
        uint64_t t2 = timer_now_ns();
        if (is_zstd) {
            dec_sz = ZSTD_decompressDCtx(zdctx, dbuf, block_sz, cbuf, c_sz);
            if (ZSTD_isError(dec_sz) || dec_sz != block_sz) { success = 0; break; }
        } else if (is_binpack) {
            binpack_status_t st = binpack_decompress_into(cbuf, c_sz, dbuf, block_sz, &dec_sz, 0);
            if (st != BINPACK_OK || dec_sz != block_sz) { success = 0; break; }
        } else {
            int d_ret = LZ4_decompress_safe((const char*)cbuf, (char*)dbuf, (int)c_sz, (int)block_sz);
            if (d_ret != (int)block_sz) { success = 0; break; }
            dec_sz = (size_t)d_ret;
        }
        uint64_t t3 = timer_now_ns();
        d_times[b] = (double)(t3 - t2) / 1000.0;

        if (memcmp(dbuf, blk, block_sz) != 0) {
            success = 0;
            break;
        }
    }

    if (success) {
        qsort(c_times, n_blocks, sizeof(double), compare_doubles);
        qsort(d_times, n_blocks, sizeof(double), compare_doubles);
        *out_comp_us = c_times[n_blocks / 2];
        *out_decomp_us = d_times[n_blocks / 2];
    } else {
        *out_comp_us = 0.0;
        *out_decomp_us = 0.0;
    }

    if (zcctx) ZSTD_freeCCtx(zcctx);
    if (zdctx) ZSTD_freeDCtx(zdctx);
    free(cbuf); free(dbuf);
    return success ? 0 : -1;
}

/* ========================================================================= */
/* Implementaciones de Evaluadores por Motor                                 */
/* ========================================================================= */

/* 1. LZ4 Fast (Raw Block API, preallocated buffers) */
static int run_lz4_fast(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    /* Uniform Warm-up */
    c_sz = (size_t)LZ4_compress_default((const char*)src, (char*)comp_buf, (int)src_sz, (int)max_buf_sz);
    if (c_sz == 0) all_pass = 0;
    int d_ret = LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)src_sz);
    if (d_ret != (int)src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        c_sz = (size_t)LZ4_compress_default((const char*)src, (char*)comp_buf, (int)src_sz, (int)max_buf_sz);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (c_sz == 0) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        d_ret = LZ4_decompress_safe((const char*)comp_buf, (char*)decomp_buf, (int)c_sz, (int)src_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (d_ret != (int)src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    int lat_ret1 = measure_latencies_generic(src, src_sz, 4096, 0, 0, 0, NULL, &res->lat_4k_comp_us, &res->lat_4k_decomp_us);
    int lat_ret2 = measure_latencies_generic(src, src_sz, 65536, 0, 0, 0, NULL, &res->lat_64k_comp_us, &res->lat_64k_decomp_us);
    if (lat_ret1 != 0 || lat_ret2 != 0) {
        all_pass = 0;
    }
    res->integrity_all_pass = all_pass;

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 2. Zstandard Level 1 (Framed Container, preallocated buffers) */
static int run_zstd_l1(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!cctx || !dctx) {
        if (cctx) ZSTD_freeCCtx(cctx);
        if (dctx) ZSTD_freeDCtx(dctx);
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);

    /* Uniform Warm-up */
    size_t rst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    if (ZSTD_isError(rst)) all_pass = 0;
    c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 1);
    if (ZSTD_isError(c_sz) || (c_sz > 4 && (comp_buf[4] & 0x04) == 0)) all_pass = 0;
    size_t dec_ret = ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
    if (ZSTD_isError(dec_ret) || dec_ret != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        rst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
        if (ZSTD_isError(rst)) all_pass = 0;
        uint64_t t0 = timer_now_ns();
        c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 1);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (ZSTD_isError(c_sz) || (c_sz > 4 && (comp_buf[4] & 0x04) == 0)) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        dec_ret = ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (ZSTD_isError(dec_ret) || dec_ret != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    int lat_ret1 = measure_latencies_generic(src, src_sz, 4096, 1, 1, 0, NULL, &res->lat_4k_comp_us, &res->lat_4k_decomp_us);
    int lat_ret2 = measure_latencies_generic(src, src_sz, 65536, 1, 1, 0, NULL, &res->lat_64k_comp_us, &res->lat_64k_decomp_us);
    if (lat_ret1 != 0 || lat_ret2 != 0) {
        all_pass = 0;
    }
    res->integrity_all_pass = all_pass;

    ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 3. BinPack Fast Zero-CRC (Algorithmic Core, preallocated buffer) */
static int run_binpack_fast_zerocrc(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.nb_workers = 1;
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    /* Uniform Warm-up */
    binpack_status_t st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    if (st != BINPACK_OK) all_pass = 0;
    size_t dec_sz = 0;
    st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 0);
    if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (st != BINPACK_OK) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 0 /* Zero-CRC */);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    int lat_ret1 = measure_latencies_generic(src, src_sz, 4096, 0, 0, 1, &opt, &res->lat_4k_comp_us, &res->lat_4k_decomp_us);
    int lat_ret2 = measure_latencies_generic(src, src_sz, 65536, 0, 0, 1, &opt, &res->lat_64k_comp_us, &res->lat_64k_decomp_us);
    if (lat_ret1 != 0 || lat_ret2 != 0) {
        all_pass = 0;
    }
    res->integrity_all_pass = all_pass;

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 4. BinPack Strict CRC32 (Decompression Verification Path — Compression marked N/A) */
static int run_binpack_strict_crc32(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.nb_workers = 1;
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    /* Produce payload once outside the timing window (Compression is N/A) */
    binpack_status_t st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    if (st != BINPACK_OK) all_pass = 0;

    /* Uniform Warm-up of decompression */
    size_t dec_sz = 0;
    st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    /* Timed 20 iterations of decompression under strict CRC32 */
    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1 /* Strict CRC32 */);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 0; /* Compression is explicitly N/A! No fake sentinels! */
    res->integrity_all_pass = all_pass;
    memset(&res->c_stat, 0, sizeof(res->c_stat));
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 5. BinPack Convenience API (Measures allocation overhead of binpack_compress_buffer) */
static int run_binpack_convenience_api(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!decomp_buf) return -1;
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    void *out_dst = NULL;
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.nb_workers = 1;
    opt.mode = BINPACK_MODE_RUNTIME;
    opt.clevel = 1;

    /* Warm-up */
    binpack_status_t st = binpack_compress_buffer(src, src_sz, &out_dst, &c_sz, &opt);
    if (st != BINPACK_OK || !out_dst) all_pass = 0;
    if (out_dst) {
        size_t dec_sz = 0;
        st = binpack_decompress_into(out_dst, c_sz, decomp_buf, src_sz, &dec_sz, 0);
        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;
        free(out_dst); out_dst = NULL;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_compress_buffer(src, src_sz, &out_dst, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (st != BINPACK_OK || !out_dst) all_pass = 0;
        if (i < BENCH_ITERS - 1 && out_dst) { free(out_dst); out_dst = NULL; }
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        size_t dec_sz = 0;
        uint64_t t0 = timer_now_ns();
        st = binpack_decompress_into(out_dst, c_sz, decomp_buf, src_sz, &dec_sz, 0);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    if (out_dst) free(out_dst);
    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 6. Zstandard Level 19 (Tier 2 Balanced Density) */
static int run_zstd_l19(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    ZSTD_DCtx *dctx = ZSTD_createDCtx();
    if (!cctx || !dctx) {
        if (cctx) ZSTD_freeCCtx(cctx);
        if (dctx) ZSTD_freeDCtx(dctx);
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_compressionLevel, 19);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_windowLog, 25);
    ZSTD_CCtx_setParameter(cctx, ZSTD_c_checksumFlag, 1);

    /* Warm-up */
    size_t rst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    if (ZSTD_isError(rst)) all_pass = 0;
    c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 19);
    if (ZSTD_isError(c_sz) || (c_sz > 4 && (comp_buf[4] & 0x04) == 0)) all_pass = 0;
    size_t dec_ret = ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
    if (ZSTD_isError(dec_ret) || dec_ret != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        rst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
        if (ZSTD_isError(rst)) all_pass = 0;
        uint64_t t0 = timer_now_ns();
        c_sz = ZSTD_compressCCtx(cctx, comp_buf, max_buf_sz, src, src_sz, 19);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (ZSTD_isError(c_sz) || (c_sz > 4 && (comp_buf[4] & 0x04) == 0)) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        dec_ret = ZSTD_decompressDCtx(dctx, decomp_buf, src_sz, comp_buf, c_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (ZSTD_isError(dec_ret) || dec_ret != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    ZSTD_freeCCtx(cctx); ZSTD_freeDCtx(dctx);
    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 7. BinPack Default Level 19 (Tier 2 Balanced Density) */
static int run_binpack_default_l19(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.nb_workers = 1;
    opt.mode = BINPACK_MODE_DEFAULT;
    opt.clevel = 19;

    /* Warm-up */
    binpack_status_t st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    if (st != BINPACK_OK) all_pass = 0;
    size_t dec_sz = 0;
    st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (st != BINPACK_OK) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 8. Tier 3: Ruta A — 7-Zip LZMA1 Puro */
static int run_tier3_ruta_a(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    size_t props_sz = LZMA_PROPS_SIZE;
    uint8_t props[LZMA_PROPS_SIZE];

    /* Warm-up */
    size_t dst_len = max_buf_sz - LZMA_PROPS_SIZE;
    int lz_ret = LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, src, src_sz,
                              props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
    if (lz_ret != SZ_OK) all_pass = 0;
    c_sz = dst_len + LZMA_PROPS_SIZE;
    size_t uncmp_len = src_sz;
    size_t src_processed = dst_len;
    lz_ret = LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                            props, props_sz);
    if (lz_ret != SZ_OK || uncmp_len != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        dst_len = max_buf_sz - LZMA_PROPS_SIZE;
        props_sz = LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        lz_ret = LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, src, src_sz,
                              props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        c_sz = dst_len + LZMA_PROPS_SIZE;
        if (lz_ret != SZ_OK) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uncmp_len = src_sz;
        src_processed = c_sz - LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        lz_ret = LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                                props, props_sz);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (lz_ret != SZ_OK || uncmp_len != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 9. Tier 3: Ruta B — Pavlov BCJ ARM Filter + 7-Zip LZMA1 */
static int run_tier3_ruta_b(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *bcj_work = (uint8_t*)malloc(max_buf_sz);
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!bcj_work || !comp_buf || !decomp_buf) {
        free(bcj_work); free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    size_t props_sz = LZMA_PROPS_SIZE;
    uint8_t props[LZMA_PROPS_SIZE];

    /* Warm-up */
    memcpy(bcj_work, src, src_sz);
    ARM_Convert(bcj_work, src_sz, 0, 1 /* forward encoding */);
    size_t dst_len = max_buf_sz - LZMA_PROPS_SIZE;
    int lz_ret = LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, bcj_work, src_sz,
                              props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
    if (lz_ret != SZ_OK) all_pass = 0;
    c_sz = dst_len + LZMA_PROPS_SIZE;
    size_t uncmp_len = src_sz;
    size_t src_processed = dst_len;
    lz_ret = LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                            props, props_sz);
    ARM_Convert(decomp_buf, src_sz, 0, 0 /* inverse decoding */);
    if (lz_ret != SZ_OK || uncmp_len != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        memcpy(bcj_work, src, src_sz);
        dst_len = max_buf_sz - LZMA_PROPS_SIZE;
        props_sz = LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        ARM_Convert(bcj_work, src_sz, 0, 1);
        lz_ret = LzmaCompress(comp_buf + LZMA_PROPS_SIZE, &dst_len, bcj_work, src_sz,
                              props, &props_sz, 9, 1 << 25, 3, 0, 2, 32, 1);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        c_sz = dst_len + LZMA_PROPS_SIZE;
        if (lz_ret != SZ_OK) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uncmp_len = src_sz;
        src_processed = c_sz - LZMA_PROPS_SIZE;
        uint64_t t0 = timer_now_ns();
        lz_ret = LzmaUncompress(decomp_buf, &uncmp_len, comp_buf + LZMA_PROPS_SIZE, &src_processed,
                                props, props_sz);
        ARM_Convert(decomp_buf, src_sz, 0, 0);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (lz_ret != SZ_OK || uncmp_len != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    free(bcj_work); free(comp_buf); free(decomp_buf);
    return all_pass ? 0 : -1;
}

/* 10. Tier 3: Ruta C — BinPack Archival Core (BINPACK_MODE_ARCHIVAL) */
static int run_tier3_ruta_c(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.nb_workers = 1;
    opt.mode = BINPACK_MODE_ARCHIVAL;
    opt.clevel = 19;

    /* Warm-up */
    binpack_status_t st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    if (st != BINPACK_OK) all_pass = 0;
    size_t dec_sz = 0;
    st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (st != BINPACK_OK) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

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
    if (!comp_buf || !dec_f) {
        free(comp_buf); free(dec_f);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    /* Warm-up */
    c_sz = gorilla_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
    if (c_sz == 0) all_pass = 0;
    gorilla_decompress_f32(comp_buf, c_sz, dec_f, n_floats);
    if (memcmp(dec_f, src_f, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        c_sz = gorilla_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (c_sz == 0) all_pass = 0;
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
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

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
    if (!comp_buf || !dec_f) {
        free(comp_buf); free(dec_f);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    /* Warm-up */
    c_sz = chimp_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
    if (c_sz == 0) all_pass = 0;
    chimp_decompress_f32(comp_buf, c_sz, dec_f, n_floats);
    if (memcmp(dec_f, src_f, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        c_sz = chimp_compress_f32(src_f, n_floats, comp_buf, max_buf_sz);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (c_sz == 0) all_pass = 0;
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
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

    free(comp_buf); free(dec_f);
    return all_pass ? 0 : -1;
}

/* 13. Tier 4A: BinPack Transpose-F32 */
static int run_binpack_transpose_f32(const uint8_t *src, size_t src_sz, bench_result_t *res) {
    size_t max_buf_sz = src_sz * 2 + 65536;
    uint8_t *comp_buf = (uint8_t*)malloc(max_buf_sz);
    uint8_t *decomp_buf = (uint8_t*)malloc(max_buf_sz);
    if (!comp_buf || !decomp_buf) {
        free(comp_buf); free(decomp_buf);
        return -1;
    }
    double c_samples[BENCH_ITERS], d_samples[BENCH_ITERS];
    size_t c_sz = 0;
    int all_pass = 1;

    binpack_options_t opt;
    binpack_default_options(&opt);
    opt.nb_workers = 1;
    opt.mode = BINPACK_MODE_DEFAULT;
    opt.filter = BINPACK_FILTER_TRANSPOSE_DELTA;
    opt.transpose_stride = 4;
    opt.clevel = 19;

    /* Warm-up */
    binpack_status_t st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
    if (st != BINPACK_OK) all_pass = 0;
    size_t dec_sz = 0;
    st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
    if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) all_pass = 0;

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_compress_into(src, src_sz, comp_buf, max_buf_sz, &c_sz, &opt);
        uint64_t t1 = timer_now_ns();
        c_samples[i] = (double)(t1 - t0) / 1e9;
        if (st != BINPACK_OK) all_pass = 0;
    }

    for (int i = 0; i < BENCH_ITERS; i++) {
        uint64_t t0 = timer_now_ns();
        st = binpack_decompress_into(comp_buf, c_sz, decomp_buf, src_sz, &dec_sz, 1);
        uint64_t t1 = timer_now_ns();
        d_samples[i] = (double)(t1 - t0) / 1e9;

        if (st != BINPACK_OK || dec_sz != src_sz || memcmp(decomp_buf, src, src_sz) != 0) {
            all_pass = 0;
        }
    }

    res->orig_sz = src_sz;
    res->cmp_sz = c_sz;
    res->ratio_pct = (double)c_sz / (double)src_sz * 100.0;
    res->has_comp_measurement = 1;
    res->integrity_all_pass = all_pass;
    calc_stats(c_samples, BENCH_ITERS, src_sz, &res->c_stat);
    calc_stats(d_samples, BENCH_ITERS, src_sz, &res->d_stat);

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
        fprintf(stderr, "[FATAL] mmap failed for IPC: %s\n", strerror(errno));
        return -1;
    }
    memset(shm, 0, sizeof(bench_result_t));

    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "[FATAL] fork failed: %s\n", strerror(errno));
        munmap(shm, sizeof(bench_result_t));
        return -1;
    }

    if (pid == 0) {
        /* Child Process: Set CPU Affinity and verify */
        cpu_set_t cpuset;
        CPU_ZERO(&cpuset);
        CPU_SET(aff->logical_id, &cpuset);
        if (sched_setaffinity(0, sizeof(cpu_set_t), &cpuset) != 0) {
            fprintf(stderr, "[CHILD ERROR] sched_setaffinity failed: %s\n", strerror(errno));
            _exit(2);
        }
        cpu_set_t verify_set;
        CPU_ZERO(&verify_set);
        if (sched_getaffinity(0, sizeof(cpu_set_t), &verify_set) != 0 ||
            !CPU_ISSET(aff->logical_id, &verify_set) ||
            CPU_COUNT(&verify_set) != 1) {
            fprintf(stderr, "[CHILD ERROR] sched_getaffinity verification failed in child\n");
            _exit(3);
        }
        sched_yield();

        /* Verify that child process execution has passed topology/affinity gate */
        if (!aff->topology_gate_passed) {
            fprintf(stderr, "[CHILD ERROR] Child running without verified topology gate\n");
            _exit(4);
        }

        /* Execute benchmark function and propagate exit code */
        int run_ret = cfg->run_fn(src, src_sz, shm);
        if (run_ret != 0 || !shm->integrity_all_pass) {
            _exit(1);
        }
        _exit(0);
    }

    /* Parent Process: wait for child and check status */
    int status = 0;
    struct rusage ru;
    memset(&ru, 0, sizeof(ru));
    pid_t w = wait4(pid, &status, 0, &ru);
    if (w < 0) {
        fprintf(stderr, "[FATAL] wait4 failed: %s\n", strerror(errno));
        munmap(shm, sizeof(bench_result_t));
        return -1;
    }

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        fprintf(stderr, "[FATAL] Engine '%s' child process failed (exit status %d, signal %d)\n",
                cfg->engine_id, WEXITSTATUS(status), WIFSIGNALED(status) ? WTERMSIG(status) : 0);
        munmap(shm, sizeof(bench_result_t));
        return -1;
    }

    if (!shm->integrity_all_pass) {
        fprintf(stderr, "[FATAL] Engine '%s' failed bit-exact integrity checks\n", cfg->engine_id);
        munmap(shm, sizeof(bench_result_t));
        return -1;
    }

    shm->peak_rss_kb = (size_t)ru.ru_maxrss; /* Kernel-measured isolated RSS of child process */
    memcpy(out_res, shm, sizeof(bench_result_t));
    munmap(shm, sizeof(bench_result_t));
    return 0;
#else
    /* Windows in-process execution with memory tracking */
    int ret = cfg->run_fn(src, src_sz, out_res);
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        out_res->peak_rss_kb = pmc.PeakWorkingSetSize / 1024;
    }
    return ret;
#endif
}

/* ========================================================================= */
/* Registro en Archivo CSV Crudo (BP-008D-R7 Campana Decisiva)               */
/* ========================================================================= */

static FILE *g_csv_file = NULL;
static char g_csv_path[512] = {0};

/* Result code for atomic exclusive CSV opening */
typedef enum {
    CSV_OPEN_SUCCESS = 0,
    CSV_OPEN_COLLISION_EEXIST, /* File already exists: safe collision, try next suffix */
    CSV_OPEN_FATAL_ERROR       /* Permission denied, path not found, fd exhaustion, etc. */
} csv_open_result_t;

/* Kernel-level atomic exclusive file creation: eliminates TOCTOU race conditions.
 * Uses O_CREAT | O_EXCL (POSIX) and _O_CREAT | _O_EXCL (Windows).
 * - Returns NULL with CSV_OPEN_COLLISION_EEXIST if the file already exists, leaving it untouched.
 * - If fdopen/_fdopen fails, the descriptor is closed and the newly created empty file is unlinked.
 * - Distinguishes EEXIST from other operational errors (EACCES, ENOENT, EMFILE, ENFILE).
 */
static FILE* open_atomic_exclusive_csv(const char *path, csv_open_result_t *out_res) {
    int saved_errno = 0;
#if defined(_WIN32)
    int fd = _open(path, _O_WRONLY | _O_CREAT | _O_EXCL, _S_IREAD | _S_IWRITE);
    if (fd < 0) {
        saved_errno = errno;
        if (saved_errno == EEXIST) {
            if (out_res) *out_res = CSV_OPEN_COLLISION_EEXIST;
        } else {
            if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
            fprintf(stderr, "[WARN] Atomic CSV create failed on '%s': errno=%d (%s)\n",
                    path, saved_errno, strerror(saved_errno));
        }
        return NULL;
    }
    FILE *f = _fdopen(fd, "w");
    if (!f) {
        saved_errno = errno;
        _close(fd);
        _unlink(path); /* Clean up allocated empty file to prevent dangling remnants */
        if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
        fprintf(stderr, "[ERROR] _fdopen failed on '%s' (fd=%d): errno=%d (%s)\n",
                path, fd, saved_errno, strerror(saved_errno));
        return NULL;
    }
    if (out_res) *out_res = CSV_OPEN_SUCCESS;
    return f;
#else
    int fd = open(path, O_WRONLY | O_CREAT | O_EXCL, 0644);
    if (fd < 0) {
        saved_errno = errno;
        if (saved_errno == EEXIST) {
            if (out_res) *out_res = CSV_OPEN_COLLISION_EEXIST;
        } else {
            if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
            fprintf(stderr, "[WARN] Atomic CSV create failed on '%s': errno=%d (%s)\n",
                    path, saved_errno, strerror(saved_errno));
        }
        return NULL;
    }
    FILE *f = fdopen(fd, "w");
    if (!f) {
        saved_errno = errno;
        close(fd);
        unlink(path); /* Clean up allocated empty file to prevent dangling remnants */
        if (out_res) *out_res = CSV_OPEN_FATAL_ERROR;
        fprintf(stderr, "[ERROR] fdopen failed on '%s' (fd=%d): errno=%d (%s)\n",
                path, fd, saved_errno, strerror(saved_errno));
        return NULL;
    }
    if (out_res) *out_res = CSV_OPEN_SUCCESS;
    return f;
#endif
}

static FILE* open_safe_csv_r5(char *out_path, size_t out_path_sz) {
    const char *base_dirs[] = { "docs/audit", "." };
    for (int d = 0; d < 2; d++) {
        char target[512];
        csv_open_result_t res = CSV_OPEN_FATAL_ERROR;
        snprintf(target, sizeof(target), "%s/decisive_benchmark_raw_r5.csv", base_dirs[d]);
        FILE *f = open_atomic_exclusive_csv(target, &res);
        if (f) {
            if (out_path) snprintf(out_path, out_path_sz, "%s", target);
            return f;
        }
        /* Only probe sequential run suffixes if the failure was a legitimate file existence collision */
        if (res == CSV_OPEN_COLLISION_EEXIST) {
            for (int run = 1; run < 1000; run++) {
                snprintf(target, sizeof(target), "%s/decisive_benchmark_raw_r5_run%d.csv", base_dirs[d], run);
                f = open_atomic_exclusive_csv(target, &res);
                if (f) {
                    if (out_path) snprintf(out_path, out_path_sz, "%s", target);
                    return f;
                }
                if (res != CSV_OPEN_COLLISION_EEXIST) {
                    /* Non-collision error (permissions, fd exhaustion, etc.): do not iterate further */
                    break;
                }
            }
        }
        /* If directory did not exist (e.g. ENOENT on docs/audit), loop continues to try current directory '.' */
    }
    return NULL;
}

static void log_csv_record_r5(
    const char *tier, const char *dataset, const char *engine,
    const bench_result_t *res, uint32_t seed
) {
    if (!g_csv_file) return;

    if (res->has_comp_measurement) {
        fprintf(g_csv_file,
                "%s,%s,%s,%zu,%zu,%.3f,"
                "%.2f,%.2f,%.2f,%d,"
                "%.2f,%.2f,%.2f,%d,"
                "%.4f,%.4f,"
                "%.2f,%.2f,%.2f,%.2f,"
                "%zu,%s,%u",
                tier, dataset, engine, res->orig_sz, res->cmp_sz, res->ratio_pct,
                res->c_stat.raw_median_tp_mbs, res->c_stat.filtered_median_tp_mbs,
                res->c_stat.filtered_iqr_tp_mbs, res->c_stat.outliers_count,
                res->d_stat.raw_median_tp_mbs, res->d_stat.filtered_median_tp_mbs,
                res->d_stat.filtered_iqr_tp_mbs, res->d_stat.outliers_count,
                res->c_stat.filtered_median_sec * 1000.0, res->d_stat.filtered_median_sec * 1000.0,
                res->lat_4k_comp_us, res->lat_4k_decomp_us,
                res->lat_64k_comp_us, res->lat_64k_decomp_us,
                res->peak_rss_kb, res->integrity_all_pass ? "PASS" : "FAIL", seed);
    } else {
        /* Compression is explicitly N/A (Strict CRC32 decompression evaluation) */
        fprintf(g_csv_file,
                "%s,%s,%s,%zu,%zu,%.3f,"
                "N/A,N/A,N/A,0,"
                "%.2f,%.2f,%.2f,%d,"
                "N/A,%.4f,"
                "N/A,%.2f,N/A,%.2f,"
                "%zu,%s,%u",
                tier, dataset, engine, res->orig_sz, res->cmp_sz, res->ratio_pct,
                res->d_stat.raw_median_tp_mbs, res->d_stat.filtered_median_tp_mbs,
                res->d_stat.filtered_iqr_tp_mbs, res->d_stat.outliers_count,
                res->d_stat.filtered_median_sec * 1000.0,
                res->lat_4k_decomp_us, res->lat_64k_decomp_us,
                res->peak_rss_kb, res->integrity_all_pass ? "PASS" : "FAIL", seed);
    }

    /* Export 20 raw samples for compression */
    for (int i = 0; i < BENCH_ITERS; i++) {
        fprintf(g_csv_file, ",%.6f", res->has_comp_measurement ? res->c_stat.raw_samples[i] : 0.0);
    }
    /* Export 20 raw samples for decompression */
    for (int i = 0; i < BENCH_ITERS; i++) {
        fprintf(g_csv_file, ",%.6f", res->d_stat.raw_samples[i]);
    }
    fprintf(g_csv_file, "\n");
    fflush(g_csv_file);
}

/* ========================================================================= */
/* Punto de Entrada Principal (BP-008C-R4)                                   */
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
    printf("BINPACK ENTERPRISE SDK — SCIENTIFIC BENCHMARK & COMPARABILITY PROTOCOL (BP-008C-R6)\n");
    if (aff.core_id >= 0) {
        printf("Hardware Topology: Logical Core %d (Core ID %d) | %s [Physical Verified: %s | Topology Gate: %s]\n",
               aff.logical_id, aff.core_id, aff.description,
               aff.physical_core_verified ? "YES" : "NO (Virtualized/Abstracted)",
               aff.topology_gate_passed ? "PASS" : "FAIL");
    } else {
        printf("Hardware Topology: Logical Core %d (Core ID Unknown) | %s [Physical Verified: %s | Topology Gate: %s]\n",
               aff.logical_id, aff.description,
               aff.physical_core_verified ? "YES" : "NO (Virtualized/Abstracted)",
               aff.topology_gate_passed ? "PASS" : "FAIL");
    }
    printf("Execution Seed: %u (Fisher-Yates Randomized Suite) | Isolated Memory per Subprocess (fork/wait4)\n", seed);
    printf("Runtime Linked Versions: Zstd %s (Num: %u) | LZ4 %s (Num: %u) | BinPack %s\n",
           ZSTD_versionString(), ZSTD_versionNumber(),
           LZ4_versionString(), LZ4_versionNumber(),
           binpack_version_string());
    printf("Protocol: 1 Warm-up + 20 Timed Iterations | Tukey Outlier Rule | Decimal MB/s (10^6 B/s) Directly from Samples\n");
    printf("Zstd Memory Budgets (Official Estimator): L19 CCtx = %.2f MB (CStream = %.2f MB) | Runtime L4 CCtx = %.2f MB (CStream = %.2f MB)\n",
           (double)ZSTD_estimateCCtxSize(19) / (1024.0 * 1024.0),
           (double)ZSTD_estimateCStreamSize(19) / (1024.0 * 1024.0),
           (double)ZSTD_estimateCCtxSize(4) / (1024.0 * 1024.0),
           (double)ZSTD_estimateCStreamSize(4) / (1024.0 * 1024.0));
    printf("==================================================================================================================================\n\n");
    fflush(stdout);

    if (!pre_flight_validate_float_codecs()) {
        fprintf(stderr, "[FATAL] Pre-flight codec validation failed! Aborting.\n");
        return 1;
    }

    if (!pre_flight_validate_zstd_checksum_rejection()) {
        fprintf(stderr, "[FATAL] Pre-flight Zstd strict checksum validation failed! Aborting.\n");
        return 1;
    }

    g_csv_file = open_safe_csv_r5(g_csv_path, sizeof(g_csv_path));
    if (g_csv_file) {
        printf("Output CSV Target: '%s' (Safe non-truncating exclusive creation)\n", g_csv_path);
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

    /* Extract Single Sensor Channel 0 (de-interleaved) from NASA telemetry */
    size_t n_total_floats = sz_tf / sizeof(float);
    size_t n_ch_floats = n_total_floats / 24;
    float *d_tf_ch0 = (float*)malloc(n_ch_floats * sizeof(float));
    const float *all_floats = (const float*)d_tf;
    for (size_t i = 0; i < n_ch_floats; i++) {
        d_tf_ch0[i] = all_floats[i * 24];
    }
    size_t sz_tf_ch0 = n_ch_floats * sizeof(float);

    rng_init(&g_suite_rng, seed);

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

        engine_config_t *shuffled[16];
        for (int i = 0; i < n_arm; i++) shuffled[i] = &engines_arm[i];
        for (int i = n_arm - 1; i > 0; i--) {
            int j = rng_next_unbiased_lemire(&g_suite_rng, i + 1);
            engine_config_t *tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }

        for (int e = 0; e < n_arm; e++) {
            engine_config_t *cur_e = shuffled[e];
            bench_result_t res;
            if (run_engine_isolated(cur_e, d_arm, sz_arm, &aff, &res) != 0) {
                fprintf(stderr, "[FATAL] Execution failed for engine '%s'\n", cur_e->engine_id);
                return 1;
            }

            char comp_str[32];
            if (res.has_comp_measurement) {
                snprintf(comp_str, sizeof(comp_str), "%7.1f ± %-5.1f",
                         res.c_stat.filtered_median_tp_mbs, res.c_stat.filtered_iqr_tp_mbs);
            } else {
                snprintf(comp_str, sizeof(comp_str), "      —         ");
            }

            char lat_str[32];
            snprintf(lat_str, sizeof(lat_str), "%.1f / %.1f", res.lat_4k_comp_us, res.lat_4k_decomp_us);

            printf("%-26s | %7.2f%%  | %-17s | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   comp_str,
                   res.d_stat.filtered_median_tp_mbs, res.d_stat.filtered_iqr_tp_mbs,
                   lat_str, res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r5(cur_e->tier, "busybox_armv7l", cur_e->engine_id, &res, seed);
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
            if (run_engine_isolated(cur_e, d_tf, sz_tf, &aff, &res) != 0) {
                fprintf(stderr, "[FATAL] Execution failed for engine '%s'\n", cur_e->engine_id);
                return 1;
            }

            printf("%-26s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   res.c_stat.filtered_median_tp_mbs, res.c_stat.filtered_iqr_tp_mbs,
                   res.d_stat.filtered_median_tp_mbs, res.d_stat.filtered_iqr_tp_mbs,
                   "—", res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r5(cur_e->tier, "nasa_turbofan_f32", cur_e->engine_id, &res, seed);
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
            if (run_engine_isolated(cur_e, (const uint8_t*)d_tf_ch0, sz_tf_ch0, &aff, &res) != 0) {
                fprintf(stderr, "[FATAL] Execution failed for engine '%s'\n", cur_e->engine_id);
                return 1;
            }

            printf("  %-24s | %7.2f%%  | %7.1f ± %-5.1f   | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   res.c_stat.filtered_median_tp_mbs, res.c_stat.filtered_iqr_tp_mbs,
                   res.d_stat.filtered_median_tp_mbs, res.d_stat.filtered_iqr_tp_mbs,
                   "—", res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r5(cur_e->tier, "nasa_turbofan_ch0", cur_e->engine_id, &res, seed);
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
            int j = rng_next_unbiased_lemire(&g_suite_rng, i + 1);
            engine_config_t *tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }

        for (int e = 0; e < n_moz; e++) {
            engine_config_t *cur_e = shuffled[e];
            bench_result_t res;
            if (run_engine_isolated(cur_e, d_moz, sz_moz, &aff, &res) != 0) {
                fprintf(stderr, "[FATAL] Execution failed for engine '%s'\n", cur_e->engine_id);
                return 1;
            }

            char comp_str[32];
            if (res.has_comp_measurement) {
                snprintf(comp_str, sizeof(comp_str), "%7.1f ± %-5.1f",
                         res.c_stat.filtered_median_tp_mbs, res.c_stat.filtered_iqr_tp_mbs);
            } else {
                snprintf(comp_str, sizeof(comp_str), "      —         ");
            }

            char lat_str[32];
            snprintf(lat_str, sizeof(lat_str), "%.1f / %.1f", res.lat_4k_comp_us, res.lat_4k_decomp_us);

            printf("%-26s | %7.2f%%  | %-17s | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   comp_str,
                   res.d_stat.filtered_median_tp_mbs, res.d_stat.filtered_iqr_tp_mbs,
                   lat_str, res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r5(cur_e->tier, "silesia_mozilla", cur_e->engine_id, &res, seed);
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
            int j = rng_next_unbiased_lemire(&g_suite_rng, i + 1);
            engine_config_t *tmp = shuffled[i];
            shuffled[i] = shuffled[j];
            shuffled[j] = tmp;
        }

        for (int e = 0; e < n_sam; e++) {
            engine_config_t *cur_e = shuffled[e];
            bench_result_t res;
            if (run_engine_isolated(cur_e, d_sam, sz_sam, &aff, &res) != 0) {
                fprintf(stderr, "[FATAL] Execution failed for engine '%s'\n", cur_e->engine_id);
                return 1;
            }

            char comp_str[32];
            if (res.has_comp_measurement) {
                snprintf(comp_str, sizeof(comp_str), "%7.1f ± %-5.1f",
                         res.c_stat.filtered_median_tp_mbs, res.c_stat.filtered_iqr_tp_mbs);
            } else {
                snprintf(comp_str, sizeof(comp_str), "      —         ");
            }

            char lat_str[32];
            snprintf(lat_str, sizeof(lat_str), "%.1f / %.1f", res.lat_4k_comp_us, res.lat_4k_decomp_us);

            printf("%-26s | %7.2f%%  | %-17s | %7.1f ± %-5.1f   | %-12s | %6zu KB | %s\n",
                   cur_e->display_name, res.ratio_pct,
                   comp_str,
                   res.d_stat.filtered_median_tp_mbs, res.d_stat.filtered_iqr_tp_mbs,
                   lat_str, res.peak_rss_kb, res.integrity_all_pass ? "PASS" : "FAIL");
            fflush(stdout);

            log_csv_record_r5(cur_e->tier, "silesia_samba", cur_e->engine_id, &res, seed);
        }
    }

    printf("\n==================================================================================================================================\n");
    printf("[COMPLETE] Benchmark execution finished cleanly. Raw sample data exported to '%s'.\n",
           g_csv_path[0] ? g_csv_path : "decisive_benchmark_raw_r5.csv");
    printf("==================================================================================================================================\n");
    fflush(stdout);

    if (g_csv_file) fclose(g_csv_file);
    free(d_arm); free(d_tf); free(d_tf_ch0); free(d_moz); free(d_sam);
    return 0;
}
