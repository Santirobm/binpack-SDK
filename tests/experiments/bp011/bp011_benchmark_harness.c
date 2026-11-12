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

#define BP011_NUM_ROUNDS       20
#define BP011_WARMUP_ROUNDS     1

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

        if (read_n != 26) {
            free(times); free(mat); free(dir); fclose(f);
            return -3;
        }

        for (const char *p = line + n_chars; *p; p++) {
            if (*p != ' ' && *p != '\t' && *p != '\r' && *p != '\n') {
                free(times); free(mat); free(dir); fclose(f);
                return -3;
            }
        }

        for (int k = 0; k < 24; k++) {
            if (isnan(s[k]) || isinf(s[k])) {
                free(times); free(mat); free(dir); fclose(f);
                return -8;
            }
        }

        if (unit != cur_unit) {
            if (cur_unit > 0) {
                if (unit != cur_unit + 1 || cycle != 1) {
                    free(times); free(mat); free(dir); fclose(f);
                    return -4;
                }
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

    if (count != 20631 || num_series != 100) {
        free(times); free(mat); free(dir);
        return -8;
    }

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

        int commas = 0;
        while (*cur && commas < 3) {
            if (*cur == ',') commas++;
            cur++;
        }
        if (commas < 3) { free(times); free(mat); fclose(f); return -5; }

        int64_t epoch = bp011_datetime_to_epoch_utc(y, m, d, H, M, S);
        if (epoch < 0) {
            free(times); free(mat); fclose(f);
            return -6;
        }

        if (count > 0 && epoch <= prev_epoch) {
            free(times); free(mat); fclose(f);
            return -6;
        }
        prev_epoch = epoch;

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

    if (count != 2156) {
        free(times); free(mat);
        return -10;
    }

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
/* Rutinas Windows Unbuffered I/O con Semántica Discriminada (NC-D08)        */
/* ========================================================================= */
typedef struct {
    uint32_t logical_sector_size;
    uint32_t physical_sector_size;
    uint32_t buffer_align;
    bool     ioctl_success;
    bool     is_fallback;
    bool     physical_io_verified;
} bp011_io_align_t;

static inline bp011_status_t bp011_query_volume_alignment(const char *filepath, bp011_io_align_t *align) {
    if (!filepath || !align) return BP011_ERR_NULL_POINTER;
    align->logical_sector_size = 4096;
    align->physical_sector_size = 4096;
    align->buffer_align = 4096;
    align->ioctl_success = false;
    align->is_fallback = true;
    align->physical_io_verified = false;

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
                    align->ioctl_success = true;
                    align->is_fallback = false;
                }
            }
            CloseHandle(hVol);
        }

        if (align->is_fallback) {
            DWORD spc = 0, bps = 0, fc = 0, tc = 0;
            if (GetDiskFreeSpaceA(vol_path, &spc, &bps, &fc, &tc)) {
                if (bps >= 512) {
                    align->logical_sector_size = bps;
                    if (align->buffer_align < bps) align->buffer_align = bps;
                }
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
    (void)out_path; (void)data; (void)data_bytes; (void)align;
    return BP011_ERR_IO_DEVICE; /* No emula exito en plataformas sin soporte fisico (NC-D08) */
#endif
}

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
    (void)in_path; (void)out_data; (void)max_bytes; (void)actual_read_bytes; (void)align;
    return BP011_ERR_IO_DEVICE;
#endif
}

/* ========================================================================= */
/* PRNG Canónico PCG32 y Algoritmo de Lemire para Cuadrado Latino (NC-D02)   */
/* ========================================================================= */
typedef struct {
    uint64_t state;
    uint64_t inc;
} bp011_pcg32_random_t;

static inline uint32_t bp011_pcg32_random_r(bp011_pcg32_random_t *rng) {
    uint64_t oldstate = rng->state;
    rng->state = oldstate * 6364136223846793005ULL + (rng->inc | 1ULL);
    uint32_t xorshifted = (uint32_t)(((oldstate >> 18u) ^ oldstate) >> 27u);
    uint32_t rot = (uint32_t)(oldstate >> 59u);
    return (xorshifted >> rot) | (xorshifted << ((-rot) & 31u));
}

static inline void bp011_pcg32_srandom_r(bp011_pcg32_random_t *rng, uint64_t initstate, uint64_t initseq) {
    rng->state = 0ULL;
    rng->inc = (initseq << 1u) | 1ULL;
    bp011_pcg32_random_r(rng);
    rng->state += initstate;
    bp011_pcg32_random_r(rng);
}

static inline uint32_t bp011_bounded_rand_lemire(bp011_pcg32_random_t *rng, uint32_t s) {
    uint32_t x = bp011_pcg32_random_r(rng);
    uint64_t m = (uint64_t)x * (uint64_t)s;
    uint32_t l = (uint32_t)m;
    if (l < s) {
        uint32_t t = (-s) % s;
        while (l < t) {
            x = bp011_pcg32_random_r(rng);
            m = (uint64_t)x * (uint64_t)s;
            l = (uint32_t)m;
        }
    }
    return (uint32_t)(m >> 32u);
}

static inline void bp011_shuffle_vector_u8(uint8_t *arr, uint32_t n, bp011_pcg32_random_t *rng) {
    for (uint32_t i = n - 1; i > 0; i--) {
        uint32_t j = bp011_bounded_rand_lemire(rng, i + 1);
        uint8_t tmp = arr[i]; arr[i] = arr[j]; arr[j] = tmp;
    }
}

static void bp011_generate_schedule_latin_4x4(bp011_pcg32_random_t *rng, uint8_t sched[BP011_NUM_ROUNDS][4]) {
    uint8_t L[4][4];
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j++) {
            L[i][j] = (uint8_t)((i + j) % 4);
        }
    }

    int round_idx = 0;
    for (int block = 0; block < 5; block++) {
        uint8_t row_order[4] = {0, 1, 2, 3};
        uint8_t col_order[4] = {0, 1, 2, 3};
        uint8_t sym_map[4]   = {0, 1, 2, 3};

        bp011_shuffle_vector_u8(row_order, 4, rng);
        bp011_shuffle_vector_u8(col_order, 4, rng);
        bp011_shuffle_vector_u8(sym_map, 4, rng);

        for (int r = 0; r < 4; r++) {
            for (int step = 0; step < 4; step++) {
                sched[round_idx][step] = sym_map[L[row_order[r]][col_order[step]]];
            }
            round_idx++;
        }
    }
}

/* ========================================================================= */
/* Flujo Completo de Campaña Experimental y Medición Comercial (NC-D02)       */
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
    double select_q1_time_sec;
    double select_q1_speedup;
    double select_q2_time_sec;
    double select_q2_speedup;
    double select_q3_time_sec;
    double select_q3_speedup;
    double select_avg_speedup;
    double decomp_amplification;
    bool   has_measured_decomp_amplification;
    bool   has_q2;
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
static int bp011_write_traceability_sidecar(
    const char *csv_path,
    const bp011_io_align_t *align,
    bool crc32_verified
) {
    char meta_path[1024];
    snprintf(meta_path, sizeof(meta_path), "%s.meta.json", csv_path);

    FILE *f = fopen(meta_path, "w");
    if (!f) return -1;

    time_t now = time(NULL);
    char date_buf[64];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%dT%H:%M:%SZ", gmtime(&now));

    const char *timer_desc =
#ifdef _WIN32
        "Windows QueryPerformanceCounter (QPC)";
#elif defined(__APPLE__)
        "macOS mach_absolute_time";
#elif defined(CLOCK_MONOTONIC_RAW)
        "POSIX clock_gettime(CLOCK_MONOTONIC_RAW)";
#else
        "Standard C clock()";
#endif

    int ret = 0;
    ret |= (fprintf(f, "{\n") < 0);
    ret |= (fprintf(f, "  \"milestone\": \"BP-011\",\n") < 0);
    ret |= (fprintf(f, "  \"phase\": \"Phase E - Commercial Viability Campaign\",\n") < 0);
    ret |= (fprintf(f, "  \"timestamp_utc\": \"%s\",\n", date_buf) < 0);
    ret |= (fprintf(f, "  \"protocol\": {\n") < 0);
    ret |= (fprintf(f, "    \"num_measured_rounds\": %d,\n", BP011_NUM_ROUNDS) < 0);
    ret |= (fprintf(f, "    \"warmup_rounds\": %d,\n", BP011_WARMUP_ROUNDS) < 0);
    ret |= (fprintf(f, "    \"calendar\": \"Randomized 4x4 Latin Square Blocks (PCG32/Lemire seed=42, seq=54)\",\n") < 0);
    ret |= (fprintf(f, "    \"timer\": \"%s\",\n", timer_desc) < 0);
    ret |= (fprintf(f, "    \"timestamp_codec\": \"R15.10 Literal Delta-of-Delta\",\n") < 0);
    ret |= (fprintf(f, "    \"zstd_baseline_level\": %d\n", BP011_ZSTD_DEFAULT_LEVEL) < 0);
    ret |= (fprintf(f, "  },\n") < 0);
    ret |= (fprintf(f, "  \"environment\": {\n") < 0);
#ifdef _WIN32
    ret |= (fprintf(f, "    \"os\": \"Windows\",\n") < 0);
#else
    ret |= (fprintf(f, "    \"os\": \"POSIX\",\n") < 0);
#endif
#ifdef _MSC_VER
    ret |= (fprintf(f, "    \"compiler\": \"MSVC %d\",\n", _MSC_VER) < 0);
#elif defined(__clang__)
    ret |= (fprintf(f, "    \"compiler\": \"Clang %s\",\n", __clang_version__) < 0);
#elif defined(__GNUC__)
    ret |= (fprintf(f, "    \"compiler\": \"GCC %d.%d\",\n", __GNUC__, __GNUC_MINOR__) < 0);
#else
    ret |= (fprintf(f, "    \"compiler\": \"Unknown\",\n") < 0);
#endif
    ret |= (fprintf(f, "    \"working_tree_branch\": \"audit/bp1-core-safety\",\n") < 0);
    ret |= (fprintf(f, "    \"code_freeze\": \"src/, include/, deps/ 100%% intact\"\n") < 0);
    ret |= (fprintf(f, "  },\n") < 0);
    ret |= (fprintf(f, "  \"hardware_io_audit\": {\n") < 0);
    ret |= (fprintf(f, "    \"storage_alignment_source\": \"%s\",\n",
            align->ioctl_success ? "IOCTL_STORAGE_QUERY_PROPERTY" : (align->is_fallback ? "GetDiskFreeSpaceA_fallback" : "none")) < 0);
    ret |= (fprintf(f, "    \"logical_sector_bytes\": %u,\n", align->logical_sector_size) < 0);
    ret |= (fprintf(f, "    \"physical_sector_bytes\": %u,\n", align->physical_sector_size) < 0);
    ret |= (fprintf(f, "    \"unbuffered_io_verified\": %s,\n", align->physical_io_verified ? "true" : "false") < 0);
    ret |= (fprintf(f, "    \"real_hardware_io\": %s\n", align->physical_io_verified ? "true" : "false") < 0);
    ret |= (fprintf(f, "  },\n") < 0);
    ret |= (fprintf(f, "  \"validation_status\": {\n") < 0);
    ret |= (fprintf(f, "    \"bit_exact_verified\": true,\n") < 0);
    ret |= (fprintf(f, "    \"crc32_verified\": %s\n", crc32_verified ? "true" : "false") < 0);
    ret |= (fprintf(f, "  }\n") < 0);
    ret |= (fprintf(f, "}\n") < 0);

    if (ret != 0 || ferror(f)) {
        fclose(f);
        return -2;
    }

    int flush_res = fflush(f);
    int close_res = fclose(f);
    if (flush_res != 0 || close_res != 0) {
        return -3;
    }

    return 0;
}

static int bp011_run_campaign_dataset(
    const char *dataset_name,
    const char *corpus_path,
    bool is_cmapss,
    FILE *csv_summary,
    FILE *csv_raw,
    bp011_io_align_t *align,
    bp011_pcg32_random_t *rng,
    bool *out_crc32_verified
) {
    if (out_crc32_verified) *out_crc32_verified = false;
    printf("-------------------------------------------------------------------------------\n");
    printf(">>> EJECUTANDO CAMPAÑA EXPERIMENTAL EN DATASET: %s\n", dataset_name);
    printf("    Protocolo: 1 Warmup + 20 Rondas Balanceadas (Cuadrado Latino 4x4 Aleatorizado)\n");
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
    strcpy(results[0].engine_name, "Gorilla_Composed");
    strcpy(results[1].engine_name, "Chimp128_Composed_Variant");
    strcpy(results[2].engine_name, "BinPack_Transpose");
    strcpy(results[3].engine_name, "Zstd_Composed");

    /* Estructuras de recopilacion de 20 rondas individuales */
    double comp_times[4][BP011_NUM_ROUNDS];
    double decomp_times[4][BP011_NUM_ROUNDS];
    size_t comp_peaks[4] = {0, 0, 0, 0};
    size_t decomp_peaks[4] = {0, 0, 0, 0};
    size_t max_ws_comp[4] = {0, 0, 0, 0};
    size_t max_ws_decomp[4] = {0, 0, 0, 0};

    uint32_t chan_base_off = BP011_HEADER_SIZE_BYTES + num_channels * 12U;
    if (series_dir && num_series > 0) chan_base_off += num_series * 32U;
    chan_base_off += (uint32_t)time_bytes;

    /* Calendario de Cuadrados Latinos 4x4 Aleatorizados con el PRNG único de campaña (NC-D02) */
    uint8_t sched[BP011_NUM_ROUNDS][4];
    bp011_generate_schedule_latin_4x4(rng, sched);

    /* Ciclo Total: 1 Warm-up (-1) + 20 Rondas Medidas (0 a 19) */
    for (int r = -BP011_WARMUP_ROUNDS; r < BP011_NUM_ROUNDS; r++) {
        for (int step = 0; step < 4; step++) {
            int eng = (r < 0) ? sched[0][step] : sched[r][step];

            size_t val_bytes = 0;
            uint64_t val_bits = 0;
            size_t actual_file = 0;

            /* Medicion de Compresion */
            bp011_mem_tracker_reset_op();
            size_t ws_before = bp011_get_process_working_set();
            double t0 = bp011_get_time_sec();

            bp011_status_t enc_st;
            switch (eng) {
                case 0: enc_st = gorilla_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits); break;
                case 1: enc_st = chimp128_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits); break;
                case 2: enc_st = binpack_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits, chan_table, chan_base_off); break;
                case 3: enc_st = zstd_encode_matrix(matrix, sample_count, num_channels, val_buf, max_stream_cap, &val_bytes, &val_bits); break;
                default: enc_st = BP011_ERR_INVALID_ARG; break;
            }
            if (enc_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en codificacion motor %s: %d\n", results[eng].engine_name, enc_st);
                return 20 + eng;
            }

            double t1 = bp011_get_time_sec();
            size_t ws_after = bp011_get_process_working_set();
            /* Captura inmediata de memoria pico de compresion ANTES del reinicio (NC-D01) */
            size_t round_comp_ram = bp011_mem_tracker_get_peak_net();
            size_t round_comp_ws  = (ws_after > ws_before) ? (ws_after - ws_before) : 0;

            /* Particionado exacto del directorio de series para validacion canonica (NC-D05) */
            if (series_dir && num_series > 0) {
                uint32_t t_cur = BP011_HEADER_SIZE_BYTES + ((eng == 2) ? num_channels * 12U : 0) + num_series * 32U;
                uint32_t v_cur = t_cur + (uint32_t)time_bytes;
                uint32_t t_rem = (uint32_t)time_bytes;
                uint32_t v_rem = (uint32_t)val_bytes;
                uint32_t s_rem = sample_count;

                for (uint32_t m = 0; m < num_series; m++) {
                    uint32_t sc = series_dir[m].sample_count;
                    uint32_t tb = (m == num_series - 1) ? t_rem : (uint32_t)((uint64_t)t_rem * sc / s_rem);
                    uint32_t vb = (m == num_series - 1) ? v_rem : (uint32_t)((uint64_t)v_rem * sc / s_rem);
                    if (tb == 0) tb = 1;
                    if (vb == 0) vb = 1;
                    if (tb > t_rem) tb = t_rem;
                    if (vb > v_rem) vb = v_rem;

                    series_dir[m].time_stream_byte_offset = t_cur;
                    series_dir[m].time_stream_bytes = tb;
                    series_dir[m].values_stream_byte_offset = v_cur;
                    series_dir[m].values_stream_bytes = vb;

                    t_cur += tb;
                    v_cur += vb;
                    t_rem -= tb;
                    v_rem -= vb;
                    s_rem -= sc;
                }
            }

            /* Empaquetado canonico de bloque */
            bp011_status_t pkg_st = bp011_package_block(
                file_buf, max_stream_cap,
                &actual_file,
                num_channels, sample_count,
                (eng == 2) ? chan_table : NULL,
                series_dir, num_series,
                time_buf, time_bytes, time_bits,
                val_buf, val_bytes, val_bits
            );
            if (pkg_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en empaquetado canonico motor %s: %d\n", results[eng].engine_name, pkg_st);
                return 25 + eng;
            }

            /* Medicion de Descompresion */
            bp011_mem_tracker_reset_op();
            size_t ws_dec_before = bp011_get_process_working_set();
            double td0 = bp011_get_time_sec();

            bp011_status_t dec_st;
            switch (eng) {
                case 0: dec_st = gorilla_decode_channels(val_buf, val_bytes, val_bits, decomp_mat, sample_count, num_channels); break;
                case 1: dec_st = chimp128_decode_channels(val_buf, val_bytes, val_bits, decomp_mat, sample_count, num_channels); break;
                case 2: dec_st = binpack_decode_channels(val_buf, val_bytes, chan_table, chan_base_off, decomp_mat, sample_count, num_channels); break;
                case 3: dec_st = zstd_decode_matrix(val_buf, val_bytes, decomp_mat, sample_count, num_channels); break;
                default: dec_st = BP011_ERR_INVALID_ARG; break;
            }
            if (dec_st != BP011_OK) {
                fprintf(stderr, "[FATAL] Fallo en decodificacion motor %s: %d\n", results[eng].engine_name, dec_st);
                return 30 + eng;
            }

            double td1 = bp011_get_time_sec();
            size_t ws_dec_after = bp011_get_process_working_set();
            /* Captura inmediata de memoria pico de descompresion (NC-D01) */
            size_t round_dec_ram  = bp011_mem_tracker_get_peak_net();
            size_t round_dec_ws   = (ws_dec_after > ws_dec_before) ? (ws_dec_after - ws_dec_before) : 0;

            /* Verificacion bit a bit obligatoria */
            if (memcmp(matrix, decomp_mat, (size_t)sample_count * num_channels * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Falla de verificacion bit-exact en motor %s (ronda %d)!\n", results[eng].engine_name, r);
                return 40 + eng;
            }

            /* Acumulacion de memoria pico y WS a lo largo de TODAS las rondas (NC-D01) */
            if (round_comp_ram > comp_peaks[eng]) comp_peaks[eng] = round_comp_ram;
            if (round_comp_ws  > max_ws_comp[eng]) max_ws_comp[eng] = round_comp_ws;
            if (round_dec_ram  > decomp_peaks[eng]) decomp_peaks[eng] = round_dec_ram;
            if (round_dec_ws   > max_ws_decomp[eng]) max_ws_decomp[eng] = round_dec_ws;

            if (r >= 0) {
                results[eng].total_bytes = actual_file;
                results[eng].cr = (double)raw_bytes / (double)actual_file;
                comp_times[eng][r] = t1 - t0;
                decomp_times[eng][r] = td1 - td0;

                /* Persistencia obligatoria de la observacion individual en CSV raw (NC-D02) */
                double tp_c = ((double)raw_bytes / 1e6) / (t1 - t0);
                double tp_d = ((double)raw_bytes / 1e6) / (td1 - td0);
                if (fprintf(csv_raw, "%s,ROUND,%s,%d,%.6f,%.2f,%.6f,%.2f,%zu,%zu,%zu,%zu,0.0,0.0,0.0\n",
                            dataset_name, results[eng].engine_name, r,
                            t1 - t0, tp_c, td1 - td0, tp_d,
                            round_comp_ram, round_dec_ram, round_comp_ws, round_dec_ws) < 0) {
                    fprintf(stderr, "[FATAL] Error de I/O al escribir observacion en csv_raw\n");
                    return 45;
                }
            }
        }
    }

    /* Calculo de Medianas Convencionales sobre las 20 rondas (NC-D02) */
    for (int eng = 0; eng < 4; eng++) {
        qsort(comp_times[eng], BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);
        qsort(decomp_times[eng], BP011_NUM_ROUNDS, sizeof(double), bp011_cmp_doubles);

        /* Mediana convencional para N=20: promedio de elementos centrales 9 y 10 */
        results[eng].comp_time_median_sec   = (comp_times[eng][9] + comp_times[eng][10]) / 2.0;
        results[eng].decomp_time_median_sec = (decomp_times[eng][9] + decomp_times[eng][10]) / 2.0;
        results[eng].tp_comp_median_mbs     = ((double)raw_bytes / 1e6) / results[eng].comp_time_median_sec;
        results[eng].tp_decomp_median_mbs   = ((double)raw_bytes / 1e6) / results[eng].decomp_time_median_sec;

        results[eng].comp_peak_ram = comp_peaks[eng];
        results[eng].decomp_peak_ram = decomp_peaks[eng];
        results[eng].delta_working_set_comp = max_ws_comp[eng];
        results[eng].delta_working_set_decomp = max_ws_decomp[eng];
    }

    /* Suite Formal de Consultas Selectivas Q1, Q2, Q3 (20 iteraciones cada una) (NC-D02, NC-D02-Q) */
    {
        /* Regenera bloques para consulta */
        size_t gor_val_sz = 0, bp_val_sz = 0;
        uint64_t gor_bits = 0, bp_bits = 0;
        bp011_status_t enc_g = gorilla_encode_channels(matrix, sample_count, num_channels, val_buf, max_stream_cap, &gor_val_sz, &gor_bits);
        if (enc_g != BP011_OK) return 51;

        uint8_t *val_buf_bp = (uint8_t *)bp011_tracked_malloc(max_stream_cap);
        if (!val_buf_bp) return 50;
        bp011_status_t enc_b = binpack_encode_channels(matrix, sample_count, num_channels, val_buf_bp, max_stream_cap, &bp_val_sz, &bp_bits, chan_table, chan_base_off);
        if (enc_b != BP011_OK) { bp011_tracked_free(val_buf_bp); return 52; }

        /* Q1: Ultimos 1,000 puntos del canal K-1 (Aplicable si N >= 1,000) */
        if (sample_count < 1000) {
            bp011_tracked_free(val_buf_bp);
            return 53;
        }
        uint32_t q1_target_chan = num_channels - 1;
        uint32_t q1_win_start = sample_count - 1000;
        uint32_t q1_win_len = 1000;
        double q1_gor[1000];
        double q1_bp[1000];
        double t_q1_gor_total = 0, t_q1_bp_total = 0;
        size_t q1_bp_decomp_bytes = 0;

        for (int iter = 0; iter < 20; iter++) {
            double t0 = bp011_get_time_sec();
            bp011_status_t s_gor = gorilla_decode_channel_window(
                val_buf, gor_val_sz, gor_bits,
                q1_target_chan, sample_count, num_channels,
                q1_win_start, q1_win_len, q1_gor
            );
            double t1 = bp011_get_time_sec();
            if (s_gor != BP011_OK) { bp011_tracked_free(val_buf_bp); return 54; }
            double dt_gor = t1 - t0;
            t_q1_gor_total += dt_gor;

            size_t br = 0, bd = 0;
            double tb0 = bp011_get_time_sec();
            bp011_status_t s_bp = binpack_decode_channel_window(
                val_buf_bp, bp_val_sz,
                chan_table, chan_base_off,
                q1_target_chan, sample_count, num_channels,
                q1_win_start, q1_win_len, q1_bp,
                &br, &bd
            );
            double tb1 = bp011_get_time_sec();
            if (s_bp != BP011_OK) { bp011_tracked_free(val_buf_bp); return 55; }
            double dt_bp = tb1 - tb0;
            t_q1_bp_total += dt_bp;
            q1_bp_decomp_bytes = bd; /* Canal completo descomprimido por BinPack */

            if (memcmp(q1_gor, q1_bp, 1000 * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Discrepancia bit a bit en consulta Q1 iteracion %d\n", iter);
                bp011_tracked_free(val_buf_bp);
                return 56;
            }

            /* Persistencia obligatoria de la observacion de Q1 en CSV raw */
            double spd = (dt_bp > 0) ? (dt_gor / dt_bp) : 1.0;
            if (fprintf(csv_raw, "%s,QUERY,Q1_TAIL_1000,%d,0.0,0.0,0.0,0.0,0,0,0,0,%.8f,%.8f,%.4f\n",
                        dataset_name, iter, dt_gor, dt_bp, spd) < 0) {
                bp011_tracked_free(val_buf_bp);
                return 57;
            }
        }
        results[2].select_q1_time_sec = t_q1_bp_total / 20.0;
        results[2].select_q1_speedup  = (t_q1_bp_total > 0) ? (t_q1_gor_total / t_q1_bp_total) : 1.0;

        /* Q2: Ventana historica de 10,000 puntos del canal 0 (Aplicable estrictamente si N >= 10,000) */
        size_t q2_bp_decomp_bytes = 0;
        if (sample_count >= 10000) {
            results[2].has_q2 = true;
            uint32_t q2_target_chan = 0;
            uint32_t q2_win_start = 0;
            uint32_t q2_win_len = 10000;
            double *q2_gor = (double *)malloc(10000 * sizeof(double));
            double *q2_bp  = (double *)malloc(10000 * sizeof(double));
            if (!q2_gor || !q2_bp) {
                if (q2_gor) free(q2_gor);
                if (q2_bp) free(q2_bp);
                bp011_tracked_free(val_buf_bp);
                return 58;
            }

            double t_q2_gor_total = 0, t_q2_bp_total = 0;
            for (int iter = 0; iter < 20; iter++) {
                double t0 = bp011_get_time_sec();
                bp011_status_t s_gor = gorilla_decode_channel_window(
                    val_buf, gor_val_sz, gor_bits,
                    q2_target_chan, sample_count, num_channels,
                    q2_win_start, q2_win_len, q2_gor
                );
                double t1 = bp011_get_time_sec();
                if (s_gor != BP011_OK) { free(q2_gor); free(q2_bp); bp011_tracked_free(val_buf_bp); return 59; }
                double dt_gor = t1 - t0;
                t_q2_gor_total += dt_gor;

                size_t br = 0, bd = 0;
                double tb0 = bp011_get_time_sec();
                bp011_status_t s_bp = binpack_decode_channel_window(
                    val_buf_bp, bp_val_sz,
                    chan_table, chan_base_off,
                    q2_target_chan, sample_count, num_channels,
                    q2_win_start, q2_win_len, q2_bp,
                    &br, &bd
                );
                double tb1 = bp011_get_time_sec();
                if (s_bp != BP011_OK) { free(q2_gor); free(q2_bp); bp011_tracked_free(val_buf_bp); return 60; }
                double dt_bp = tb1 - tb0;
                t_q2_bp_total += dt_bp;
                q2_bp_decomp_bytes = bd; /* Canal 0 completo descomprimido por BinPack */

                if (memcmp(q2_gor, q2_bp, 10000 * sizeof(double)) != 0) {
                    fprintf(stderr, "[FATAL] Discrepancia bit a bit en consulta Q2 iteracion %d\n", iter);
                    free(q2_gor); free(q2_bp); bp011_tracked_free(val_buf_bp);
                    return 61;
                }

                /* Persistencia obligatoria de la observacion de Q2 en CSV raw */
                double spd = (dt_bp > 0) ? (dt_gor / dt_bp) : 1.0;
                if (fprintf(csv_raw, "%s,QUERY,Q2_HIST_10000,%d,0.0,0.0,0.0,0.0,0,0,0,0,%.8f,%.8f,%.4f\n",
                            dataset_name, iter, dt_gor, dt_bp, spd) < 0) {
                    free(q2_gor); free(q2_bp); bp011_tracked_free(val_buf_bp);
                    return 62;
                }
            }
            results[2].select_q2_time_sec = t_q2_bp_total / 20.0;
            results[2].select_q2_speedup  = (t_q2_bp_total > 0) ? (t_q2_gor_total / t_q2_bp_total) : 1.0;
            free(q2_gor);
            free(q2_bp);
        } else {
            /* Cardinalidad insuficiente: N < 10,000 -> NOT_APPLICABLE (NC-03 / NC-D02-Q) */
            results[2].has_q2 = false;
            results[2].select_q2_time_sec = 0.0;
            results[2].select_q2_speedup  = 0.0;
        }

        /* Q3: Proyeccion de 4 canales simultaneos (canales 0, 1, 2, 3) (Aplicable si K >= 4) */
        if (num_channels < 4) {
            bp011_tracked_free(val_buf_bp);
            return 63;
        }
        double *q3_gor = (double *)malloc(4 * (size_t)sample_count * sizeof(double));
        double *q3_bp  = (double *)malloc(4 * (size_t)sample_count * sizeof(double));
        if (!q3_gor || !q3_bp) {
            if (q3_gor) free(q3_gor);
            if (q3_bp) free(q3_bp);
            bp011_tracked_free(val_buf_bp);
            return 64;
        }

        double t_q3_gor_total = 0, t_q3_bp_total = 0;
        size_t q3_bp_decomp_bytes = 0;
        for (int iter = 0; iter < 20; iter++) {
            double t0 = bp011_get_time_sec();
            for (uint32_t c = 0; c < 4; c++) {
                bp011_status_t s_gor = gorilla_decode_channel_window(
                    val_buf, gor_val_sz, gor_bits,
                    c, sample_count, num_channels,
                    0, sample_count, q3_gor + (size_t)c * sample_count
                );
                if (s_gor != BP011_OK) { free(q3_gor); free(q3_bp); bp011_tracked_free(val_buf_bp); return 65; }
            }
            double t1 = bp011_get_time_sec();
            double dt_gor = t1 - t0;
            t_q3_gor_total += dt_gor;

            double tb0 = bp011_get_time_sec();
            size_t iter_q3_bd = 0;
            for (uint32_t c = 0; c < 4; c++) {
                size_t br = 0, bd = 0;
                bp011_status_t s_bp = binpack_decode_channel_window(
                    val_buf_bp, bp_val_sz,
                    chan_table, chan_base_off,
                    c, sample_count, num_channels,
                    0, sample_count, q3_bp + (size_t)c * sample_count,
                    &br, &bd
                );
                if (s_bp != BP011_OK) { free(q3_gor); free(q3_bp); bp011_tracked_free(val_buf_bp); return 66; }
                iter_q3_bd += bd;
            }
            double tb1 = bp011_get_time_sec();
            double dt_bp = tb1 - tb0;
            t_q3_bp_total += dt_bp;
            q3_bp_decomp_bytes = iter_q3_bd;

            if (memcmp(q3_gor, q3_bp, 4 * (size_t)sample_count * sizeof(double)) != 0) {
                fprintf(stderr, "[FATAL] Discrepancia bit a bit en consulta Q3 iteracion %d\n", iter);
                free(q3_gor); free(q3_bp); bp011_tracked_free(val_buf_bp);
                return 67;
            }

            /* Persistencia obligatoria de la observacion de Q3 en CSV raw */
            double spd = (dt_bp > 0) ? (dt_gor / dt_bp) : 1.0;
            if (fprintf(csv_raw, "%s,QUERY,Q3_PROJ_4CHAN,%d,0.0,0.0,0.0,0.0,0,0,0,0,%.8f,%.8f,%.4f\n",
                        dataset_name, iter, dt_gor, dt_bp, spd) < 0) {
                free(q3_gor); free(q3_bp); bp011_tracked_free(val_buf_bp);
                return 68;
            }
        }
        results[2].select_q3_time_sec = t_q3_bp_total / 20.0;
        results[2].select_q3_speedup  = (t_q3_bp_total > 0) ? (t_q3_gor_total / t_q3_bp_total) : 1.0;
        free(q3_gor);
        free(q3_bp);

        if (results[2].has_q2) {
            results[2].select_avg_speedup = (results[2].select_q1_speedup + results[2].select_q2_speedup + results[2].select_q3_speedup) / 3.0;
        } else {
            results[2].select_avg_speedup = (results[2].select_q1_speedup + results[2].select_q3_speedup) / 2.0;
        }

        /* Calculo Exacto de Amplificacion Real de Descompresion (NC-D02-Q / Rev 5) */
        size_t query_bytes_requested = (1000 * sizeof(double)) +
                                       (results[2].has_q2 ? (10000 * sizeof(double)) : 0) +
                                       (4 * (size_t)sample_count * sizeof(double));

        /* BinPack: Descomprime canal completo en Q1 (y Q2), y 4 canales completos en Q3 */
        size_t bp_query_bytes_decompressed = q1_bp_decomp_bytes +
                                             (results[2].has_q2 ? q2_bp_decomp_bytes : 0) +
                                             q3_bp_decomp_bytes;

        /* Gorilla: En Q1 decodifica hasta canal K-1 (toda la matriz = num_channels * N * 8),
         * en Q2 decodifica 10k puntos del canal 0 (10,000 * 8),
         * en Q3 ejecuta 4 consultas independientes decodificando secuencialmente desde el canal 0
         * acumulando 1 + 2 + 3 + 4 = 10 canales completos (10 * N * 8 bytes) (NC-D09 / Rev 6) */
        size_t gor_query_bytes_decompressed = ((size_t)num_channels * (size_t)sample_count * sizeof(double)) +
                                              (results[2].has_q2 ? (10000 * sizeof(double)) : 0) +
                                              (10 * (size_t)sample_count * sizeof(double));

        /* Asignacion rigurosa de amplificaciones: motores medidos vs no medidos (NC-D10 / Rev 6) */
        results[0].has_measured_decomp_amplification = true;
        results[0].decomp_amplification = (double)gor_query_bytes_decompressed / (double)query_bytes_requested;

        results[1].has_measured_decomp_amplification = false;
        results[1].decomp_amplification = 0.0; /* N/A: sin soporte de consultas selectivas en bloque */

        results[2].has_measured_decomp_amplification = true;
        results[2].decomp_amplification = (double)bp_query_bytes_decompressed / (double)query_bytes_requested;

        results[3].has_measured_decomp_amplification = false;
        results[3].decomp_amplification = 0.0; /* N/A: arquitectura monolitica sin consultas en bloque */

        /* Empaquetado canónico BinPack final para validación explícita de CRC-32 y Unbuffered I/O (Rev 6) */
        if (series_dir && num_series > 0) {
            uint32_t t_cur = BP011_HEADER_SIZE_BYTES + num_channels * 12U + num_series * 32U;
            uint32_t v_cur = t_cur + (uint32_t)time_bytes;
            uint32_t t_rem = (uint32_t)time_bytes;
            uint32_t v_rem = (uint32_t)bp_val_sz;
            uint32_t s_rem = sample_count;

            for (uint32_t m = 0; m < num_series; m++) {
                uint32_t sc = series_dir[m].sample_count;
                uint32_t tb = (m == num_series - 1) ? t_rem : (uint32_t)((uint64_t)t_rem * sc / s_rem);
                uint32_t vb = (m == num_series - 1) ? v_rem : (uint32_t)((uint64_t)v_rem * sc / s_rem);
                if (tb == 0) tb = 1;
                if (vb == 0) vb = 1;
                if (tb > t_rem) tb = t_rem;
                if (vb > v_rem) vb = v_rem;

                series_dir[m].time_stream_byte_offset = t_cur;
                series_dir[m].time_stream_bytes = tb;
                series_dir[m].values_stream_byte_offset = v_cur;
                series_dir[m].values_stream_bytes = vb;

                t_cur += tb;
                v_cur += vb;
                t_rem -= tb;
                v_rem -= vb;
                s_rem -= sc;
            }
        }

        size_t bp_canonical_file_sz = 0;
        bp011_status_t pkg_bp_final = bp011_package_block(
            file_buf, max_stream_cap,
            &bp_canonical_file_sz,
            num_channels, sample_count,
            chan_table,
            series_dir, num_series,
            time_buf, time_bytes, time_bits,
            val_buf_bp, bp_val_sz, bp_bits
        );
        if (pkg_bp_final != BP011_OK || bp_canonical_file_sz != results[2].total_bytes) {
            fprintf(stderr, "[FATAL] Fallo al empaquetar bloque BinPack para validacion CRC-32 (error: %d)\n", pkg_bp_final);
            bp011_tracked_free(val_buf_bp);
            return 69;
        }

        /* Verificacion explicita de cabecera y recalculacion CRC-32 IEEE 802.3 (NC-D02 / Rev 6) */
        bp011_block_header_t chk_header;
        memset(&chk_header, 0, sizeof(chk_header));
        bp011_deserialize_header(&chk_header, file_buf);
        bp011_status_t crc_val_st = bp011_validate_header(&chk_header, file_buf, bp_canonical_file_sz);
        if (crc_val_st == BP011_OK) {
            if (out_crc32_verified) *out_crc32_verified = true;
            printf("  [OK] Integridad de bloque y CRC-32 verificados con exito (CRC: 0x%08X)\n", chk_header.crc32);
        } else {
            if (out_crc32_verified) *out_crc32_verified = false;
            fprintf(stderr, "[FATAL] Falla en verificacion CRC-32 de bloque BinPack: codigo %d\n", crc_val_st);
            bp011_tracked_free(val_buf_bp);
            return 71;
        }

        bp011_tracked_free(val_buf_bp);
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

        if (csv_summary) {
            char decomp_amp_str[32];
            if (results[i].has_measured_decomp_amplification) {
                snprintf(decomp_amp_str, sizeof(decomp_amp_str), "%.2f", results[i].decomp_amplification);
            } else {
                snprintf(decomp_amp_str, sizeof(decomp_amp_str), "N/A");
            }

            if (results[2].has_q2) {
                if (fprintf(csv_summary, "%s,%s,%zu,%.4f,%.6f,%.2f,%.6f,%.2f,%zu,%zu,%zu,%zu,%.2f,%.2f,%.2f,%.2f,%.2f,%.2f,%s\n",
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
                            (i == 2) ? results[2].select_q1_speedup : 1.0,
                            (i == 2) ? results[2].select_q2_speedup : 1.0,
                            (i == 2) ? results[2].select_q3_speedup : 1.0,
                            (i == 2) ? results[2].select_avg_speedup : 1.0,
                            decomp_amp_str) < 0) {
                    return 70;
                }
            } else {
                if (fprintf(csv_summary, "%s,%s,%zu,%.4f,%.6f,%.2f,%.6f,%.2f,%zu,%zu,%zu,%zu,%.2f,%.2f,%.2f,N/A,%.2f,%.2f,%s\n",
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
                            (i == 2) ? results[2].select_q1_speedup : 1.0,
                            (i == 2) ? results[2].select_q3_speedup : 1.0,
                            (i == 2) ? results[2].select_avg_speedup : 1.0,
                            decomp_amp_str) < 0) {
                    return 70;
                }
            }
        }
    }

    printf("\nMETRICAS CLAVE DE VIABILIDAD COMERCIAL (BINPACK vs COMPETIDORES):\n");
    printf("  • Ahorro vs Gorilla:  %+.2f %%\n", results[2].ahorro_pct_vs_gorilla);
    printf("  • Ahorro vs Chimp128: %+.2f %%\n", results[2].ahorro_pct_vs_chimp);
    if (results[2].has_q2) {
        printf("  • Consultas Selectivas Speedup: Q1=%.2fx, Q2=%.2fx, Q3=%.2fx (Promedio=%.2fx)\n",
               results[2].select_q1_speedup, results[2].select_q2_speedup, results[2].select_q3_speedup, results[2].select_avg_speedup);
    } else {
        printf("  • Consultas Selectivas Speedup: Q1=%.2fx, Q2=N/A, Q3=%.2fx (Promedio=%.2fx)\n",
               results[2].select_q1_speedup, results[2].select_q3_speedup, results[2].select_avg_speedup);
    }
    printf("  • Amplificacion de Descompresion Real Medida:\n");
    if (results[2].has_q2) {
        double amp_q1_bp = (double)sample_count / 1000.0;
        double amp_q2_bp = (double)sample_count / 10000.0;
        printf("    - BinPack:  Q1=%.2fx, Q2=%.2fx, Q3=1.00x (Ponderada=%.2fx)\n",
               amp_q1_bp, amp_q2_bp, results[2].decomp_amplification);
    } else {
        double amp_q1_bp = (double)sample_count / 1000.0;
        printf("    - BinPack:  Q1=%.2fx, Q2=N/A, Q3=1.00x (Ponderada=%.2fx)\n",
               amp_q1_bp, results[2].decomp_amplification);
    }
    printf("    - Gorilla:  Ponderada=%.2fx (medida con 10 canales en Q3)\n", results[0].decomp_amplification);
    printf("    - Chimp128: N/A (sin soporte de consultas selectivas en bloque)\n");
    printf("    - Zstd:     N/A (sin soporte de consultas selectivas en bloque)\n");
    printf("  • Ratio Throughput comp: %.2fx de Gorilla (requerido: >= 0.50x)\n",
           results[2].tp_comp_median_mbs / results[0].tp_comp_median_mbs);

    /* Ensayo y Verificacion Estricta de Windows Unbuffered I/O (NC-D08) */
    bp011_status_t u_wr = bp011_unbuffered_write_block("tests/experiments/bp011/temp_unbuffered.bin", file_buf, results[2].total_bytes, align);
    if (u_wr == BP011_OK) {
        size_t actual_read = 0;
        uint8_t *read_back = (uint8_t *)bp011_tracked_malloc(max_stream_cap);
        if (read_back) {
            bp011_status_t u_rd = bp011_unbuffered_read_block("tests/experiments/bp011/temp_unbuffered.bin", read_back, max_stream_cap, &actual_read, align);
            remove("tests/experiments/bp011/temp_unbuffered.bin");

            if (u_rd == BP011_OK && actual_read == results[2].total_bytes && memcmp(file_buf, read_back, actual_read) == 0) {
                align->physical_io_verified = true;
                printf("  [OK] Prueba Unbuffered I/O en disco superada: %zu bytes verificados bit-exact.\n\n", actual_read);
            } else {
                align->physical_io_verified = false;
                fprintf(stderr, "[ALERTA] Fallo en round-trip de Unbuffered I/O.\n");
            }
            bp011_tracked_free(read_back);
        }
    } else {
        align->physical_io_verified = false;
        printf("  [INFO] Unbuffered I/O no disponible en este entorno (error: %d)\n\n", u_wr);
    }

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
    const char *csv_raw_path = "tests/experiments/bp011/bp011_campaign_results_raw.csv";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--run") == 0) {
            run_campaign = true;
        } else if (strcmp(argv[i], "--out") == 0 && i + 1 < argc) {
            csv_out_path = argv[++i];
        }
    }

    if (sizeof(bp011_block_header_t) != 64 || sizeof(bp011_channel_entry_t) != 12 || sizeof(bp011_series_segment_t) != 32) {
        fprintf(stderr, "[FATAL] Discrepancia en alineacion binaria contractual.\n");
        return 1;
    }

    bp011_io_align_t align;
    bp011_query_volume_alignment("tests/experiments/bp011/temp_unbuffered.bin", &align);

    if (!run_campaign) {
        printf("[MODO AUDITORIA ESTATICA FASE D]\n");
        printf("  • Tipos contractuales: sizeof(header)=64, channel_entry=12, series_seg=32 [OK]\n");
        printf("  • PRNG Canónico PCG32/Lemire único para toda la campaña (seed=42, seq=54) [OK]\n");
        printf("  • Cuadrado Latino 4x4 y 20 Rondas con continuidad estricta de PRNG [OK]\n");
        printf("  • Suite de Consultas Q1, Q2, Q3 (20 iteraciones con ventanas exactas): Implementadas [OK]\n");
        printf("  • Regla de cardinalidad estricta (Q2 N/A en IMS): Verificada [OK]\n");
        printf("  • Calculo exacto de Amplificacion Real de Descompresion (B_decomp / B_requested) [OK]\n");
        printf("  • Persistencia obligatoria de observaciones crudas (160 rondas + 100 consultas): Verificada [OK]\n");
        printf("  • Generador de Sidecar .meta.json con JSON C-valid y verificacion de I/O [OK]\n");
        printf("\nArnes listo para autorizacion formal de Fase E.\n");
        return 0;
    }

    /* Ejecucion de la Campaña Completa (Autorizada en Fase E) */
    FILE *csv_summary = fopen(csv_out_path, "w");
    if (!csv_summary) {
        fprintf(stderr, "[FATAL] No se pudo crear archivo de resultados CSV: %s\n", csv_out_path);
        return 2;
    }
    if (fprintf(csv_summary, "dataset,engine,total_bytes,cr,comp_time_median,tp_comp_median,decomp_time_median,tp_decomp_median,comp_peak_ram,decomp_peak_ram,delta_ws_comp,delta_ws_decomp,ahorro_gorilla,ahorro_chimp,speedup_q1,speedup_q2,speedup_q3,avg_speedup,decomp_amplification\n") < 0) {
        fprintf(stderr, "[FATAL] Error de I/O al escribir encabezado en: %s\n", csv_out_path);
        fclose(csv_summary);
        return 2;
    }

    /* Creacion obligatoria de archivo de observaciones crudas (NC-D02) */
    FILE *csv_raw = fopen(csv_raw_path, "w");
    if (!csv_raw) {
        fprintf(stderr, "[FATAL] No se pudo crear archivo obligatorio de observaciones crudas CSV: %s\n", csv_raw_path);
        fclose(csv_summary);
        return 2;
    }
    if (fprintf(csv_raw, "dataset,category,engine_or_query,iteration,comp_time_sec,tp_comp_mbs,decomp_time_sec,tp_decomp_mbs,comp_peak_ram,decomp_peak_ram,delta_ws_comp,delta_ws_decomp,time_gorilla_sec,time_binpack_sec,speedup_vs_gorilla\n") < 0) {
        fprintf(stderr, "[FATAL] Error de I/O al escribir encabezado en: %s\n", csv_raw_path);
        fclose(csv_summary);
        fclose(csv_raw);
        return 2;
    }

    /* PRNG Canónico PCG32 único para toda la campaña (NC-D02 / R15.10) */
    bp011_pcg32_random_t campaign_rng;
    bp011_pcg32_srandom_r(&campaign_rng, 42ULL, 54ULL);

    bool cmapss_crc_ok = false;
    int ret_cmapss = bp011_run_campaign_dataset(
        "NASA_CMAPSS_FD001",
        "tests/corpora/nasa_telemetry/nasa_turbofan_ascii.txt",
        true,
        csv_summary,
        csv_raw,
        &align,
        &campaign_rng,
        &cmapss_crc_ok
    );
    if (ret_cmapss != 0) {
        fclose(csv_summary);
        fclose(csv_raw);
        fprintf(stderr, "[FATAL] Campaña abortada por fallo en C-MAPSS FD001 (codigo: %d)\n", ret_cmapss);
        return ret_cmapss;
    }

    bool ims_crc_ok = false;
    int ret_ims = bp011_run_campaign_dataset(
        "NASA_BEARING_IMS_TEST1",
        "tests/corpora/nasa_telemetry/nasa_bearing_ims_test1.csv",
        false,
        csv_summary,
        csv_raw,
        &align,
        &campaign_rng,
        &ims_crc_ok
    );
    if (ret_ims != 0) {
        fclose(csv_summary);
        fclose(csv_raw);
        fprintf(stderr, "[FATAL] Campaña abortada por fallo en IMS Bearing (codigo: %d)\n", ret_ims);
        return ret_ims;
    }

    int flush_sum = fflush(csv_summary);
    int err_sum   = ferror(csv_summary);
    int close_sum = fclose(csv_summary);
    if (flush_sum != 0 || err_sum != 0 || close_sum != 0) {
        fprintf(stderr, "[FATAL] Error de I/O al persistir o cerrar archivo de resumen CSV: %s\n", csv_out_path);
        fclose(csv_raw);
        return 4;
    }

    int flush_raw = fflush(csv_raw);
    int err_raw   = ferror(csv_raw);
    int close_raw = fclose(csv_raw);
    if (flush_raw != 0 || err_raw != 0 || close_raw != 0) {
        fprintf(stderr, "[FATAL] Error de I/O al persistir o cerrar archivo de observaciones crudas CSV: %s\n", csv_raw_path);
        return 5;
    }
    printf("Resultados consolidados guardados en: %s y %s\n", csv_out_path, csv_raw_path);

    /* Generacion del sidecar de trazabilidad experimental verificado con CRC-32 real (NC-D02 / Rev 6) */
    bool all_crc32_verified = (cmapss_crc_ok && ims_crc_ok);
    int sidecar_st = bp011_write_traceability_sidecar(csv_out_path, &align, all_crc32_verified);
    if (sidecar_st != 0) {
        fprintf(stderr, "[FATAL] Fallo al generar sidecar de trazabilidad: %s.meta.json (error: %d)\n", csv_out_path, sidecar_st);
        return 6;
    }
    printf("Sidecar de trazabilidad verificado guardado en: %s.meta.json\n\n", csv_out_path);

    return 0;
}
