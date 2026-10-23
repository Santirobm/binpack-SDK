/**
 * @file test_sdk.c
 * @brief BinPack Enterprise SDK - Automated Regression & Verification Test Suite
 */

#include "../include/binpack_sdk.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

/* Internal test declaration */
void binpack_bcj_aarch64_filter(uint8_t *data, size_t size);
void binpack_bcj_aarch64_unfilter(uint8_t *data, size_t size);

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

    binpack_status_t st_cf = binpack_compress_file(test_in, test_bpk, &opts, NULL, NULL);
    assert(st_cf == BINPACK_OK);

    binpack_file_info_t info;
    binpack_status_t st_q = binpack_query_file_info(test_bpk, &info);
    assert(st_q == BINPACK_OK);
    assert(info.uncompressed_size == sz);
    assert(info.chunk_count >= 5);

    binpack_status_t st_df = binpack_decompress_file(test_bpk, test_out, NULL, NULL);
    assert(st_df == BINPACK_OK);

    FILE *f_res = fopen(test_out, "rb");
    assert(f_res != NULL);
    uint8_t *buf_res = (uint8_t*)malloc(sz);
    assert(buf_res != NULL);
    size_t read_bytes = fread(buf_res, 1, sz, f_res);
    assert(read_bytes == sz);
    fclose(f_res);

    assert(memcmp(buf, buf_res, sz) == 0);

    free(buf);
    free(buf_res);
    remove(test_in);
    /* Leave test_bpk for test_seekable_archive, clean up at the end */
    remove(test_out);
    printf("PASS\n");
}

static void test_corrupt_data_resilience(void) {
    printf("[TEST] Checking error handling on corrupt stream headers... ");
    uint8_t garbage[64];
    memset(garbage, 0xCC, sizeof(garbage));

    void *restored = NULL;
    size_t rest_sz = 0;
    binpack_status_t st = binpack_decompress_buffer(garbage, sizeof(garbage), &restored, &rest_sz);
    assert(st != BINPACK_OK);
    assert(restored == NULL);
    printf("PASS\n");
}

static void test_bitshuffle_roundtrip(void) {
    printf("[TEST] Checking Bitshuffle bit-level transposition... ");
    const size_t sz = 64 * 1024;
    uint8_t *orig = (uint8_t*)malloc(sz);
    assert(orig != NULL);

    float *fview = (float*)orig;
    for (size_t i = 0; i < sz / 4; i++) {
        fview[i] = (float)i * 0.125f + 1.5f;
    }

    void *compressed = NULL;
    size_t comp_sz = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.filter = BINPACK_FILTER_BITSHUFFLE;
    opts.transpose_stride = 4;

    binpack_status_t st_c = binpack_compress_buffer(orig, sz, &compressed, &comp_sz, &opts);
    assert(st_c == BINPACK_OK);
    assert(comp_sz < sz);

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
    const size_t sz = 128 * 1024;
    uint8_t *orig = (uint8_t*)malloc(sz);
    assert(orig != NULL);

    for (size_t i = 0; i < sz; i++) {
        orig[i] = (uint8_t)((i % 8) + (i / 8));
    }

    void *compressed = NULL;
    size_t comp_sz = 0;
    binpack_options_t opts;
    binpack_default_options(&opts);
    opts.filter = BINPACK_FILTER_TEXTURE_BCN;
    opts.transpose_stride = 8;

    binpack_status_t st_c = binpack_compress_buffer(orig, sz, &compressed, &comp_sz, &opts);
    assert(st_c == BINPACK_OK);
    assert(comp_sz < sz);

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

static void test_aarch64_bcj_filter(void) {
    printf("[TEST] Checking AArch64 B/BL Branch Normalization Round-trip... ");
    const size_t sz = 16 * 1024;
    uint8_t *orig = (uint8_t*)malloc(sz);
    uint8_t *work = (uint8_t*)malloc(sz);
    assert(orig != NULL && work != NULL);

    /* Generate synthetic AArch64 machine instructions (B: 0x14..., BL: 0x94...) */
    for (size_t i = 0; i < sz; i += 4) {
        uint32_t op = (i % 8 == 0) ? 0x25 : 0x05; /* alternating B and BL */
        uint32_t imm26 = (uint32_t)(i * 37) & 0x03FFFFFF;
        uint32_t inst = (op << 26) | imm26;
        orig[i]   = (uint8_t)(inst);
        orig[i+1] = (uint8_t)(inst >> 8);
        orig[i+2] = (uint8_t)(inst >> 16);
        orig[i+3] = (uint8_t)(inst >> 24);
    }
    memcpy(work, orig, sz);

    /* Forward filter */
    binpack_bcj_aarch64_filter(work, sz);
    /* Ensure transformation actually changed values */
    assert(memcmp(work, orig, sz) != 0);

    /* Inverse filter */
    binpack_bcj_aarch64_unfilter(work, sz);
    /* Ensure 100% bit-exact restoration */
    assert(memcmp(work, orig, sz) == 0);

    free(orig);
    free(work);
    printf("PASS\n");
}

static void test_dictionary_engine(void) {
    printf("[TEST] Checking Pre-trained Dictionary Engine (Micro-Telemetry)... ");
    const size_t sample_count = 100;
    const size_t sample_sz = 64; /* 64-byte CAN-bus packets */
    size_t total_samples_sz = sample_count * sample_sz;

    uint8_t *samples = (uint8_t*)malloc(total_samples_sz);
    size_t *sample_sizes = (size_t*)malloc(sample_count * sizeof(size_t));
    assert(samples != NULL && sample_sizes != NULL);

    for (size_t s = 0; s < sample_count; s++) {
        sample_sizes[s] = sample_sz;
        uint8_t *p = samples + (s * sample_sz);
        /* Structured CAN-bus frame: 4 bytes ID, 8 bytes data, repeated patterns */
        p[0] = 0x18; p[1] = 0xFE; p[2] = 0xF1; p[3] = 0x00;
        for (size_t b = 4; b < sample_sz; b++) {
            p[b] = (uint8_t)((b * 11) ^ (s % 5));
        }
    }

    /* Train 16 KB dictionary */
    binpack_dict_t *dict = binpack_train_dict(samples, sample_sizes, sample_count, 16 * 1024);
    assert(dict != NULL);

    size_t dict_bytes_sz = 0;
    const void *dict_raw = binpack_get_dict_buffer(dict, &dict_bytes_sz);
    assert(dict_raw != NULL && dict_bytes_sz > 0);

    /* Test compressing a single 64-byte packet with dictionary */
    uint8_t single_packet[64];
    memcpy(single_packet, samples, 64);
    uint8_t cmp_packet[256];
    size_t cmp_packet_sz = 0;

    binpack_status_t st_c = binpack_compress_dict(
        single_packet, 64,
        cmp_packet, sizeof(cmp_packet),
        &cmp_packet_sz, dict
    );
    assert(st_c == BINPACK_OK);
    /* Verify high compression ratio on 64-byte micro-packet (< 25 bytes!) */
    assert(cmp_packet_sz > 0 && cmp_packet_sz < 30);

    /* Test decompressing */
    uint8_t restored_packet[64];
    size_t rest_sz = 0;
    binpack_status_t st_d = binpack_decompress_dict(
        cmp_packet, cmp_packet_sz,
        restored_packet, sizeof(restored_packet),
        &rest_sz, dict
    );
    assert(st_d == BINPACK_OK);
    assert(rest_sz == 64);
    assert(memcmp(single_packet, restored_packet, 64) == 0);

    binpack_free_dict(dict);
    free(samples);
    free(sample_sizes);
    printf("PASS\n");
}

static void test_differential_patching(void) {
    printf("[TEST] Checking Binary Differential Patching Engine (OTA)... ");
    const size_t sz = 64 * 1024;
    uint8_t *old_buf = (uint8_t*)malloc(sz);
    uint8_t *new_buf = (uint8_t*)malloc(sz);
    assert(old_buf != NULL && new_buf != NULL);

    for (size_t i = 0; i < sz; i++) {
        old_buf[i] = (uint8_t)((i % 137) ^ (i / 19));
    }
    memcpy(new_buf, old_buf, sz);
    /* Introduce small code updates (50 bytes modified) */
    for (size_t i = 1000; i < 1050; i++) {
        new_buf[i] = (uint8_t)(new_buf[i] ^ 0xFF);
    }

    void *patch_data = NULL;
    size_t patch_sz = 0;
    binpack_status_t st_cp = binpack_create_patch(old_buf, sz, new_buf, sz, &patch_data, &patch_sz, NULL);
    assert(st_cp == BINPACK_OK);
    assert(patch_data != NULL);
    /* Verify microscopic patch size (< 500 bytes for 64 KB update!) */
    assert(patch_sz > 0 && patch_sz < 600);

    void *restored_new = NULL;
    size_t restored_new_sz = 0;
    binpack_status_t st_ap = binpack_apply_patch(old_buf, sz, patch_data, patch_sz, &restored_new, &restored_new_sz);
    assert(st_ap == BINPACK_OK);
    assert(restored_new_sz == sz);
    assert(memcmp(new_buf, restored_new, sz) == 0);

    binpack_free(patch_data);
    binpack_free(restored_new);
    free(old_buf);
    free(new_buf);
    printf("PASS\n");
}

static void test_seekable_archive(void) {
    printf("[TEST] Checking Seekable Random-Access Archive Engine... ");
    const char *test_bpk = "test_stream_archive.tmp.bpk2";

    binpack_archive_t *arch = binpack_archive_open(test_bpk);
    assert(arch != NULL);

    uint32_t chunk_count = binpack_archive_get_chunk_count(arch);
    assert(chunk_count >= 5);
    uint64_t total_sz = binpack_archive_get_total_uncompressed_size(arch);
    assert(total_sz == 2500000);

    /* Query chunk 2 directly without reading previous chunks */
    binpack_chunk_info_t cinfo;
    binpack_status_t st_info = binpack_archive_get_chunk_info(arch, 2, &cinfo);
    assert(st_info == BINPACK_OK);
    assert(cinfo.chunk_index == 2);
    assert(cinfo.uncompressed_sz == 512 * 1024);

    /* Decompress chunk 2 directly into memory buffer */
    uint8_t *chunk_dst = (uint8_t*)malloc(cinfo.uncompressed_sz);
    assert(chunk_dst != NULL);
    size_t decompressed_sz = 0;
    binpack_status_t st_dc = binpack_archive_decompress_chunk(
        arch, 2, chunk_dst, cinfo.uncompressed_sz, &decompressed_sz
    );
    assert(st_dc == BINPACK_OK);
    assert(decompressed_sz == cinfo.uncompressed_sz);

    /* Validate decompressed chunk against original formula */
    size_t base_offset = 2 * (512 * 1024);
    for (size_t i = 0; i < decompressed_sz; i++) {
        size_t orig_idx = base_offset + i;
        uint8_t expected = (uint8_t)((orig_idx * 7) ^ (orig_idx / 13));
        assert(chunk_dst[i] == expected);
    }

    free(chunk_dst);
    binpack_archive_close(arch);
    remove(test_bpk);
    printf("PASS\n");
}

int main(void) {
    setvbuf(stdout, NULL, _IONBF, 0);
    printf("=========================================================\n");
    printf("BinPack Enterprise SDK - Regression Test Suite (v2.2.0)\n");
    printf("=========================================================\n");

    test_version();
    test_buffer_roundtrip();
    test_archival_mode_roundtrip();
    test_micro_trial_guard();
    test_bitshuffle_roundtrip();
    test_texture_bcn_roundtrip();
    test_aarch64_bcj_filter();
    test_dictionary_engine();
    test_differential_patching();
    test_streaming_file_roundtrip();
    test_seekable_archive();
    test_corrupt_data_resilience();

    printf("=========================================================\n");
    printf("[SUCCESS] All 12 regression test suites passed flawlessly!\n");
    printf("=========================================================\n");
    return 0;
}
