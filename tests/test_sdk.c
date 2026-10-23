/**
 * @file test_sdk.c
 * @brief BinPack Enterprise SDK - Automated Regression & Verification Test Suite
 */

#include "../include/binpack_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

static void test_version(void) {
    printf("[TEST] Checking version & error descriptions... ");
    const char *ver = binpack_version_string();
    assert(ver != NULL);
    assert(strcmp(ver, "2.2.0") == 0);
    assert(binpack_error_string(BINPACK_OK) != NULL);
    assert(binpack_error_string(BINPACK_ERR_CHECKSUM_MISMATCH) != NULL);
    printf("PASS\n");
}

static void test_buffer_roundtrip(void) {
    printf("[TEST] Checking in-memory buffer compression & decompression... ");
    const size_t sz = 256 * 1024; /* 256 KB */
    uint8_t *orig = (uint8_t*)malloc(sz);
    assert(orig != NULL);

    /* Fill with structured synthetic data */
    for (size_t i = 0; i < sz; i++) {
        orig[i] = (uint8_t)((i % 256) ^ ((i / 4) % 128));
    }

    void *compressed = NULL;
    size_t comp_sz = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.clevel = 5; /* fast for unit test */

    binpack_status_t st_c = binpack_compress_buffer(orig, sz, &compressed, &comp_sz, &opts);
    assert(st_c == BINPACK_OK);
    assert(compressed != NULL);
    assert(comp_sz > 0 && comp_sz < sz);

    void *restored = NULL;
    size_t rest_sz = 0;
    binpack_status_t st_d = binpack_decompress_buffer(compressed, comp_sz, &restored, &rest_sz);
    assert(st_d == BINPACK_OK);
    assert(rest_sz == sz);
    assert(memcmp(orig, restored, sz) == 0);

    binpack_free(compressed);
    binpack_free(restored);
    free(orig);
    printf("PASS\n");
}

static void test_micro_trial_guard(void) {
    printf("[TEST] Checking Micro-Trial Zero-Regression Guard on pseudo-random noise... ");
    const size_t sz = 32 * 1024;
    uint8_t *noise = (uint8_t*)malloc(sz);
    assert(noise != NULL);

    /* Generate pseudo-random noise with high entropy */
    uint32_t seed = 0x12345678;
    for (size_t i = 0; i < sz; i++) {
        seed = seed * 1664525 + 1013904223;
        noise[i] = (uint8_t)(seed >> 16);
    }

    /* On pure noise, x86 filter must be rejected by micro-trial */
    int gain_x86 = binpack_evaluate_filter_gain(noise, sz, BINPACK_FILTER_X86_64, 0);
    assert(gain_x86 == 0); /* Zero regression guard correctly rejects filter */

    free(noise);
    printf("PASS\n");
}

static void test_streaming_file_roundtrip(void) {
    printf("[TEST] Checking Streaming Bounded Chunk File Engine... ");
    const char *test_in = "test_stream_input.tmp";
    const char *test_bpk = "test_stream_archive.tmp.bpk2";
    const char *test_out = "test_stream_restored.tmp";

    /* Create 2.5 MB test file */
    const size_t sz = 2500000;
    uint8_t *buf = (uint8_t*)malloc(sz);
    assert(buf != NULL);
    for (size_t i = 0; i < sz; i++) {
        buf[i] = (uint8_t)((i * 7) ^ (i / 13));
    }

    FILE *f = fopen(test_in, "wb");
    assert(f != NULL);
    fwrite(buf, 1, sz, f);
    fclose(f);

    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.chunk_size = 512 * 1024; /* 512 KB chunk to force multiple streaming chunks */
    opts.clevel = 3;

    binpack_status_t st_c = binpack_compress_file(test_in, test_bpk, &opts, NULL, NULL);
    assert(st_c == BINPACK_OK);

    binpack_file_info_t info;
    binpack_status_t st_q = binpack_query_file_info(test_bpk, &info);
    assert(st_q == BINPACK_OK);
    assert(info.uncompressed_size == sz);
    assert(info.chunk_count == 5); /* 2.5 MB / 512 KB = 5 chunks */

    binpack_status_t st_d = binpack_decompress_file(test_bpk, test_out, NULL, NULL);
    assert(st_d == BINPACK_OK);

    FILE *fin = fopen(test_out, "rb");
    assert(fin != NULL);
    uint8_t *rest = (uint8_t*)malloc(sz);
    assert(rest != NULL);
    size_t r = fread(rest, 1, sz, fin);
    fclose(fin);
    assert(r == sz);
    assert(memcmp(buf, rest, sz) == 0);

    free(buf);
    free(rest);
    remove(test_in);
    remove(test_bpk);
    remove(test_out);
    printf("PASS\n");
}

static void test_corrupt_data_resilience(void) {
    printf("[TEST] Checking error handling on corrupt stream headers... ");
    uint8_t corrupt[64];
    memset(corrupt, 0xAA, sizeof(corrupt));

    void *out = NULL;
    size_t out_sz = 0;
    binpack_status_t st = binpack_decompress_buffer(corrupt, sizeof(corrupt), &out, &out_sz);
    assert(st != BINPACK_OK); /* Must return error, never crash */
    assert(out == NULL);
    printf("PASS\n");
}

static void test_bitshuffle_roundtrip(void) {
    printf("[TEST] Checking Bitshuffle bit-level transposition... ");
    const size_t sz = 64 * 1024;
    uint8_t *orig = (uint8_t*)malloc(sz);
    assert(orig != NULL);

    /* Generate IEEE 754-like float pattern */
    float *floats = (float*)orig;
    for (size_t i = 0; i < sz / sizeof(float); i++) {
        floats[i] = (float)(1.0 + (double)(i % 100) * 0.01);
    }

    void *compressed = NULL;
    size_t comp_sz = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.filter = BINPACK_FILTER_BITSHUFFLE;
    opts.transpose_stride = 4;

    binpack_status_t st_c = binpack_compress_buffer(orig, sz, &compressed, &comp_sz, &opts);
    assert(st_c == BINPACK_OK);
    assert(comp_sz < sz / 4); /* Floats should collapse massively under bitshuffle */

    void *restored = NULL;
    size_t rest_sz = 0;
    binpack_status_t st_d = binpack_decompress_buffer(compressed, comp_sz, &restored, &rest_sz);
    assert(st_d == BINPACK_OK);
    assert(rest_sz == sz);
    assert(memcmp(orig, restored, sz) == 0);

    binpack_free(compressed);
    binpack_free(restored);
    free(orig);
    printf("PASS\n");
}

static void test_texture_bcn_roundtrip(void) {
    printf("[TEST] Checking GPU Texture BC1-BC7 block decoupling... ");
    const size_t sz = 32 * 1024; /* 4096 blocks of 8 bytes (BC1) */
    uint8_t *orig = (uint8_t*)malloc(sz);
    assert(orig != NULL);

    for (size_t i = 0; i < sz; i++) {
        orig[i] = (uint8_t)((i * 3) ^ (i / 8));
    }

    void *compressed = NULL;
    size_t comp_sz = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.filter = BINPACK_FILTER_TEXTURE_BCN;
    opts.transpose_stride = 8;

    binpack_status_t st_c = binpack_compress_buffer(orig, sz, &compressed, &comp_sz, &opts);
    assert(st_c == BINPACK_OK);

    void *restored = NULL;
    size_t rest_sz = 0;
    binpack_status_t st_d = binpack_decompress_buffer(compressed, comp_sz, &restored, &rest_sz);
    assert(st_d == BINPACK_OK);
    assert(rest_sz == sz);
    assert(memcmp(orig, restored, sz) == 0);

    binpack_free(compressed);
    binpack_free(restored);
    free(orig);
    printf("PASS\n");
}

static void test_archival_mode_roundtrip(void) {
    printf("[TEST] Checking Dual-Engine Archival Mode (LZMA2 Core)... ");
    const size_t sz = 128 * 1024;
    uint8_t *orig = (uint8_t*)malloc(sz);
    assert(orig != NULL);

    for (size_t i = 0; i < sz; i++) {
        orig[i] = (uint8_t)((i % 17) + (i % 61));
    }

    void *compressed = NULL;
    size_t comp_sz = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.mode = BINPACK_MODE_ARCHIVAL;

    binpack_status_t st_c = binpack_compress_buffer(orig, sz, &compressed, &comp_sz, &opts);
    assert(st_c == BINPACK_OK);
    assert(compressed != NULL);
    assert(comp_sz > 0 && comp_sz < sz / 2);

    void *restored = NULL;
    size_t rest_sz = 0;
    binpack_status_t st_d = binpack_decompress_buffer(compressed, comp_sz, &restored, &rest_sz);
    assert(st_d == BINPACK_OK);
    assert(rest_sz == sz);
    assert(memcmp(orig, restored, sz) == 0);

    binpack_free(compressed);
    binpack_free(restored);
    free(orig);
    printf("PASS\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=========================================================\n");
    printf("BinPack Enterprise SDK - Regression Test Suite\n");
    printf("=========================================================\n");

    test_version();
    test_buffer_roundtrip();
    test_archival_mode_roundtrip();
    test_micro_trial_guard();
    test_bitshuffle_roundtrip();
    test_texture_bcn_roundtrip();
    test_streaming_file_roundtrip();
    test_corrupt_data_resilience();

    printf("=========================================================\n");
    printf("[SUCCESS] All 8 regression suites passed flawlessly!\n");
    printf("=========================================================\n");
    return 0;
}
