#include <stdlib.h>
#include "thread.h"

#ifdef WIN32
const static int kStackSize = 1048576;

sys_thread_t Sys_CreateThread( sys_thread_action_t action, void* arg ){
    return CreateThread(NULL, kStackSize, (LPTHREAD_START_ROUTINE)action, arg, 0, NULL);
}

void Sys_JoinThread( sys_thread_t thread ){
    WaitForSingleObject(thread, INFINITE);
}

sys_lock_t Sys_CreateLock(){
    LPCRITICAL_SECTION lock = (CRITICAL_SECTION*)malloc(sizeof(CRITICAL_SECTION));
    InitializeCriticalSection(lock);
    return lock;
}

void Sys_Lock( sys_lock_t lock ){
    EnterCriticalSection(lock);
}

void Sys_Unlock( sys_lock_t lock ){
    LeaveCriticalSection(lock);
}

#else

sys_thread_t Sys_CreateThread( sys_thread_action_t action, void* arg ){
    pthread_t *t = malloc(sizeof(pthread_t));
    pthread_create(t, NULL, (void*(*)(void*))action, arg);
    return t;
    
}

void Sys_JoinThread( sys_thread_t thread ){
    pthread_join(*thread, NULL);
}

sys_lock_t Sys_CreateLock(){
    pthread_mutex_t *lock = malloc(sizeof(pthread_mutex_t));
    pthread_mutex_init(lock, NULL);
    return lock;
}

void Sys_Lock( sys_lock_t lock ){
    pthread_mutex_lock(lock);
}

void Sys_Unlock( sys_lock_t lock ){
    pthread_mutex_unlock(lock);
}

#endif