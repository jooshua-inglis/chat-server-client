#ifndef CHAT_CLIENT_H
#define CHAT_CLIENT_H

#include <pthread.h>

typedef struct next_job next_job_t;
typedef struct list list_t;

struct list {
    next_job_t* head;
    next_job_t* tail;
};

struct next_job {
    int channel;
    int request;
    next_job_t* next;
};

typedef struct user {
    int channels[256];
    int connectionFd;

    sem_t sem;
    list_t list;

    pthread_mutex_t port_mutex;
} user_t;


void handle_interrupt(int sig);

void user_int(user_t* user);

int connect_to_server(user_t *user_ptr, char *server_name, int port);

void livefeed_init(user_t* user);

void user_input(user_t* user);

void quit(user_t* user);


#endif //CHAT_CLIENT_H
