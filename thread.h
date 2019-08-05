#ifndef THREAD_H
#define THREAD_H

#include <pthread.h>
typedef pthread_t* sys_thread_t;
typedef pthread_mutex_t* sys_lock_t;


typedef void (*sys_thread_action_t)(void*);

sys_thread_t Sys_CreateThread(sys_thread_action_t, void*);
void         Sys_JoinThread(sys_thread_t);

sys_lock_t   Sys_CreateLock();
void         Sys_Lock(sys_lock_t);
void         Sys_Unlock(sys_lock_t);

#endif
