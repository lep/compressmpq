#ifndef TABLE_H
#define TABLE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


struct htentry {
    uint32_t hashA;
    uint32_t hashB;
    uint32_t _padding;
    uint32_t blockIndex;
} __attribute__((packed));

typedef struct htentry htentry_t;

struct btentry {
    uint32_t filePos;
    uint32_t compressedSize;
    uint32_t normalSize;
    uint32_t flags;
} __attribute__((packed));

typedef struct btentry btentry_t;


struct table {
    uint32_t htSize;
    htentry_t *ht; //[65536];
    
    uint32_t btSize;
    btentry_t *bt; //[65536];
};

typedef struct table table_t;


void InitTable(table_t *tbl, uint32_t numEntries);
void Insert(table_t *tbl, const char *path, btentry_t *bte);

#endif
