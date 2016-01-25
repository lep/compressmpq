#ifndef TABLE_H
#define TABLE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>


typedef struct htentry_t {
    uint32_t hashA;
    uint32_t hashB;
    uint32_t _padding;
    uint32_t blockIndex;
} htentry;

typedef struct btentry_t {
    uint32_t filePos;
    uint32_t compressedSize;
    uint32_t normalSize;
    uint32_t flags;
} btentry;


typedef struct table_t {
    uint32_t htSize;
    htentry *ht; //[65536];
    
    uint32_t btSize;
    btentry *bt; //[65536];
} table;


void InitTable(table *tbl, uint32_t numEntries);
void Insert(table *tbl, const char *path, btentry *bte);

#endif
