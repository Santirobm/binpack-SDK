/**
 * @file binpack_cli.c
 * @brief BinPack Enterprise SDK - High-Performance Command Line Tool
 */

#include "../include/binpack_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
static double get_time_sec(void) {
    LARGE_INTEGER freq, count;
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart / (double)freq.QuadPart;
}
#else
static double get_time_sec(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + (double)ts.tv_nsec / 1e9;
}
#endif

static void print_banner(void) {
    printf("=================================================================\n");
    printf("BinPack Enterprise SDK CLI v%s\n", binpack_version_string());
    printf("Industrial Architecture Preconditioning & Streaming Engine\n");
    printf("Copyright (c) 2026 Santiago Robledo. All Rights Reserved.\n");
    printf("=================================================================\n\n");
}

static void print_usage(const char *prog) {
    print_banner();
    printf("Usage:\n");
    printf("  %s -c <input> <output.bpk2> [options]   Compress file in bounded streaming chunks\n", prog);
    printf("  %s -d <input.bpk2> <output>             Decompress archive\n", prog);
    printf("  %s -i <archive.bpk2>                    Inspect archive metadata\n", prog);
    printf("  %s -b <input>                           Run compression & throughput benchmark\n", prog);
    printf("\nOptions:\n");
    printf("  -l, --level <1-22>       Compression level (default: 19)\n");
    printf("  -t, --threads <N>        Worker threads (default: 0 = auto-detect all cores)\n");
    printf("  -s, --chunk-size <MB>    Chunk buffer size in MB (default: 16 MB)\n");
    printf("  -f, --filter <mode>      Force filter: auto, none, x86, arm32, arm64, transpose, bitshuffle, texture\n");
    printf("  -m, --mode <profile>     Profile: default, runtime (fast decode 2GB/s), archival (max ratio)\n");
    printf("  --no-micro-trial         Disable 32KB sample trial (forces filter unconditionally)\n");
    printf("  -v, --version            Display version information\n");
    printf("  -h, --help               Display this help guide\n");
}

static void cli_progress(void *user_data, uint64_t in_cur, uint64_t in_total, uint64_t out_cur) {
    (void)user_data;
    if (in_total > 0) {
        double pct = (double)in_cur * 100.0 / (double)in_total;
        printf("\r[PROG] %llu / %llu bytes (%.1f%%) -> Out: %llu bytes", 
               (unsigned long long)in_cur, (unsigned long long)in_total, pct, (unsigned long long)out_cur);
    } else {
        printf("\r[PROG] Processed: %llu bytes -> Out: %llu bytes", 
               (unsigned long long)in_cur, (unsigned long long)out_cur);
    }
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        print_usage(argv[0]);
        return 0;
    }

    if (strcmp(argv[1], "-v") == 0 || strcmp(argv[1], "--version") == 0) {
        printf("BinPack Enterprise SDK Version %s\n", binpack_version_string());
        return 0;
    }

    /* Mode: Info */
    if (strcmp(argv[1], "-i") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[ERROR] Missing archive path for -i\n");
            return 1;
        }
        binpack_file_info_t info;
        binpack_status_t st = binpack_query_file_info(argv[2], &info);
        if (st != BINPACK_OK) {
            fprintf(stderr, "[ERROR] %s: %s\n", argv[2], binpack_error_string(st));
            return 1;
        }
        print_banner();
        printf("Archive Metadata: %s\n", argv[2]);
        printf("  Format Version:     v%u\n", info.version);
        printf("  Uncompressed Size:  %llu bytes (%.2f MB)\n", 
               (unsigned long long)info.uncompressed_size, (double)info.uncompressed_size / 1048576.0);
        printf("  Compressed Size:    %llu bytes (%.2f MB)\n", 
               (unsigned long long)info.compressed_size, (double)info.compressed_size / 1048576.0);
        if (info.uncompressed_size > 0) {
            double ratio = (double)info.compressed_size * 100.0 / (double)info.uncompressed_size;
            printf("  Overall Ratio:      %.2f%%\n", ratio);
        }
        printf("  Independent Chunks: %u\n", info.chunk_count);
        printf("  Integrity CRC32:    0x%08X\n", info.original_crc32);
        return 0;
    }

    /* Mode: Decompress */
    if (strcmp(argv[1], "-d") == 0) {
        if (argc < 4) {
            fprintf(stderr, "[ERROR] Usage: %s -d <input.bpk2> <output>\n", argv[0]);
            return 1;
        }
        const char *in_file = argv[2];
        const char *out_file = argv[3];

        printf("[INFO] Decompressing '%s' -> '%s'...\n", in_file, out_file);
        double t0 = get_time_sec();
        binpack_status_t st = binpack_decompress_file(in_file, out_file, cli_progress, NULL);
        double t1 = get_time_sec();
        printf("\n");

        if (st != BINPACK_OK) {
            fprintf(stderr, "[ERROR] Decompression failed: %s\n", binpack_error_string(st));
            return 1;
        }
        printf("[OK] Decompression verified successfully in %.3f seconds.\n", t1 - t0);
        return 0;
    }

    /* Mode: Compress */
    if (strcmp(argv[1], "-c") == 0) {
        if (argc < 4) {
            fprintf(stderr, "[ERROR] Usage: %s -c <input> <output.bpk2> [options]\n", argv[0]);
            return 1;
        }
        const char *in_file = argv[2];
        const char *out_file = argv[3];

        binpack_options_t opts;
        binpack_default_options(&opts);

        for (int i = 4; i < argc; i++) {
            if ((strcmp(argv[i], "-l") == 0 || strcmp(argv[i], "--level") == 0) && i + 1 < argc) {
                opts.clevel = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-t") == 0 || strcmp(argv[i], "--threads") == 0) && i + 1 < argc) {
                opts.nb_workers = atoi(argv[++i]);
            } else if ((strcmp(argv[i], "-s") == 0 || strcmp(argv[i], "--chunk-size") == 0) && i + 1 < argc) {
                opts.chunk_size = atoi(argv[++i]) * 1024 * 1024;
            } else if (strcmp(argv[i], "--no-micro-trial") == 0) {
                opts.enable_micro_trial = 0;
            } else if ((strcmp(argv[i], "-m") == 0 || strcmp(argv[i], "--mode") == 0) && i + 1 < argc) {
                const char *m = argv[++i];
                if (strcmp(m, "runtime") == 0) opts.mode = BINPACK_MODE_RUNTIME;
                else if (strcmp(m, "archival") == 0) opts.mode = BINPACK_MODE_ARCHIVAL;
                else opts.mode = BINPACK_MODE_DEFAULT;
            } else if ((strcmp(argv[i], "-f") == 0 || strcmp(argv[i], "--filter") == 0) && i + 1 < argc) {
                const char *f = argv[++i];
                if (strcmp(f, "none") == 0) opts.filter = BINPACK_FILTER_NONE;
                else if (strcmp(f, "x86") == 0) opts.filter = BINPACK_FILTER_X86_64;
                else if (strcmp(f, "arm32") == 0) opts.filter = BINPACK_FILTER_ARM32;
                else if (strcmp(f, "arm64") == 0) opts.filter = BINPACK_FILTER_AARCH64;
                else if (strcmp(f, "transpose") == 0) opts.filter = BINPACK_FILTER_TRANSPOSE_DELTA;
                else if (strcmp(f, "bitshuffle") == 0) opts.filter = BINPACK_FILTER_BITSHUFFLE;
                else if (strcmp(f, "texture") == 0) opts.filter = BINPACK_FILTER_TEXTURE_BCN;
            }
        }

        printf("[INFO] Compressing '%s' -> '%s' (Level: %d, Chunk: %d MB, Threads: %s, Micro-Trial: %s)...\n",
               in_file, out_file, opts.clevel, opts.chunk_size / (1024*1024),
               opts.nb_workers == 0 ? "auto" : "manual",
               opts.enable_micro_trial ? "ON" : "OFF");

        double t0 = get_time_sec();
        binpack_status_t st = binpack_compress_file(in_file, out_file, &opts, cli_progress, NULL);
        double t1 = get_time_sec();
        printf("\n");

        if (st != BINPACK_OK) {
            fprintf(stderr, "[ERROR] Compression failed: %s\n", binpack_error_string(st));
            return 1;
        }

        binpack_file_info_t info;
        binpack_query_file_info(out_file, &info);
        double ratio = info.uncompressed_size > 0 ? (double)info.compressed_size * 100.0 / (double)info.uncompressed_size : 0.0;
        double speed = (double)info.uncompressed_size / (1048576.0 * (t1 - t0));

        printf("[OK] Compression completed in %.3f s (Throughput: %.2f MB/s)\n", t1 - t0, speed);
        printf("[OK] Original: %llu bytes | Compressed: %llu bytes (Ratio: %.2f%%)\n",
               (unsigned long long)info.uncompressed_size, (unsigned long long)info.compressed_size, ratio);
        return 0;
    }

    /* Mode: Benchmark */
    if (strcmp(argv[1], "-b") == 0) {
        if (argc < 3) {
            fprintf(stderr, "[ERROR] Usage: %s -b <input_file>\n", argv[0]);
            return 1;
        }
        const char *bench_in = argv[2];
        char temp_archive[260];
        char temp_restored[260];
        snprintf(temp_archive, sizeof(temp_archive), "%s.sdk_bench.bpk2", bench_in);
        snprintf(temp_restored, sizeof(temp_restored), "%s.sdk_bench.restored", bench_in);

        print_banner();
        printf("Running Automated Benchmark Suite on '%s'...\n\n", bench_in);

        binpack_options_t opts;
        binpack_default_options(&opts);

        double t_c0 = get_time_sec();
        binpack_status_t st_c = binpack_compress_file(bench_in, temp_archive, &opts, NULL, NULL);
        double t_c1 = get_time_sec();

        if (st_c != BINPACK_OK) {
            fprintf(stderr, "[ERROR] Benchmark compression failed: %s\n", binpack_error_string(st_c));
            return 1;
        }

        double t_d0 = get_time_sec();
        binpack_status_t st_d = binpack_decompress_file(temp_archive, temp_restored, NULL, NULL);
        double t_d1 = get_time_sec();

        if (st_d != BINPACK_OK) {
            fprintf(stderr, "[ERROR] Benchmark decompression failed: %s\n", binpack_error_string(st_d));
            remove(temp_archive);
            return 1;
        }

        binpack_file_info_t info;
        binpack_query_file_info(temp_archive, &info);

        double comp_time = t_c1 - t_c0;
        double decomp_time = t_d1 - t_d0;
        double comp_speed = (double)info.uncompressed_size / (1048576.0 * (comp_time > 0 ? comp_time : 0.001));
        double decomp_speed = (double)info.uncompressed_size / (1048576.0 * (decomp_time > 0 ? decomp_time : 0.001));
        double ratio = (double)info.compressed_size * 100.0 / (double)info.uncompressed_size;

        printf("Benchmark Results:\n");
        printf("-----------------------------------------------------------------\n");
        printf("Uncompressed Size:    %llu bytes (%.2f MB)\n", (unsigned long long)info.uncompressed_size, (double)info.uncompressed_size / 1048576.0);
        printf("Compressed Size:      %llu bytes (%.2f MB)\n", (unsigned long long)info.compressed_size, (double)info.compressed_size / 1048576.0);
        printf("Compression Ratio:    %.2f%%\n", ratio);
        printf("Compression Time:     %.3f s (Speed: %.2f MB/s)\n", comp_time, comp_speed);
        printf("Decompression Time:   %.3f s (Speed: %.2f MB/s)\n", decomp_time, decomp_speed);
        printf("Integrity Status:     VERIFIED MATCH (CRC32: 0x%08X)\n", info.original_crc32);
        printf("-----------------------------------------------------------------\n");

        remove(temp_archive);
        remove(temp_restored);
        return 0;
    }

    fprintf(stderr, "[ERROR] Unknown option '%s'\n", argv[1]);
    print_usage(argv[0]);
    return 1;
}
