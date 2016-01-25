#include "listfile.h"

#include <stdlib.h>
#include <string.h>

void InitListfile(listfile_t *listfile, size_t size){
    listfile->list = malloc(sizeof(listfile_elem_t)*size);
    memset(listfile->list, 0, sizeof(listfile_elem_t)*size);
    listfile->size = size;
}

void FreeListfile(listfile_t *listfile){
    free(listfile->list);
}

uint8_t AddPath(listfile_t *listfile, uint32_t a, uint32_t b, char *path){
    uint64_t hash = (uint64_t)a << 32 | b;
    size_t start = (size_t)(hash % listfile->size);
    size_t idx = start;
    do {
        if(listfile->list[idx].hash == 0){
            listfile->list[idx].hash = hash;
            listfile->list[idx].path = path;
            return 1;
        }else if(listfile->list[idx].hash == hash){
            return 1;
        }
        idx = (idx+1) % listfile->size;
    } while(idx != start);
    return 0;
}

char* FindPath(listfile_t *listfile, uint32_t a, uint32_t b){
    uint64_t hash = (uint64_t)a << 32 | b;
    size_t start = (size_t)(hash % listfile->size);
    size_t idx = start;
    do {
        if(listfile->list[idx].hash == hash){
            return listfile->list[idx].path;
        }
        idx = (idx+1) % listfile->size;
    } while(idx != start);
    return NULL;
}
