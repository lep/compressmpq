CFLAGS := -O2 -std=c99
LDFLAGS := -lm -pthread

ZOPFLI_OBJS := zopfli/blocksplitter.o zopfli/cache.o \
                zopfli/deflate.o zopfli/gzip_container.o \
                zopfli/hash.o zopfli/katajainen.o \
                zopfli/lz77.o zopfli/squeeze.o \
                zopfli/tree.o zopfli/util.o \
                zopfli/zlib_container.o zopfli/zopfli_lib.o

OBJS := crypto.o table.o listfile.o queue.o thread.o compress-mpq.o

.PHONY: clean


compress-mpq: $(OBJS) $(ZOPFLI_OBJS)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) $(ZOPFLI_OBJS) compress-mpq

