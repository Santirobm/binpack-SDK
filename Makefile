CC ?= gcc
CFLAGS ?= -O3 -mavx2 -D_7ZIP_ST -Wall -Wextra -Iinclude -Ideps/include -Ideps/lzma
LDFLAGS ?= -Ldeps/lib -lzstd

SRC = src/binpack_core.c src/binpack_eval.c src/binpack_stream.c src/binpack_sdk.c \
      src/binpack_dict.c src/binpack_patch.c src/binpack_archive.c \
      deps/lzma/Alloc.c deps/lzma/LzFind.c deps/lzma/LzmaDec.c \
      deps/lzma/LzmaEnc.c deps/lzma/LzmaLib.c deps/lzma/Bra86.c deps/lzma/Bra.c
OBJ = $(SRC:.c=.o)

all: libbinpack.so binpack-sdk test_sdk

%.o: %.c
	$(CC) $(CFLAGS) -fPIC -c $< -o $@

libbinpack.so: $(OBJ)
	$(CC) -shared $(OBJ) $(LDFLAGS) -o $@

libbinpack.a: $(OBJ)
	ar rcs $@ $(OBJ)

binpack-sdk: cli/binpack_cli.c libbinpack.a
	$(CC) $(CFLAGS) cli/binpack_cli.c libbinpack.a $(LDFLAGS) -o $@

test_sdk: tests/test_sdk.c libbinpack.a
	$(CC) $(CFLAGS) tests/test_sdk.c libbinpack.a $(LDFLAGS) -o $@

test: test_sdk
	./test_sdk

clean:
	rm -f src/*.o cli/*.o tests/*.o *.a *.so binpack-sdk test_sdk

.PHONY: all test clean
