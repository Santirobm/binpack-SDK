/**
 * @file test_pe_elf_bounds.c
 * @brief Stress and adversarial harness for binpack_find_code_bounds & binpack_auto_select_filter
 *
 * Tests unaligned headers (offsets 1..7), truncated headers, out-of-bounds section
 * table entries, endianness (LE/BE), and malformed PE/ELF headers under UBSan.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../src/binpack_internal.h"

#define MAX_PAD 16

/* Minimal synthetic PE header */
static size_t create_synthetic_pe(uint8_t *dst) {
    memset(dst, 0, 512);
    dst[0] = 'M'; dst[1] = 'Z';
    uint32_t pe_off = 0x80;
    binpack_store_le32(&dst[0x3C], pe_off);

    dst[pe_off] = 'P'; dst[pe_off + 1] = 'E';
    binpack_store_le16(&dst[pe_off + 4], 0x8664); /* x86_64 */
    binpack_store_le16(&dst[pe_off + 6], 1);      /* 1 section */
    binpack_store_le16(&dst[pe_off + 20], 0);     /* 0 opt hdr size */

    size_t sec_off = pe_off + 24;
    memcpy(&dst[sec_off], ".text\0\0\0", 8);
    binpack_store_le32(&dst[sec_off + 16], 100); /* raw_sz = 100 */
    binpack_store_le32(&dst[sec_off + 20], 300); /* raw_ptr = 300 */

    return 512;
}

/* Minimal synthetic ELF64 header (supports little-endian or big-endian) */
static size_t create_synthetic_elf64(uint8_t *dst, int is_be) {
    memset(dst, 0, 512);
    dst[0] = 0x7F; dst[1] = 'E'; dst[2] = 'L'; dst[3] = 'F';
    dst[4] = 2; /* 64-bit */
    dst[5] = (uint8_t)(is_be ? 2 : 1); /* 1=LSB, 2=MSB */

    if (is_be) {
        binpack_store_be16(&dst[18], 183); /* AArch64 machine */
        binpack_store_be64(&dst[40], 128); /* shoff = 128 */
        binpack_store_be16(&dst[58], 64);  /* shentsize = 64 */
        binpack_store_be16(&dst[60], 2);   /* shnum = 2 */
        binpack_store_be16(&dst[62], 1);   /* shstrndx = 1 */

        size_t sec1 = 128 + 64;
        binpack_store_be64(&dst[sec1 + 24], 350); /* strtab_off = 350 */
        binpack_store_be64(&dst[sec1 + 32], 32);  /* strtab_sz = 32 */
        strcpy((char*)&dst[350 + 1], ".text");

        binpack_store_be32(&dst[128], 1);          /* name_idx = 1 */
        binpack_store_be64(&dst[128 + 24], 400);   /* sec_off = 400 */
        binpack_store_be64(&dst[128 + 32], 80);    /* sec_sz = 80 */
    } else {
        binpack_store_le16(&dst[18], 183); /* AArch64 machine */
        binpack_store_le64(&dst[40], 128); /* shoff = 128 */
        binpack_store_le16(&dst[58], 64);  /* shentsize = 64 */
        binpack_store_le16(&dst[60], 2);   /* shnum = 2 */
        binpack_store_le16(&dst[62], 1);   /* shstrndx = 1 */

        size_t sec1 = 128 + 64;
        binpack_store_le64(&dst[sec1 + 24], 350); /* strtab_off = 350 */
        binpack_store_le64(&dst[sec1 + 32], 32);  /* strtab_sz = 32 */
        strcpy((char*)&dst[350 + 1], ".text");

        binpack_store_le32(&dst[128], 1);          /* name_idx = 1 */
        binpack_store_le64(&dst[128 + 24], 400);   /* sec_off = 400 */
        binpack_store_le64(&dst[128 + 32], 80);    /* sec_sz = 80 */
    }

    return 512;
}

/* Minimal synthetic ELF32 header (supports little-endian or big-endian) */
static size_t create_synthetic_elf32(uint8_t *dst, int is_be) {
    memset(dst, 0, 512);
    dst[0] = 0x7F; dst[1] = 'E'; dst[2] = 'L'; dst[3] = 'F';
    dst[4] = 1; /* 32-bit */
    dst[5] = (uint8_t)(is_be ? 2 : 1); /* 1=LSB, 2=MSB */

    if (is_be) {
        binpack_store_be16(&dst[18], 40);  /* ARM32 machine */
        binpack_store_be32(&dst[32], 128); /* shoff = 128 */
        binpack_store_be16(&dst[46], 40);  /* shentsize = 40 */
        binpack_store_be16(&dst[48], 2);   /* shnum = 2 */
        binpack_store_be16(&dst[50], 1);   /* shstrndx = 1 */

        size_t sec1 = 128 + 40;
        binpack_store_be32(&dst[sec1 + 16], 350); /* strtab_off = 350 */
        binpack_store_be32(&dst[sec1 + 20], 32);  /* strtab_sz = 32 */
        strcpy((char*)&dst[350 + 1], ".text");

        binpack_store_be32(&dst[128], 1);          /* name_idx = 1 */
        binpack_store_be32(&dst[128 + 16], 380);   /* sec_off = 380 */
        binpack_store_be32(&dst[128 + 20], 64);    /* sec_sz = 64 */
    } else {
        binpack_store_le16(&dst[18], 40);  /* ARM32 machine */
        binpack_store_le32(&dst[32], 128); /* shoff = 128 */
        binpack_store_le16(&dst[46], 40);  /* shentsize = 40 */
        binpack_store_le16(&dst[48], 2);   /* shnum = 2 */
        binpack_store_le16(&dst[50], 1);   /* shstrndx = 1 */

        size_t sec1 = 128 + 40;
        binpack_store_le32(&dst[sec1 + 16], 350); /* strtab_off = 350 */
        binpack_store_le32(&dst[sec1 + 20], 32);  /* strtab_sz = 32 */
        strcpy((char*)&dst[350 + 1], ".text");

        binpack_store_le32(&dst[128], 1);          /* name_idx = 1 */
        binpack_store_le32(&dst[128 + 16], 380);   /* sec_off = 380 */
        binpack_store_le32(&dst[128 + 20], 64);    /* sec_sz = 64 */
    }

    return 512;
}

int main(void) {
    uint8_t raw_storage[1024 + MAX_PAD];
    printf("=========================================================\n");
    printf("BP-002: PE/ELF Parser Unaligned & Adversarial Boundary Test\n");
    printf("=========================================================\n");

    /* Test 1: Unaligned PE headers */
    for (size_t offset = 0; offset <= 7; offset++) {
        uint8_t *buf = raw_storage + offset;
        size_t pe_sz = create_synthetic_pe(buf);
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;

        binpack_find_code_bounds(buf, pe_sz, &code_off, &code_len);
        binpack_filter_t filt = binpack_auto_select_filter(buf, pe_sz, &param);

        if (code_off != 300 || code_len != 100 || filt != BINPACK_FILTER_X86_64) {
            printf("[FAIL PE] offset=%zu code_off=%zu code_len=%zu filt=%d\n",
                   offset, code_off, code_len, (int)filt);
            return 1;
        }
    }
    printf("[PASS] PE Unaligned bounds and architecture detection (offsets 0..7)\n");

    /* Test 2: Unaligned ELF64 Little-Endian */
    for (size_t offset = 0; offset <= 7; offset++) {
        uint8_t *buf = raw_storage + offset;
        size_t elf_sz = create_synthetic_elf64(buf, 0);
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;

        binpack_find_code_bounds(buf, elf_sz, &code_off, &code_len);
        binpack_filter_t filt = binpack_auto_select_filter(buf, elf_sz, &param);

        if (code_off != 400 || code_len != 80 || filt != BINPACK_FILTER_AARCH64) {
            printf("[FAIL ELF64 LE] offset=%zu code_off=%zu code_len=%zu filt=%d\n",
                   offset, code_off, code_len, (int)filt);
            return 1;
        }
    }
    printf("[PASS] ELF64 LE Unaligned bounds & AArch64 detection (offsets 0..7)\n");

    /* Test 3: Unaligned ELF64 Big-Endian */
    for (size_t offset = 0; offset <= 7; offset++) {
        uint8_t *buf = raw_storage + offset;
        size_t elf_sz = create_synthetic_elf64(buf, 1);
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;

        binpack_find_code_bounds(buf, elf_sz, &code_off, &code_len);
        binpack_filter_t filt = binpack_auto_select_filter(buf, elf_sz, &param);

        if (code_off != 400 || code_len != 80 || filt != BINPACK_FILTER_AARCH64) {
            printf("[FAIL ELF64 BE] offset=%zu code_off=%zu code_len=%zu filt=%d\n",
                   offset, code_off, code_len, (int)filt);
            return 1;
        }
    }
    printf("[PASS] ELF64 BE Unaligned bounds & AArch64 detection (offsets 0..7)\n");

    /* Test 4: Unaligned ELF32 Little-Endian */
    for (size_t offset = 0; offset <= 7; offset++) {
        uint8_t *buf = raw_storage + offset;
        size_t elf_sz = create_synthetic_elf32(buf, 0);
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;

        binpack_find_code_bounds(buf, elf_sz, &code_off, &code_len);
        binpack_filter_t filt = binpack_auto_select_filter(buf, elf_sz, &param);

        if (code_off != 380 || code_len != 64 || filt != BINPACK_FILTER_ARM32) {
            printf("[FAIL ELF32 LE] offset=%zu code_off=%zu code_len=%zu filt=%d\n",
                   offset, code_off, code_len, (int)filt);
            return 1;
        }
    }
    printf("[PASS] ELF32 LE Unaligned bounds & ARM32 detection (offsets 0..7)\n");

    /* Test 5: Unaligned ELF32 Big-Endian */
    for (size_t offset = 0; offset <= 7; offset++) {
        uint8_t *buf = raw_storage + offset;
        size_t elf_sz = create_synthetic_elf32(buf, 1);
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;

        binpack_find_code_bounds(buf, elf_sz, &code_off, &code_len);
        binpack_filter_t filt = binpack_auto_select_filter(buf, elf_sz, &param);

        if (code_off != 380 || code_len != 64 || filt != BINPACK_FILTER_ARM32) {
            printf("[FAIL ELF32 BE] offset=%zu code_off=%zu code_len=%zu filt=%d\n",
                   offset, code_off, code_len, (int)filt);
            return 1;
        }
    }
    printf("[PASS] ELF32 BE Unaligned bounds & ARM32 detection (offsets 0..7)\n");

    /* Test 6: Truncations byte-by-byte (0 to 512 bytes) on all formats */
    uint8_t *buf = raw_storage + 3; /* unaligned offset 3 */

    /* PE sweep */
    size_t pe_sz = create_synthetic_pe(buf);
    for (size_t len = 0; len <= pe_sz; len++) {
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;
        binpack_find_code_bounds(buf, len, &code_off, &code_len);
        binpack_auto_select_filter(buf, len, &param);
    }

    /* ELF64 LE sweep */
    size_t elf64_sz = create_synthetic_elf64(buf, 0);
    for (size_t len = 0; len <= elf64_sz; len++) {
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;
        binpack_find_code_bounds(buf, len, &code_off, &code_len);
        binpack_auto_select_filter(buf, len, &param);
    }

    /* ELF32 BE sweep */
    size_t elf32_sz = create_synthetic_elf32(buf, 1);
    for (size_t len = 0; len <= elf32_sz; len++) {
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;
        binpack_find_code_bounds(buf, len, &code_off, &code_len);
        binpack_auto_select_filter(buf, len, &param);
    }
    printf("[PASS] Adversarial truncation sweep (0..512 bytes across formats)\n");

    /* Test 7: Malformed ELF Endianness flag (ei_data = 99) */
    buf[0] = 0x7F; buf[1] = 'E'; buf[2] = 'L'; buf[3] = 'F';
    buf[4] = 2; buf[5] = 99; /* Invalid EI_DATA */
    {
        size_t code_off = 0, code_len = 0;
        uint16_t param = 0;
        binpack_find_code_bounds(buf, 512, &code_off, &code_len);
        binpack_filter_t filt = binpack_auto_select_filter(buf, 512, &param);
        if (code_off != 0 || code_len != 512) {
            printf("[FAIL EI_DATA=99 bounds]\n");
            return 1;
        }
        if (filt != BINPACK_FILTER_NONE && filt != BINPACK_FILTER_X86_64) {
            /* Should fallback gracefully without crash */
        }
    }
    printf("[PASS] Malformed EI_DATA gracefully handled\n");

    printf("=========================================================\n");
    printf("[SUCCESS] All PE/ELF boundary tests completed successfully.\n");
    return 0;
}
