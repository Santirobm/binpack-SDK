#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "deps/timeseries/timeseries_codecs.h"

int main() {
    FILE *f = fopen("C:/Users/sgi.lm/Desktop/Compiler/sensor_data.raw", "rb");
    if (!f) { printf("Cannot open\n"); return 1; }
    fseek(f, 0, SEEK_END);
    size_t sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    int16_t *buf = (int16_t*)malloc(sz);
    fread(buf, 1, sz, f);
    fclose(f);

    size_t count = sz / 2;
    uint8_t *cbuf = (uint8_t*)malloc(sz * 2);
    int16_t *dec = (int16_t*)malloc(sz);

    size_t csz = sprintz_compress_i16(buf, count, cbuf, sz * 2);
    sprintz_decompress_i16(cbuf, csz, dec, count);

    int diff_count = 0;
    for (size_t i = 0; i < count; i++) {
        if (buf[i] != dec[i]) {
            if (diff_count < 10) {
                printf("Diff at %zu: orig=%d, dec=%d\n", i, buf[i], dec[i]);
            }
            diff_count++;
        }
    }
    printf("Total diffs: %d / %zu\n", diff_count, count);
    return 0;
}
