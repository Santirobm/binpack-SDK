/**
 * @file test_bounds_safety.c
 * @brief BP-004 Adversarial Memory Bounds, Arithmetic Overflow & Framing Test Suite (v2.1)
 *
 * Distinción de auditoría estricta:
 *  - [MODELO-MATEMATICO-AISLADO]: Pruebas de expresiones y algoritmos de crecimiento
 *                                 en contextos controlados (evalúan el comportamiento ante
 *                                 desbordamientos SIZE_MAX sin requerir asignación física de RAM).
 *  - [API-PUBLICA:BUFFER]:        Pruebas de descompresión in-memory vía binpack_decompress_buffer/into.
 *  - [API-PUBLICA:STREAM]:        Pruebas de descompresión streaming vía binpack_decompress_stream
 *                                 con callbacks dúplex reales de lectura y escritura.
 *  - [API-PUBLICA:ARCHIVE]:       Pruebas de apertura y consulta de archives sobre archivos corruptos.
 *  - [RECURSION-SISTEMA]:         Pruebas funcionales de descompresión con prefijos rodantes dependientes.
 *  - [INTEGRIDAD-ATOMICIDAD]:     Pruebas de consistencia de punteros, atomicidad y resiliencia ante errores.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <assert.h>
#include <zstd.h>

#include "../include/binpack_sdk.h"
#include "../src/binpack_internal.h"

#define TEST_PASS 0
#define TEST_FAIL 1

static int g_tests_run = 0;
static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define RUN_TEST(fn) do { \
    g_tests_run++; \
    printf("  [RUN ] %s...\n", #fn); \
    if ((fn)() == TEST_PASS) { \
        g_tests_passed++; \
        printf("  [PASS] %s\n", #fn); \
    } else { \
        g_tests_failed++; \
        printf("  [FAIL] %s\n", #fn); \
    } \
} while(0)

/* ========================================================================= */
/* GRUPO A: Modelos Matemáticos Aislados de Aritmética y Crecimiento         */
/* (Nota: Evalúan las expresiones de src/binpack_sdk.c en modelos aislados)  */
/* ========================================================================= */

typedef struct test_mem_stream_ctx_s {
    const uint8_t *src;
    size_t src_size;
    size_t src_pos;

    uint8_t *dst;
    size_t dst_cap;
    size_t dst_size;
} test_mem_stream_ctx_t;

/* Algoritmo de mem_write_cb en src/binpack_sdk.c:71-84 */
static size_t model_unpatched_mem_write_cb(test_mem_stream_ctx_t *ctx, size_t size) {
    if (ctx->dst_size + size > ctx->dst_cap) {
        size_t new_cap = ctx->dst_cap ? ctx->dst_cap * 2 : 65536;
        while (new_cap < ctx->dst_size + size) {
            if (new_cap > SIZE_MAX / 2) return 0;
            new_cap *= 2;
        }
        ctx->dst_cap = new_cap;
    }
    ctx->dst_size += size;
    return size;
}

typedef struct test_fixed_mem_stream_ctx_s {
    uint8_t *dst;
    size_t dst_cap;
    size_t dst_size;
    int overflow;
} test_fixed_mem_stream_ctx_t;

/* Algoritmo de fixed_mem_write_cb en src/binpack_sdk.c:558-567 */
static size_t model_unpatched_fixed_mem_write_cb(test_fixed_mem_stream_ctx_t *ctx, size_t size) {
    if (ctx->dst_size + size > ctx->dst_cap) {
        ctx->overflow = 1;
        return 0;
    }
    ctx->dst_size += size;
    return size;
}

/* Algoritmo defensivo corregido con validación de dos pasos */
static size_t model_defensive_fixed_mem_write_cb(test_fixed_mem_stream_ctx_t *ctx, size_t size) {
    if (ctx->dst_size > ctx->dst_cap ||
        size > ctx->dst_cap - ctx->dst_size) {
        ctx->overflow = 1;
        return 0;
    }
    ctx->dst_size += size;
    return size;
}

static int test_A1_addition_overflow_wrap_around(void) {
    /* [MODELO-MATEMATICO-AISLADO] V-01:
     * Evalúa la expresión (dst_size + size) cuando dst_size = SIZE_MAX, size = 1.
     * Resultado: SIZE_MAX + 1 = 0. En código no protegido, 0 <= dst_cap elude el if.
     */
    test_mem_stream_ctx_t ctx;
    ctx.dst_cap = 65536;
    ctx.dst_size = SIZE_MAX;

    size_t sum = ctx.dst_size + 1;
    if (sum != 0) return TEST_FAIL;

    int bypassed_in_unpatched = (ctx.dst_size + 1 <= ctx.dst_cap);
    if (!bypassed_in_unpatched) return TEST_FAIL;

    /* Verificación de regla sustractiva: size > SIZE_MAX - dst_size */
    size_t size = 1;
    int safe_rejected = (size > SIZE_MAX - ctx.dst_size);
    if (!safe_rejected) return TEST_FAIL;

    return TEST_PASS;
}

static int test_A2_addition_overflow_offset(void) {
    /* [MODELO-MATEMATICO-AISLADO] V-01:
     * dst_size = SIZE_MAX - 10, size = 20. (SIZE_MAX - 10) + 20 = 9 <= 65536.
     */
    test_mem_stream_ctx_t ctx;
    ctx.dst_cap = 65536;
    ctx.dst_size = SIZE_MAX - 10;
    size_t size = 20;

    size_t sum = ctx.dst_size + size;
    if (sum != 9) return TEST_FAIL;

    int bypassed = (ctx.dst_size + size <= ctx.dst_cap);
    if (!bypassed) return TEST_FAIL;

    int safe_rejected = (size > SIZE_MAX - ctx.dst_size);
    if (!safe_rejected) return TEST_FAIL;

    return TEST_PASS;
}

static int test_A3_geometric_capacity_overflow(void) {
    /* [MODELO-MATEMATICO-AISLADO] V-02:
     * Si cap > SIZE_MAX / 2, la operación new_cap *= 2 produce desbordamiento.
     * Demostración del bucle: si new_cap duplica hasta desbordar a 0,
     * un bucle while (new_cap < required) new_cap *= 2; con new_cap == 0 gira indefinidamente.
     */
    size_t cap = (SIZE_MAX / 2) + 100;
    size_t doubled = cap * 2;
    if (doubled >= cap) return TEST_FAIL; /* Confirmar desbordamiento */

    /* Demostración de bucle infinito potencial si new_cap llegara a 0:
     * 0 * 2 = 0, condición (0 < required) siempre verdadera.
     */
    size_t zero_cap = 0;
    int would_infinite_loop = (zero_cap * 2 == 0);
    if (!would_infinite_loop) return TEST_FAIL;

    /* Regla segura: cap > SIZE_MAX / 2 detecta la imposibilidad de duplicar */
    int safe_detected = (cap > SIZE_MAX / 2);
    if (!safe_detected) return TEST_FAIL;

    return TEST_PASS;
}

static int test_A4_fixed_buffer_overflow_wrap(void) {
    /* [MODELO-MATEMATICO-AISLADO] V-03:
     * Buffer fijo ante wrap-around y verificación de condición doble:
     * 1) ctx->dst_size > ctx->dst_cap
     * 2) size > ctx->dst_cap - ctx->dst_size
     */
    test_fixed_mem_stream_ctx_t ctx;
    ctx.dst = (uint8_t*)0x1000;
    ctx.dst_cap = 100;
    ctx.dst_size = SIZE_MAX - 5;
    ctx.overflow = 0;

    /* En código desprotegido: (SIZE_MAX - 5) + 10 = 4 <= 100 -> elude overflow */
    size_t ret = model_unpatched_fixed_mem_write_cb(&ctx, 10);
    if (ret != 10 || ctx.overflow != 0) return TEST_FAIL;

    /* En código defensivo: detecta desbordamiento cuando dst_size es grande */
    ctx.dst_size = SIZE_MAX - 5;
    ctx.overflow = 0;
    ret = model_defensive_fixed_mem_write_cb(&ctx, 10);
    if (ret != 0 || ctx.overflow != 1) return TEST_FAIL;

    /* Verificación de segundo caso: dst_size ya supera dst_cap */
    ctx.dst_size = 150;
    ctx.overflow = 0;
    ret = model_defensive_fixed_mem_write_cb(&ctx, 10);
    if (ret != 0 || ctx.overflow != 1) return TEST_FAIL;

    return TEST_PASS;
}

static int test_A5_zero_size_write_contract(void) {
    /* [MODELO-MATEMATICO-AISLADO] Contrato de escritura con tamaño cero:
     * Cuando size == 0, la función debe ser un no-op seguro:
     * no debe realizar realloc, no debe intentar escribir, y debe retornar 0.
     */
    test_mem_stream_ctx_t ctx;
    ctx.dst = NULL;
    ctx.dst_cap = 0;
    ctx.dst_size = 0;

    size_t ret = model_unpatched_mem_write_cb(&ctx, 0);
    if (ret != 0 || ctx.dst_cap != 0 || ctx.dst_size != 0) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

/* ========================================================================= */
/* GRUPO B: Pruebas de API Pública: In-Memory Buffer vs Streaming Dúplex      */
/* ========================================================================= */

/* Contexto Dúplex unificado para binpack_decompress_stream() */
typedef struct {
    const uint8_t *src;
    size_t src_size;
    size_t src_pos;

    uint8_t *dst;
    size_t dst_cap;
    size_t dst_size;
    size_t total_written;
} test_stream_duplex_ctx_t;

static size_t test_stream_duplex_reader(void *user_data, void *buffer, size_t size) {
    test_stream_duplex_ctx_t *ctx = (test_stream_duplex_ctx_t*)user_data;
    if (ctx->src_pos >= ctx->src_size) return 0;
    size_t avail = ctx->src_size - ctx->src_pos;
    size_t to_read = (size < avail) ? size : avail;
    memcpy(buffer, ctx->src + ctx->src_pos, to_read);
    ctx->src_pos += to_read;
    return to_read;
}

static size_t test_stream_duplex_writer(void *user_data, const void *buffer, size_t size) {
    test_stream_duplex_ctx_t *ctx = (test_stream_duplex_ctx_t*)user_data;
    if (ctx->dst && ctx->dst_cap >= ctx->dst_size + size) {
        memcpy(ctx->dst + ctx->dst_size, buffer, size);
        ctx->dst_size += size;
    }
    ctx->total_written += size;
    return size;
}

/* Helpers seguros para invocar ambas interfaces con ensamblado alineado */
static binpack_status_t decompress_via_buffer_api(const uint8_t *data, size_t size) {
    void *out = NULL;
    size_t out_sz = 0;
    binpack_status_t st = binpack_decompress_buffer(data, size, &out, &out_sz);
    if (out) free(out);
    return st;
}

static binpack_status_t decompress_via_stream_api(const uint8_t *data, size_t size, size_t *out_written) {
    test_stream_duplex_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = data;
    ctx.src_size = size;

    binpack_status_t st = binpack_decompress_stream(test_stream_duplex_reader, test_stream_duplex_writer, &ctx, NULL);
    if (out_written) *out_written = ctx.total_written;
    return st;
}

/* Helper para ensamblar flujos V2 alineados sin punteros directos no alineados */
static void assemble_v2_stream(
    uint8_t *out_buf, size_t *out_len,
    const binpack_v2_header_t *hdr,
    const binpack_chunk_header_t *chdr,
    const binpack_chunk_bounds_t *bounds,
    const uint8_t *payload, size_t payload_sz
) {
    size_t off = 0;
    if (hdr) {
        memcpy(out_buf + off, hdr, sizeof(*hdr));
        off += sizeof(*hdr);
    }
    if (chdr) {
        memcpy(out_buf + off, chdr, sizeof(*chdr));
        off += sizeof(*chdr);
    }
    if (bounds) {
        memcpy(out_buf + off, bounds, sizeof(*bounds));
        off += sizeof(*bounds);
    }
    if (payload && payload_sz > 0) {
        memcpy(out_buf + off, payload, payload_sz);
        off += payload_sz;
    }
    if (out_len) *out_len = off;
}

static void init_default_v2_header(binpack_v2_header_t *hdr) {
    memset(hdr, 0, sizeof(*hdr));
    hdr->magic = BINPACK_MAGIC_V2;
    hdr->version_major = 2;
    hdr->version_minor = 2;
    hdr->flags = BP_FLAG_CHECKSUM;
    hdr->default_chunk_sz = 65536;
}

static int test_B0_valid_stream_roundtrip(void) {
    /* [API-PUBLICA:STREAM] Validación previa: comprobar que la ruta streaming con callbacks dúplex
     * funciona perfectamente en un caso válido y que ctx.total_written coincide con el tamaño original.
     */
    uint8_t original[1024];
    for (size_t i = 0; i < sizeof(original); i++) original[i] = (uint8_t)(i & 0xFF);

    void *cmp_data = NULL;
    size_t cmp_sz = 0;
    binpack_status_t st_cmp = binpack_compress_buffer(original, sizeof(original), &cmp_data, &cmp_sz, NULL);
    if (st_cmp != BINPACK_OK || !cmp_data) return TEST_FAIL;

    uint8_t restored[1024];
    test_stream_duplex_ctx_t ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.src = (const uint8_t*)cmp_data;
    ctx.src_size = cmp_sz;
    ctx.dst = restored;
    ctx.dst_cap = sizeof(restored);

    binpack_status_t st_dec = binpack_decompress_stream(test_stream_duplex_reader, test_stream_duplex_writer, &ctx, NULL);
    binpack_free(cmp_data);

    if (st_dec != BINPACK_OK) {
        printf("    -> Fallo: decompress_stream fallo con st=%d\n", st_dec);
        return TEST_FAIL;
    }
    if (ctx.total_written != sizeof(original) || memcmp(restored, original, sizeof(original)) != 0) {
        printf("    -> Fallo: bytes escritos (%zu) no coinciden con original (%zu)\n", ctx.total_written, sizeof(original));
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_B1_uncompressed_sz_zero(void) {
    /* [API-PUBLICA:BUFFER y STREAM] uncompressed_sz = 0 */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = 0;
    chdr.compressed_sz = 32;

    uint8_t stream[sizeof(hdr) + sizeof(chdr)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, NULL, NULL, 0);

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) {
        printf("    -> Fallo: st_buf=%d, st_stm=%d\n", st_buf, st_stm);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_B2_compressed_sz_zero_discrepancy(void) {
    /* [API-PUBLICA:BUFFER vs STREAM] compressed_sz = 0
     * Verificación de consistencia: ambas interfaces (Buffer API y Streaming API)
     * rechazan upfront con BINPACK_ERR_CORRUPT_DATA (-5) sin asignar memoria.
     */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = 100;
    chdr.compressed_sz = 0;

    uint8_t stream[sizeof(hdr) + sizeof(chdr)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, NULL, NULL, 0);

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    printf("    -> Buffer API retorno:    %d (BINPACK_ERR_CORRUPT_DATA)\n", st_buf);
    printf("    -> Streaming API retorno: %d (BINPACK_ERR_CORRUPT_DATA)\n", st_stm);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) {
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_B3_uncompressed_sz_exceeds_max(void) {
    /* [API-PUBLICA:BUFFER y STREAM] uncompressed_sz > 64 MB */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = (uint32_t)(BINPACK_MAX_CHUNK_SIZE * 4ULL + 1);
    chdr.compressed_sz = 64;

    uint8_t stream[sizeof(hdr) + sizeof(chdr)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, NULL, NULL, 0);

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) return TEST_FAIL;
    return TEST_PASS;
}


static int test_B4_compressed_sz_exceeds_max(void) {
    /* [API-PUBLICA:BUFFER y STREAM] compressed_sz > 80 MB */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = 1024;
    chdr.compressed_sz = (uint32_t)(BINPACK_MAX_CHUNK_SIZE * 5ULL + 1);

    uint8_t stream[sizeof(hdr) + sizeof(chdr)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, NULL, NULL, 0);

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) return TEST_FAIL;
    return TEST_PASS;
}


static int test_B5_truncated_stream_header(void) {
    /* [API-PUBLICA:BUFFER y STREAM] Header V2 truncado */
    uint8_t short_hdr[10] = { 'B', 'P', 'K', '2', 2, 2, 0, 0, 0, 0 };
    binpack_status_t st_buf = decompress_via_buffer_api(short_hdr, sizeof(short_hdr));
    binpack_status_t st_stm = decompress_via_stream_api(short_hdr, sizeof(short_hdr), NULL);

    if (st_buf == BINPACK_OK || st_stm == BINPACK_OK) return TEST_FAIL;
    return TEST_PASS;
}

static int test_B6_truncated_chunk_header(void) {
    /* [API-PUBLICA:BUFFER y STREAM] Chunk header incompleto */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    uint8_t stream[sizeof(hdr) + 4];
    memcpy(stream, &hdr, sizeof(hdr));
    memcpy(stream + sizeof(hdr), "CHNK", 4);

    binpack_status_t st_buf = decompress_via_buffer_api(stream, sizeof(stream));
    binpack_status_t st_stm = decompress_via_stream_api(stream, sizeof(stream), NULL);

    if (st_buf == BINPACK_OK || st_stm == BINPACK_OK) return TEST_FAIL;
    return TEST_PASS;
}

static int test_B7_truncated_chunk_payload(void) {
    /* [API-PUBLICA:BUFFER y STREAM] Payload truncado respecto a compressed_sz */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = 1000;
    chdr.compressed_sz = 500;

    uint8_t dummy_payload[20] = { 0 };
    uint8_t stream[sizeof(hdr) + sizeof(chdr) + sizeof(dummy_payload)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, NULL, dummy_payload, sizeof(dummy_payload));

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) return TEST_FAIL;
    return TEST_PASS;
}


static int test_B8_bounds_offset_oob(void) {
    /* [API-PUBLICA:FRAMING] code_offset fuera de uncompressed_sz */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.filter_flags = BP_CHUNK_FLAG_BOUNDS;
    chdr.uncompressed_sz = 500;
    chdr.compressed_sz = 32;

    binpack_chunk_bounds_t bounds;
    memset(&bounds, 0, sizeof(bounds));
    bounds.code_offset = 600;
    bounds.code_length = 50;

    uint8_t dummy_payload[32] = { 0 };
    uint8_t stream[sizeof(hdr) + sizeof(chdr) + sizeof(bounds) + sizeof(dummy_payload)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, &bounds, dummy_payload, sizeof(dummy_payload));

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) return TEST_FAIL;
    return TEST_PASS;
}


static int test_B9_bounds_length_overflow(void) {
    /* [API-PUBLICA:FRAMING] code_offset + code_length > uncompressed_sz */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.filter_flags = BP_CHUNK_FLAG_BOUNDS;
    chdr.uncompressed_sz = 500;
    chdr.compressed_sz = 32;

    binpack_chunk_bounds_t bounds;
    memset(&bounds, 0, sizeof(bounds));
    bounds.code_offset = 400;
    bounds.code_length = 200;

    uint8_t dummy_payload[32] = { 0 };
    uint8_t stream[sizeof(hdr) + sizeof(chdr) + sizeof(bounds) + sizeof(dummy_payload)];
    size_t stream_sz = 0;
    assemble_v2_stream(stream, &stream_sz, &hdr, &chdr, &bounds, dummy_payload, sizeof(dummy_payload));

    binpack_status_t st_buf = decompress_via_buffer_api(stream, stream_sz);
    binpack_status_t st_stm = decompress_via_stream_api(stream, stream_sz, NULL);

    if (st_buf != BINPACK_ERR_CORRUPT_DATA || st_stm != BINPACK_ERR_CORRUPT_DATA) return TEST_FAIL;
    return TEST_PASS;
}

/* ========================================================================= */
/* Tests B10-B14: Endurecimiento Avanzado de Streaming (BP-004C.3)           */
/* ========================================================================= */

static size_t malformed_reader_exceeds_chunk(void *user_data, void *buffer, size_t size) {
    (void)user_data;
    memset(buffer, 0xAA, size);
    return size + 128; /* Ilegal: devuelve más que el chunk_size solicitado */
}

static size_t dummy_discard_writer(void *user_data, const void *buffer, size_t size) {
    (void)user_data; (void)buffer;
    return size;
}

static int test_B10_compress_stream_reader_exceeds_chunk_sz(void) {
    /* [API-PUBLICA:STREAM] Lector que reporta más bytes que chunk_size debe retornar BINPACK_ERR_IO */
    binpack_options_t opt;
    binpack_default_options(&opt);
    binpack_status_t st = binpack_compress_stream(malformed_reader_exceeds_chunk, dummy_discard_writer, NULL, 0, &opt, NULL);
    if (st != BINPACK_ERR_IO) {
        printf("    -> Fallo esperado BINPACK_ERR_IO, obtenido %d\n", st);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_B11_decompress_stream_incomplete_and_no_progress(void) {
    /* [API-PUBLICA:STREAM] Comprobación rigurosa de avance y frames incompletos */
    uint8_t original[512];
    for (size_t i = 0; i < sizeof(original); i++) original[i] = (uint8_t)(i ^ 0x5A);

    uint8_t cbuf[512];
    size_t c_sz = ZSTD_compress(cbuf, sizeof(cbuf), original, sizeof(original), 1);
    if (ZSTD_isError(c_sz)) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    /* Escenario 1: Frame incompleto / corrupto con prefijo rodante (BP_CHUNK_FLAG_PREFIX) */
    {
        binpack_chunk_header_t chdr0;
        memset(&chdr0, 0, sizeof(chdr0));
        chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr0.uncompressed_sz = (uint32_t)sizeof(original);
        chdr0.compressed_sz = (uint32_t)c_sz;
        chdr0.chunk_crc32 = binpack_crc32(0, original, sizeof(original));

        binpack_chunk_header_t chdr1;
        memset(&chdr1, 0, sizeof(chdr1));
        chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr1.filter_flags = BP_CHUNK_FLAG_PREFIX;
        chdr1.uncompressed_sz = 256;
        chdr1.compressed_sz = 32;
        chdr1.chunk_crc32 = 0x12345678;

        uint8_t bad_payload[32] = { 0xFF, 0xFE, 0xFD, 0xFC, 0x00, 0x01, 0x02, 0x03 };

        uint8_t stream[2048];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr0, sizeof(chdr0)); off += sizeof(chdr0);
        memcpy(stream + off, cbuf, c_sz); off += c_sz;
        memcpy(stream + off, &chdr1, sizeof(chdr1)); off += sizeof(chdr1);
        memcpy(stream + off, bad_payload, sizeof(bad_payload)); off += sizeof(bad_payload);

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_DECOMPRESSION_FAILED && st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Escenario 1 (prefijo corrupto): obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    /* Escenario 2: Frame incompleto sin prefijo (estándar Zstd) */
    {
        binpack_chunk_header_t chdr;
        memset(&chdr, 0, sizeof(chdr));
        chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr.uncompressed_sz = (uint32_t)sizeof(original);
        chdr.compressed_sz = 16; /* Truncado: solo 16 bytes de payload en vez de c_sz */
        chdr.chunk_crc32 = 0x12345678;

        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf, 16); off += 16;

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_DECOMPRESSION_FAILED && st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Escenario 2 (frame estandar truncado): obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    /* Escenario 3: Payload válido pero con salida menor que la declarada en chdr */
    {
        binpack_chunk_header_t chdr;
        memset(&chdr, 0, sizeof(chdr));
        chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr.uncompressed_sz = (uint32_t)sizeof(original) + 500; /* Declaración fraudulenta */
        chdr.compressed_sz = (uint32_t)c_sz;
        chdr.chunk_crc32 = binpack_crc32(0, original, sizeof(original));

        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf, c_sz); off += c_sz;

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_DECOMPRESSION_FAILED && st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Escenario 3 (salida menor que declarada): obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

static int test_B12_decompress_stream_concatenated_frames_and_trailing_chunk_bytes(void) {
    /* [API-PUBLICA:STREAM] Regla de frame único por chunk: rechazo de frames concatenados o trailing bytes en payload */
    uint8_t data1[256];
    uint8_t data2[256];
    memset(data1, 'X', sizeof(data1));
    memset(data2, 'Y', sizeof(data2));

    uint8_t cbuf1[256];
    uint8_t cbuf2[256];
    size_t c_sz1 = ZSTD_compress(cbuf1, sizeof(cbuf1), data1, sizeof(data1), 1);
    size_t c_sz2 = ZSTD_compress(cbuf2, sizeof(cbuf2), data2, sizeof(data2), 1);
    if (ZSTD_isError(c_sz1) || ZSTD_isError(c_sz2)) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    /* Subcaso 1: Bloque estándar con dos frames Zstd concatenados dentro del mismo chunk */
    {
        binpack_chunk_header_t chdr;
        memset(&chdr, 0, sizeof(chdr));
        chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr.uncompressed_sz = (uint32_t)sizeof(data1);
        chdr.compressed_sz = (uint32_t)(c_sz1 + c_sz2); /* Declara ambos frames en un único chunk */
        chdr.chunk_crc32 = binpack_crc32(0, data1, sizeof(data1));

        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf1, c_sz1); off += c_sz1;
        memcpy(stream + off, cbuf2, c_sz2); off += c_sz2;

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Subcaso 1 (frames concatenados en chunk estandar): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    /* Subcaso 2: Bloque con prefijo rodante donde el frame termina (zret == 0) pero quedan bytes en payload */
    {
        /* Chunk 0 previo válido */
        binpack_chunk_header_t chdr0;
        memset(&chdr0, 0, sizeof(chdr0));
        chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr0.uncompressed_sz = (uint32_t)sizeof(data1);
        chdr0.compressed_sz = (uint32_t)c_sz1;
        chdr0.chunk_crc32 = binpack_crc32(0, data1, sizeof(data1));

        /* Chunk 1 prefijo con bytes adicionales tras el frame */
        binpack_chunk_header_t chdr1;
        memset(&chdr1, 0, sizeof(chdr1));
        chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr1.filter_flags = BP_CHUNK_FLAG_PREFIX;
        chdr1.uncompressed_sz = (uint32_t)sizeof(data2);
        chdr1.compressed_sz = (uint32_t)(c_sz2 + 16); /* 16 bytes extras de basura tras el frame */
        chdr1.chunk_crc32 = binpack_crc32(0, data2, sizeof(data2));

        uint8_t trailing_garbage[16] = { 0xAA, 0xBB, 0xCC, 0xDD };

        uint8_t stream[2048];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr0, sizeof(chdr0)); off += sizeof(chdr0);
        memcpy(stream + off, cbuf1, c_sz1); off += c_sz1;
        memcpy(stream + off, &chdr1, sizeof(chdr1)); off += sizeof(chdr1);
        memcpy(stream + off, cbuf2, c_sz2); off += c_sz2;
        memcpy(stream + off, trailing_garbage, sizeof(trailing_garbage)); off += sizeof(trailing_garbage);

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Subcaso 2 (trailing bytes en chunk de prefijo): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

static int test_B13_decompress_stream_footer_trailing_bytes_and_missing_footer(void) {
    /* [API-PUBLICA:STREAM] Validación fail-closed de footer, terminación estricta y trailing bytes */
    uint8_t original[256];
    memset(original, 'A', sizeof(original));
    uint8_t cbuf[256];
    size_t c_sz = ZSTD_compress(cbuf, sizeof(cbuf), original, sizeof(original), 1);
    if (ZSTD_isError(c_sz)) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = (uint32_t)sizeof(original);
    chdr.compressed_sz = (uint32_t)c_sz;
    chdr.chunk_crc32 = binpack_crc32(0, original, sizeof(original));

    binpack_v2_footer_t footer;
    memset(&footer, 0, sizeof(footer));
    footer.footer_magic = BINPACK_MAGIC_FOOTER;
    footer.stream_crc32 = binpack_crc32(0, original, sizeof(original));
    footer.total_uncompressed = sizeof(original);

    /* Subcaso 1: Footer válido seguido de bytes adicionales al final del stream */
    {
        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf, c_sz); off += c_sz;
        memcpy(stream + off, &footer, sizeof(footer)); off += sizeof(footer);
        uint8_t trailing_bytes[8] = { 0xDE, 0xAD, 0xBE, 0xEF };
        memcpy(stream + off, trailing_bytes, sizeof(trailing_bytes)); off += sizeof(trailing_bytes);

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Subcaso 1 (trailing bytes tras footer): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    /* Subcaso 2: Stream truncado que finaliza limpiamente (EOF) sin footer */
    {
        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf, c_sz); off += c_sz;

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Subcaso 2 (stream sin footer): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    /* Subcaso 3: Footer con total_uncompressed fraudulento */
    {
        binpack_v2_footer_t bad_ftr = footer;
        bad_ftr.total_uncompressed = sizeof(original) + 999;

        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf, c_sz); off += c_sz;
        memcpy(stream + off, &bad_ftr, sizeof(bad_ftr)); off += sizeof(bad_ftr);

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Subcaso 3 (footer uncompressed mismatch): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    /* Subcaso 4: Footer truncado (solo 4 bytes leídos) */
    {
        uint8_t stream[1024];
        size_t off = 0;
        memcpy(stream + off, &hdr, sizeof(hdr)); off += sizeof(hdr);
        memcpy(stream + off, &chdr, sizeof(chdr)); off += sizeof(chdr);
        memcpy(stream + off, cbuf, c_sz); off += c_sz;
        uint32_t partial_magic = BINPACK_MAGIC_FOOTER;
        memcpy(stream + off, &partial_magic, 4); off += 4;

        binpack_status_t st = decompress_via_stream_api(stream, off, NULL);
        if (st != BINPACK_ERR_CORRUPT_DATA) {
            printf("    -> Fallo Subcaso 4 (footer truncado): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st);
            return TEST_FAIL;
        }
    }

    return TEST_PASS;
}

static int test_B14_decompress_stream_v1_legacy_bounds_three_cases(void) {
    /* [API-PUBLICA:STREAM] V1 Single-Block: comprobación exhaustiva del modelo de 3 casos:
     * 1) Bounds válidos dentro del rango -> Descompresión OK
     * 2) Bounds ausentes (0, 0) -> Tratamiento legítimo de buffer completo OK
     * 3) Bounds corruptos (code_off > orig_sz o code_len > orig_sz - code_off) -> BINPACK_ERR_CORRUPT_DATA
     */
    uint8_t raw[128];
    for (size_t i = 0; i < sizeof(raw); i++) raw[i] = (uint8_t)i;

    uint8_t cbuf[256];
    size_t comp_sz = ZSTD_compress(cbuf, sizeof(cbuf), raw, sizeof(raw), 1);
    if (ZSTD_isError(comp_sz)) return TEST_FAIL;

    #define ASSEMBLE_V1(dst, dlen, f_id, c_off, c_len, crc_val) do { \
        uint32_t mag = BINPACK_MAGIC_V1; \
        uint8_t hdr28[28] = { 0 }; \
        binpack_store_le32(&hdr28[0], (uint32_t)sizeof(raw)); \
        binpack_store_le32(&hdr28[4], (uint32_t)comp_sz); \
        binpack_store_le32(&hdr28[8], (uint32_t)(crc_val)); \
        hdr28[12] = (uint8_t)(f_id); \
        binpack_store_le32(&hdr28[20], (uint32_t)(c_off)); \
        binpack_store_le32(&hdr28[24], (uint32_t)(c_len)); \
        memcpy(dst, &mag, 4); \
        memcpy((dst) + 4, hdr28, 28); \
        memcpy((dst) + 32, cbuf, comp_sz); \
        *(dlen) = 32 + comp_sz; \
    } while(0)

    uint8_t s_buf[512];
    size_t s_len = 0;

    /* Caso 1: Bounds válidos (code_off=16, code_len=32 dentro de orig_sz=128, filter_id=2 x86) */
    uint8_t raw_case1[128];
    memcpy(raw_case1, raw, sizeof(raw));
    binpack_bcj_x86_unfilter(raw_case1 + 16, 32);
    uint32_t crc_case1 = binpack_crc32(0, raw_case1, sizeof(raw_case1));

    ASSEMBLE_V1(s_buf, &s_len, 2, 16, 32, crc_case1);
    binpack_status_t st1 = decompress_via_stream_api(s_buf, s_len, NULL);
    if (st1 != BINPACK_OK) {
        printf("    -> Fallo Caso 1 (bounds validos): esperado BINPACK_OK, obtenido %d\n", st1);
        return TEST_FAIL;
    }

    /* Caso 2: Bounds ausentes (code_off=0, code_len=0, filter_id=2 x86 -> desfiltra buffer completo) */
    uint8_t raw_case2[128];
    memcpy(raw_case2, raw, sizeof(raw));
    binpack_bcj_x86_unfilter(raw_case2, sizeof(raw_case2));
    uint32_t crc_case2 = binpack_crc32(0, raw_case2, sizeof(raw_case2));

    ASSEMBLE_V1(s_buf, &s_len, 2, 0, 0, crc_case2);
    binpack_status_t st2 = decompress_via_stream_api(s_buf, s_len, NULL);
    if (st2 != BINPACK_OK) {
        printf("    -> Fallo Caso 2 (bounds ausentes 0,0): esperado BINPACK_OK, obtenido %d\n", st2);
        return TEST_FAIL;
    }

    /* Caso 3a: Bounds corruptos por code_off > orig_sz (code_off=200, code_len=10) */
    ASSEMBLE_V1(s_buf, &s_len, 2, 200, 10, 0);
    binpack_status_t st3a = decompress_via_stream_api(s_buf, s_len, NULL);
    if (st3a != BINPACK_ERR_CORRUPT_DATA) {
        printf("    -> Fallo Caso 3a (code_off > orig_sz): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st3a);
        return TEST_FAIL;
    }

    /* Caso 3b: Bounds corruptos por code_len > orig_sz - code_off (code_off=100, code_len=50 en buffer de 128) */
    ASSEMBLE_V1(s_buf, &s_len, 2, 100, 50, 0);
    binpack_status_t st3b = decompress_via_stream_api(s_buf, s_len, NULL);
    if (st3b != BINPACK_ERR_CORRUPT_DATA) {
        printf("    -> Fallo Caso 3b (code_len overflow): esperado BINPACK_ERR_CORRUPT_DATA, obtenido %d\n", st3b);
        return TEST_FAIL;
    }

    #undef ASSEMBLE_V1
    return TEST_PASS;
}

static int test_B15_v1_historical_fixture_compatibility(void) {
    /* [API-PUBLICA:STREAM/BUFFER] Compatibilidad histórica con binario real (Canterbury 'sum') en formato V1 */
    const char *corpus_path = "tests/corpora/canterbury/sum";
    FILE *fc = fopen(corpus_path, "rb");
    if (!fc) {
        printf("    -> Aviso: no se encontro %s, saltando prueba de fixture\n", corpus_path);
        return TEST_PASS;
    }
    fseek(fc, 0, SEEK_END);
    long orig_len = ftell(fc);
    fseek(fc, 0, SEEK_SET);
    if (orig_len <= 0) { fclose(fc); return TEST_FAIL; }

    uint8_t *orig_data = (uint8_t*)malloc((size_t)orig_len);
    if (!orig_data) { fclose(fc); return TEST_FAIL; }
    if (fread(orig_data, 1, (size_t)orig_len, fc) != (size_t)orig_len) {
        free(orig_data);
        fclose(fc);
        return TEST_FAIL;
    }
    fclose(fc);

    uint32_t expected_crc = binpack_crc32(0, orig_data, (size_t)orig_len);

    /* Generar fixture V1 con bounds ausentes [0,0] */
    uint8_t *filtered = (uint8_t*)malloc((size_t)orig_len);
    if (!filtered) { free(orig_data); return TEST_FAIL; }
    memcpy(filtered, orig_data, (size_t)orig_len);
    binpack_bcj_x86_filter(filtered, (size_t)orig_len);

    size_t cbound = ZSTD_compressBound((size_t)orig_len);
    uint8_t *cdata = (uint8_t*)malloc(cbound);
    if (!cdata) { free(orig_data); free(filtered); return TEST_FAIL; }
    size_t c_sz = ZSTD_compress(cdata, cbound, filtered, (size_t)orig_len, 5);
    free(filtered);
    if (ZSTD_isError(c_sz)) { free(orig_data); free(cdata); return TEST_FAIL; }

    size_t v1_total = 32 + c_sz;
    uint8_t *v1_stream = (uint8_t*)malloc(v1_total);
    if (!v1_stream) { free(orig_data); free(cdata); return TEST_FAIL; }

    uint32_t magic = BINPACK_MAGIC_V1;
    uint8_t hdr28[28] = { 0 };
    binpack_store_le32(&hdr28[0], (uint32_t)orig_len);
    binpack_store_le32(&hdr28[4], (uint32_t)c_sz);
    binpack_store_le32(&hdr28[8], expected_crc);
    hdr28[12] = 2; /* filter_id = 2 (x86) */
    /* code_off = 0, code_len = 0 (bounds ausentes legítimos) */

    memcpy(v1_stream, &magic, 4);
    memcpy(v1_stream + 4, hdr28, 28);
    memcpy(v1_stream + 32, cdata, c_sz);
    free(cdata);

    /* Probar descompresión vía Buffer API */
    void *restored_buf = NULL;
    size_t restored_sz = 0;
    binpack_status_t stb = binpack_decompress_buffer(v1_stream, v1_total, &restored_buf, &restored_sz);
    if (stb != BINPACK_OK || restored_sz != (size_t)orig_len || memcmp(restored_buf, orig_data, (size_t)orig_len) != 0) {
        printf("    -> Fallo: buffer decompress sobre fixture historico V1 fallo (st=%d)\n", stb);
        binpack_free(restored_buf);
        free(v1_stream);
        free(orig_data);
        return TEST_FAIL;
    }
    binpack_free(restored_buf);

    /* Probar descompresión vía Streaming API */
    size_t written = 0;
    binpack_status_t sts = decompress_via_stream_api(v1_stream, v1_total, &written);
    if (sts != BINPACK_OK || written != (size_t)orig_len) {
        printf("    -> Fallo: stream decompress sobre fixture historico V1 fallo (st=%d, written=%zu)\n", sts, written);
        free(v1_stream);
        free(orig_data);
        return TEST_FAIL;
    }

    free(v1_stream);
    free(orig_data);
    return TEST_PASS;
}

static int test_B16_stream_arithmetic_safe_add_helper_boundary_checks(void) {
    /* [API-PUBLICA:ARITMETICA] Validación exhaustiva del helper binpack_safe_add_u64 */
    uint64_t acc = UINT64_MAX - 1;

    /* Caso 1: UINT64_MAX - 1 más 1 -> Éxito exacto en UINT64_MAX */
    if (binpack_safe_add_u64(&acc, 1) != BINPACK_OK || acc != UINT64_MAX) {
        printf("    -> Fallo Caso 1: se esperaba éxito exacto en UINT64_MAX\n");
        return TEST_FAIL;
    }

    /* Caso 2: UINT64_MAX más 1 -> Rechazo por desbordamiento */
    if (binpack_safe_add_u64(&acc, 1) != BINPACK_ERR_CORRUPT_DATA) {
        printf("    -> Fallo Caso 2: se esperaba rechazo por overflow de UINT64_MAX + 1\n");
        return TEST_FAIL;
    }

    /* Caso 3: UINT64_MAX - 5 más 6 -> Rechazo por desbordamiento */
    acc = UINT64_MAX - 5;
    if (binpack_safe_add_u64(&acc, 6) != BINPACK_ERR_CORRUPT_DATA) {
        printf("    -> Fallo Caso 3: se esperaba rechazo por overflow (UINT64_MAX - 5 + 6)\n");
        return TEST_FAIL;
    }

    /* Caso 4: Límite exacto sin overflow (0 más UINT64_MAX) -> Éxito */
    acc = 0;
    if (binpack_safe_add_u64(&acc, UINT64_MAX) != BINPACK_OK || acc != UINT64_MAX) {
        printf("    -> Fallo Caso 4: se esperaba éxito en suma de 0 + UINT64_MAX\n");
        return TEST_FAIL;
    }

    return TEST_PASS;
}

/* ========================================================================= */
/* GRUPO C: Pruebas de API Pública sobre Archive Engine & Recursión Real     */
/* ========================================================================= */

static int test_C1_archive_truncated_footer(void) {
    /* [API-PUBLICA:ARCHIVE] Footer truncado: comprobar que no se acepta como archive indexado */
    const char *tmp_path = "test_adv_c1.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    uint8_t fake_ftr[10] = { 'F', 'P', 'K', '2', 0 };
    fwrite(fake_ftr, 1, sizeof(fake_ftr), f);
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (arch) {
        /* Si abre mediante fallback secuencial, no debe encontrar ningún chunk válido */
        uint32_t count = binpack_archive_get_chunk_count(arch);
        uint8_t dst[64];
        size_t dsz = 0;
        binpack_status_t st_dec = binpack_archive_decompress_chunk(arch, 0, dst, sizeof(dst), &dsz);

        binpack_archive_close(arch);
        remove(tmp_path);

        if (count != 0 || st_dec != BINPACK_ERR_INVALID_PARAM) return TEST_FAIL;
        return TEST_PASS;
    }
    remove(tmp_path);
    return TEST_PASS;
}

static int test_C2_archive_fraudulent_chunk_count_in_footer(void) {
    /* [API-PUBLICA:ARCHIVE] Footer declara 500,000 chunks en archivo de 100 bytes.
     * idx_pos resulta negativo en la comprobación EOF. El motor debe abortar la búsqueda
     * de tabla de índices y caer al escaneo secuencial encontrando 0 chunks.
     */
    const char *tmp_path = "test_adv_c2.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 500000;
    ftr.total_uncompressed = 1000000;
    fwrite(&ftr, 1, sizeof(ftr), f);
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (arch) {
        uint32_t c = binpack_archive_get_chunk_count(arch);
        binpack_archive_close(arch);
        remove(tmp_path);
        /* Debe tener 0 chunks */
        if (c != 0) return TEST_FAIL;
        return TEST_PASS;
    }
    remove(tmp_path);
    return TEST_PASS;
}

static int test_C3_archive_invalid_chunk_idx(void) {
    /* [API-PUBLICA:ARCHIVE] Solicitud de chunk fuera de rango en archive válido */
    const char *tmp_path = "test_adv_c3.bpk";
    uint8_t sample_data[256];
    memset(sample_data, 0xAB, sizeof(sample_data));

    void *cmp_data = NULL;
    size_t cmp_sz = 0;
    binpack_status_t st = binpack_compress_buffer(sample_data, sizeof(sample_data), &cmp_data, &cmp_sz, NULL);
    if (st != BINPACK_OK || !cmp_data) return TEST_FAIL;

    FILE *f = fopen(tmp_path, "wb");
    if (!f) { binpack_free(cmp_data); return TEST_FAIL; }
    fwrite(cmp_data, 1, cmp_sz, f);
    fclose(f);
    binpack_free(cmp_data);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) { remove(tmp_path); return TEST_FAIL; }

    uint8_t dst[256];
    size_t decomp_sz = 0;
    binpack_status_t st_oob = binpack_archive_decompress_chunk(arch, 9999, dst, sizeof(dst), &decomp_sz);

    binpack_archive_close(arch);
    remove(tmp_path);

    return (st_oob == BINPACK_ERR_INVALID_PARAM) ? TEST_PASS : TEST_FAIL;
}

static int test_C4_archive_payload_offset_out_of_file(void) {
    /* [API-PUBLICA:ARCHIVE] file_offset fuera de archivo al descomprimir */
    const char *tmp_path = "test_adv_c4.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = 100;
    chdr.compressed_sz = 50;
    fwrite(&chdr, 1, sizeof(chdr), f);

    uint8_t dummy[10] = { 0 };
    fwrite(dummy, 1, sizeof(dummy), f);
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) { remove(tmp_path); return TEST_PASS; }

    uint8_t dst[200];
    size_t dsz = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 0, dst, sizeof(dst), &dsz);
    binpack_archive_close(arch);
    remove(tmp_path);

    return (st != BINPACK_OK) ? TEST_PASS : TEST_FAIL;
}

static int test_C5_rolling_prefix_recursion_validation(void) {
    /* [RECURSION-SISTEMA] V-08: Validación funcional de descompresión con prefijos rodantes reales
     * Comprueba la corrección funcional de la cadena recursiva con ZSTD_c_refPrefix.
     * (Nota: Esta prueba valida la funcionalidad de la recursión; no realiza telemetría de consumo físico)
     */
    const int CHAIN_CHUNKS = 3;
    const size_t CHUNK_SZ = 1024;
    const char *tmp_path = "test_adv_c5_prefix_real.bpk";

    uint8_t raw0[1024], raw1[1024], raw2[1024];
    memset(raw0, 0x11, sizeof(raw0));
    memset(raw1, 0x22, sizeof(raw1));
    memset(raw2, 0x33, sizeof(raw2));

    /* Comprimir chunk 0 sin prefijo */
    uint8_t cmp0[1024 + 128];
    size_t cmp0_sz = ZSTD_compress(cmp0, sizeof(cmp0), raw0, sizeof(raw0), 1);
    assert(!ZSTD_isError(cmp0_sz));

    /* Comprimir chunk 1 con prefijo raw0 */
    ZSTD_CCtx *cctx = ZSTD_createCCtx();
    assert(cctx != NULL);
    ZSTD_CCtx_refPrefix(cctx, raw0, sizeof(raw0));
    uint8_t cmp1[1024 + 128];
    ZSTD_inBuffer in1 = { raw1, sizeof(raw1), 0 };
    ZSTD_outBuffer out1 = { cmp1, sizeof(cmp1), 0 };
    ZSTD_compressStream2(cctx, &out1, &in1, ZSTD_e_end);
    size_t cmp1_sz = out1.pos;

    /* Comprimir chunk 2 con prefijo raw1 */
    ZSTD_CCtx_reset(cctx, ZSTD_reset_session_only);
    ZSTD_CCtx_refPrefix(cctx, raw1, sizeof(raw1));
    uint8_t cmp2[1024 + 128];
    ZSTD_inBuffer in2 = { raw2, sizeof(raw2), 0 };
    ZSTD_outBuffer out2 = { cmp2, sizeof(cmp2), 0 };
    ZSTD_compressStream2(cctx, &out2, &in2, ZSTD_e_end);
    size_t cmp2_sz = out2.pos;
    ZSTD_freeCCtx(cctx);

    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    binpack_chunk_header_t ch0;
    memset(&ch0, 0, sizeof(ch0));
    ch0.chunk_magic = BINPACK_MAGIC_CHUNK;
    ch0.uncompressed_sz = (uint32_t)CHUNK_SZ;
    ch0.compressed_sz = (uint32_t)cmp0_sz;
    ch0.chunk_crc32 = binpack_crc32(0, raw0, sizeof(raw0));
    fwrite(&ch0, 1, sizeof(ch0), f);
    fwrite(cmp0, 1, cmp0_sz, f);

    binpack_chunk_header_t ch1;
    memset(&ch1, 0, sizeof(ch1));
    ch1.chunk_magic = BINPACK_MAGIC_CHUNK;
    ch1.uncompressed_sz = (uint32_t)CHUNK_SZ;
    ch1.compressed_sz = (uint32_t)cmp1_sz;
    ch1.filter_flags = BP_CHUNK_FLAG_PREFIX;
    ch1.chunk_crc32 = binpack_crc32(0, raw1, sizeof(raw1));
    fwrite(&ch1, 1, sizeof(ch1), f);
    fwrite(cmp1, 1, cmp1_sz, f);

    binpack_chunk_header_t ch2;
    memset(&ch2, 0, sizeof(ch2));
    ch2.chunk_magic = BINPACK_MAGIC_CHUNK;
    ch2.uncompressed_sz = (uint32_t)CHUNK_SZ;
    ch2.compressed_sz = (uint32_t)cmp2_sz;
    ch2.filter_flags = BP_CHUNK_FLAG_PREFIX;
    ch2.chunk_crc32 = binpack_crc32(0, raw2, sizeof(raw2));
    fwrite(&ch2, 1, sizeof(ch2), f);
    fwrite(cmp2, 1, cmp2_sz, f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = CHAIN_CHUNKS;
    ftr.total_uncompressed = CHAIN_CHUNKS * CHUNK_SZ;
    fwrite(&ftr, 1, sizeof(ftr), f);
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) { remove(tmp_path); return TEST_FAIL; }

    uint8_t dst2[1024];
    size_t dsz2 = 0;
    binpack_status_t st = binpack_archive_decompress_chunk(arch, 2, dst2, sizeof(dst2), &dsz2);

    binpack_archive_close(arch);
    remove(tmp_path);

    if (st != BINPACK_OK) {
        printf("    -> Fallo: la descompresion recursiva de prefijo fallo con st=%d\n", st);
        return TEST_FAIL;
    }
    if (dsz2 != sizeof(raw2) || memcmp(dst2, raw2, sizeof(raw2)) != 0) {
        printf("    -> Fallo: los datos descomprimidos no coinciden con raw2\n");
        return TEST_FAIL;
    }

    printf("    -> Descompresion recursiva funcional verificada con exito (st=0, memcmp coincidente)\n");
    return TEST_PASS;
}

static int test_C6_archive_v10_truncated_sequential_payload(void) {
    /* [API-PUBLICA:ARCHIVE] V-10: Inconsistencia estructural en binpack_archive_open()
     * Comprueba que binpack_archive_open() NO acepte un chunk cuyo payload comprimido
     * este truncado respecto al tamaño declarado en disco. El chunk incompleto
     * no debe ser registrado en arch->chunks ni computado en chunk_count.
     */
    const char *tmp_path = "test_adv_c6_v10.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    binpack_chunk_header_t chdr;
    memset(&chdr, 0, sizeof(chdr));
    chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr.uncompressed_sz = 100;
    chdr.compressed_sz = 1000; /* Declara 1000 bytes en disco */
    fwrite(&chdr, 1, sizeof(chdr), f);

    /* Solo escribimos 10 bytes de payload en vez de 1000 */
    uint8_t dummy[10] = { 0 };
    fwrite(dummy, 1, sizeof(dummy), f);
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_PASS;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);

    /* Si el archivo se abre, no debe indexar ningun chunk incompleto */
    uint8_t dst[200];
    size_t dsz = 0;
    binpack_status_t st_dec = binpack_archive_decompress_chunk(arch, 0, dst, sizeof(dst), &dsz);

    binpack_archive_close(arch);
    remove(tmp_path);

    /* V-10 resuelto: el chunk truncado provoca rechazo total del archivo (fail-closed) */
    if (arch == NULL || count == 0) {
        return TEST_PASS;
    }
    return TEST_FAIL;
}

static int test_C7_archive_empty_file(void) {
    /* [API-PUBLICA:ARCHIVE] C7: Archivo vacío (0 bytes) -> NULL */
    const char *tmp_path = "test_adv_c7_empty.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    remove(tmp_path);

    return (arch == NULL) ? TEST_PASS : TEST_FAIL;
}

static int test_C8_archive_smaller_than_header(void) {
    /* [API-PUBLICA:ARCHIVE] C8: Archivo menor que cabecera V2 mínima -> NULL */
    const char *tmp_path = "test_adv_c8_short.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;
    uint8_t short_data[10] = { 'B', 'P', 'K', '2', 2, 2, 0, 0, 0, 0 };
    fwrite(short_data, 1, sizeof(short_data), f);
    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    remove(tmp_path);

    return (arch == NULL) ? TEST_PASS : TEST_FAIL;
}

static int test_C9_archive_nonexistent_file(void) {
    /* [API-PUBLICA:ARCHIVE] C9: Archivo inexistente -> NULL */
    binpack_archive_t *arch = binpack_archive_open("non_existent_file_c9.bpk");
    return (arch == NULL) ? TEST_PASS : TEST_FAIL;
}

static int test_C10_archive_first_valid_second_truncated(void) {
    /* [API-PUBLICA:ARCHIVE] C10: Primer chunk válido, segundo truncado -> NULL (fail-closed) */
    const char *tmp_path = "test_adv_c10_trunc2.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0: Válido */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 20;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[20] = { 0xAA };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1: Truncado (declara 500 bytes de payload, solo escribe 5) */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 500;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[5] = { 0xBB };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    remove(tmp_path);

    /* Fail-closed: No debe devolver un archivo parcialmente válido */
    return (arch == NULL) ? TEST_PASS : TEST_FAIL;
}

static int test_C11_archive_multiple_valid_last_truncated(void) {
    /* [API-PUBLICA:ARCHIVE] C11: Varios chunks válidos, último truncado -> NULL (fail-closed) */
    const char *tmp_path = "test_adv_c11_trunc_last.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunks 0, 1, 2: Válidos */
    for (int i = 0; i < 3; i++) {
        binpack_chunk_header_t chdr;
        memset(&chdr, 0, sizeof(chdr));
        chdr.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr.uncompressed_sz = 100;
        chdr.compressed_sz = 30;
        fwrite(&chdr, 1, sizeof(chdr), f);
        uint8_t dummy[30];
        memset(dummy, (uint8_t)(i + 1), sizeof(dummy));
        fwrite(dummy, 1, sizeof(dummy), f);
    }

    /* Chunk 3: Truncado (declara 300 bytes, solo escribe 10) */
    binpack_chunk_header_t chdr3;
    memset(&chdr3, 0, sizeof(chdr3));
    chdr3.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr3.uncompressed_sz = 100;
    chdr3.compressed_sz = 300;
    fwrite(&chdr3, 1, sizeof(chdr3), f);
    uint8_t dummy3[10] = { 0xFF };
    fwrite(dummy3, 1, sizeof(dummy3), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    remove(tmp_path);

    /* Fail-closed: Debe rechazar el archivo completamente */
    return (arch == NULL) ? TEST_PASS : TEST_FAIL;
}

static int test_C12_archive_footer_disordered_offsets(void) {
    /* [API-PUBLICA:ARCHIVE] C12: Footer con tabla de índices con offsets desordenados
     * Debe rechazar el índice Seek Table y recuperarse exitosamente mediante fallback secuencial.
     */
    const char *tmp_path = "test_adv_c12_disordered.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x11 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x22 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table maliciosa con offsets desordenados (chunk 0 apunta a 500, chunk 1 a 48) */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t bad_tbl[2];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 0;
    bad_tbl[0].file_offset = 500; /* Desordenado: offset posterior */
    bad_tbl[0].uncompressed_sz = 100;
    bad_tbl[0].compressed_sz = 30;

    bad_tbl[1].chunk_index = 1;
    bad_tbl[1].file_offset = 48; /* Anterior */
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 30;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    /* Footer */
    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 2) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo0, cinfo1;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo0) != BINPACK_OK ||
        binpack_archive_get_chunk_info(arch, 1, &cinfo1) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint64_t real_off0 = sizeof(hdr) + sizeof(chdr0);
    uint64_t real_off1 = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);

    int ok = (cinfo0.file_offset == real_off0 && cinfo0.file_offset != 500 &&
              cinfo1.file_offset == real_off1 && cinfo1.file_offset != 48 &&
              cinfo0.uncompressed_sz == 100 && cinfo1.uncompressed_sz == 100 &&
              cinfo0.compressed_sz == 30 && cinfo1.compressed_sz == 30);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}



static int test_C13_archive_footer_overlapping_chunks(void) {
    /* [API-PUBLICA:ARCHIVE] C13: Footer con chunks solapados en el índice
     * Debe rechazar el índice Seek Table y recuperarse exitosamente mediante fallback secuencial.
     */
    const char *tmp_path = "test_adv_c13_overlap.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 en disco */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 40;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[40] = { 0x33 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 en disco */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 40;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[40] = { 0x44 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table maliciosa: chunk 1 solapado dentro de chunk 0 */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t bad_tbl[2];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 0;
    bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
    bad_tbl[0].uncompressed_sz = 100;
    bad_tbl[0].compressed_sz = 40;

    bad_tbl[1].chunk_index = 1;
    bad_tbl[1].file_offset = bad_tbl[0].file_offset + 10; /* Solapado */
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 40;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 2) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo0, cinfo1;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo0) != BINPACK_OK ||
        binpack_archive_get_chunk_info(arch, 1, &cinfo1) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint64_t real_off0 = sizeof(hdr) + sizeof(chdr0);
    uint64_t real_off1 = sizeof(hdr) + sizeof(chdr0) + 40 + sizeof(chdr1);

    int ok = (cinfo0.file_offset == real_off0 &&
              cinfo1.file_offset == real_off1 &&
              cinfo1.file_offset != bad_tbl[1].file_offset &&
              cinfo0.compressed_sz == 40 && cinfo1.compressed_sz == 40);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}



static int test_C14_archive_footer_mismatch_with_chnk_header(void) {
    /* [API-PUBLICA:ARCHIVE] C14: Metadatos del índice no coinciden con cabecera CHNK en disco
     * Debe rechazar el índice Seek Table y recuperarse exitosamente mediante fallback secuencial.
     */
    const char *tmp_path = "test_adv_c14_mismatch.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 real */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    chdr0.chunk_crc32 = 0x12345678;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x55 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 real */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    chdr1.chunk_crc32 = 0x87654321;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x66 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table maliciosa: declara chunk 0 con uncompressed_sz falso de 999999 */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t bad_tbl[2];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 0;
    bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
    bad_tbl[0].uncompressed_sz = 999999; /* Falso */
    bad_tbl[0].compressed_sz = 30;
    bad_tbl[0].chunk_crc32 = 0x12345678;

    bad_tbl[1].chunk_index = 1;
    bad_tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 30;
    bad_tbl[1].chunk_crc32 = 0x87654321;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 2) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo0, cinfo1;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo0) != BINPACK_OK ||
        binpack_archive_get_chunk_info(arch, 1, &cinfo1) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    int ok = (cinfo0.uncompressed_sz == 100 && cinfo0.chunk_crc32 == 0x12345678 &&
              cinfo1.uncompressed_sz == 100 && cinfo1.chunk_crc32 == 0x87654321);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}



static int test_C15_archive_footer_filter_flags_mismatch(void) {
    /* [API-PUBLICA:ARCHIVE] C15: Metadatos de filtro y flags en índice no coinciden con cabecera CHNK
     * Debe verificar ambos casos:
     *   1) Mismatch de filter_flags (en disco 0, en índice BP_CHUNK_FLAG_BOUNDS)
     *   2) Mismatch de filter_id (en disco NONE, en índice ARM32)
     * Ambos deben rechazar el índice Seek Table y recuperarse exitosamente vía fallback.
     */
    /* Caso 1: filter_flags mismatch */
    {
        const char *tmp_path = "test_adv_c15_flags.bpk";
        FILE *f = fopen(tmp_path, "wb");
        if (!f) return TEST_FAIL;

        binpack_v2_header_t hdr;
        init_default_v2_header(&hdr);
        fwrite(&hdr, 1, sizeof(hdr), f);

        binpack_chunk_header_t chdr0;
        memset(&chdr0, 0, sizeof(chdr0));
        chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr0.uncompressed_sz = 100;
        chdr0.compressed_sz = 30;
        chdr0.filter_id = BINPACK_FILTER_NONE;
        chdr0.filter_flags = 0; /* En disco: 0 */
        fwrite(&chdr0, 1, sizeof(chdr0), f);
        uint8_t dummy0[30] = { 0x55 };
        fwrite(dummy0, 1, sizeof(dummy0), f);

        binpack_chunk_header_t chdr1;
        memset(&chdr1, 0, sizeof(chdr1));
        chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr1.uncompressed_sz = 100;
        chdr1.compressed_sz = 30;
        fwrite(&chdr1, 1, sizeof(chdr1), f);
        uint8_t dummy1[30] = { 0x66 };
        fwrite(dummy1, 1, sizeof(dummy1), f);

        binpack_index_header_t idx_hdr;
        idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
        idx_hdr.entry_count = 2;
        fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

        binpack_chunk_info_t bad_tbl[2];
        memset(bad_tbl, 0, sizeof(bad_tbl));
        bad_tbl[0].chunk_index = 0;
        bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
        bad_tbl[0].uncompressed_sz = 100;
        bad_tbl[0].compressed_sz = 30;
        bad_tbl[0].filter_flags = BP_CHUNK_FLAG_BOUNDS; /* Mismatch */

        bad_tbl[1].chunk_index = 1;
        bad_tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);
        bad_tbl[1].uncompressed_sz = 100;
        bad_tbl[1].compressed_sz = 30;
        fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

        binpack_v2_footer_t ftr;
        memset(&ftr, 0, sizeof(ftr));
        ftr.footer_magic = BINPACK_MAGIC_FOOTER;
        ftr.chunk_count = 2;
        ftr.total_uncompressed = 200;
        fwrite(&ftr, 1, sizeof(ftr), f);
        fclose(f);

        binpack_archive_t *arch = binpack_archive_open(tmp_path);
        if (!arch) {
            remove(tmp_path);
            return TEST_FAIL;
        }
        if (binpack_archive_get_chunk_count(arch) != 2) {
            binpack_archive_close(arch);
            remove(tmp_path);
            return TEST_FAIL;
        }
        binpack_chunk_info_t cinfo;
        if (binpack_archive_get_chunk_info(arch, 0, &cinfo) != BINPACK_OK || cinfo.filter_flags != 0) {
            binpack_archive_close(arch);
            remove(tmp_path);
            return TEST_FAIL;
        }
        binpack_archive_close(arch);
        remove(tmp_path);
    }

    /* Caso 2: filter_id mismatch */
    {
        const char *tmp_path = "test_adv_c15_fid.bpk";
        FILE *f = fopen(tmp_path, "wb");
        if (!f) return TEST_FAIL;

        binpack_v2_header_t hdr;
        init_default_v2_header(&hdr);
        fwrite(&hdr, 1, sizeof(hdr), f);

        binpack_chunk_header_t chdr0;
        memset(&chdr0, 0, sizeof(chdr0));
        chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr0.uncompressed_sz = 100;
        chdr0.compressed_sz = 30;
        chdr0.filter_id = BINPACK_FILTER_NONE;
        chdr0.filter_flags = 0;
        fwrite(&chdr0, 1, sizeof(chdr0), f);
        uint8_t dummy0[30] = { 0x55 };
        fwrite(dummy0, 1, sizeof(dummy0), f);

        binpack_chunk_header_t chdr1;
        memset(&chdr1, 0, sizeof(chdr1));
        chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
        chdr1.uncompressed_sz = 100;
        chdr1.compressed_sz = 30;
        fwrite(&chdr1, 1, sizeof(chdr1), f);
        uint8_t dummy1[30] = { 0x66 };
        fwrite(dummy1, 1, sizeof(dummy1), f);

        binpack_index_header_t idx_hdr;
        idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
        idx_hdr.entry_count = 2;
        fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

        binpack_chunk_info_t bad_tbl[2];
        memset(bad_tbl, 0, sizeof(bad_tbl));
        bad_tbl[0].chunk_index = 0;
        bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
        bad_tbl[0].uncompressed_sz = 100;
        bad_tbl[0].compressed_sz = 30;
        bad_tbl[0].filter_id = BINPACK_FILTER_ARM32; /* Mismatch */

        bad_tbl[1].chunk_index = 1;
        bad_tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);
        bad_tbl[1].uncompressed_sz = 100;
        bad_tbl[1].compressed_sz = 30;
        fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

        binpack_v2_footer_t ftr;
        memset(&ftr, 0, sizeof(ftr));
        ftr.footer_magic = BINPACK_MAGIC_FOOTER;
        ftr.chunk_count = 2;
        ftr.total_uncompressed = 200;
        fwrite(&ftr, 1, sizeof(ftr), f);
        fclose(f);

        binpack_archive_t *arch = binpack_archive_open(tmp_path);
        if (!arch) {
            remove(tmp_path);
            return TEST_FAIL;
        }
        if (binpack_archive_get_chunk_count(arch) != 2) {
            binpack_archive_close(arch);
            remove(tmp_path);
            return TEST_FAIL;
        }
        binpack_chunk_info_t cinfo;
        if (binpack_archive_get_chunk_info(arch, 0, &cinfo) != BINPACK_OK || cinfo.filter_id != BINPACK_FILTER_NONE) {
            binpack_archive_close(arch);
            remove(tmp_path);
            return TEST_FAIL;
        }
        binpack_archive_close(arch);
        remove(tmp_path);
    }

    return TEST_PASS;
}

static int test_C16_archive_footer_physical_bounds_mismatch(void) {
    /* [API-PUBLICA:ARCHIVE] C16: Bounds físicos en archivo difieren de los declarados en el índice
     * Debe rechazar el índice Seek Table y recuperarse exitosamente mediante fallback secuencial.
     */
    const char *tmp_path = "test_adv_c16_bounds_mismatch.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 real con flag BOUNDS activo y bounds: off=10, len=80 */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    chdr0.filter_flags = BP_CHUNK_FLAG_BOUNDS;
    fwrite(&chdr0, 1, sizeof(chdr0), f);

    binpack_chunk_bounds_t bounds0;
    bounds0.code_offset = 10;
    bounds0.code_length = 80;
    fwrite(&bounds0, 1, sizeof(bounds0), f);

    uint8_t dummy0[30] = { 0x55 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 real */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x66 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table maliciosa: declara chunk 0 con bounds alterados: off=30, len=50 */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t bad_tbl[2];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 0;
    bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0) + sizeof(bounds0);
    bad_tbl[0].uncompressed_sz = 100;
    bad_tbl[0].compressed_sz = 30;
    bad_tbl[0].filter_flags = BP_CHUNK_FLAG_BOUNDS;
    bad_tbl[0].code_offset = 30; /* Falso: en disco es 10 */
    bad_tbl[0].code_length = 50; /* Falso: en disco es 80 */

    bad_tbl[1].chunk_index = 1;
    bad_tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + sizeof(bounds0) + 30 + sizeof(chdr1);
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 30;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 2) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    int ok = (cinfo.code_offset == 10 && cinfo.code_length == 80);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}



static int test_C17_archive_footer_chunk_index_disordered(void) {
    /* [API-PUBLICA:ARCHIVE] C17: chunk_index desordenado en las entradas del índice
     * Debe rechazar el índice Seek Table y recuperarse exitosamente mediante fallback secuencial.
     * Comprueba la recuperación correcta de todos los índices (cinfo0.chunk_index == 0, cinfo1.chunk_index == 1).
     */
    const char *tmp_path = "test_adv_c17_idx_disordered.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunks reales */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x55 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x66 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table maliciosa: chunk_index desordenado (entrada 0 tiene index=1, entrada 1 tiene index=0) */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t bad_tbl[2];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 1; /* Desordenado: debe ser 0 */
    bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
    bad_tbl[0].uncompressed_sz = 100;
    bad_tbl[0].compressed_sz = 30;

    bad_tbl[1].chunk_index = 0; /* Desordenado: debe ser 1 */
    bad_tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 30;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 2) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo0, cinfo1;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo0) != BINPACK_OK ||
        binpack_archive_get_chunk_info(arch, 1, &cinfo1) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    int ok = (cinfo0.chunk_index == 0 && cinfo1.chunk_index == 1);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}



static int test_C18_archive_trailing_bytes_after_footer(void) {
    /* [API-PUBLICA:ARCHIVE] C18: Bytes adicionales de basura tras el footer
     * Fail-closed: Tanto la ruta Seek Table como el escaneo secuencial deben
     * exigir footer_end == file_sz, rechazando tajantemente el contenedor corrupto.
     */
    const char *tmp_path = "test_adv_c18_trailing_garbage.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x11 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x22 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t tbl[2];
    memset(tbl, 0, sizeof(tbl));
    tbl[0].chunk_index = 0;
    tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
    tbl[0].uncompressed_sz = 100;
    tbl[0].compressed_sz = 30;

    tbl[1].chunk_index = 1;
    tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);
    tbl[1].uncompressed_sz = 100;
    tbl[1].compressed_sz = 30;
    fwrite(tbl, 1, sizeof(tbl), f);

    /* Footer */
    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    /* 16 bytes de datos espurios tras el footer */
    uint8_t trailing_garbage[16] = { 0xDE, 0xAD, 0xBE, 0xEF, 0x01, 0x02, 0x03, 0x04,
                                     0x55, 0x66, 0x77, 0x88, 0xAA, 0xBB, 0xCC, 0xDD };
    fwrite(trailing_garbage, 1, sizeof(trailing_garbage), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    remove(tmp_path);

    /* Debe fallar cerrando el contenedor y devolviendo NULL */
    if (arch != NULL) {
        binpack_archive_close(arch);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_C19_archive_seek_table_gap_unindexed_chunk(void) {
    /* [API-PUBLICA:ARCHIVE] C19: Seek table con gap / chunk no registrado
     * 3 chunks válidos en disco, pero el índice omite el chunk 1 y salta directamente al 2.
     * La ruta Seek Table debe detectar la falta de continuidad (chunk_hdr_offset != prev_payload_end),
     * rechazar el índice Seek Table, y el escaneo secuencial debe descubrir e indexar los 3 chunks.
     */
    const char *tmp_path = "test_adv_c19_gap.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x11 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 (en disco existe, pero la tabla de índice lo omitirá) */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x22 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Chunk 2 */
    binpack_chunk_header_t chdr2;
    memset(&chdr2, 0, sizeof(chdr2));
    chdr2.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr2.uncompressed_sz = 100;
    chdr2.compressed_sz = 30;
    fwrite(&chdr2, 1, sizeof(chdr2), f);
    uint8_t dummy2[30] = { 0x33 };
    fwrite(dummy2, 1, sizeof(dummy2), f);

    /* Seek Table maliciosa con 3 entradas pero omitiendo chunk 1 (entrada 1 salta a chunk 2) */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 3;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    uint64_t off0 = sizeof(hdr) + sizeof(chdr0);
    uint64_t off1 = off0 + 30 + sizeof(chdr1);
    uint64_t off2 = off1 + 30 + sizeof(chdr2);

    binpack_chunk_info_t bad_tbl[3];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 0;
    bad_tbl[0].file_offset = off0;
    bad_tbl[0].uncompressed_sz = 100;
    bad_tbl[0].compressed_sz = 30;

    /* Entrada 1 salta el chunk 1 y apunta a chunk 2 -> genera un gap respecto al fin de payload de chunk 0 */
    bad_tbl[1].chunk_index = 1;
    bad_tbl[1].file_offset = off2;
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 30;

    bad_tbl[2].chunk_index = 2;
    bad_tbl[2].file_offset = off2;
    bad_tbl[2].uncompressed_sz = 100;
    bad_tbl[2].compressed_sz = 30;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    /* Footer con total de 3 chunks */
    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 3;
    ftr.total_uncompressed = 300;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 3) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo0, cinfo1, cinfo2;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo0) != BINPACK_OK ||
        binpack_archive_get_chunk_info(arch, 1, &cinfo1) != BINPACK_OK ||
        binpack_archive_get_chunk_info(arch, 2, &cinfo2) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    /* Verificar que los 3 chunks fueron descubiertos secuencialmente en sus offsets físicos reales */
    int ok = (cinfo0.file_offset == off0 &&
              cinfo1.file_offset == off1 &&
              cinfo2.file_offset == off2 &&
              cinfo0.chunk_index == 0 &&
              cinfo1.chunk_index == 1 &&
              cinfo2.chunk_index == 2);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}

static int test_C20_archive_index_misplaced_between_chunks(void) {
    /* [API-PUBLICA:ARCHIVE] C20: Bloque INDEX ubicado erróneamente entre dos chunks
     * Estructura del archivo:
     *   Header V2 -> Chunk 0 -> Bloque INDEX -> Chunk 1 -> Footer
     * Regla fail-closed: El bloque INDEX solo puede ubicarse después de todos los chunks.
     * Cualquier chunk posterior al INDEX es una violación estructural y debe rechazarse con NULL.
     */
    const char *tmp_path = "test_adv_c20_misplaced_index.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x11 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Bloque INDEX anómalo / desubicado entre chunk 0 y chunk 1 */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 1;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t tbl[1];
    memset(tbl, 0, sizeof(tbl));
    tbl[0].chunk_index = 0;
    tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
    tbl[0].uncompressed_sz = 100;
    tbl[0].compressed_sz = 30;
    fwrite(tbl, 1, sizeof(tbl), f);

    /* Chunk 1 (ubicado después de INDEX, lo cual es inválido) */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x22 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Footer */
    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    remove(tmp_path);

    /* Debe fallar cerrando el contenedor y devolviendo NULL */
    if (arch != NULL) {
        binpack_archive_close(arch);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_C21_archive_footer_filter_param_mismatch(void) {
    /* [API-PUBLICA:ARCHIVE] C21: filter_param en Seek Table no coincide con cabecera física CHNK
     * Debe invalidar la tabla O(1) y recurrir con éxito al escaneo secuencial,
     * recuperando fielmente el filter_param real del archivo físico.
     */
    const char *tmp_path = "test_adv_c21_filter_param_mismatch.bpk";
    FILE *f = fopen(tmp_path, "wb");
    if (!f) return TEST_FAIL;

    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);
    fwrite(&hdr, 1, sizeof(hdr), f);

    /* Chunk 0 real: filter_param en cabecera física = 4 */
    binpack_chunk_header_t chdr0;
    memset(&chdr0, 0, sizeof(chdr0));
    chdr0.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr0.uncompressed_sz = 100;
    chdr0.compressed_sz = 30;
    chdr0.filter_id = BINPACK_FILTER_ARM32;
    chdr0.filter_flags = 0;
    chdr0.filter_param = 4;
    fwrite(&chdr0, 1, sizeof(chdr0), f);
    uint8_t dummy0[30] = { 0x55 };
    fwrite(dummy0, 1, sizeof(dummy0), f);

    /* Chunk 1 real */
    binpack_chunk_header_t chdr1;
    memset(&chdr1, 0, sizeof(chdr1));
    chdr1.chunk_magic = BINPACK_MAGIC_CHUNK;
    chdr1.uncompressed_sz = 100;
    chdr1.compressed_sz = 30;
    chdr1.filter_param = 0;
    fwrite(&chdr1, 1, sizeof(chdr1), f);
    uint8_t dummy1[30] = { 0x66 };
    fwrite(dummy1, 1, sizeof(dummy1), f);

    /* Seek Table maliciosa: declara chunk 0 con filter_param = 8 (mismatch con físico 4) */
    binpack_index_header_t idx_hdr;
    idx_hdr.index_magic = BINPACK_MAGIC_INDEX;
    idx_hdr.entry_count = 2;
    fwrite(&idx_hdr, 1, sizeof(idx_hdr), f);

    binpack_chunk_info_t bad_tbl[2];
    memset(bad_tbl, 0, sizeof(bad_tbl));
    bad_tbl[0].chunk_index = 0;
    bad_tbl[0].file_offset = sizeof(hdr) + sizeof(chdr0);
    bad_tbl[0].uncompressed_sz = 100;
    bad_tbl[0].compressed_sz = 30;
    bad_tbl[0].filter_id = BINPACK_FILTER_ARM32;
    bad_tbl[0].filter_param = 8; /* Mismatch con el valor 4 en cabecera física */

    bad_tbl[1].chunk_index = 1;
    bad_tbl[1].file_offset = sizeof(hdr) + sizeof(chdr0) + 30 + sizeof(chdr1);
    bad_tbl[1].uncompressed_sz = 100;
    bad_tbl[1].compressed_sz = 30;
    bad_tbl[1].filter_param = 0;
    fwrite(bad_tbl, 1, sizeof(bad_tbl), f);

    binpack_v2_footer_t ftr;
    memset(&ftr, 0, sizeof(ftr));
    ftr.footer_magic = BINPACK_MAGIC_FOOTER;
    ftr.chunk_count = 2;
    ftr.total_uncompressed = 200;
    fwrite(&ftr, 1, sizeof(ftr), f);

    fclose(f);

    binpack_archive_t *arch = binpack_archive_open(tmp_path);
    if (!arch) {
        remove(tmp_path);
        return TEST_FAIL;
    }

    uint32_t count = binpack_archive_get_chunk_count(arch);
    if (count != 2) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    binpack_chunk_info_t cinfo;
    if (binpack_archive_get_chunk_info(arch, 0, &cinfo) != BINPACK_OK) {
        binpack_archive_close(arch);
        remove(tmp_path);
        return TEST_FAIL;
    }

    /* Debe haber descartado el índice O(1) con param=8 y leído el param=4 real del CHNK */
    int ok = (cinfo.filter_param == 4);

    binpack_archive_close(arch);
    remove(tmp_path);

    return ok ? TEST_PASS : TEST_FAIL;
}



/* ========================================================================= */
/* GRUPO D: Integridad, Atomicidad y Entradas Corruptas                      */
/* ========================================================================= */

static int test_D1_corrupt_stream_state_consistency(void) {
    /* [INTEGRIDAD-ATOMICIDAD] Comprobar consistencia tras errores:
     * Si la descompresión falla, *dst debe quedar NULL y *dst_sz en 0,
     * sin dejar punteros colgantes ni memoria asignada no liberada.
     */
    binpack_v2_header_t hdr;
    init_default_v2_header(&hdr);

    uint8_t corrupt_payload[16];
    memset(corrupt_payload, 0xFF, sizeof(corrupt_payload));

    uint8_t stream[sizeof(hdr) + sizeof(corrupt_payload)];
    memcpy(stream, &hdr, sizeof(hdr));
    memcpy(stream + sizeof(hdr), corrupt_payload, sizeof(corrupt_payload));

    void *out = (void*)0xDEADBEEF; /* Centinela */
    size_t out_sz = 12345;

    binpack_status_t st = binpack_decompress_buffer(stream, sizeof(stream), &out, &out_sz);

    if (st == BINPACK_OK) {
        if (out) free(out);
        return TEST_FAIL;
    }

    /* En fallo, la API debe garantizar *dst == NULL y *dst_sz == 0 */
    if (out != NULL || out_sz != 0) {
        printf("    -> Fallo de atomicidad: out=%p, out_sz=%zu tras error %d\n", out, out_sz, st);
        return TEST_FAIL;
    }
    return TEST_PASS;
}

static int test_D2_deterministic_corrupt_inputs_resilience(void) {
    /* [INTEGRIDAD-ATOMICIDAD] Batería determinista de 100 entradas corruptas con magic BPK2:
     * Comprobar que en ninguna de las iteraciones ocurre caída abrupta ni violación de atomicidad.
     */
    uint8_t bad_data[256];
    for (int iter = 0; iter < 100; iter++) {
        for (size_t i = 0; i < sizeof(bad_data); i++) {
            bad_data[i] = (uint8_t)((iter * 41 + i * 17) & 0xFF);
        }
        bad_data[0] = 'B'; bad_data[1] = 'P'; bad_data[2] = 'K'; bad_data[3] = '2';

        void *out = NULL;
        size_t out_sz = 0;
        binpack_status_t st = binpack_decompress_buffer(bad_data, sizeof(bad_data), &out, &out_sz);
        if (st == BINPACK_OK) {
            if (out) free(out);
        } else {
            if (out != NULL || out_sz != 0) {
                return TEST_FAIL;
            }
        }
    }
    return TEST_PASS;
}

int main(void) {
    printf("======================================================================\n");
    printf("BP-004 Adversarial Test Suite: Memory Bounds & Structural Safety (v2.1)\n");
    printf("======================================================================\n");

    printf("\n[GRUPO A] Modelos Matematicos Aislados de Aritmetica:\n");
    RUN_TEST(test_A1_addition_overflow_wrap_around);
    RUN_TEST(test_A2_addition_overflow_offset);
    RUN_TEST(test_A3_geometric_capacity_overflow);
    RUN_TEST(test_A4_fixed_buffer_overflow_wrap);
    RUN_TEST(test_A5_zero_size_write_contract);

    printf("\n[GRUPO B] Pruebas de API Publica (Buffer vs Streaming Duplex):\n");
    RUN_TEST(test_B0_valid_stream_roundtrip);
    RUN_TEST(test_B1_uncompressed_sz_zero);
    RUN_TEST(test_B2_compressed_sz_zero_discrepancy);
    RUN_TEST(test_B3_uncompressed_sz_exceeds_max);
    RUN_TEST(test_B4_compressed_sz_exceeds_max);
    RUN_TEST(test_B5_truncated_stream_header);
    RUN_TEST(test_B6_truncated_chunk_header);
    RUN_TEST(test_B7_truncated_chunk_payload);
    RUN_TEST(test_B8_bounds_offset_oob);
    RUN_TEST(test_B9_bounds_length_overflow);
    RUN_TEST(test_B10_compress_stream_reader_exceeds_chunk_sz);
    RUN_TEST(test_B11_decompress_stream_incomplete_and_no_progress);
    RUN_TEST(test_B12_decompress_stream_concatenated_frames_and_trailing_chunk_bytes);
    RUN_TEST(test_B13_decompress_stream_footer_trailing_bytes_and_missing_footer);
    RUN_TEST(test_B14_decompress_stream_v1_legacy_bounds_three_cases);
    RUN_TEST(test_B15_v1_historical_fixture_compatibility);
    RUN_TEST(test_B16_stream_arithmetic_safe_add_helper_boundary_checks);

    printf("\n[GRUPO C] Pruebas de API Publica (Archive Engine & Recursion):\n");
    RUN_TEST(test_C1_archive_truncated_footer);
    RUN_TEST(test_C2_archive_fraudulent_chunk_count_in_footer);
    RUN_TEST(test_C3_archive_invalid_chunk_idx);
    RUN_TEST(test_C4_archive_payload_offset_out_of_file);
    RUN_TEST(test_C5_rolling_prefix_recursion_validation);
    RUN_TEST(test_C6_archive_v10_truncated_sequential_payload);
    RUN_TEST(test_C7_archive_empty_file);
    RUN_TEST(test_C8_archive_smaller_than_header);
    RUN_TEST(test_C9_archive_nonexistent_file);
    RUN_TEST(test_C10_archive_first_valid_second_truncated);
    RUN_TEST(test_C11_archive_multiple_valid_last_truncated);
    RUN_TEST(test_C12_archive_footer_disordered_offsets);
    RUN_TEST(test_C13_archive_footer_overlapping_chunks);
    RUN_TEST(test_C14_archive_footer_mismatch_with_chnk_header);
    RUN_TEST(test_C15_archive_footer_filter_flags_mismatch);
    RUN_TEST(test_C16_archive_footer_physical_bounds_mismatch);
    RUN_TEST(test_C17_archive_footer_chunk_index_disordered);
    RUN_TEST(test_C18_archive_trailing_bytes_after_footer);
    RUN_TEST(test_C19_archive_seek_table_gap_unindexed_chunk);
    RUN_TEST(test_C20_archive_index_misplaced_between_chunks);
    RUN_TEST(test_C21_archive_footer_filter_param_mismatch);

    printf("\n[GRUPO D] Integridad, Atomicidad y Entradas Corruptas:\n");
    RUN_TEST(test_D1_corrupt_stream_state_consistency);
    RUN_TEST(test_D2_deterministic_corrupt_inputs_resilience);

    printf("\n======================================================================\n");
    printf("Resultados: %d/%d pruebas ejecutadas con exito (%d fallos)\n", g_tests_passed, g_tests_run, g_tests_failed);
    printf("======================================================================\n");

    return (g_tests_failed == 0) ? 0 : 1;
}
