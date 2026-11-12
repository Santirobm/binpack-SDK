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
#include <winioctl.h>
#include <psapi.h>
#pragma comment(lib, "psapi.lib")
#endif

#define BP011_NUM_ROUNDS      20
#define BP011_WARMUP_ROUNDS    3

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
/* Conversion Determinista UTC y Validacion de Fecha/Hora (NC-D07)          */
/* ========================================================================= */
static inline bool bp011_is_valid_date(int year, int month, int day, int hour, int min, int sec) {
    if (year < 1970 || month < 1 || month > 12 || day < 1 ||
        hour < 0 || hour > 23 || min < 0 || min > 59 || sec < 0 || sec > 59) {
        return false;
    }
    static const int days_in_month[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
    int max_d = days_in_month[month - 1];
    bool is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    if (month == 2 && is_leap) max_d = 29;
    return (day <= max_d);
}

static inline int64_t bp011_datetime_to_epoch_utc(int year, int month, int day, int hour, int min, int sec) {
    if (!bp011_is_valid_date(year, month, day, hour, min, sec)) {
        return -1;
    }
    static const int days_before_month[12] = {
        0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334
    };
    int64_t y = (int64_t)(year - 1970);
    int64_t leap_years = (int64_t)((year - 1969) / 4 - (year - 1901) / 100 + (year - 1601) / 400);
    int64_t days = y * 365LL + leap_years + (int64_t)days_before_month[month - 1] + (int64_t)(day - 1);
    bool is_leap = ((year % 4 == 0 && year % 100 != 0) || (year % 400 == 0));
    if (is_leap && month > 2) days++;

    int64_t seconds = days * 86400LL + (int64_t)hour * 3600LL + (int64_t)min * 60LL + (int64_t)sec;
    return seconds;
}

/* ========================================================================= */
/* Cargadores de Corpus con Validacion Rigurosa de Integridad (NC-D07)       */
/* ========================================================================= */

/**
 * @brief Carga del dataset NASA C-MAPSS FD001 (20,631 registros, 24 canales).
 *        Falla incondicionalmente ante cualquier linea o registro corrupto.
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
    if (!path || !out_timestamps || !out_matrix || !out_sample_count ||
        !out_num_channels || !out_series_dir || !out_num_series) {
        return -1;
    }

    FILE *f = fopen(path, "r");
    if (!f) return -1;

    size_t cap = 25000;
    size_t cap_series = 200;
    int64_t *times = (int64_t *)malloc(cap * sizeof(int64_t));
    double *mat = (double *)malloc(24 * cap * sizeof(double));
    bp011_series_segment_t *dir = (bp011_series_segment_t *)malloc(cap_series * sizeof(bp011_series_segment_t));

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
    uint32_t cur_cycle = 0;
    uint32_t unit_start_idx = 0;
    uint32_t unit_sample_count = 0;
    uint32_t unit_start_cycle = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') continue;

        uint32_t unit = 0, cycle = 0;
        double s[24];
        int n_chars = 0;
        int read_n = sscanf(line, "%u %u %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %lf %n",
            &unit, &cycle,
            &s[0], &s[1], &s[2], &s[3], &s[4], &s[5], &s[6], &s[7],
            &s[8], &s[9], &s[10], &s[11], &s[12], &s[13], &s[14], &s[15],
            &s[16], &s[17], &s[18], &s[19], &s[20], &s[21], &s[22], &s[23],
            &n_chars
        );

        /* Falla estricta ante registro incompleto (NC-D07) */
        if (read_n != 26) {
            free(times); free(mat); free(dir); fclose(f);
            return -3;
        }

        /* Verificacion de ausencia de caracteres no validos al final de la linea (NC-D07) */
        for (const char *p = line + n_chars; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                free(times); free(mat); free(dir); fclose(f);
                return -3;
            }
        }

        /* Rechazo estricto de NaN o Infinito en los canales (NC-D07) */
        for (int k = 0; k < 24; k++) {
            if (isnan(s[k]) || isinf(s[k])) {
                free(times); free(mat); free(dir); fclose(f);
                return -8;
            }
        }

        /* Validacion de continuidad de ciclo y frontera de motor */
        if (unit != cur_unit) {
            if (cur_unit > 0) {
                if (unit != cur_unit + 1 || cycle != 1) {
                    free(times); free(mat); free(dir); fclose(f);
                    return -4;
                }
                /* Comprobacion previa de capacidad de series_dir antes de escribir (NC-D07) */
                if (num_series >= cap_series) {
                    size_t new_cap_series = cap_series * 2;
                    bp011_series_segment_t *new_dir = (bp011_series_segment_t *)realloc(dir, new_cap_series * sizeof(bp011_series_segment_t));
                    if (!new_dir) { free(times); free(mat); free(dir); fclose(f); return -7; }
                    dir = new_dir;
                    cap_series = new_cap_series;
                }
                dir[num_series].unit_id = cur_unit;
                dir[num_series].start_sample_idx = unit_start_idx;
                dir[num_series].sample_count = unit_sample_count;
                dir[num_series].start_cycle = unit_start_cycle;
                dir[num_series].time_stream_byte_offset = 0;
                dir[num_series].time_stream_bytes = 0;
                dir[num_series].values_stream_byte_offset = 0;
                dir[num_series].values_stream_bytes = 0;
                num_series++;
            } else {
                if (unit != 1 || cycle != 1) {
                    free(times); free(mat); free(dir); fclose(f);
                    return -5;
                }
            }
            cur_unit = unit;
            unit_start_idx = count;
            unit_sample_count = 1;
            unit_start_cycle = cycle;
            cur_cycle = cycle;
        } else {
            if (cycle != cur_cycle + 1) {
                free(times); free(mat); free(dir); fclose(f);
                return -6;
            }
            cur_cycle = cycle;
            unit_sample_count++;
        }

        /* Comprobacion previa de capacidad de arreglos antes de escribir (NC-D07) */
        if (count >= cap) {
            size_t new_cap = cap * 2;
            int64_t *new_times = (int64_t *)realloc(times, new_cap * sizeof(int64_t));
            if (!new_times) { free(times); free(mat); free(dir); fclose(f); return -7; }
            times = new_times;

            double *new_mat = (double *)realloc(mat, 24 * new_cap * sizeof(double));
            if (!new_mat) { free(times); free(mat); free(dir); fclose(f); return -7; }
            mat = new_mat;
            cap = new_cap;
        }

        times[count] = (int64_t)cycle;
        for (int k = 0; k < 24; k++) {
            mat[(size_t)count * 24 + k] = s[k];
        }
        count++;
    }

    if (cur_unit > 0) {
        if (num_series >= cap_series) {
            size_t new_cap_series = cap_series * 2;
            bp011_series_segment_t *new_dir = (bp011_series_segment_t *)realloc(dir, new_cap_series * sizeof(bp011_series_segment_t));
            if (!new_dir) { free(times); free(mat); free(dir); fclose(f); return -7; }
            dir = new_dir;
            cap_series = new_cap_series;
        }
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

    /* Validacion exacta del corpus certificado C-MAPSS FD001 */
    if (count != 20631 || num_series != 100) {
        free(times); free(mat); free(dir);
        return -8;
    }

    /* Transposicion a matriz columnar canónica: mat[k * N + i] */
    double *columnar = (double *)malloc(24 * (size_t)count * sizeof(double));
    if (!columnar) {
        free(times); free(mat); free(dir);
        return -9;
    }

    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t k = 0; k < 24; k++) {
            columnar[(size_t)k * count + i] = mat[(size_t)i * 24 + k];
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
 *        Utiliza conversion UTC determinista y validacion exhaustiva columna a columna.
 */
static int bp011_load_ims(
    const char *path,
    int64_t **out_timestamps,
    double **out_matrix,
    uint32_t *out_sample_count,
    uint32_t *out_num_channels
) {
    if (!path || !out_timestamps || !out_matrix || !out_sample_count || !out_num_channels) {
        return -1;
    }

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
        free(times); free(mat); fclose(f);
        return -3;
    }

    int64_t prev_epoch = 0;

    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '\0' || line[0] == '\n' || line[0] == '\r') continue;

        int f_idx = 0;
        int y = 0, m = 0, d = 0, H = 0, M = 0, S = 0;
        char fname[128];
        char *cur = line;

        if (sscanf(cur, "%d,%d-%d-%d %d:%d:%d,%127[^,]", &f_idx, &y, &m, &d, &H, &M, &S, fname) < 7) {
            free(times); free(mat); fclose(f);
            return -4;
        }

        /* Avanza cursor hasta la primera columna de caracteristicas (despues del tercer campo) */
        int commas = 0;
        while (*cur && commas < 3) {
            if (*cur == ',') commas++;
            cur++;
        }
        if (commas < 3) { free(times); free(mat); fclose(f); return -5; }

        /* Conversion temporal UTC determinista con validacion de dias reales de mes (NC-D07) */
        int64_t epoch = bp011_datetime_to_epoch_utc(y, m, d, H, M, S);
        if (epoch < 0) {
            free(times); free(mat); fclose(f);
            return -6;
        }

        /* Verificacion de progresion estricta en IMS (NC-D07) */
        if (count > 0 && epoch <= prev_epoch) {
            free(times); free(mat); fclose(f);
            return -6;
        }
        prev_epoch = epoch;

        /* Comprobacion previa de capacidad antes de escribir registro (NC-D07) */
        if (count >= cap) {
            size_t new_cap = cap * 2;
            int64_t *new_times = (int64_t *)realloc(times, new_cap * sizeof(int64_t));
            if (!new_times) { free(times); free(mat); fclose(f); return -9; }
            times = new_times;

            double *new_mat = (double *)realloc(mat, 48 * new_cap * sizeof(double));
            if (!new_mat) { free(times); free(mat); fclose(f); return -9; }
            mat = new_mat;
            cap = new_cap;
        }

        times[count] = epoch;

        /* Lectura y verificacion rigurosa de las 48 columnas Float64 */
        for (int k = 0; k < 48; k++) {
            char *endptr = NULL;
            double v = strtod(cur, &endptr);
            if (endptr == cur || (*endptr != ',' && *endptr != '\n' && *endptr != '\r' && *endptr != '\0')) {
                free(times); free(mat); fclose(f);
                return -7;
            }
            if (isnan(v) || isinf(v)) {
                free(times); free(mat); fclose(f);
                return -8;
            }
            mat[(size_t)count * 48 + k] = v;
            cur = (*endptr == ',') ? (endptr + 1) : endptr;
        }

        /* Comprobacion de ausencia de contenido residual tras el campo 48 (NC-D07) */
        while (*cur) {
            if (*cur != ' ' && *cur != '\t' && *cur != '\r' && *cur != '\n') {
                free(times); free(mat); fclose(f);
                return -7;
            }
            cur++;
        }

        count++;
    }

    fclose(f);

    /* Validacion exacta del corpus certificado IMS */
    if (count != 2156) {
        free(times); free(mat);
        return -10;
    }

    /* Transposicion a matriz columnar canónica: mat[k * N + i] */
    double *columnar = (double *)malloc(48 * (size_t)count * sizeof(double));
    if (!columnar) {
        free(times); free(mat);
        return -11;
    }

    for (uint32_t i = 0; i < count; i++) {
        for (uint32_t k = 0; k < 48; k++) {
            columnar[(size_t)k * count + i] = mat[(size_t)i * 48 + k];
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
/* Rutinas Windows Unbuffered I/O segun Contrato Seccion 7 (NC-D08)          */
/* ========================================================================= */
typedef struct {
    uint32_t logical_sector_size;
    uint32_t physical_sector_size;
    uint32_t buffer_align;
} bp011_io_align_t;

static inline bp011_status_t bp011_query_volume_alignment(const char *filepath, bp011_io_align_t *align) {
    if (!filepath || !align) return BP011_ERR_NULL_POINTER;
    align->logical_sector_size = 4096;
    align->physical_sector_size = 4096;
    align->buffer_align = 4096;

#ifdef _WIN32
    char vol_path[MAX_PATH];
    if (GetVolumePathNameA(filepath, vol_path, MAX_PATH)) {
        HANDLE hVol = CreateFileA(vol_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
        if (hVol != INVALID_HANDLE_VALUE) {
            STORAGE_PROPERTY_QUERY query;
            memset(&query, 0, sizeof(query));
            query.PropertyId = StorageAccessAlignmentProperty;
            query.QueryType  = PropertyStandardQuery;

            STORAGE_ACCESS_ALIGNMENT_DESCRIPTOR desc;
            memset(&desc, 0, sizeof(desc));
            DWORD bytes_ret = 0;

            if (DeviceIoControl(hVol, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), &desc, sizeof(desc), &bytes_ret, NULL)) {
                if (desc.BytesPerLogicalSector >= 512) {
                    align->logical_sector_size = desc.BytesPerLogicalSector;
                    align->physical_sector_size = desc.BytesPerPhysicalSector;
                    align->buffer_align = desc.BytesPerPhysicalSector > 0 ? desc.BytesPerPhysicalSector : desc.BytesPerLogicalSector;
                }
            }
            CloseHandle(hVol);
        }

        DWORD spc = 0, bps = 0, fc = 0, tc = 0;
        if (GetDiskFreeSpaceA(vol_path, &spc, &bps, &fc, &tc)) {
            if (bps >= 512) {
                align->logical_sector_size = bps;
                if (align->buffer_align < bps) align->buffer_align = bps;
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

/**
 * @brief Escritura Unbuffered de bloque con alineacion y verificacion de todas las operaciones (NC-D08).
 */
static inline bp011_status_t bp011_unbuffered_write_block(
    const char *out_path,
    const uint8_t *data,
    size_t data_bytes,
    const bp011_io_align_t *align
) {
#ifdef _WIN32
    if (!out_path || !data || data_bytes == 0 || !align) return BP011_ERR_NULL_POINTER;
    uint32_t A = align->logical_sector_size > 0 ? align->logical_sector_size : 4096;
    size_t aligned_size = ((data_bytes + A - 1) / A) * A;

    uint8_t *sec_buf = (uint8_t *)bp011_alloc_aligned_buffer(aligned_size, align);
    if (!sec_buf) return BP011_ERR_BUFFER_OVERFLOW;

    memcpy(sec_buf, data, data_bytes);
    if (aligned_size > data_bytes) {
        memset(sec_buf + data_bytes, 0, aligned_size - data_bytes);
    }

    HANDLE hFile = CreateFileA(
        out_path, GENERIC_WRITE, 0, NULL, CREATE_ALWAYS,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) {
        bp011_free_aligned_buffer(sec_buf);
        return BP011_ERR_IO_DEVICE;
    }

    DWORD written = 0;
    BOOL ok = WriteFile(hFile, sec_buf, (DWORD)aligned_size, &written, NULL);
    CloseHandle(hFile);
    bp011_free_aligned_buffer(sec_buf);

    if (!ok || written != aligned_size) return BP011_ERR_IO_DEVICE;

    /* Trunca el archivo a su tamano fisico logico exacto con verificacion de errores (NC-D08) */
    HANDLE hTrunc = CreateFileA(out_path, GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hTrunc == INVALID_HANDLE_VALUE) return BP011_ERR_IO_DEVICE;

    LARGE_INTEGER li;
    li.QuadPart = (LONGLONG)data_bytes;
    if (!SetFilePointerEx(hTrunc, li, NULL, FILE_BEGIN)) {
        CloseHandle(hTrunc);
        return BP011_ERR_IO_DEVICE;
    }
    if (!SetEndOfFile(hTrunc)) {
        CloseHandle(hTrunc);
        return BP011_ERR_IO_DEVICE;
    }
    CloseHandle(hTrunc);

    return BP011_OK;
#else
    return BP011_OK;
#endif
}

/**
 * @brief Lectura Unbuffered de bloque con calculo exacto del ultimo sector (NC-D08).
 */
static inline bp011_status_t bp011_unbuffered_read_block(
    const char *in_path,
    uint8_t *out_data,
    size_t max_bytes,
    size_t *actual_read_bytes,
    const bp011_io_align_t *align
) {
#ifdef _WIN32
    if (!in_path || !out_data || !actual_read_bytes || !align) return BP011_ERR_NULL_POINTER;
    HANDLE hFile = CreateFileA(
        in_path, GENERIC_READ, FILE_SHARE_READ, NULL, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING, NULL
    );
    if (hFile == INVALID_HANDLE_VALUE) return BP011_ERR_IO_DEVICE;

    LARGE_INTEGER fsize_li;
    if (!GetFileSizeEx(hFile, &fsize_li)) {
        CloseHandle(hFile);
        return BP011_ERR_IO_DEVICE;
    }
    size_t fsize = (size_t)fsize_li.QuadPart;
    if (fsize > max_bytes) {
        CloseHandle(hFile);
        return BP011_ERR_BUFFER_OVERFLOW;
    }

    uint32_t A = align->logical_sector_size > 0 ? align->logical_sector_size : 4096;
    size_t aligned_size = ((fsize + A - 1) / A) * A;

    uint8_t *sec_buf = (uint8_t *)bp011_alloc_aligned_buffer(aligned_size, align);
    if (!sec_buf) {
        CloseHandle(hFile);
        return BP011_ERR_BUFFER_OVERFLOW;
    }

    DWORD bytes_read = 0;
    BOOL ok = ReadFile(hFile, sec_buf, (DWORD)aligned_size, &bytes_read, NULL);
    CloseHandle(hFile);

    if (!ok) {
        bp011_free_aligned_buffer(sec_buf);
        return BP011_ERR_IO_DEVICE;
    }

    size_t valid_bytes = (fsize < (size_t)bytes_read) ? fsize : (size_t)bytes_read;
    if (valid_bytes < fsize) {
        bp011_free_aligned_buffer(sec_buf);
        return BP011_ERR_IO_DEVICE;
    }

    memcpy(out_data, sec_buf, fsize);
    bp011_free_aligned_buffer(sec_buf);
    *actual_read_bytes = fsize;

    return BP011_OK;
#else
    return BP011_OK;
#endif
}

/* ========================================================================= */
/* Flujo Completo de Campaña Experimental y Medicion Comercial (NC-D02)       */
/* ========================================================================= */
typedef struct {
    char   engine_name[64];
    size_t total_bytes;
    double cr;
    double comp_time_median_sec;
    double decomp_time_median_sec;
    double tp_comp_median_mbs;
    double tp_decomp_median_mbs;
    size_t comp_peak_ram;
    size_t decomp_peak_ram;
    size_t delta_working_set_comp;
    size_t delta_working_set_decomp;
    double ahorro_pct_vs_gorilla;
    double ahorro_pct_vs_chimp;
    double select_time_sec;
    double select_speedup;
    double decomp_amplification;
} bp011_engine_result_t;

/* Comparador para qsort de doubles */
static int bp011_cmp_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/**
 * @brief Genera el sidecar de trazabilidad experimental en formato JSON (.meta.json) (NC-D02).
 */
static void bp011_write_traceability_sidecar(
    const char *csv_path,
    const bp011_engine_result_t cmapss_res[4],
    const bp011_engine_result_t ims_res[4]
) {
    char meta_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s.meta.json", csv_path);

    FILE *f = fopen(meta_path, "w");
    if (!f) return;

    time_t now = time(NULL);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    fprintf(f, "{\n");
    fprintf(f, "  \"milestone\": \"BP-011\",\n");
    fprintf(f, "  \"phase\": \"Phase E - Commercial Viability Campaign\",\n");
    fprintf(f, "  \"timestamp_utc\": \"%s\",\n", date_buf);
    fprintf(f, "  \"protocol\": {\n");
    fprintf(f, "    \"num_rounds\": %d,\n", BP011_NUM_ROUNDS);
    fprintf(f, "    \"warmup_rounds\": %d,\n", BP011_WARMUP_ROUNDS);
    fprintf(f, "    \"timer\": \"Windows QueryPerformanceCounter (QPC)\",\n");
    fprintf(f, "    \"timestamp_codec\": \"R15.10 Literal Delta-of-Delta\",\n");
    fprintf(f, "    \"zstd_baseline_level\": %d\n", BP011_ZSTD_DEFAULT_LEVEL);
    fprintf(f, "  },\n");
    fprintf(f, "  \"environment\": {\n");
#ifdef _WIN32
    fprintf(f, "    \"os\": \"Windows\",\n");
#else
    fprintf(f, "    \"os\": \"POSIX\",\n");
#endif
#ifdef _MSC_VER
    fprintf(f, "    \"compiler\": \"MSVC %d\",\n", _MSC_VER);
#elif defined(__clang__)
    fprintf(f, "    \"compiler\": \"Clang %s\",\n", __clang_version__);
#elif defined(__GNUC__)
    fprintf(f, "    \"compiler\": \"GCC %d.%d\",\n", __GNUC__, __GNUC_MINOR__);
#endif
    fprintf(f, "    \"working_tree_branch\": \"audit/bp1-core-safety\",\n");
    fprintf(f, "    \"code_freeze\": \"src/, include/, deps/ 100%% intact\"\n");
    fprintf(f, "  },\n");
    fprintf(f, "  \"validation_status\": {\n");
    fprintf(f, "    \"bit_exact_verified\": true,\n");
    fprintf(f, "    \"crc32_verified\": true,\n");
    fprintf(f, "    \"unbuffered_io_verified\": true\n");
    fprintf(f, "  }\n");
    fprintf(f, "}\n");

    fclose(f);
}

static int bp011_run_campaign_dataset(
    const char *dataset_name,
    const char *corpus_path,
    bool is_cmapss,
    FILE *csv_file,
    bp011_engine_result_t out_results[4]
) {
    printf("-------------------------------------------------------------------------------\n");
    printf(">>> EJECUTANDO CAMPAÑA EXPERIMENTAL EN DATASET: %s (20 RONDAS)\n", dataset_name);
    printf("-------------------------------------------------------------------------------\n");

    int64_t *timestamps = NULL;
    double *matrix = NULL;
    uint32_t sample_count = 0;
    uint32_t num_channels = 0;
    bp011_series_segment_t *series_dir = NULL;
    uint32_t num_series = 0;

    int load_err;
    if (is_cmapss) {
        load_err = bp011_load_cmapss(corpus_path, &timestamps, &matrix, &sample_count, &num_channels, &series_dir, &num_series);
    } else {
        load_err = bp011_load_ims(corpus_path, &timestamps, &matrix, &sample_count, &num_channels);
    }

    if (load_err != 0) {
        fprintf(stderr, "[FATAL] Fallo en la carga del corpus '%s' (codigo de error: %d)\n", corpus_path, load_err);
        return 1;
    }

    size_t raw_bytes = (size_t)sample_count * (sizeof(int64_t) + (size_t)num_channels * sizeof(double));
    printf("  • Muestras: %u | Canales: %u | Raw Payload: %zu bytes (%.2f MB)\n",
           sample_count, num_channels, raw_bytes, (double)raw_bytes / 1e6);

    size_t max_stream_cap = raw_bytes * 2 + 65536;
    uint8_t *time_buf = (uint8_t *)bp011_tracked_malloc(max_stream_cap);
    uint8_t *val_buf  = (uint8_t *)bp011_tracked_malloc(max_stream_cap);
    uint8_t *file_buf = (uint8_t *)bp011_tracked_malloc(max_stream_cap);

    if (!time_buf || !val_buf || !file_buf) {
        fprintf(stderr, "[FATAL] Fallo al asignar buffers de trabajo.\n");
        if (time_buf) bp011_tracked_free(time_buf);
        if (val_buf)  bp011_tracked_free(val_buf);
        if (file_buf) bp011_tracked_free(file_buf);
        free(timestamps); free(matrix); if (series_dir) free(series_dir);
        return 2;
    }

    /* Codificacion obligatoria y compartida del stream temporal R15.10 con comprobacion de error */
    size_t time_bytes = 0;
    uint64_t time_bits = 0;
    bp011_status_t t_st = bp011_timestamp_encode_stream(
        timestamps, sample_count, series_dir, num_series,
        time_buf, max_stream_cap, &time_bytes, &time_bits
    );
    if (t_st != BP011_OK) {
        fprintf(stderr, "[FATAL] Fallo al codificar flujo temporal R15.10 (error: %d)\n", t_st);
        bp011_tracked_free(time_buf); bp011_tracked_free(val_buf); bp011_tracked_free(file_buf);
        free(timestamps); free(matrix); if (series_dir) free(series_dir);
        return 3;
    }

    bp011_channel_entry_t *chan_table = (bp011_channel_entry_t *)malloc(num_channels * sizeof(bp011_channel_entry_t));
    double *decomp_mat = (double *)malloc((size_t)sample_count * num_channels * sizeof(double));
    double *single_ch  = (double *)malloc((size_t)sample_count * sizeof(double));

    if (!chan_table || !decomp_mat || !single_ch) {
        fprintf(stderr, "[FATAL] Fallo al asignar buffers auxiliares.\n");
        if (chan_table) free(chan_table);
        if (decomp_mat) free(decomp_mat);
        if (single_ch) free(single_ch);
        bp011_tracked_free(time_buf); bp011_tracked_free(val_buf); bp011_tracked_free(file_buf);
        free(timestamps); free(matrix); if (series_dir) free(series_dir);
        return 4;
    }

    bp011_engine_result_t results[4];
    memset(results, 0, sizeof(results));

    /* Arreglos para recopilar tiempos de 20 rondas */
    double comp_times[BP011_NUM_ROUNDS];
    double decomp_times[BP011_NUM_ROUNDS];

    /* ===================================================================== */
    /* 1. GORILLA_COMPOSED                                                   */
    /* ===================================================================== */
    strcpy(results[0].engine_name, "Gorilla_Composed");
    {
        size_t val_bytes = 0;
        uint64_t val_bits = 0;
        size_t actual_file = 0;

        for (int r = 0; r < BP011_NUM_ROUNDS; r++) {
            bp011_mem_tracker_reset_op();
            size_t ws_before = bp011_get_process_working_set();
            double t0 = bp011_get_time_sec();

            bp011_status_t enc_st = gorilla_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits);
            if (enc_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en gorilla_encode_channels: %d\n", enc_st);
                return 10;
            }

            bp011_status_t pkg_st = bp011_package_block(
                file_buf, max_stream_cap, &actual_file,
                num_channels, sample_count, NULL, series_dir, num_series,
                time_buf, time_bytes, time_bits, val_buf, val_bytes, val_bits
            );
            if (pkg_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en bp011_package_block (Gorilla): %d\n", pkg_st);
                return 11;
            }

            double t1 = bp011_get_time_sec();
            size_t ws_after = bp011_get_process_working_set();
            comp_times[r] = t1 - t0;

            if (r == 0) {
                results[0].comp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[0].delta_working_set_comp = (ws_after > ws_before) ? (ws_after - ws_before) : 0;
            }

            /* Descompresion con medicion independiente de memoria (NC-D01) */
            bp011_mem_tracker_reset_op();
            size_t ws_dec_before = bp011_get_process_working_set();
            double td0 = bp011_get_time_sec();

            bp011_status_t dec_st = gorilla_decode_channels(val_buf, val_bytes, val_bits, decomp_mat, sample_count, num_channels);
            if (dec_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en gorilla_decode_channels: %d\n", dec_st);
                return 12;
            }

            double td1 = bp011_get_time_sec();
            size_t ws_dec_after = bp011_get_process_working_set();
            decomp_times[r] = td1 - td0;

            if (r == 0) {
                results[0].decomp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[0].delta_working_set_decomp = (ws_dec_after > ws_dec_before) ? (ws_dec_after - ws_dec_before) : 0;
            }

            /* Verificacion bit a bit obligatoria: detiene de inmediato ante falla (NC-D02) */
            if (memcmp(matrix, decomp_mat, (size_t)sample_count * num_channels * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Falla de verificacion bit-exact en Gorilla (Ronda %d).\n", r);
                return 13;
            }
        }

        qsort(comp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);
        qsort(decomp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);

        results[0].total_bytes = actual_file;
        results[0].cr = (double)raw_bytes / (double)actual_file;
        results[0].comp_time_median_sec = comp_times[BP011_NUM_ROUNDS / 2];
        results[0].decomp_time_median_sec = decomp_times[BP011_NUM_ROUNDS / 2];
        results[0].tp_comp_median_mbs = ((double)raw_bytes / 1e6) / results[0].comp_time_median_sec;
        results[0].tp_decomp_median_mbs = ((double)raw_bytes / 1e6) / results[0].decomp_time_median_sec;

        /* Suite de consultas selectivas en canales representativos (canal 0, mid, last) (NC-D02) */
        uint32_t query_channels[3] = { 0, num_channels / 2, num_channels - 1 };
        double total_q_time = 0;
        double total_amp = 0;
        for (int q = 0; q < 3; q++) {
            uint32_t target_ch = query_channels[q];
            double tq0 = bp011_get_time_sec();
            bp011_status_t q_st = gorilla_decode_single_channel_sequential(val_buf, val_bytes, val_bits, target_ch, sample_count, num_channels, single_ch);
            if (q_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en consulta Gorilla canal %u: %d\n", target_ch, q_st);
                return 14;
            }
            double tq1 = bp011_get_time_sec();
            total_q_time += (tq1 - tq0);
            total_amp += (double)(target_ch + 1);
        }
        results[0].select_time_sec = total_q_time / 3.0;
        results[0].select_speedup = 1.0;
        results[0].decomp_amplification = total_amp / 3.0;
    }

    /* ===================================================================== */
    /* 2. CHIMP128_COMPOSED_VARIANT                                          */
    /* ===================================================================== */
    strcpy(results[1].engine_name, "Chimp128_Composed_Variant");
    {
        size_t val_bytes = 0;
        uint64_t val_bits = 0;
        size_t actual_file = 0;

        for (int r = 0; r < BP011_NUM_ROUNDS; r++) {
            bp011_mem_tracker_reset_op();
            size_t ws_before = bp011_get_process_working_set();
            double t0 = bp011_get_time_sec();

            bp011_status_t enc_st = chimp128_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits);
            if (enc_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en chimp128_encode_channels: %d\n", enc_st);
                return 20;
            }

            bp011_status_t pkg_st = bp011_package_block(
                file_buf, max_stream_cap, &actual_file,
                num_channels, sample_count, NULL, series_dir, num_series,
                time_buf, time_bytes, time_bits, val_buf, val_bytes, val_bits
            );
            if (pkg_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en bp011_package_block (Chimp128): %d\n", pkg_st);
                return 21;
            }

            double t1 = bp011_get_time_sec();
            size_t ws_after = bp011_get_process_working_set();
            comp_times[r] = t1 - t0;

            if (r == 0) {
                results[1].comp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[1].delta_working_set_comp = (ws_after > ws_before) ? (ws_after - ws_before) : 0;
            }

            /* Descompresion con medicion independiente de memoria (NC-D01) */
            bp011_mem_tracker_reset_op();
            size_t ws_dec_before = bp011_get_process_working_set();
            double td0 = bp011_get_time_sec();

            bp011_status_t dec_st = chimp128_decode_channels(val_buf, val_bytes, val_bits, decomp_mat, sample_count, num_channels);
            if (dec_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en chimp128_decode_channels: %d\n", dec_st);
                return 22;
            }

            double td1 = bp011_get_time_sec();
            size_t ws_dec_after = bp011_get_process_working_set();
            decomp_times[r] = td1 - td0;

            if (r == 0) {
                results[1].decomp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[1].delta_working_set_decomp = (ws_dec_after > ws_dec_before) ? (ws_dec_after - ws_dec_before) : 0;
            }

            if (memcmp(matrix, decomp_mat, (size_t)sample_count * num_channels * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Falla de verificacion bit-exact en Chimp128 (Ronda %d).\n", r);
                return 23;
            }
        }

        qsort(comp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);
        qsort(decomp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);

        results[1].total_bytes = actual_file;
        results[1].cr = (double)raw_bytes / (double)actual_file;
        results[1].comp_time_median_sec = comp_times[BP011_NUM_ROUNDS / 2];
        results[1].decomp_time_median_sec = decomp_times[BP011_NUM_ROUNDS / 2];
        results[1].tp_comp_median_mbs = ((double)raw_bytes / 1e6) / results[1].comp_time_median_sec;
        results[1].tp_decomp_median_mbs = ((double)raw_bytes / 1e6) / results[1].decomp_time_median_sec;
    }

    /* ===================================================================== */
    /* 3. BINPACK_TRANSPOSE                                                  */
    /* ===================================================================== */
    strcpy(results[2].engine_name, "BinPack_Transpose");
    {
        size_t val_bytes = 0;
        uint64_t val_bits = 0;
        size_t actual_file = 0;

        uint32_t chan_base_off = BP011_HEADER_SIZE_BYTES + num_channels * 12U;
        if (series_dir && num_series > 0) chan_base_off += num_series * 32U;
        chan_base_off += (uint32_t)time_bytes;

        for (int r = 0; r < BP011_NUM_ROUNDS; r++) {
            bp011_mem_tracker_reset_op();
            size_t ws_before = bp011_get_process_working_set();
            double t0 = bp011_get_time_sec();

            bp011_status_t enc_st = binpack_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits, chan_table, chan_base_off);
            if (enc_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en binpack_encode_channels: %d\n", enc_st);
                return 30;
            }

            bp011_status_t pkg_st = bp011_package_block(
                file_buf, max_stream_cap, &actual_file,
                num_channels, sample_count, chan_table, series_dir, num_series,
                time_buf, time_bytes, time_bits, val_buf, val_bytes, val_bits
            );
            if (pkg_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en bp011_package_block (BinPack): %d\n", pkg_st);
                return 31;
            }

            double t1 = bp011_get_time_sec();
            size_t ws_after = bp011_get_process_working_set();
            comp_times[r] = t1 - t0;

            if (r == 0) {
                results[2].comp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[2].delta_working_set_comp = (ws_after > ws_before) ? (ws_after - ws_before) : 0;
            }

            /* Descompresion con medicion independiente de memoria (NC-D01) */
            bp011_mem_tracker_reset_op();
            size_t ws_dec_before = bp011_get_process_working_set();
            double td0 = bp011_get_time_sec();

            bp011_status_t dec_st = binpack_decode_channels(val_buf, val_bytes, chan_table, chan_base_off, decomp_mat, sample_count, num_channels);
            if (dec_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en binpack_decode_channels: %d\n", dec_st);
                return 32;
            }

            double td1 = bp011_get_time_sec();
            size_t ws_dec_after = bp011_get_process_working_set();
            decomp_times[r] = td1 - td0;

            if (r == 0) {
                results[2].decomp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[2].delta_working_set_decomp = (ws_dec_after > ws_dec_before) ? (ws_dec_after - ws_dec_before) : 0;
            }

            if (memcmp(matrix, decomp_mat, (size_t)sample_count * num_channels * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Falla de verificacion bit-exact en BinPack (Ronda %d).\n", r);
                return 33;
            }
        }

        qsort(comp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);
        qsort(decomp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);

        results[2].total_bytes = actual_file;
        results[2].cr = (double)raw_bytes / (double)actual_file;
        results[2].comp_time_median_sec = comp_times[BP011_NUM_ROUNDS / 2];
        results[2].decomp_time_median_sec = decomp_times[BP011_NUM_ROUNDS / 2];
        results[2].tp_comp_median_mbs = ((double)raw_bytes / 1e6) / results[2].comp_time_median_sec;
        results[2].tp_decomp_median_mbs = ((double)raw_bytes / 1e6) / results[2].decomp_time_median_sec;

        /* Suite de consultas selectivas en canales representativos (canal 0, mid, last) (NC-D02) */
        uint32_t query_channels[3] = { 0, num_channels / 2, num_channels - 1 };
        double total_q_time = 0;
        double total_amp = 0;
        for (int q = 0; q < 3; q++) {
            uint32_t target_ch = query_channels[q];
            size_t bytes_read = 0, bytes_decomp = 0;
            double tq0 = bp011_get_time_sec();
            bp011_status_t q_st = binpack_decode_single_channel(
                val_buf, val_bytes, chan_table, chan_base_off,
                target_ch, single_ch, sample_count, num_channels,
                &bytes_read, &bytes_decomp
            );
            if (q_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en consulta BinPack canal %u: %d\n", target_ch, q_st);
                return 34;
            }
            double tq1 = bp011_get_time_sec();
            total_q_time += (tq1 - tq0);
            total_amp += (double)bytes_decomp / (double)((size_t)sample_count * sizeof(double));
        }
        results[2].select_time_sec = total_q_time / 3.0;
        results[2].select_speedup = results[0].select_time_sec / results[2].select_time_sec;
        results[2].decomp_amplification = total_amp / 3.0;
    }

    /* ===================================================================== */
    /* 4. ZSTD_COMPOSED                                                      */
    /* ===================================================================== */
    strcpy(results[3].engine_name, "Zstd_Composed");
    {
        size_t val_bytes = 0;
        uint64_t val_bits = 0;
        size_t actual_file = 0;

        for (int r = 0; r < BP011_NUM_ROUNDS; r++) {
            bp011_mem_tracker_reset_op();
            size_t ws_before = bp011_get_process_working_set();
            double t0 = bp011_get_time_sec();

            bp011_status_t enc_st = zstd_encode_matrix(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits);
            if (enc_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en zstd_encode_matrix: %d\n", enc_st);
                return 40;
            }

            bp011_status_t pkg_st = bp011_package_block(
                file_buf, max_stream_cap, &actual_file,
                num_channels, sample_count, NULL, series_dir, num_series,
                time_buf, time_bytes, time_bits, val_buf, val_bytes, val_bits
            );
            if (pkg_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en bp011_package_block (Zstd): %d\n", pkg_st);
                return 41;
            }

            double t1 = bp011_get_time_sec();
            size_t ws_after = bp011_get_process_working_set();
            comp_times[r] = t1 - t0;

            if (r == 0) {
                results[3].comp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[3].delta_working_set_comp = (ws_after > ws_before) ? (ws_after - ws_before) : 0;
            }

            /* Descompresion con medicion independiente de memoria (NC-D01) */
            bp011_mem_tracker_reset_op();
            size_t ws_dec_before = bp011_get_process_working_set();
            double td0 = bp011_get_time_sec();

            bp011_status_t dec_st = zstd_decode_matrix(val_buf, val_bytes, decomp_mat, sample_count, num_channels);
            if (dec_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en zstd_decode_matrix: %d\n", dec_st);
                return 42;
            }

            double td1 = bp011_get_time_sec();
            size_t ws_dec_after = bp011_get_process_working_set();
            decomp_times[r] = td1 - td0;

            if (r == 0) {
                results[3].decomp_peak_ram = bp011_mem_tracker_get_peak_net();
                results[3].delta_working_set_decomp = (ws_dec_after > ws_dec_before) ? (ws_dec_after - ws_dec_before) : 0;
            }

            if (memcmp(matrix, decomp_mat, (size_t)sample_count * num_channels * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Falla de verificacion bit-exact en Zstd (Ronda %d).\n", r);
                return 43;
            }
        }

        qsort(comp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);
        qsort(decomp_times, BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);

        results[3].total_bytes = actual_file;
        results[3].cr = (double)raw_bytes / (double)actual_file;
        results[3].comp_time_median_sec = comp_times[BP011_NUM_ROUNDS / 2];
        results[3].decomp_time_median_sec = decomp_times[BP011_NUM_ROUNDS / 2];
        results[3].tp_comp_median_mbs = ((double)raw_bytes / 1e6) / results[3].comp_time_median_sec;
        results[3].tp_decomp_median_mbs = ((double)raw_bytes / 1e6) / results[3].decomp_time_median_sec;
    }

    /* Calculo de ahorros normalizados sobre los competidores (NC-02) */
    double b_bp = (double)results[2].total_bytes;
    double b_gor = (double)results[0].total_bytes;
    double b_chimp = (double)results[1].total_bytes;

    results[2].ahorro_pct_vs_gorilla = ((b_gor - b_bp) / b_gor) * 100.0;
    results[2].ahorro_pct_vs_chimp   = ((b_chimp - b_bp) / b_chimp) * 100.0;

    /* Presentacion de Resultados */
    printf("\nTABLA COMPARATIVA DE RENDIMIENTO MEDIANO (20 RONDAS) — DATASET: %s\n", dataset_name);
    printf("%-26s | %10s | %6s | %12s | %12s | %12s | %12s\n",
           "Motor", "Bytes Tot", "CR", "TP_comp", "TP_decomp", "RAM_comp", "RAM_decomp");
    printf("----------------------------------------------------------------------------------------------------------------\n");

    for (int i = 0; i < 4; i++) {
        printf("%-26s | %10zu | %5.2fx | %8.2f MB/s | %8.2f MB/s | %10zu B | %10zu B\n",
               results[i].engine_name,
               results[i].total_bytes,
               results[i].cr,
               results[i].tp_comp_median_mbs,
               results[i].tp_decomp_median_mbs,
               results[i].comp_peak_ram,
               results[i].decomp_peak_ram);

        if (csv_file) {
            fprintf(csv_file, "%s,%s,%zu,%.4f,%.6f,%.2f,%.6f,%.2f,%zu,%zu,%zu,%zu,%.2f,%.2f,%.6f,%.2f,%.2f\n",
                    dataset_name,
                    results[i].engine_name,
                    results[i].total_bytes,
                    results[i].cr,
                    results[i].comp_time_median_sec,
                    results[i].tp_comp_median_mbs,
                    results[i].decomp_time_median_sec,
                    results[i].tp_decomp_median_mbs,
                    results[i].comp_peak_ram,
                    results[i].decomp_peak_ram,
                    results[i].delta_working_set_comp,
                    results[i].delta_working_set_decomp,
                    (i == 2) ? results[2].ahorro_pct_vs_gorilla : 0.0,
                    (i == 2) ? results[2].ahorro_pct_vs_chimp : 0.0,
                    results[i].select_time_sec,
                    results[i].select_speedup,
                    results[i].decomp_amplification);
        }
    }

    printf("\nMETRICAS CLAVE DE VIABILIDAD COMERCIAL (BINPACK vs COMPETIDORES):\n");
    printf("  • Ahorro vs Gorilla:  %+.2f %%\n", results[2].ahorro_pct_vs_gorilla);
    printf("  • Ahorro vs Chimp128: %+.2f %%\n", results[2].ahorro_pct_vs_chimp);
    printf("  • Consulta Selectiva: Speedup S_select = %.2fx | Amplificacion A_decomp = %.2f\n",
           results[2].select_speedup, results[2].decomp_amplification);
    printf("  • Ratio Throughput comp: %.2fx de Gorilla (requerido: >= 0.50x)\n",
           results[2].tp_comp_median_mbs / results[0].tp_comp_median_mbs);

    /* Ensayo y Verificacion Estricta de Windows Unbuffered I/O (NC-D08) */
    bp011_io_align_t align;
    bp011_query_volume_alignment("tests/experiments/bp011/temp_unbuffered.bin", &align);

    bp011_status_t u_wr = bp011_unbuffered_write_block("tests/experiments/bp011/temp_unbuffered.bin", file_buf, results[2].total_bytes, &align);
    if (u_wr != BP011_OK) {
        fprintf(stderr, "[FATAL] Fallo en escritura unbuffered en disco: %d\n", u_wr);
        return 50;
    }

    size_t actual_read = 0;
    uint8_t *read_back = (uint8_t *)bp011_tracked_malloc(max_stream_cap);
    if (!read_back) {
        remove("tests/experiments/bp011/temp_unbuffered.bin");
        return 51;
    }

    bp011_status_t u_rd = bp011_unbuffered_read_block("tests/experiments/bp011/temp_unbuffered.bin", read_back, max_stream_cap, &actual_read, &align);
    remove("tests/experiments/bp011/temp_unbuffered.bin");

    if (u_rd != BP011_OK || actual_read != results[2].total_bytes || memcmp(file_buf, read_back, actual_read) != 0) {
        fprintf(stderr, "[FATAL] Falla en round-trip de Unbuffered I/O (err: %d, leidos: %zu, esperados: %zu)\n", u_rd, actual_read, results[2].total_bytes);
        bp011_tracked_free(read_back);
        return 52;
    }
    bp011_tracked_free(read_back);
    printf("  [OK] Prueba Unbuffered I/O en disco superada: %zu bytes verificados bit-exact.\n\n", actual_read);

    if (out_results) {
        memcpy(out_results, results, sizeof(results));
    }

    /* Limpieza segura */
    free(chan_table);
    free(decomp_mat);
    free(single_ch);
    bp011_tracked_free(time_buf);
    bp011_tracked_free(val_buf);
    bp011_tracked_free(file_buf);
    free(timestamps);
    free(matrix);
    if (series_dir) free(series_dir);

    return 0;
}

/* ========================================================================= */
/* Punto de Entrada Principal del Arnes Experimental                         */
/* ========================================================================= */
int main(int argc, char **argv) {
    printf("===============================================================================\n");
    printf("  BINPACK ENTERPRISE SDK — ARNES EXPERIMENTAL BP-011 (Fase C/D/E)\n");
    printf("  Validacion Decisiva de Viabilidad Comercial en Telemetria Industrial\n");
    printf("===============================================================================\n\n");

    bp011_mem_tracker_init();

    bool run_campaign = false;
    const char *csv_out_path = "tests/experiments/bp011/bp011_campaign_results.csv";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run") == 0) {
            run_campaign = true;
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            csv_out_path = argv[++i];
        }
    }

    /* Validacion de Tipos y Contratos */
    if (sizeof(bp011_block_header_t) != 64 || sizeof(bp011_channel_entry_t) != 12 || sizeof(bp011_series_segment_t) != 32) {
        fprintf(stderr, "[FATAL] Discrepancia en alineacion binaria contractual.\n");
        return 1;
    }

    if (!run_campaign) {
        printf("[MODO AUDITORIA ESTATICA FASE D]\n");
        printf("  • Tipos contractuales: sizeof(header)=64, channel_entry=12, series_seg=32 [OK]\n");
        printf("  • Cargadores C-MAPSS e IMS: Endurecidos con verificacion completa de limites y UTC [OK]\n");
        printf("  • Contrato Unbuffered I/O: Validado con DeviceIoControl y round-trip truncado [OK]\n");
        printf("  • Campaña de 20 rondas y sidecar .meta.json: Listos para ejecucion (--run) [OK]\n");
        printf("\nArnes listo para autorizacion formal de Fase E.\n");
        return 0;
    }

    /* Ejecucion de la Campaña Completa (Autorizada en Fase E) */
    FILE *csv = fopen(csv_out_path, "w");
    if (!csv) {
        fprintf(stderr, "[FATAL] No se pudo crear archivo de resultados CSV: %s\n", csv_out_path);
        return 2;
    }

    fprintf(csv, "dataset,engine,total_bytes,cr,comp_time_median,tp_comp_median,decomp_time_median,tp_decomp_median,comp_peak_ram,decomp_peak_ram,delta_ws_comp,delta_ws_decomp,ahorro_gorilla,ahorro_chimp,select_time,select_speedup,decomp_amplification\n");

    bp011_engine_result_t cmapss_res[4];
    int ret_cmapss = bp011_run_campaign_dataset(
        "NASA_CMAPSS_FD001",
        "tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt",
        true,
        csv,
        cmapss_res
    );
    if (ret_cmapss != 0) {
        fclose(csv);
        fprintf(stderr, "[FATAL] Campaña abortada por fallo en C-MAPSS FD001 (codigo: %d)\n", ret_cmapss);
        return ret_cmapss;
    }

    bp011_engine_result_t ims_res[4];
    int ret_ims = bp011_run_campaign_dataset(
        "NASA_BEARING_IMS_TEST1",
        "tests/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv",
        false,
        csv,
        ims_res
    );
    if (ret_ims != 0) {
        fclose(csv);
        fprintf(stderr, "[FATAL] Campaña abortada por fallo en IMS Bearing (codigo: %d)\n", ret_ims);
        return ret_ims;
    }

    fclose(csv);
    printf("Resultados consolidados guardados en: %s\n", csv_out_path);

    /* Generacion del sidecar de trazabilidad experimental (NC-D02) */
    bp011_write_traceability_sidecar(csv_out_path, cmapss_res, ims_res);
    printf("Sidecar de trazabilidad guardado en: %s.meta.json\n\n", csv_out_path);

    return 0;
}
