#include <stdlib.h>
#include "thread.h"

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

