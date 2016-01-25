#include "table.h"
#include "crypto.h"

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

void InitTable(table_t *tbl, uint32_t numEntries){
    tbl->ht = malloc(nextPowerOf2(numEntries) * sizeof(htentry_t));
    tbl->bt = malloc(numEntries * sizeof(btentry_t));
    tbl->htSize = nextPowerOf2(numEntries);
    tbl->btSize = 0;
    for(uint32_t i = 0; i != tbl->htSize; i++){
        tbl->ht[i].blockIndex = 0xffffffff;
        tbl->ht[i].hashA = 0xffffffff;
        tbl->ht[i].hashB = 0xffffffff;
        tbl->ht[i]._padding = 0xffffffff;
    }
    
}


static uint32_t InsertBT(table_t *tbl, btentry_t *bte){
    memcpy(&tbl->bt[tbl->btSize], bte, sizeof(btentry_t));
    return tbl->btSize++;
}

static void InsertHT(table_t *tbl, const char *path, uint32_t btPos){
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

void Insert(table_t *tbl, const char *path, btentry_t *bte){
    InsertHT(tbl, path, InsertBT(tbl, bte));
}
