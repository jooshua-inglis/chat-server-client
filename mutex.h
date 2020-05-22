
#ifndef CHAT_MUTEX_H
#define CHAT_MUTEX_H

#include <pthread.h>

typedef struct wr_mutex {
    pthread_mutex_t read;
    pthread_mutex_t write;
    pthread_mutex_t rc_m;
    int rc;
} rw_mutex_t;

void rw_mutex_init(rw_mutex_t* m);

/*
 * Locks the mutex however, is not blocked by other read locks, only write locks.
 */
void read_lock(rw_mutex_t* m);

void read_unlock(rw_mutex_t* m);

void write_lock(rw_mutex_t* m);

void write_unlock(rw_mutex_t* m);

#endif
