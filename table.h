#ifndef TABLE_H
#define TABLE_H

#include <stdint.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "crypto.h"

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


static uint32_t nextPowerOf2(uint32_t v){
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    v++;
    return v;
}

void InitTable(table *tbl, uint32_t numEntries){
    tbl->ht = malloc(nextPowerOf2(numEntries)*sizeof(htentry));
    tbl->bt = malloc(numEntries*sizeof(btentry));
    tbl->htSize = nextPowerOf2(numEntries);
    tbl->btSize = 0;
    for(uint32_t i = 0; i != tbl->htSize; i++){
        tbl->ht[i].blockIndex = 0xffffffff;
        tbl->ht[i].hashA = 0xffffffff;
        tbl->ht[i].hashB = 0xffffffff;
        tbl->ht[i]._padding = 0xffffffff;
    }
    
}


static uint32_t InsertBT(table *tbl, btentry *bte){
    memcpy(&tbl->bt[tbl->btSize], bte, sizeof(btentry));
    return tbl->btSize++;
}

static void InsertHT(table *tbl, const char *path, uint32_t btPos){
    uint32_t hashA = hash(path, HashA),
             hashB = hash(path, HashB),
             start = hash(path, HashOffset) % tbl->htSize,
             pos = start;
    while(tbl->ht[pos].blockIndex != 0xFFFFFFFF){
        pos = (pos+1) % tbl->htSize;
        if(pos == start){
            // this actually should not happen...
            fprintf(stderr, "the impossible happened");
            exit(1);
        }
    }
    tbl->ht[pos].hashA = hashA;
    tbl->ht[pos].hashB = hashB;
    tbl->ht[pos].blockIndex = btPos;
    tbl->ht[pos]._padding = 0;
}

void Insert(table *tbl, const char *path, btentry *bte){
    InsertHT(tbl, path, InsertBT(tbl, bte));
}

#endif