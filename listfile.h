#ifndef LISTFILE_H
#define LISTFILE_H

#include <stdint.h>
#include <stddef.h>

struct listfile_elem {
    uint64_t hash;
    char *path;
};
typedef struct listfile_elem listfile_elem_t;

struct listfile {
    size_t size;
    listfile_elem_t *list;
};
typedef struct listfile listfile_t;

void InitListfile(listfile_t *listfile, size_t size);
void FreeListfile(listfile_t *listfile);
uint8_t AddPath(listfile_t *listfile, uint32_t a, uint32_t b, char *path);
char* FindPath(listfile_t *listfile, uint32_t a, uint32_t b);

#endif
