#ifndef CHAT_CLIENT_H
#define CHAT_CLIENT_H

#include <pthread.h>
#include <semaphore.h>

typedef struct next_job next_job_t;

typedef struct list {
    next_job_t* head;
    next_job_t* tail;
} list_t;

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

// ==============================================================================
//                              USER AND CONNECTIONS
//                      Handles state of the user and connection
// ==============================================================================

int connect_to_server(user_t* user_ptr, char* server_name, int port);

void livefeed_init(user_t* user);

void user_int(user_t* user);

// ==============================================================================
//                                    REQUESTS
//                          Handles requests to the server
// ==============================================================================

int subscription(user_t* user, int channelId, int req);

void subscribe(user_t* user, int channelId);

void unsubscribe(user_t* user, int channelId);

void list(user_t* user);

void next(user_t* user, int channelId);

void send_message(int channel, user_t* user, char* message);

void livefeed(int channelId, user_t* user);

void stop(user_t* user);

// ============================================================================== //
//                                 THREADED REQUESTS                              //
//                             Handles next and livefeed                          //
// ============================================================================== //

void que_next(user_t* user, int channel);

void que_livefeed(user_t* user, int channel);

// ======================================================================== //
//                                SHELL                                     //
//                             Handles intput
// ======================================================================== //

void handle_interrupt(int sig);

void quit(user_t* user);

void user_input(user_t* user);

#endif  // CHAT_CLIENT_H
