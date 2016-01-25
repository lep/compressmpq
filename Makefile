CFLAGS = -O2 -std=c99
LDFLAGS = -lm -pthread

ZOPFLILIB_SRC = zopfli/blocksplitter.c zopfli/cache.c\
                zopfli/deflate.c zopfli/gzip_container.c\
                zopfli/hash.c zopfli/katajainen.c\
                zopfli/lz77.c zopfli/squeeze.c\
                zopfli/tree.c zopfli/util.c\
                zopfli/zlib_container.c zopfli/zopfli_lib.c

HEADERS = table.h queue.h thread.h crypto.h listfile.h

OBJS := crypto.o table.o listfile.o queue.o thread.o compress-mpq.o

.PHONY: clean


compress-mpq: $(OBJS) $(ZOPFLILIB_SRC)
	$(CC) $(LDFLAGS) $(CFLAGS) -o $@ $^

clean:
	rm -f $(OBJS) compress-mpq

