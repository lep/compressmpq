#ifndef QUEUE_H
#define QUEUE_H

#include "thread.h"

struct queue {
    size_t size;
    size_t cur;
    size_t elemSize;
    void *elements;
    sys_lock_t lock;
};

typedef struct queue queue_t;

void InitQueue(queue_t *q, void *elems, size_t size, size_t elemSize);
void *pop(queue_t *q, size_t *status);


#endif
