/**
 * @file bp011_benchmark_harness.c
 * @brief Arnes de benchmarking y validacion comercial decisiva BP-011 (Fase C/D/E).
 * @note Gobernanza: Hito BP-011 / Fase C-D. Preserva code freeze en SDK productivo.
 */

#define _CRT_SECURE_NO_WARNINGS
#include "bp011_types.h"
#include "timeseries_bitstream_safe.h"
#include "timeseries_timestamp_codec.h"
#include "gorilla_composed.h"
#include "chimp128_composed.h"
#include "binpack_timeseries_adapter.h"
#include "zstd_adapter.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdint.h>
#include <time.h>
#include <math.h>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

/* ========================================================================= */
/* Definicion Global de Variable de Rastreo de Memoria (Satisface extern)    */
/* ========================================================================= */
bp011_mem_tracker_t g_bp011_mem_tracker = {0, 0, 0};

/* ========================================================================= */
/* Cronometraje de Alta Precision y Diagnostico de Memoria OS               */
/* ========================================================================= */
static inline double bp011_get_time_sec(void) {
#ifdef _WIN32
    LARGE_INTEGER freq, counter;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&counter);
    return (double)counter.QuadPart / (double)freq.QuadPart;
#else
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec * 1e-9;
#endif
}

static inline size_t bp011_get_process_working_set(void) {
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS pmc;
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc))) {
        return (size_t)pmc.WorkingSetSize;
    }
#endif
    return 0;
}

/* ========================================================================= */
/* Cargadores de Corpus Certificados (C-MAPSS y Bearing IMS)                 */
/* ========================================================================= */

/**
 * @brief Carga del dataset NASA C-MAPSS FD001 (20,631 registros, 24 canales).
 */
static int bp011_load_cmapss(
    const char *path,
    int64_t **out_timestamps,
    double **out_matrix,
    uint32_t *out_sample_count,
    uint32_t *out_num_channels,
    bp011_series_segment_t **out_series_dir,
    uint32_t *out_num_series
) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    /* Capacidad inicial: 25000 muestras */
    size_t cap = 25000;
    int64_t *times = (int64_t *)malloc(cap * sizeof(int64_t));
    /* Matriz columnar: 24 canales x 25000 */
    double *mat = (double *)malloc(24 * cap * sizeof(double));
    bp011_series_segment_t *dir = (bp011_series_segment_t *)malloc(200 * sizeof(bp011_series_segment_t));

    if (!times || !mat || !dir) {
        if (times) free(times);
        if (mat) free(mat);
        if (dir) free(dir);
        fclose(f);
        return -2;
    }

    char line[1024];
    uint32_t count = 0;
    uint32_t num_series = 0;
    uint32_t cur_unit = 0;
    uint32_t unit_start_idx = 0;
    uint32_t unit_sample_count = 0;
    uint32_t unit_start_cycle = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') continue;

        uint32_t unit = 0, cycle = 0;
        double s[24];
        int read_n = sscanf(line, "%u %u %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf",
            &unit, &cycle,
            &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6], &s[7],
            &s[8], &s[9], &s[10], &s[11], &s[12], &s[13], &s[14], &s[15],
            &s[16], &s[17], &s[18], &s[19], &s[20], &s[21], &s[22], &s[23]
        );

        if (read_n != 26) continue;

        if (count >= cap) {
            cap *= 2;
            times = (int64_t *)realloc(times, cap * sizeof(int64_t));
            mat = (double *)realloc(mat, 24 * cap * sizeof(double));
        }

        times[count] = (int64_t)cycle;

        /* Almacena en memoria temporal por fila para luego transponer a columnar */
        for (int k = 0; k < 24; k++) {
            mat[count * 24 + k] = s[k];
        }

        if (unit != cur_unit) {
            if (cur_unit > 0) {
                dir[num_series].unit_id = cur_unit;
                dir[num_series].start_sample_idx = unit_start_idx;
                dir[num_series].sample_count = unit_sample_count;
                dir[num_series].start_cycle = unit_start_cycle;
                dir[num_series].time_stream_byte_offset = 0;
                dir[num_series].time_stream_bytes = 0;
                dir[num_series].values_stream_byte_offset = 0;
                dir[num_series].values_stream_bytes = 0;
                num_series++;
            }
            cur_unit = unit;
            unit_start_idx = count;
            unit_sample_count = 1;
            unit_start_cycle = cycle;
        } else {
            unit_sample_count++;
        }

        count++;
    }

    if (cur_unit > 0) {
        dir[num_series].unit_id = cur_unit;
        dir[num_series].start_sample_idx = unit_start_idx;
        dir[num_series].sample_count = unit_sample_count;
        dir[num_series].start_cycle = unit_start_cycle;
        dir[num_series].time_stream_byte_offset = 0;
        dir[num_series].time_stream_bytes = 0;
        dir[num_series].values_stream_byte_offset = 0;
        dir[num_series].values_stream_bytes = 0;
        num_series++;
    }

    fclose(f);

    /* Transposicion a matriz columnar canónica: mat[k * N + i] */
    double *columnar = (double *)malloc(24 * count * sizeof(double));
    if (!columnar) {
        free(times);
        free(mat);
        free(dir);
        return -3;
    }

    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t k = 0; k < 24; k++) {
            columnar[k * count + i] = mat[i * 24 + k];
        }
    }
    free(mat);

    *out_timestamps = times;
    *out_matrix = columnar;
    *out_sample_count = count;
    *out_num_channels = 24;
    *out_series_dir = dir;
    *out_num_series = num_series;
    return 0;
}

/**
 * @brief Carga del dataset NASA Bearing IMS Test 1 (2,156 registros, 48 canales).
 */
static int bp011_load_ims(
    const char *path,
    int64_t **out_timestamps,
    double **out_matrix,
    uint32_t *out_sample_count,
    uint32_t *out_num_channels
) {
    FILE *f = fopen(path, "r");
    if (!f) return -1;

    size_t cap = 3000;
    int64_t *times = (int64_t *)malloc(cap * sizeof(int64_t));
    double *mat = (double *)malloc(48 * cap * sizeof(double));

    if (!times || !mat) {
        if (times) free(times);
        if (mat) free(mat);
        fclose(f);
        return -2;
    }

    char line[4096];
    uint32_t count = 0;

    /* Ignora cabecera CSV */
    if (!fgets(line, sizeof(line), f)) {
        free(times);
        free(mat);
        fclose(f);
        return -3;
    }

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') continue;

        /* Line format: file_index,timestamp,filename,48_doubles... */
        int f_idx = 0;
        int y = 0, m = 0, d = 0, H = 0, M = 0, S = 0;
        char fname[128];
        char *cur = line;

        if (sscanf(cur, "%d,%d-%d-%d %d:%d:%d,%127[^,]", &f_idx, &y, &m, &d, &H, &M, &S, fname) < 7) {
            continue;
        }

        /* Avanza despues de filename */
        int comma_count = 0;
        while (*cur && comma_count < 3) {
            if (*cur == ',') comma_count++;
            cur++;
        }

        struct tm t_tm;
        memset(&t_tm, 0, sizeof(t_tm));
        t_tm.tm_year = y - 1900;
        t_tm.tm_mon  = m - 1;
        t_tm.tm_mday = d;
        t_tm.tm_hour = H;
        t_tm.tm_min  = M;
        t_tm.tm_sec  = S;
        time_t epoch = mktime(&t_tm);

        times[count] = (int64_t)epoch;

        /* Lee los 48 valores flotantes */
        for (int k = 0; k < 48; k++) {
            double v = strtod(cur, &cur);
            mat[count * 48 + k] = v;
            if (*cur == ',') cur++;
        }

        count++;
        if (count >= cap) {
            cap *= 2;
            times = (int64_t *)realloc(times, cap * sizeof(int64_t));
            mat = (double *)realloc(mat, 48 * cap * sizeof(double));
        }
    }

    fclose(f);

    /* Transposicion a matriz columnar canónica: mat[k * N + i] */
    double *columnar = (double *)malloc(48 * count * sizeof(double));
    if (!columnar) {
        free(times);
        free(mat);
        return -4;
    }

    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t k = 0; k < 48; k++) {
            columnar[k * count + i] = mat[i * 48 + k];
        }
    }
    free(mat);

    *out_timestamps = times;
    *out_matrix = columnar;
    *out_sample_count = count;
    *out_num_channels = 48;
    return 0;
}

/* ========================================================================= */
/* Rutinas Windows Unbuffered I/O segun Seccion 7                            */
/* ========================================================================= */
typedef struct {
    uint32_t sector_size;
    uint32_t buffer_align;
} bp011_io_align_t;

static inline bp011_status_t bp011_query_volume_alignment(const char *filepath, bp011_io_align_t *align) {
    if (!filepath || !align) return BP011_ERR_NULL_POINTER;
    align->sector_size = 4096;
    align->buffer_align = 4096;

#ifdef _WIN32
    char vol_path[MAX_PATH];
    if (GetVolumePathNameA(filepath, vol_path, MAX_PATH)) {
        DWORD sectors_per_cluster = 0, bytes_per_sector = 0, free_clusters = 0, total_clusters = 0;
        if (GetDiskFreeSpaceA(vol_path, &sectors_per_cluster, &bytes_per_sector, &free_clusters, &total_clusters)) {
            if (bytes_per_sector >= 512) {
                align->sector_size = bytes_per_sector;
                align->buffer_align = bytes_per_sector;
            }
        }
    }
#endif
    return BP011_OK;
}

static inline void *bp011_alloc_aligned_buffer(size_t size, const bp011_io_align_t *align) {
#ifdef _WIN32
    void *buf = VirtualAlloc(NULL, size, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (buf) {
        if (((uintptr_t)buf & (align->buffer_align - 1)) != 0) {
            VirtualFree(buf, 0, MEM_RELEASE);
            return NULL;
        }
    }
    return buf;
#else
    void *buf = NULL;
    if (posix_memalign(&buf, align->buffer_align, size) != 0) return NULL;
    return buf;
#endif
}

static inline void bp011_free_aligned_buffer(void *buf) {
#ifdef _WIN32
    if (buf) VirtualFree(buf, 0, MEM_RELEASE);
#else
    if (buf) free(buf);
#endif
}

/* ========================================================================= */
/* Punto de Entrada del Arnes Experimental                                   */
/* ========================================================================= */
int main(int argc, char **argv) {
    printf("===============================================================================\n");
    printf("  BINPACK ENTERPRISE SDK — ARNES EXPERIMENTAL BP-011 (Fase C/D/E)\n");
    printf("  Validacion Decisiva de Viabilidad Comercial en Telemetria Industrial\n");
    printf("===============================================================================\n\n");

    /* Inicializa rastreador global de memoria */
    bp011_mem_tracker_init();

    printf("[1/3] Verificacion de tipos, estructuras y contratos binarios:\n");
    printf("  • sizeof(bp011_block_header_t)     = %u bytes (esperado: 64)\n", (uint32_t)sizeof(bp011_block_header_t));
    printf("  • sizeof(bp011_channel_entry_t)    = %u bytes (esperado: 12)\n", (uint32_t)sizeof(bp011_channel_entry_t));
    printf("  • sizeof(bp011_series_segment_t)   = %u bytes (esperado: 32)\n", (uint32_t)sizeof(bp011_series_segment_t));

    if (sizeof(bp011_block_header_t) != 64 || sizeof(bp011_channel_entry_t) != 12 || sizeof(bp011_series_segment_t) != 32) {
        printf("  [ERROR] Discrepancia en alineacion de estructuras contractuales.\n");
        return 1;
    }
    printf("  [OK] Estructuras binarias alineadas a la perfeccion.\n\n");

    printf("[2/3] Verificacion de Unbuffered I/O en sistema Windows:\n");
    bp011_io_align_t align;
    bp011_query_volume_alignment("tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt", &align);
    printf("  • Sector size consultado: %u bytes\n", align.sector_size);
    printf("  • Buffer alignment:       %u bytes\n", align.buffer_align);

    void *test_aligned_buf = bp011_alloc_aligned_buffer(align.sector_size * 4, &align);
    if (!test_aligned_buf || ((uintptr_t)test_aligned_buf & (align.buffer_align - 1)) != 0) {
        printf("  [ERROR] Fallo de alineacion en VirtualAlloc.\n");
        return 2;
    }
    printf("  [OK] VirtualAlloc y alineacion de sector 100%% conformes.\n");
    bp011_free_aligned_buffer(test_aligned_buf);
    printf("\n");

    printf("[3/3] Estado del arnes listo para Auditoria Estatica Fase D.\n");
    printf("  • Codificacion completada en tests/experiments/bp011/.\n");
    printf("  • Code freeze preservado en src/, include/, deps/.\n");
    printf("  • Ejecucion de benchmarks congelada a la espera de Aprobacion Fase D.\n\n");

    return 0;
}
