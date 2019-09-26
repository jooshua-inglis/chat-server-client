#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>
#include <sys/shm.h> 
#include <sys/mman.h>
#include <sys/stat.h>        /* For mode constants */
#include <fcntl.h>           /* For O_* constants */
#include <stdbool.h>
#include <time.h>
#include "util.h"
#include <pthread.h>

#define BUFFER_SIZE 32
#define MAX_USES  256
#define MAX_CHANNELS 256


int LISTENFD;


// ===========================================================================
//                                   CHAT LOG 
// ===========================================================================


#define SHARED_CHAT_NAME "CHAT%d"
#define CHANNAL_INIT_SIZE 1024
#define MESSAGE_SIZE 1024 * 8

typedef struct message {
    int client_id;
    int channel;
    char message[MESSAGE_SIZE]; 
    int time;
    int pos;
} Message_t;

typedef struct node Node_t;

typedef struct channel {
    Message_t messages[CHANNAL_INIT_SIZE];
    size_t pos;
    int shm_sg;
    char  shm_name[8];
} Channel_t;


Channel_t* channels[MAX_CHANNELS];
int chat_mem;

Channel_t* channel_memory_init(int i) {
    Channel_t* channel;
    char name[8];
    sprintf(name, SHARED_CHAT_NAME, i);
    int shm_sg = shm_open(name, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_sg, sizeof(Channel_t));
    channel = mmap(0, sizeof(Channel_t), PROT_WRITE, MAP_SHARED, shm_sg, 0);
    sprintf(channel->shm_name, name);
    return channel;
}

void channel_close() {
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        unlink(channels[i]->shm_name);
    }
}

void channel_init() {
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        channels[i] = channel_memory_init(i);
        channels[i]->pos = 0;
    }
}

void message_print(Message_t message) {
    printf("#%d: %s\n", message.client_id, message.message);
}

void channel_print(int channel) {
    Channel_t* c = channels[channel];
    for (int i = 0; i < c->pos; ++i) {
        message_print(c->messages[i]);
    }
}

Message_t* message_put(int channel , Message_t message) {
    Channel_t* c = channels[channel];
    message.pos = c->pos;
    c->messages[c->pos] = message; 
    return &c->messages[c->pos++];
}

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
} Client_t;


#define SHARED_PRCOESS_NAME "PROCESSES"
Client_t *clients;
int processes_mem;

void shared_memory_init() {
    processes_mem = shm_open(SHARED_PRCOESS_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(processes_mem, MAX_USES * sizeof(Client_t));
    clients = mmap(0, MAX_USES * sizeof(Client_t), PROT_WRITE, MAP_SHARED, processes_mem, 0);
}

void client_init() {
    for (int i = 0; i < MAX_USES; ++i) { 
        clients[i].free = true; 
        clients[i].client_id = i;
        for (int j = 0; j < MAX_CHANNELS; ++j) {
            clients[i].livefeeds[j] = false;
            clients[i].positions[j] = -1;
        }
    }
}

// returns -1 if no child slot avaliable, else returns client id
Client_t *client_add() {
    for (int i = 0; i < MAX_USES; ++i) {
        if (clients[i].free) { return &clients[i]; }
    }
    return NULL;
}

void client_close(Client_t* client) {
    close(client->connectionFd);
    close(LISTENFD);
    client->free = 1;
}

void client_close_all() {
    pid_t pid;
    for (int i = 0; i < MAX_USES; ++i) {
        pid = clients[i].pid;
        if (pid) {
            kill(pid, SIGINT);
        }
    }
}

bool is_subscribed(Client_t* client, int c) {
    return client->positions[c] != -1;
}

// ===========================================================================
//                                   MESSAGE QUE 
// ===========================================================================


#define MSG_QUE_BUFFER_SIZE 1000
#define MESSAGE_QUE_NAME "MESSAGE_QUE"

typedef struct message_node message_node_t;

struct message_node {
    Message_t* message;
    int time;
    message_node_t* next;
};

struct message_buffer {
    Message_t *buffer[MSG_QUE_BUFFER_SIZE];
    int writer_pos;
};

struct message_buffer* mess_buffer;

void que_memory_init() {
    int shm_sg = shm_open(MESSAGE_QUE_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_sg, sizeof(struct message_buffer));
    mess_buffer = mmap(0, sizeof(struct message_buffer), PROT_WRITE, MAP_SHARED, shm_sg, 0);
    mess_buffer->writer_pos = 0;
}

void buffer_add(Message_t* message, struct message_buffer* buffer) {
    buffer->buffer[buffer->writer_pos++] = message;
}


bool is_livefeed(Client_t* client, int channel) {
    return client->livefeed_all || client->livefeeds[channel];
}


Message_t* que_add(Client_t* client) {
    message_que_t* que = &client->que;
    message_node_t* node = malloc(sizeof(message_node_t));
    node->message = mess_buffer->buffer[client->buffer_pos++];
    node->time = node->message->time;
    node->next = NULL;

    if (is_livefeed(client, node->message->channel)) {
        send(client->connectionFd, node->message->message, BUFFER_SIZE, MSG_OOB);
        client->positions[node->message->channel]++;
        return node->message;
    }

    if (que->head == NULL) {
        que->head = node;
        que->tail = node;
    } else {
        que->tail->next = node;
        que->tail = node;
    }

    return node->message;
}

bool _message_read(int mess_pos, int read_pos, int write_pos) { 
    if (read_pos == write_pos) return true;
    else if (write_pos > read_pos) return !(read_pos <= mess_pos && mess_pos < write_pos);
    else return write_pos <= mess_pos && mess_pos < read_pos;
}

bool message_read(Client_t* client, Message_t* message) {
    int mess_pos = message->pos;
    int read_pos = client->positions[message->channel];
    int write_pos = channels[message->channel]->pos;

    return _message_read(mess_pos, read_pos, write_pos);
}


void message_reader(Client_t* client) {
    while(1) {
        sleep(1);
        if (client->buffer_pos == mess_buffer->writer_pos) continue;

        else {
            printf("adding to que!\n");
            Message_t* new_message = que_add(client);
        }
    }
}

void next_time(Client_t* client, message_que_t *m) {
    if (m->head == NULL ) {
        send(client->connectionFd, "", BUFFER_SIZE, 0);
        return;
    }
    message_node_t* node = m->head;
    if (node->message->time != node->time || message_read(client, node->message)) {
        m->head = node->next;
        free(node);
        next_time(client, m);
        return;
    } else {
        send(client->connectionFd, node->message->message, BUFFER_SIZE, 0);
        client->positions[node->message->channel]++;
        m->head = node->next;
        free(node);
        return;
    }
}


// ===========================================================================
//                                   REQUESTS 
// ===========================================================================


int subscribe(Client_t* client, int c) {
    if (!is_subscribed(client, c)) {
        client->positions[c] = channels[c]->pos;
        printf("%d subbed to channel %d\n", client->client_id, c);
        return 0;
    } else return -1;
}

int unsubscribe(Client_t* client, int c) {
    if (is_subscribed(client, c)) {
        client->positions[c] = -1;
        printf("%d subbed to channel %d\n", client->client_id, c);

        return 0;
    } else return -1;

}

int add_message(int channel, char * message, Client_t * client) {
    Message_t m;
    m.channel = channel;
    m.client_id = client->client_id;
    m.time = time(NULL);
    printf("%d posted %s to %d\n", client->client_id, message, channel);
    sprintf(m.message, message);

    Message_t* m_ptr = message_put(channel, m);
    if (is_subscribed(client, channel)) buffer_add(m_ptr, mess_buffer);
    return 0;
}

void next_id(int c, Client_t* client) {
    Channel_t *channel = channels[c];
    if (channel->pos == client->positions[c]){
        send(client->connectionFd, "", BUFFER_SIZE, 0);
        return;
    } else if (!is_subscribed(client, c)) {
        char buffer[BUFFER_SIZE];
        sprintf(buffer, "Not subscribed to %d", c);
        send(client->connectionFd, buffer, BUFFER_SIZE, 0);
    }
    else {
        Message_t message = channel->messages[client->positions[c]++];
        send(client->connectionFd, message.message, BUFFER_SIZE, 0);
    }
}


void catch_up(Client_t* client, int c) {
    if (c == -1) {
        while (client->que.head != NULL) {
            next_time(client, &client->que);
        }
    } else {

        Channel_t* channel = channels[c];
        while (client->positions[c] != channel->pos) {
            next_id(c, client);
        }
    }
}

void remove_livefeed(Client_t* client, int channel) {
    if (channel == -1) client->livefeed_all = false;
    else client->livefeeds[channel] = false;
}

void add_livefeed(Client_t* client, int channel) {
    if (channel == -1) client->livefeed_all = true;
    client->livefeeds[channel] = true;
    catch_up(client, channel);
}


// ===========================================================================
//                               SERVER MAIN 
// ===========================================================================

int int_range(char* message, int start, int finnish, int* error) {
    if (start > finnish) *error = -1;
    char buffer[finnish - start]; 
    snprintf(buffer, finnish - start + 1, message + start);
    return strtol(buffer, NULL, 0);
}

int socket_init(int port) {
    struct sockaddr_in serverAddr;
    
    int listenFd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenFd == -1) {
        fprintf(stderr, "Failed to create listen socket\n");
        exit(1);
    }

    bzero(&serverAddr, sizeof(serverAddr));
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = htonl(INADDR_ANY);
    serverAddr.sin_port = htons(port);

    if (bind(listenFd, (struct sockaddr *)&serverAddr, sizeof(serverAddr)) == -1) {
        fprintf(stderr, "Failed to bind socket\n");
        close(listenFd);
        exit(1);
    }

    if (listen(listenFd, 5) == -1) {
        fprintf(stderr, "Failed to listen on socket\n");
        close(listenFd);
        exit(1);
    }

    return listenFd;
}

#define REQUESET_BITS 1

void chat_listen(int connectFd, Client_t *client) {
    pthread_t thread;
    pthread_create(&thread, NULL, (void * (*) (void *) )message_reader, client);

    char buffer[BUFFER_SIZE];
    char *tmp;
    while (1) {
        fflush(stdout);
        recv(client->connectionFd, buffer, BUFFER_SIZE, MSG_CONFIRM);
        send(client->connectionFd, "SUCCESS", BUFFER_SIZE, 0);

        int request = buffer[0] - '0';

        if (request == Send) {
            char _channel[3];
            snprintf(_channel, 3 + 1, buffer + REQUESET_BITS);
            int channel = strtol(_channel, NULL, 0);

            char message[BUFFER_SIZE - REQUESET_BITS - 3];
            strcpy(message, buffer + REQUESET_BITS + 3);

            printf("adding to channel %d\n", channel);
            add_message(channel, message, client);
        }

        else if (request == NextId) {
            char _channel[3];
            strncpy(_channel, buffer + REQUESET_BITS, 3);
            int channel = atoi(_channel);
            if (channel != -1)
                next_id(channel, client);
            else 
                next_time(client, &client->que);
        }

        else if (request == LivefeedId) {
            int channel = int_range(buffer, 1, 4, NULL);
            printf("client %d is now livefeeding channel %d", client->client_id, channel );
            add_livefeed(client, channel);
        }

        else if (request == Sub) {
            int channel = int_range(buffer, 1, 4, NULL);
            if (subscribe(client, channel) == -1) {
                sprintf(buffer, "ALREADY SUBBED");
            } else {
                sprintf(buffer, "SUCCESS");
            }
            send(client->connectionFd, buffer, BUFFER_SIZE, 0);

        }

        else if (request == UnSub) {
            int channel = int_range(buffer, 1, 4, NULL);
            if (unsubscribe(client, channel) == -1) {
                sprintf(buffer, "NOT SUBBED");
            } else {
                sprintf(buffer, "SUCCESS");
            }
            send(client->connectionFd, buffer, BUFFER_SIZE, 0);
        }

        if (strcmp("CLOSE", buffer) == 0) {
            printf("Closing Process for user\n");
            client_close(client);
            return;
        }
    }
}


void incoming_connections(int listenFd) {
    pid_t pid;

    Client_t *client_ptr;
    a:
    printf("Waiting for another connection\n");
    int connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }

        
    client_ptr = client_add(); // Checks for avaliable client slot

    if (client_ptr == NULL) {
        send(connectFd, "SERVER FULL", 32, 0);
        close(connectFd);
        goto a;
    } else {
        char buffer[2];
        sprintf(buffer, "%d", client_ptr->client_id);
        send(connectFd, buffer, BUFFER_SIZE, 0);

        pid = fork();
        if (pid != 0) {
            close(connectFd); 
            goto a;
        } else {
            client_ptr->connectionFd = connectFd;
            client_ptr->free = 0;
            client_ptr->pid = getpid();

            chat_listen(connectFd, client_ptr);
        }
    }
}

void incoming_connections_single_process(int listenFd) {
    pid_t pid;

    a:
    printf("Waiting for another connection\n");
    int connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }

    char buffer[2];
    sprintf(buffer, "%d", 0);
    send(connectFd, buffer, BUFFER_SIZE, 0);

    Client_t * client = &clients[0];
    client->connectionFd = connectFd;
    client->pid= getpid();
    client->buffer_pos = mess_buffer->writer_pos;
    client->que.head = NULL;
    client->que.tail = NULL;

    printf("Client %d connected\n", client->client_id);
    chat_listen(connectFd, &clients[0]);
}

void chat_shutdown() {
    printf("bye\n");
    close(LISTENFD);
    client_close_all();
    channel_close();
    unlink(SHARED_PRCOESS_NAME);
    unlink(SHARED_CHAT_NAME);
    unlink(MESSAGE_QUE_NAME);
    exit(0);
}


int main(int argc, char const *argv[])
{
    shared_memory_init();
    channel_init();
    client_init();
    que_memory_init();
    signal(SIGINT, chat_shutdown);
    if (argc != 2) {
        printf("server takes in 1 inputs, you have %d\n", argc);
        return -1;
    }

    int port = atoi(argv[1]);
    printf("Creating server on port %d\n", port);

    int listenFd = socket_init(port);

    LISTENFD = listenFd;
    incoming_connections_single_process(listenFd);
    // incoming_connections(listenFd);
    chat_shutdown();
    return 0;
}
