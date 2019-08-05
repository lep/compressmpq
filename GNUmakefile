CFLAGS := -O3 -std=c99
CXXFLAGS := -O3
LDFLAGS := -lm -pthread

ZOPFLI_OBJS := zopfli/blocksplitter.o zopfli/cache.o \
                zopfli/deflate.o zopfli/gzip_container.o \
                zopfli/hash.o zopfli/katajainen.o \
                zopfli/lz77.o zopfli/squeeze.o \
                zopfli/tree.o zopfli/util.o \
                zopfli/zlib_container.o zopfli/zopfli_lib.o

ENCODING_OBJS := Adpcm/adpcm.o Huffman/huff.o Pklib/pklib.o Pklib/explode.o

OBJS := crypto.o table.o listfile.o queue.o thread.o compress-mpq.o

.PHONY: clean all prof debug
all: compress-mpq

prof: CFLAGS = -w -pg -std=c99 -DWIN32
prof: CXXFLAGS = -w -pg
prof: compress-mpq

debug: CFLAGS = -w -g -std=c99 -DWIN32
debug: CXXFLAGS = -w -g
debug: compress-mpq

compress-mpq: $(OBJS) $(ZOPFLI_OBJS) $(ENCODING_OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<

%.o: %.cpp %.h
	$(CXX) $(CXXFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(ZOPFLI_OBJS) $(ENCODING_OBJS) compress-mpq

