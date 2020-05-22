#include "mutex.h"

#include <pthread.h>

void rw_mutex_init(rw_mutex_t* m) {
    m->rc = 0;
    pthread_mutex_init(&m->rc_m, NULL);
    pthread_mutex_init(&m->write, NULL);
    pthread_mutex_init(&m->read, NULL);
}

void read_lock(rw_mutex_t* m) {
    pthread_mutex_lock(&m->read);
    pthread_mutex_lock(&m->rc_m);
    m->rc++;
    if (m->rc == 1) {
        pthread_mutex_lock(&m->write);
    }
    pthread_mutex_unlock(&m->rc_m);
    pthread_mutex_unlock(&m->read);
}

void read_unlock(rw_mutex_t* m) {
    pthread_mutex_lock(&m->rc_m);
    m->rc--;
    if (m->rc == 0) {
        pthread_mutex_unlock(&m->write);
    }
    pthread_mutex_unlock(&m->rc_m);
}

void write_lock(rw_mutex_t* m) {
    pthread_mutex_lock(&m->read);
    pthread_mutex_lock(&m->write);
}

void write_unlock(rw_mutex_t* m) {
    pthread_mutex_unlock(&m->read);
    pthread_mutex_unlock(&m->write);
}
