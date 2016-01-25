#ifndef QUEUE_H
#define QUEUE_H

#include "thread.h"

typedef struct queue {
    size_t size;
    size_t cur;
    size_t elemSize;
    void *elements;
    sys_lock_t lock;
} queue_t;

void InitQueue(queue_t *q, void *elems, size_t size, size_t elemSize){
    q->cur = 0;
    q->size = size;
    q->elemSize = elemSize;
    q->elements = elems;
    q->lock = Sys_CreateLock();
}

void *pop(queue_t *q, size_t *status){
    Sys_Lock(q->lock);
    
    void *r;
    if(q->cur >= q->size)
        r = NULL;
    else
        r = q->elements + q->elemSize*q->cur++;
        
    if(status)
        *status = q->cur;
        
    Sys_Unlock(q->lock);
    return r;
}

#endif