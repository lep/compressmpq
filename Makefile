CFLAGS = -lm -O2 -std=c99 -DWIN32

ZOPFLILIB_SRC = zopfli/blocksplitter.c zopfli/cache.c\
                zopfli/deflate.c zopfli/gzip_container.c\
                zopfli/hash.c zopfli/katajainen.c\
                zopfli/lz77.c zopfli/squeeze.c\
                zopfli/tree.c zopfli/util.c\
                zopfli/zlib_container.c zopfli/zopfli_lib.c

HEADERS = table.h queue.h thread.h crypto.h listfile.h

compress-mpq: thread.c compress-mpq.c $(ZOPFLILIB_SRC) miniz.c $(HEADERS)
	$(CC) $(CFLAGS) $(ZOPFLILIB_SRC) thread.c compress-mpq.c -o compress-mpq 

release: compress compress-src.tar


compress.exe: compress-mpq
	./upx --best --ultra-brute -o compress.exe compress-mpq.exe

compress-src.tar:
	tar cf compress-src.tar thread.c compress-mpq.c miniz.c $(HEADERS) $(ZOPFLILIB_SRC) Makefile
