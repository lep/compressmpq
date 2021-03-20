CFLAGS := -std=c99 -w -g -static
CXXFLAGS := -w -g -static
LDFLAGS := -static -lm -lpthread -lstdc++

ZOPFLI_OBJS := zopfli/blocksplitter.o zopfli/cache.o \
                zopfli/deflate.o zopfli/gzip_container.o \
                zopfli/hash.o zopfli/katajainen.o \
                zopfli/lz77.o zopfli/squeeze.o \
                zopfli/tree.o zopfli/util.o \
                zopfli/zlib_container.o zopfli/zopfli_lib.o

ENCODING_OBJS := Adpcm/adpcm.o Huffman/huff.o Pklib/pklib.o Pklib/explode.o miniz.o

OBJS := crypto.o table.o listfile.o queue.o thread.o compress-mpq.o

.PHONY: clean all prof debug
all: compress-mpq

prof: CFLAGS = -w -pg -std=c99
prof: CXXFLAGS = -w -pg
prof: compress-mpq

release: CFLAGS = -O3 -std=c99 -Doff64_t=_off64_t -DPTW32_STATIC_LIB -FORCEWIN
release: CXXFLAGS = -O3 -Doff64_t=_off64_t -DPTW32_STATIC_LIB -FORCEWIN
release: CC = mingw32-gcc
release: CXX = mingw32-g++
release: compress-mpq

compress-mpq: $(OBJS) $(ZOPFLI_OBJS) $(ENCODING_OBJS)
	$(CC) -o $@ $^ $(LDFLAGS)

clean:
	rm -f $(OBJS) $(ZOPFLI_OBJS) $(ENCODING_OBJS) compress-mpq

