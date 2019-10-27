#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#include <semaphore.h>
#include <stdbool.h>

#include "mutex.h"
#include "util.h"

#define MESSAGE_QUE_NAME "MESSAGE_QUE"
#define SHARED_CHAT_NAME "CHAT%d"
#define SHARED_PROCESS_NAME "PROCESSES"
#define MAX_USES  256
#define MAX_CHANNELS 256


// ===========================================================================
//                                   CHAT LOG
// ===========================================================================

#define CHANNEL_SIZE 1024

typedef struct message {
    int client_id;
    int channel;
    char message[MESSAGE_SIZE];
    int time;
    int pos;
} message_t;

typedef struct channel {
    message_t messages[CHANNEL_SIZE];
    size_t pos;
    char  shm_name[8];
    rw_mutex_t mutex;
} channel_t;

channel_t* channel_shm_init(int i);

void channel_close();

void channel_init();

message_t* message_put(int channel , message_t message);


// ===========================================================================
//                                   CLIENT
// ===========================================================================


typedef struct message_node message_node_t;

typedef struct message_que {
    message_node_t* head;
    message_node_t* tail;
} message_que_t;


typedef struct client {
    pid_t pid;
    int client_id;
    int connectionFd;
    bool free;
    int positions[MAX_CHANNELS];
    int buffer_pos;
    message_que_t que;
    bool livefeeds[MAX_CHANNELS];
    bool livefeed_all;
    sem_t exit_sem;
    sem_t buffer_sem;
} client_t;

void clients_ready();

void client_shm_init();

client_t *client_add();

void client_close(client_t* client);

void client_close_all();

bool is_subscribed(client_t* client, int c);

// ===========================================================================
//                                   MESSAGE QUE
// ===========================================================================

#define MSG_QUE_BUFFER_SIZE 1000

struct message_node {
    message_t* message;
    channel_t* channel;
    int time;
    message_node_t* next;
};

struct new_message {
    message_t* messsage;
    channel_t* channel;
};

struct message_buffer {
    struct new_message buffer[MSG_QUE_BUFFER_SIZE];
    int writer_pos;
    rw_mutex_t lock;
};

void que_shm_init();

void buffer_add(message_t* message, struct message_buffer* buffer, int channel);

bool is_livefeed(client_t* client, int channel);

bool _message_read(int mess_pos, int read_pos, int write_pos);

bool message_read(client_t* client, message_t* message);

void que_add(client_t* client);

void message_reader(client_t* client);

// ===========================================================================
//                                   REQUESTS
// ===========================================================================


void return_data(client_t* client, char* data, int data_size, int code);

int subscribe(client_t* client, int c);

int unsubscribe(client_t* client, int c);

int add_message(client_t *client, int channel, char *message);

void next_time(client_t* client, message_que_t *m);

void next_id(client_t *client, int c);

void list_sub(client_t* client);

void add_livefeed(client_t* client, int channel);

void stop(client_t* client);


// ===========================================================================
//                               SERVER MAIN
// ===========================================================================

int socket_init(int port);

void exit_wait(client_t* client);

void client_init(client_t* client, int connectFd);

void chat_listen(client_t *client);

void incoming_connections(int listenFd);

void incoming_connections_single_process(int listenFd);


#endif //CHAT_SERVER_H
