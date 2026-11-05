/**
 * @file test_archive_prefix.c
 * @brief BinPack Enterprise SDK - BP-006A Archive Rolling Prefix & V-08 Hardening Test Suite
 *
 * Exhaustive adversarial and functional test suite for rolling prefix dependencies:
 * - Bloque I:   Fixtures Legítimos (0, 1, 3, y 8 dependencias de prefijo)
 * - Bloque II:  Fixtures Adversariales (9 dependencias, prefijo en chunk 0, tamaño cero,
 *               presupuesto de memoria, payload corrupto, truncamiento físico vs metadatos)
 * - Bloque III: Resistencia de Recursos y DoS (500 dependencias cuantitativo y aislado,
 *               fault injection determinista para OOM en raíz e intermedio)
 * - Bloque IV:  Compatibilidad y Regresión (regresión histórica C5, integración BCJ x86_64 y AArch64 A14)
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <assert.h>

#include "binpack_sdk.h"
#include "../src/binpack_internal.h"
#include <zstd.h>

#if defined(_WIN32)
    #include <windows.h>
    #include <io.h>
#else
    #include <unistd.h>
    #include <sys/wait.h>
    #include <errno.h>
#endif

#define TEST_PASS 0
#define TEST_FAIL 1

#define BINPACK_TEST_MAX_PREFIX_CHAIN 8
#define BINPACK_TEST_MAX_PREFIX_MEM_BUDGET (128ULL * 1024ULL * 1024ULL) /* 128 MB */

static const char *g_self_path = NULL;

/* Prototypes for BP-006A test instrumentation and fault injection hooks (compiled with BINPACK_TESTING) */
typedef void* (*bp_archive_alloc_fn)(size_t sz);
void binpack_test_archive_set_alloc_hook(bp_archive_alloc_fn fn);
void binpack_test_archive_reset_stats(void);
void binpack_test_archive_get_stats(
    uint32_t *deps_inspected,
    uint32_t *payload_reads,
    uint32_t *prefix_buf_alloc_count,
    uint32_t *total_alloc_count
);

/* Helper: initialize default V2 container header */
static void init_v2_header(binpack_v2_header_t *hdr) {
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = BINPACK_MAGIC_V2;
    hdr->version_major = BINPACK_SDK_VERSION_MAJOR;
    hdr->version_minor = BINPACK_SDK_VERSION_MINOR;
    hdr->flags = 0;
    hdr->default_chunk_sz = (uint32_t)BINPACK_DEFAULT_CHUNK_SIZE;
    hdr->reserved = 0;
}

/* Helper: generate an archive with N prefix dependencies (N+1 chunks total).
 * Chunk 0: independent (raw).
 * Chunk 1..N: depends on Chunk i-1 via ZSTD_c_refPrefix.
 * Hardened: validates all fwrite and ZSTD calls; fails on any truncated or corrupt write.
 */
static int create_prefix_chain_archive(
    const char *bpk_path,
    uint32_t dep_count,
    size_t chunk_sz,
    uint8_t ***out_raw_chunks,
    int corrupt_chunk_idx,
    int set_chunk0_prefix,
    size_t override_chunk_uncompressed_sz,
    int physical_truncate_bytes
) {
    uint32_t total_chunks = dep_count + 1;
    uint8_t **raw_chunks = (uint8_t**)calloc(total_chunks, sizeof(uint8_t*));
    if (!raw_chunks) return 0;

    for (uint32_t i = 0; i < total_chunks; i++) {
        raw_chunks[i] = (uint8_t*)malloc(chunk_sz);
        if (!raw_chunks[i]) {
            for (uint32_t j = 0; j < i; j++) free(raw_chunks[j]);
            free(raw_chunks);
            return 0;
        }
        /* Distinct pseudo-random deterministic payload per chunk */
        for (size_t b = 0; b < chunk_sz; b++) {
            raw_chunks[i][b] = (uint8_t)(((i + 1) * 37 + b * 13 + 7) & 0xFF);
        }
    }

    FILE *f = fopen(bpk_path, "wb");
    if (!f) {
        for (uint32_t i = 0; i < total_chunks; i++) free(raw_chunks[i]);
        free(raw_chunks);
        return 0;
    }

    binpack_v2_header_t hdr;
    init_v2_header(&hdr);
    if (fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        remove(bpk_path);
        for (uint32_t i = 0; i < total_chunks; i++) free(raw_chunks[i]);
        free(raw_chunks);
        return 0;
    }

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) {
        fclose(f);
        remove(bpk_path);
        for (uint32_t i = 0; i < total_chunks; i++) free(raw_chunks[i]);
        free(raw_chunks);
        return 0;
    }

    size_t max_cmp_sz = ZSTD_compressBound(chunk_sz) + 512;
    uint8_t *cmp_buf = (uint8_t*)malloc(max_cmp_sz);
    if (!cmp_buf) {
        ZSTD_freeCCtx(cctx);
        fclose(f);
        remove(bpk_path);
        for (uint32_t i = 0; i < total_chunks; i++) free(raw_chunks[i]);
        free(raw_chunks);
        return 0;
    }

    uint64_t total_uncomp = 0;

    for (uint32_t i = 0; i < total_chunks; i++) {
        size_t actual_cmp_sz = 0;
        if (i == 0) {
            size_t zrst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
            if (ZSTD_isError(zrst)) {
                free(cmp_buf);
                ZSTD_freeCCtx(cctx);
                fclose(f);
                remove(bpk_path);
                for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
                free(raw_chunks);
                return 0;
            }
            actual_cmp_sz = ZSTD_compressCCtx(cctx, cmp_buf, max_cmp_sz, raw_chunks[0], chunk_sz, 1);
        } else {
            size_t zrst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
            if (ZSTD_isError(zrst)) {
                free(cmp_buf);
                ZSTD_freeCCtx(cctx);
                fclose(f);
                remove(bpk_path);
                for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
                free(raw_chunks);
                return 0;
            }
            size_t zpref = ZSTD_CCtx_refPrefix(cctx, raw_chunks[i - 1], chunk_sz);
            if (ZSTD_isError(zpref)) {
                free(cmp_buf);
                ZSTD_freeCCtx(cctx);
                fclose(f);
                remove(bpk_path);
                for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
                free(raw_chunks);
                return 0;
            }
            ZSTD_inBuffer in = { raw_chunks[i], chunk_sz, 0 };
            ZSTD_outBuffer out = { cmp_buf, max_cmp_sz, 0 };
            size_t zret = ZSTD_compressStream2(cctx, &out, &in, ZSTD_e_end);
            if (ZSTD_isError(zret) || zret != 0 || in.pos != in.size || out.pos == 0) {
                free(cmp_buf);
                ZSTD_freeCCtx(cctx);
                fclose(f);
                remove(bpk_path);
                for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
                free(raw_chunks);
                return 0;
            }
            actual_cmp_sz = out.pos;
        }

        if (ZSTD_isError(actual_cmp_sz) || actual_cmp_sz == 0) {
            free(cmp_buf);
            ZSTD_freeCCtx(cctx);
            fclose(f);
            remove(bpk_path);
            for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
            free(raw_chunks);
            return 0;
        }

        /* Corrupt payload if requested */
        if (corrupt_chunk_idx >= 0 && (uint32_t)corrupt_chunk_idx == i) {
            for (size_t c = 0; c < actual_cmp_sz / 2; c++) {
                cmp_buf[c] ^= 0xAA;
            }
        }

        binpack_chunk_header_t ch;
        memset(&ch, 0, sizeof(ch));
        ch.chunk_magic = BINPACK_MAGIC_CHUNK;
        ch.uncompressed_sz = (uint32_t)((override_chunk_uncompressed_sz > 0 && i == total_chunks - 1)
                             ? override_chunk_uncompressed_sz : chunk_sz);
        ch.compressed_sz = (uint32_t)actual_cmp_sz;
        ch.chunk_crc32 = binpack_crc32(0, raw_chunks[i], chunk_sz);

        if (i > 0 || set_chunk0_prefix) {
            ch.filter_flags |= BP_CHUNK_FLAG_PREFIX;
        }

        if (fwrite(&ch, 1, sizeof(ch), f) != sizeof(ch) ||
            fwrite(cmp_buf, 1, actual_cmp_sz, f) != actual_cmp_sz) {
            free(cmp_buf);
            ZSTD_freeCCtx(cctx);
            fclose(f);
            remove(bpk_path);
            for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
            free(raw_chunks);
            return 0;
        }
        total_uncomp += ch.uncompressed_sz;
    }

    free(cmp_buf);
    ZSTD_freeCCtx(cctx);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = total_chunks;
    ftr.total_uncompressed = total_uncomp;
    if (fwrite(&ftr, 1, sizeof(ftr), f) != sizeof(ftr)) {
        fclose(f);
        remove(bpk_path);
        for (uint32_t j = 0; j < total_chunks; j++) free(raw_chunks[j]);
        free(raw_chunks);
        return 0;
    }
    fclose(f);

    if (physical_truncate_bytes > 0) {
        FILE *ft = fopen(bpk_path, "r+b");
        if (ft) {
            binpack_fseek64(ft, 0, SEEK_END);
            int64_t fsz = binpack_ftell64(ft);
            if (fsz > physical_truncate_bytes) {
#if defined(_WIN32)
                _chsize_s(_fileno(ft), fsz - physical_truncate_bytes);
#else
                int fd = fileno(ft);
                if (fd >= 0) {
                    if (ftruncate(fd, fsz - physical_truncate_bytes) != 0) {
                        /* ignore failure */
                    }
                }
#endif
            }
            fclose(ft);
        }
    }

    if (out_raw_chunks) {
        *out_raw_chunks = raw_chunks;
    } else {
        for (uint32_t i = 0; i < total_chunks; i++) free(raw_chunks[i]);
        free(raw_chunks);
    }

    return 1;
}

static void free_raw_chunks(uint8_t **raw_chunks, uint32_t dep_count) {
    if (!raw_chunks) return;
    uint32_t total = dep_count + 1;
    for (uint32_t i = 0; i < total; i++) {
        if (raw_chunks[i]) free(raw_chunks[i]);
    }
    free(raw_chunks);
}

/* ========================================================================= */
/* BLOQUE I: Fixtures Legítimos (A01 - A04)                                   */
/* ========================================================================= */

static int test_A01_archive_no_prefix(void) {
    const char *path = "test_bp6a_a01.bpk";
    uint8_t **raw = NULL;
    if (!create_prefix_chain_archive(path, 0, 4096, &raw, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { free_raw_chunks(raw, 0); remove(path); return TEST_FAIL; }

    uint8_t dst[4096];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 0, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    int ok = (st == BINPACK_OK && dsz == 4096 && memcmp(dst, raw[0], 4096) == 0);
    free_raw_chunks(raw, 0);
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_A02_single_prefix_dependency(void) {
    const char *path = "test_bp6a_a02.bpk";
    uint8_t **raw = NULL;
    if (!create_prefix_chain_archive(path, 1, 4096, &raw, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { free_raw_chunks(raw, 1); remove(path); return TEST_FAIL; }

    uint8_t dst[4096];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 1, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    int ok = (st == BINPACK_OK && dsz == 4096 && memcmp(dst, raw[1], 4096) == 0);
    free_raw_chunks(raw, 1);
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_A03_three_prefix_dependencies(void) {
    /* Exactamente 3 dependencias (4 chunks totales: 0, 1, 2, 3) */
    const char *path = "test_bp6a_a03.bpk";
    uint8_t **raw = NULL;
    if (!create_prefix_chain_archive(path, 3, 4096, &raw, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { free_raw_chunks(raw, 3); remove(path); return TEST_FAIL; }

    uint8_t dst[4096];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 3, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    int ok = (st == BINPACK_OK && dsz == 4096 && memcmp(dst, raw[3], 4096) == 0);
    free_raw_chunks(raw, 3);
    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_A04_eight_prefix_dependencies_boundary(void) {
    /* Exactamente 8 dependencias (9 chunks totales: 0..8, cota de diseño BINPACK_MAX_PREFIX_CHAIN) */
    const char *path = "test_bp6a_a04.bpk";
    uint8_t **raw = NULL;
    if (!create_prefix_chain_archive(path, 8, 4096, &raw, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { free_raw_chunks(raw, 8); remove(path); return TEST_FAIL; }

    uint8_t dst[4096];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 8, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    int ok = (st == BINPACK_OK && dsz == 4096 && memcmp(dst, raw[8], 4096) == 0);
    free_raw_chunks(raw, 8);
    return ok ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* BLOQUE II: Fixtures Adversariales (A05 - A10)                              */
/* ========================================================================= */

static int test_A05_nine_prefix_dependencies_exceeds_limit(void) {
    /* 9 dependencias (10 chunks totales: 0..9). Supera BINPACK_MAX_PREFIX_CHAIN = 8 */
    const char *path = "test_bp6a_a05.bpk";
    uint8_t **raw = NULL;
    if (!create_prefix_chain_archive(path, 9, 2048, &raw, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { free_raw_chunks(raw, 9); remove(path); return TEST_FAIL; }

    uint8_t dst[2048];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 9, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);
    free_raw_chunks(raw, 9);

    /* En la arquitectura endurecida DEBE ser rechazado deterministamente con CORRUPT_DATA */
    if (st == BINPACK_ERR_CORRUPT_DATA) {
        return TEST_PASS;
    }

    printf("    [ERROR] 9 dependencias no fueron rechazadas (st=%d)\n", st);
    return TEST_FAIL;
}

static int test_A06_chunk_zero_with_prefix_flag(void) {
    /* Chunk 0 declara bandera BP_CHUNK_FLAG_PREFIX (formato inválido, no existe chunk -1) */
    const char *path = "test_bp6a_a06.bpk";
    if (!create_prefix_chain_archive(path, 0, 2048, NULL, -1, 1 /* set chunk0 prefix */, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_PASS; } /* Si el parser lo rechaza al abrir, PASS */

    uint8_t dst[2048];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 0, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    return (st == BINPACK_ERR_CORRUPT_DATA) ? TEST_PASS : TEST_FAIL;
}

static int test_A07_zero_size_prefix_chunk(void) {
    /* Chunk anterior declara uncompressed_sz == 0 */
    const char *path = "test_bp6a_a07.bpk";
    if (!create_prefix_chain_archive(path, 1, 2048, NULL, -1, 0, 0, 0)) return TEST_FAIL;

    /* Modificar el archivo binario para poner uncompressed_sz de chunk 0 en 0 */
    FILE *f = fopen(path, "r+b");
    if (!f) { remove(path); return TEST_FAIL; }
    /* Header V2 = 16 bytes. Chunk 0 header = 24 bytes. Offset 16 + 4 = uncompressed_sz */
    binpack_fseek64(f, 16 + 4, SEEK_SET);
    uint32_t zero = 0;
    if (fwrite(&zero, 1, sizeof(zero), f) != sizeof(zero)) {
        fclose(f);
        remove(path);
        return TEST_FAIL;
    }
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_PASS; }

    uint8_t dst[2048];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 1, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    return (st != BINPACK_OK) ? TEST_PASS : TEST_FAIL;
}

static int test_A08_prefix_size_exceeds_memory_budget(void) {
    /* Chunk anterior declara un tamaño que desborda el presupuesto de memoria (p. ej. 130 MB) */
    const char *path = "test_bp6a_a08.bpk";
    if (!create_prefix_chain_archive(path, 1, 2048, NULL, -1, 0, 0, 0)) return TEST_FAIL;

    FILE *f = fopen(path, "r+b");
    if (!f) { remove(path); return TEST_FAIL; }
    binpack_fseek64(f, 16 + 4, SEEK_SET);
    uint32_t huge_sz = 130 * 1024 * 1024; /* 130 MB > 128 MB budget */
    if (fwrite(&huge_sz, 1, sizeof(huge_sz), f) != sizeof(huge_sz)) {
        fclose(f);
        remove(path);
        return TEST_FAIL;
    }
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_PASS; }

    uint8_t dst[2048];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 1, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    return (st != BINPACK_OK) ? TEST_PASS : TEST_FAIL;
}

static int test_A09_corrupted_intermediate_prefix_payload(void) {
    /* Cadena de 3 dependencias donde el Chunk 1 tiene su payload corrupto */
    const char *path = "test_bp6a_a09.bpk";
    if (!create_prefix_chain_archive(path, 3, 2048, NULL, 1 /* corrupt chunk 1 */, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_FAIL; }

    uint8_t dst[2048];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 3, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    /* El fallo en el prefijo intermedio debe propagarse limpiamente sin corromper memoria */
    return (st != BINPACK_OK) ? TEST_PASS : TEST_FAIL;
}

static int test_A10_physically_truncated_archive_vs_corrupt_metadata(void) {
    /* Diferencia física: archivo truncado mientras está abierto produce BINPACK_ERR_IO,
     * a diferencia de metadatos corruptos que producen BINPACK_ERR_CORRUPT_DATA */
    const char *path = "test_bp6a_a10.bpk";
    if (!create_prefix_chain_archive(path, 1, 4096, NULL, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_FAIL; }

    /* Truncar físicamente el archivo en disco mientras el handle está abierto */
    FILE *ft = fopen(path, "r+b");
    if (ft) {
#if defined(_WIN32)
        _chsize_s(_fileno(ft), 32); /* Truncar a 32 bytes (payload inaccesible) */
#else
        int fd = fileno(ft);
        if (fd >= 0) ftruncate(fd, 32);
#endif
        fclose(ft);
    }

    uint8_t dst[4096];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 1, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(path);

    /* Debe retornar BINPACK_ERR_IO (-4) y no fallar con crash */
    return (st == BINPACK_ERR_IO) ? TEST_PASS : TEST_FAIL;
}

/* ========================================================================= */
/* BLOQUE III: Resistencia de Recursos, DoS e Inyección de Fallas (A11 - A12) */
/* ========================================================================= */

static int run_isolated_subtest(const char *self_path, const char *subtest_arg) {
#if defined(_WIN32)
    char cmdline[1024];
    snprintf(cmdline, sizeof(cmdline), "\"%s\" %s", self_path, subtest_arg);
    STARTUPINFOA si;
    PROCESS_INFORMATION pi;
    memset(&si, 0, sizeof(si));
    si.cb = sizeof(si);
    memset(&pi, 0, sizeof(pi));
    if (!CreateProcessA(NULL, cmdline, NULL, NULL, FALSE, 0, NULL, NULL, &si, &pi)) {
        return -1;
    }
    DWORD res = WaitForSingleObject(pi.hProcess, 10000); /* 10s bounded timeout */
    if (res != WAIT_OBJECT_0) {
        TerminateProcess(pi.hProcess, 1);
        WaitForSingleObject(pi.hProcess, 3000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
        return -2; /* Timeout / crash */
    }
    DWORD exit_code = 1;
    GetExitCodeProcess(pi.hProcess, &exit_code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return (int)exit_code;
#else
    pid_t pid = fork();
    if (pid < 0) return -1;
    if (pid == 0) {
        execl(self_path, self_path, subtest_arg, (char*)NULL);
        _exit(127);
    }
    int status = 0;
    for (int i = 0; i < 1000; i++) {
        pid_t r = waitpid(pid, &status, WNOHANG);
        if (r == pid) {
            if (WIFEXITED(status)) return WEXITSTATUS(status);
            return -3; /* Crashed or killed by signal */
        }
        usleep(10000); /* 10ms */
    }
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return -2; /* Timed out */
#endif
}

/**
 * Subtest A11:
 * Instrumentación cuantitativa de recursos ante cadena adversarial profunda (500 chunks).
 * Definición rigurosa de telemetría:
 * - "deps_inspected": cuenta estrictamente las aristas de dependencia examinadas hacia atrás en Fase 1.
 * - "payload_reads": cuenta las invocaciones a fread() de datos comprimidos de chunk.
 * - "prefix_buf_alloc_count": cuenta las invocaciones a bp_archive_alloc() para buffers de prefijo en Fase 2.
 *
 * Criterio de aceptación para 500 dependencias:
 * - deps_inspected <= 9 (exactamente 9 dependencias examinadas antes de rechazar por cota de 8).
 * - payload_reads == 0 (cero lecturas de payload en disco).
 * - prefix_buf_alloc_count == 0 (cero asignaciones de buffers de prefijo).
 */
static int execute_subtest_a11_isolated(void) {
    const char *path = "test_bp6a_a11_deep_500.bpk";
    FILE *f = fopen(path, "wb");
    if (!f) return 1;

    binpack_v2_header_t hdr;
    init_v2_header(&hdr);
    if (fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f);
        remove(path);
        return 1;
    }

    uint8_t dummy_cmp[64];
    memset(dummy_cmp, 0, sizeof(dummy_cmp));

    const uint32_t DEEP_CHUNKS = 500;
    for (uint32_t i = 0; i < DEEP_CHUNKS; i++) {
        binpack_chunk_header_t ch;
        memset(&ch, 0, sizeof(ch));
        ch.chunk_magic = BINPACK_MAGIC_CHUNK;
        ch.uncompressed_sz = 1024;
        ch.compressed_sz = sizeof(dummy_cmp);
        ch.chunk_crc32 = 0x12345678;
        ch.filter_flags = (i > 0) ? BP_CHUNK_FLAG_PREFIX : 0;
        if (fwrite(&ch, 1, sizeof(ch), f) != sizeof(ch) ||
            fwrite(dummy_cmp, 1, sizeof(dummy_cmp), f) != sizeof(dummy_cmp)) {
            fclose(f);
            remove(path);
            return 1;
        }
    }

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = DEEP_CHUNKS;
    ftr.total_uncompressed = DEEP_CHUNKS * 1024;
    if (fwrite(&ftr, 1, sizeof(ftr), f) != sizeof(ftr)) {
        fclose(f);
        remove(path);
        return 1;
    }
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return 1; }

    uint8_t dst[1024];
    size_t dsz = 0;

    /* Reiniciar contadores de telemetría antes de iniciar descompresión */
    binpack_test_archive_reset_stats();

    binpack_status_t st = binpack_archive_decompress_chunk(arch, DEEP_CHUNKS - 1, dst, sizeof(dst), &dsz);

    uint32_t deps_inspected = 0;
    uint32_t payload_reads = 0;
    uint32_t prefix_allocs = 0;
    uint32_t total_allocs = 0;
    binpack_test_archive_get_stats(&deps_inspected, &payload_reads, &prefix_allocs, &total_allocs);

    binpack_archive_close(arch);
    remove(path);

    /* Criterios cuantitativos obligatorios de instrumentación integral:
     * 1. Retorno de error temprano por exceder cota de dependencias.
     * 2. Máximo 9 dependencias examinadas (M <= 9 para cota 8).
     * 3. Cero lecturas de payload en disco (fread == 0).
     * 4. Cero asignaciones de buffers de prefijo (bp_archive_alloc == 0).
     * 5. Cero asignaciones dinámicas TOTALES en la ruta de descompresión (total_allocs == 0).
     */
    if (st != BINPACK_ERR_CORRUPT_DATA) return 2;
    if (deps_inspected == 0 || deps_inspected > 9) return 3;
    if (payload_reads != 0) return 4;
    if (prefix_allocs != 0) return 5;
    if (total_allocs != 0) return 6;

    return 0;
}

static int test_A11_isolated_500_prefix_chain(void) {
    /* 1. Verificación directa de telemetría cuantitativa en proceso actual */
    int direct_rc = execute_subtest_a11_isolated();
    if (direct_rc != 0) {
        printf("    [A11 Telemetria FAIL] Codigo de fallo: %d\n", direct_rc);
        return TEST_FAIL;
    }

    uint32_t deps = 0, reads = 0, prefix_allocs = 0, total_allocs = 0;
    binpack_test_archive_get_stats(&deps, &reads, &prefix_allocs, &total_allocs);
    printf("    [A11 Instrumentacion Cuantitativa] Dependencias examinadas: %u (<= 9), Lecturas payload: %u (== 0), Asignaciones buffers prefijo: %u (== 0), Asignaciones dinamicas TOTALES: %u (== 0) -> PASS\n",
           deps, reads, prefix_allocs, total_allocs);

    /* 2. Verificación de proceso aislado (inmunidad a stack overflow / DoS) */
    int rc = run_isolated_subtest(g_self_path, "--subtest-a11");
    if (rc == 0) return TEST_PASS;
    printf("    [A11 Aislamiento FAIL] Subproceso retorno codigo: %d\n", rc);
    return TEST_FAIL;
}

/* Estado global de fault injection determinista para A12 */
static int s_fault_target_alloc = 0;
static int s_fault_current_alloc = 0;

static void* a12_fault_alloc_hook(size_t sz) {
    s_fault_current_alloc++;
    if (s_fault_current_alloc == s_fault_target_alloc) {
        return NULL; /* Simular agotamiento de memoria determinista */
    }
    return malloc(sz);
}

static int test_A12_allocation_failure_handling(void) {
    /* Cadena legítima de 2 dependencias (3 chunks: 0=root, 1=intermedio, 2=target) */
    const char *path = "test_bp6a_a12_fault.bpk";
    if (!create_prefix_chain_archive(path, 2, 4096, NULL, -1, 0, 0, 0)) return TEST_FAIL;

    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_FAIL; }

    uint8_t dst[4096];
    size_t dsz = 0;

    /* Subcaso 1: Falla determinista en asignación #1 (Chunk raíz prefix_buf) */
    s_fault_target_alloc = 1;
    s_fault_current_alloc = 0;
    binpack_test_archive_set_alloc_hook(a12_fault_alloc_hook);

    binpack_status_t st1 = binpack_archive_decompress_chunk(arch, 2, dst, sizeof(dst), &dsz);

    binpack_test_archive_set_alloc_hook(NULL);
    if (st1 != BINPACK_ERR_OUT_OF_MEMORY) {
        printf("    [A12 Fail] Subcaso 1 (fallo en asignacion raiz) retorno st=%d, esperado BINPACK_ERR_OUT_OF_MEMORY\n", st1);
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    /* Subcaso 2: Falla determinista en asignación #2 (Chunk intermedio next_buf)
     * Debe liberar prefix_buf (asignación #1) limpiamente sin fugas */
    s_fault_target_alloc = 2;
    s_fault_current_alloc = 0;
    binpack_test_archive_set_alloc_hook(a12_fault_alloc_hook);

    binpack_status_t st2 = binpack_archive_decompress_chunk(arch, 2, dst, sizeof(dst), &dsz);

    binpack_test_archive_set_alloc_hook(NULL);
    if (st2 != BINPACK_ERR_OUT_OF_MEMORY) {
        printf("    [A12 Fail] Subcaso 2 (fallo en asignacion intermedia) retorno st=%d, esperado BINPACK_ERR_OUT_OF_MEMORY\n", st2);
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    /* Subcaso 3: Capacidad insuficiente en destino (dst_capacity < uncompressed_sz) */
    uint8_t small_dst[16];
    binpack_status_t st3 = binpack_archive_decompress_chunk(arch, 2, small_dst, sizeof(small_dst), &dsz);
    if (st3 != BINPACK_ERR_BUFFER_TOO_SMALL) {
        printf("    [A12 Fail] Subcaso 3 (buffer too small) retorno st=%d, esperado BINPACK_ERR_BUFFER_TOO_SMALL\n", st3);
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    /* Subcaso 4: Sin fallas inyectadas, la misma cadena de 2 dependencias descomprime con éxito */
    binpack_status_t st4 = binpack_archive_decompress_chunk(arch, 2, dst, sizeof(dst), &dsz);
    if (st4 != BINPACK_OK || dsz != 4096) {
        printf("    [A12 Fail] Subcaso 4 (sin fallas) retorno st=%d, dsz=%zu\n", st4, dsz);
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    binpack_archive_close(arch);
    remove(path);
    return TEST_PASS;
}

/* ========================================================================= */
/* BLOQUE IV: Compatibilidad, Regresión e Integración de Filtros (A13 - A14)  */
/* ========================================================================= */

static int test_A13_historical_c5_regression(void) {
    /* Regresión directa del caso C5 establecido en BP-004 */
    const char *tmp_path = "test_bp6a_c5_regress.bpk";
    uint8_t raw0[1024], raw1[1024], raw2[1024];
    memset(raw0, 0x11, sizeof(raw0));
    memset(raw1, 0x22, sizeof(raw1));
    memset(raw2, 0x33, sizeof(raw2));

    uint8_t cmp0[1024 + 128];
    size_t cmp0_sz = ZSTD_compress(cmp0, sizeof(cmp0), raw0, sizeof(raw0), 1);
    if (ZSTD_isError(cmp0_sz)) return TEST_FAIL;

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) return TEST_FAIL;

    size_t zpref1 = ZSTD_CCtx_refPrefix(cctx, raw0, sizeof(raw0));
    if (ZSTD_isError(zpref1)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    uint8_t cmp1[1024 + 128];
    ZSTD_inBuffer in1 = { raw1, sizeof(raw1), 0 };
    ZSTD_outBuffer out1 = { cmp1, sizeof(cmp1), 0 };
    size_t zret1 = ZSTD_compressStream2(cctx, &out1, &in1, ZSTD_e_end);
    if (ZSTD_isError(zret1) || zret1 != 0 || in1.pos != in1.size) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    size_t cmp1_sz = out1.pos;

    size_t zrst = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    if (ZSTD_isError(zrst)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    size_t zpref2 = ZSTD_CCtx_refPrefix(cctx, raw1, sizeof(raw1));
    if (ZSTD_isError(zpref2)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    uint8_t cmp2[1024 + 128];
    ZSTD_inBuffer in2 = { raw2, sizeof(raw2), 0 };
    ZSTD_outBuffer out2 = { cmp2, sizeof(cmp2), 0 };
    size_t zret2 = ZSTD_compressStream2(cctx, &out2, &in2, ZSTD_e_end);
    if (ZSTD_isError(zret2) || zret2 != 0 || in2.pos != in2.size) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    size_t cmp2_sz = out2.pos;
    ZSTD_freeCCtx(cctx);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_v2_header(&hdr);
    if (fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) { fclose(f); remove(tmp_path); return TEST_FAIL; }

    binpack_chunk_header_t ch0;
    memset(&ch0, 0, sizeof(ch0));
    ch0.chunk_magic = BINPACK_MAGIC_CHUNK;
    ch0.uncompressed_sz = sizeof(raw0);
    ch0.compressed_sz = (uint32_t)cmp0_sz;
    ch0.chunk_crc32 = binpack_crc32(0, raw0, sizeof(raw0));
    if (fwrite(&ch0, 1, sizeof(ch0), f) != sizeof(ch0) ||
        fwrite(cmp0, 1, cmp0_sz, f) != cmp0_sz) {
        fclose(f); remove(tmp_path); return TEST_FAIL;
    }

    binpack_chunk_header_t ch1;
    memset(&ch1, 0, sizeof(ch1));
    ch1.chunk_magic = BINPACK_MAGIC_CHUNK;
    ch1.uncompressed_sz = sizeof(raw1);
    ch1.compressed_sz = (uint32_t)cmp1_sz;
    ch1.filter_flags = BP_CHUNK_FLAG_PREFIX;
    ch1.chunk_crc32 = binpack_crc32(0, raw1, sizeof(raw1));
    if (fwrite(&ch1, 1, sizeof(ch1), f) != sizeof(ch1) ||
        fwrite(cmp1, 1, cmp1_sz, f) != cmp1_sz) {
        fclose(f); remove(tmp_path); return TEST_FAIL;
    }

    binpack_chunk_header_t ch2;
    memset(&ch2, 0, sizeof(ch2));
    ch2.chunk_magic = BINPACK_MAGIC_CHUNK;
    ch2.uncompressed_sz = sizeof(raw2);
    ch2.compressed_sz = (uint32_t)cmp2_sz;
    ch2.filter_flags = BP_CHUNK_FLAG_PREFIX;
    ch2.chunk_crc32 = binpack_crc32(0, raw2, sizeof(raw2));
    if (fwrite(&ch2, 1, sizeof(ch2), f) != sizeof(ch2) ||
        fwrite(cmp2, 1, cmp2_sz, f) != cmp2_sz) {
        fclose(f); remove(tmp_path); return TEST_FAIL;
    }

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 3;
    ftr.total_uncompressed = 3072;
    if (fwrite(&ftr, 1, sizeof(ftr), f) != sizeof(ftr)) { fclose(f); remove(tmp_path); return TEST_FAIL; }
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) { remove(tmp_path); return TEST_FAIL; }

    uint8_t dst[1024];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 2, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(tmp_path);

    int ok = (st == BINPACK_OK && dsz == sizeof(raw2) && memcmp(dst, raw2, sizeof(raw2)) == 0);
    return ok ? TEST_PASS : TEST_FAIL;
}

/**
 * Helper interno para A14:
 * Construye y valida una cadena de 3 prefijos rodantes para un filtro arquitectónico específico.
 */
static int test_a14_filter_prefix_subcase(
    binpack_filter_t filter_id,
    const char *path,
    void (*apply_filter_fn)(uint8_t*, size_t),
    void (*apply_unfilter_fn)(uint8_t*, size_t),
    int is_aarch64
) {
    const size_t CHUNK_SZ = 4096;
    uint8_t orig[3][4096];
    uint8_t filtered[3][4096];
    uint32_t crc_orig[3];

    for (int i = 0; i < 3; i++) {
        for (size_t b = 0; b < CHUNK_SZ; b++) {
            orig[i][b] = (uint8_t)((b * 7 + (i + 1) * 31) & 0xFF);
        }

        if (is_aarch64) {
            /* Patrones válidos AArch64 BL (0x25) y B (0x05) cada 64 bytes */
            for (size_t b = 0; b + 4 <= CHUNK_SZ; b += 64) {
                uint32_t op = (b % 128 == 0) ? 0x25 : 0x05;
                uint32_t imm = (uint32_t)((b * 5 + (i + 1) * 0x100) & 0x03FFFFFF);
                uint32_t instr = (op << 26) | imm;
                orig[i][b]   = (uint8_t)(instr);
                orig[i][b+1] = (uint8_t)(instr >> 8);
                orig[i][b+2] = (uint8_t)(instr >> 16);
                orig[i][b+3] = (uint8_t)(instr >> 24);
            }
        } else {
            /* Patrones válidos x86 CALL (0xE8) cada 64 bytes */
            for (size_t b = 0; b + 5 <= CHUNK_SZ; b += 64) {
                orig[i][b]   = 0xE8;
                orig[i][b+1] = (uint8_t)((b + 0x100) & 0xFF);
                orig[i][b+2] = (uint8_t)(((b + 0x100) >> 8) & 0xFF);
                orig[i][b+3] = 0x00;
                orig[i][b+4] = 0x00;
            }
        }

        crc_orig[i] = binpack_crc32(0, orig[i], CHUNK_SZ);

        /* Aplicar filtro arquitectónico */
        memcpy(filtered[i], orig[i], CHUNK_SZ);
        apply_filter_fn(filtered[i], CHUNK_SZ);

        /* Diagnóstico 1: verificar que el filtro modificó activamente el buffer */
        if (memcmp(filtered[i], orig[i], CHUNK_SZ) == 0) {
            return TEST_FAIL;
        }

        /* Diagnóstico 2: verificar que el desfiltrado local restaura 100% el contenido original */
        uint8_t test_unfilter[4096];
        memcpy(test_unfilter, filtered[i], CHUNK_SZ);
        apply_unfilter_fn(test_unfilter, CHUNK_SZ);
        if (memcmp(test_unfilter, orig[i], CHUNK_SZ) != 0) {
            return TEST_FAIL;
        }
    }

    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    if (!cctx) return TEST_FAIL;

    uint8_t cmp[3][4096 + 512];
    size_t cmp_sz[3];

    /* Chunk 0: standalone sobre filtered[0] */
    size_t zrst0 = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    if (ZSTD_isError(zrst0)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    cmp_sz[0] = ZSTD_compressCCtx(cctx, cmp[0], sizeof(cmp[0]), filtered[0], CHUNK_SZ, 1);
    if (ZSTD_isError(cmp_sz[0]) || cmp_sz[0] == 0) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }

    /* Chunk 1: refPrefix(filtered[0]) sobre filtered[1] */
    size_t zrst1 = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    if (ZSTD_isError(zrst1)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    size_t zpref1 = ZSTD_CCtx_refPrefix(cctx, filtered[0], CHUNK_SZ);
    if (ZSTD_isError(zpref1)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    ZSTD_inBuffer in1 = { filtered[1], CHUNK_SZ, 0 };
    ZSTD_outBuffer out1 = { cmp[1], sizeof(cmp[1]), 0 };
    size_t zret1 = ZSTD_compressStream2(cctx, &out1, &in1, ZSTD_e_end);
    if (ZSTD_isError(zret1) || zret1 != 0 || in1.pos != in1.size) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    cmp_sz[1] = out1.pos;

    /* Chunk 2: refPrefix(filtered[1]) sobre filtered[2] */
    size_t zrst2 = ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    if (ZSTD_isError(zrst2)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    size_t zpref2 = ZSTD_CCtx_refPrefix(cctx, filtered[1], CHUNK_SZ);
    if (ZSTD_isError(zpref2)) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    ZSTD_inBuffer in2 = { filtered[2], CHUNK_SZ, 0 };
    ZSTD_outBuffer out2 = { cmp[2], sizeof(cmp[2]), 0 };
    size_t zret2 = ZSTD_compressStream2(cctx, &out2, &in2, ZSTD_e_end);
    if (ZSTD_isError(zret2) || zret2 != 0 || in2.pos != in2.size) { ZSTD_freeCCtx(cctx); return TEST_FAIL; }
    cmp_sz[2] = out2.pos;

    ZSTD_freeCCtx(cctx);

    /* Escribir contenedor BPK con BP_FLAG_CHECKSUM activo */
    FILE *f = fopen(path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_v2_header(&hdr);
    hdr.flags |= BP_FLAG_CHECKSUM;
    if (fwrite(&hdr, 1, sizeof(hdr), f) != sizeof(hdr)) {
        fclose(f); remove(path); return TEST_FAIL;
    }

    for (int i = 0; i < 3; i++) {
        binpack_chunk_header_t ch;
        memset(&ch, 0, sizeof(ch));
        ch.chunk_magic = BINPACK_MAGIC_CHUNK;
        ch.uncompressed_sz = (uint32_t)CHUNK_SZ;
        ch.compressed_sz = (uint32_t)cmp_sz[i];
        ch.filter_id = (uint8_t)filter_id;
        ch.filter_flags = (i > 0) ? BP_CHUNK_FLAG_PREFIX : 0;
        ch.chunk_crc32 = crc_orig[i];

        if (fwrite(&ch, 1, sizeof(ch), f) != sizeof(ch) ||
            fwrite(cmp[i], 1, cmp_sz[i], f) != cmp_sz[i]) {
            fclose(f); remove(path); return TEST_FAIL;
        }
    }

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 3;
    ftr.total_uncompressed = 3 * CHUNK_SZ;
    if (fwrite(&ftr, 1, sizeof(ftr), f) != sizeof(ftr)) {
        fclose(f); remove(path); return TEST_FAIL;
    }
    fclose(f);

    /* Abrir archivo con motor BinPack Archive */
    binpack_archive_t *arch = binpack_archive_open(path);
    if (!arch) { remove(path); return TEST_FAIL; }

    /* Descomprimir Chunk 2: requiere resolver prefijos 0 -> 1 -> 2, desfiltrar y validar CRC */
    uint8_t dst2[4096];
    size_t dsz2 = 0;
    binpack_status_t st2 = binpack_archive_decompress_chunk(arch, 2, dst2, sizeof(dst2), &dsz2);
    if (st2 != BINPACK_OK || dsz2 != CHUNK_SZ || memcmp(dst2, orig[2], CHUNK_SZ) != 0) {
        printf("    [A14 Fail] Chunk 2 decompress: st=%d, dsz=%zu, memcmp=%d\n",
               st2, dsz2, memcmp(dst2, orig[2], CHUNK_SZ));
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    /* Descomprimir Chunk 1: requiere resolver prefijo 0 -> 1, desfiltrar y validar CRC */
    uint8_t dst1[4096];
    size_t dsz1 = 0;
    binpack_status_t st1 = binpack_archive_decompress_chunk(arch, 1, dst1, sizeof(dst1), &dsz1);
    if (st1 != BINPACK_OK || dsz1 != CHUNK_SZ || memcmp(dst1, orig[1], CHUNK_SZ) != 0) {
        printf("    [A14 Fail] Chunk 1 decompress: st=%d, dsz=%zu, memcmp=%d\n",
               st1, dsz1, memcmp(dst1, orig[1], CHUNK_SZ));
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    /* Descomprimir Chunk 0: standalone, desfiltrar y validar CRC */
    uint8_t dst0[4096];
    size_t dsz0 = 0;
    binpack_status_t st0 = binpack_archive_decompress_chunk(arch, 0, dst0, sizeof(dst0), &dsz0);
    if (st0 != BINPACK_OK || dsz0 != CHUNK_SZ || memcmp(dst0, orig[0], CHUNK_SZ) != 0) {
        printf("    [A14 Fail] Chunk 0 decompress: st=%d, dsz=%zu, memcmp=%d\n",
               st0, dsz0, memcmp(dst0, orig[0], CHUNK_SZ));
        binpack_archive_close(arch);
        remove(path);
        return TEST_FAIL;
    }

    binpack_archive_close(arch);
    remove(path);
    return TEST_PASS;
}

/**
 * A14: Integración completa de Filtros Arquitectónicos BCJ con Prefijos Rodantes.
 * Cubre exhaustivamente ambas arquitecturas requeridas:
 * - Parte A: BCJ x86_64 + Prefijos Rodantes
 * - Parte B: BCJ AArch64 (ARM64) + Prefijos Rodantes
 */
static int test_A14_bcj_filter_with_rolling_prefixes(void) {
    /* Parte A: x86_64 */
    int rc_x86 = test_a14_filter_prefix_subcase(
        BINPACK_FILTER_X86_64,
        "test_bp6a_a14_x86.bpk",
        binpack_bcj_x86_filter,
        binpack_bcj_x86_unfilter,
        0 /* x86 */
    );
    if (rc_x86 != TEST_PASS) {
        printf("    [A14 FAIL] Subcaso BCJ x86_64 con prefijos fallo\n");
        return TEST_FAIL;
    }

    /* Parte B: AArch64 */
    int rc_arm = test_a14_filter_prefix_subcase(
        BINPACK_FILTER_AARCH64,
        "test_bp6a_a14_arm64.bpk",
        binpack_bcj_aarch64_filter,
        binpack_bcj_aarch64_unfilter,
        1 /* AArch64 */
    );
    if (rc_arm != TEST_PASS) {
        printf("    [A14 FAIL] Subcaso BCJ AArch64 con prefijos fallo\n");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

/* ========================================================================= */
/* Runner Principal y Despacho                                               */
/* ========================================================================= */

typedef struct {
    const char *name;
    int (*fn)(void);
} test_case_t;

int main(int argc, char **argv) {
    g_self_path = argv[0];

    if (argc > 1 && strcmp(argv[1], "--subtest-a11") == 0) {
        return execute_subtest_a11_isolated();
    }

    test_case_t tests[] = {
        /* Bloque I */
        { "A01: Archive sin prefijos", test_A01_archive_no_prefix },
        { "A02: Cadena de 1 prefijo legítimo", test_A02_single_prefix_dependency },
        { "A03: Cadena de 3 prefijos legítimos (4 chunks)", test_A03_three_prefix_dependencies },
        { "A04: Cadena límite de 8 prefijos legítimos (9 chunks)", test_A04_eight_prefix_dependencies_boundary },

        /* Bloque II */
        { "A05: Cadena que supera el límite (9 dependencias)", test_A05_nine_prefix_dependencies_exceeds_limit },
        { "A06: Chunk 0 con prefijo inválido", test_A06_chunk_zero_with_prefix_flag },
        { "A07: Prefijo con tamaño cero", test_A07_zero_size_prefix_chunk },
        { "A08: Prefijo que excede presupuesto de memoria", test_A08_prefix_size_exceeds_memory_budget },
        { "A09: Chunk intermedio corrupto en la cadena", test_A09_corrupted_intermediate_prefix_payload },
        { "A10: Archivo truncado físicamente vs metadatos", test_A10_physically_truncated_archive_vs_corrupt_metadata },

        /* Bloque III */
        { "A11: Resistencia a DoS (cadena profunda 500 chunks, telemetria 9/0/0)", test_A11_isolated_500_prefix_chain },
        { "A12: Fault injection (fallos OOM en raiz e intermedio, buffer small)", test_A12_allocation_failure_handling },

        /* Bloque IV */
        { "A13: Regresión histórica C5 (compatibilidad previa)", test_A13_historical_c5_regression },
        { "A14: Integración Filtros Arquitectónicos BCJ (x86_64 y AArch64) con Prefijos Rodantes", test_A14_bcj_filter_with_rolling_prefixes }
    };

    int total = sizeof(tests) / sizeof(tests[0]);
    int passed = 0;
    int failed = 0;

    printf("======================================================================\n");
    printf("BP-006A Test Suite: Rolling Prefix & V-08 Hardening (v2.4)\n");
    printf("======================================================================\n\n");

    for (int i = 0; i < total; i++) {
        printf("[RUN ] %s...\n", tests[i].name);
        int rc = tests[i].fn();
        if (rc == TEST_PASS) {
            printf("[PASS] %s\n", tests[i].name);
            passed++;
        } else {
            printf("[FAIL] %s\n", tests[i].name);
            failed++;
        }
    }

    printf("\n======================================================================\n");
    printf("Resultados BP-006A: %d/%d pruebas ejecutadas con éxito (%d fallos)\n", passed, total, failed);
    printf("======================================================================\n");

    return failed;
}
