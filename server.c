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
#include <semaphore.h>

#define MAX_USES  256
#define MAX_CHANNELS 256


int LISTENFD;


// ===========================================================================
//                                   CHAT LOG 
// ===========================================================================


#define SHARED_CHAT_NAME "CHAT%d"
#define CHANNAL_INIT_SIZE 1024

typedef struct message {
    int client_id;
    int channel;
    char message[MESSAGE_SIZE]; 
    int time;
    int pos;
} message_t;

typedef struct node node_t;

typedef struct channel {
    message_t messages[CHANNAL_INIT_SIZE];
    size_t pos;
    int shm_sg;
    char  shm_name[8];
} channel_t;


channel_t* channels[MAX_CHANNELS];
int chat_mem;

channel_t* channel_shm_init(int i) {
    channel_t* channel;
    char name[8];
    sprintf(name, SHARED_CHAT_NAME, i);
    int shm_sg = shm_open(name, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_sg, sizeof(channel_t));
    channel = mmap(0, sizeof(channel_t), PROT_WRITE, MAP_SHARED, shm_sg, 0);
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
        channels[i] = channel_shm_init(i);
        channels[i]->pos = 0;
    }
}

void message_print(message_t message) {
    printf("#%d: %s\n", message.client_id, message.message);
}

void channel_print(int channel) {
    channel_t* c = channels[channel];
    for (int i = 0; i < c->pos; ++i) {
        message_print(c->messages[i]);
    }
}

message_t* message_put(int channel , message_t message) {
    channel_t* c = channels[channel];
    message.pos = c->pos;
    c->messages[c->pos] = message; 

    int pos = c->pos;
    c->pos = (c->pos + 1) % CHANNAL_INIT_SIZE;

    return &c->messages[pos];
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
    sem_t exit_sem;
    sem_t buffer_sem;
} client_t;


#define SHARED_PRCOESS_NAME "PROCESSES"
client_t *clients;
int processes_mem;

void client_shm_init() {
    processes_mem = shm_open(SHARED_PRCOESS_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(processes_mem, MAX_USES * sizeof(client_t));
    clients = mmap(0, MAX_USES * sizeof(client_t), PROT_WRITE, MAP_SHARED, processes_mem, 0);
}


void clients_ready() {
    client_shm_init();
    for (int i = 0; i < MAX_USES; ++i) { 
        clients[i].free = true; 
        clients[i].client_id = i;
        sem_init(&clients[i].exit_sem, 1, 0);
        clients[i].pid = 0;
    }
}

// returns -1 if no child slot avaliable, else returns client id
client_t *client_add() {
    for (int i = 0; i < MAX_USES; ++i) {
        if (clients[i].free) { return &clients[i]; }
    }
    return NULL;
}

void client_close(client_t* client) {
    close(client->connectionFd);
    close(LISTENFD);
    client->free = 1;
}

void client_close_all() {
    pid_t pid;
    for (int i = 0; i < MAX_USES; ++i) {
        pid = clients[i].pid;
        if (pid) {
            sem_post(&clients[i].exit_sem);
            sem_close(&clients[i].exit_sem);
        }
    }
}

bool is_subscribed(client_t* client, int c) {
    return client->positions[c] != -1;
}

// ===========================================================================
//                                   MESSAGE QUE 
// ===========================================================================


#define MSG_QUE_BUFFER_SIZE 1000
#define MESSAGE_QUE_NAME "MESSAGE_QUE"

typedef struct message_node message_node_t;

struct message_node {
    message_t* message;
    int time;
    message_node_t* next;
};

struct message_buffer {
    message_t *buffer[MSG_QUE_BUFFER_SIZE];
    int writer_pos;
};

struct message_buffer* mess_buffer;

void que_shm_init() {
    int shm_sg = shm_open(MESSAGE_QUE_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_sg, sizeof(struct message_buffer));
    mess_buffer = mmap(0, sizeof(struct message_buffer), PROT_WRITE, MAP_SHARED, shm_sg, 0);
    mess_buffer->writer_pos = 0;
}

void buffer_add(message_t* message, struct message_buffer* buffer) {
    buffer->buffer[buffer->writer_pos++] = message;
    for (int i = 0; i < MAX_USES; ++i) {
        if (clients[i].pid != 0) sem_post(&clients[i].buffer_sem);
    }
}


bool is_livefeed(client_t* client, int channel) {
    return client->livefeed_all || client->livefeeds[channel];
}

bool _message_read(int mess_pos, int read_pos, int write_pos) { 
    if (read_pos == write_pos) return true;
    else if (write_pos > read_pos) return !(read_pos <= mess_pos && mess_pos < write_pos);
    else return write_pos <= mess_pos && mess_pos < read_pos;
}


bool message_read(client_t* client, message_t* message) {
    int mess_pos = message->pos;
    int read_pos = client->positions[message->channel];
    int write_pos = channels[message->channel]->pos;

    return _message_read(mess_pos, read_pos, write_pos);
}


message_t* que_add(client_t* client) {
    message_que_t* que = &client->que;
    message_node_t* node = malloc(sizeof(message_node_t));
    node->message = mess_buffer->buffer[client->buffer_pos++];

    if (message_read(client, node->message)) return NULL;

    node->time = node->message->time;
    node->next = NULL;

    if (is_livefeed(client, node->message->channel)) {
        send(client->connectionFd, node->message->message, MESSAGE_SIZE, 0);
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

void message_reader(client_t* client) {
    while(1) {
        sem_wait(&client->buffer_sem);
        if (client->buffer_pos == mess_buffer->writer_pos) continue;

        else {
            message_t* new_message = que_add(client);
        }
    }
}

void next_time(client_t* client, message_que_t *m) {
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


void return_data(client_t* client, char* data, int data_size) {
    char buffer[REQ_BUF_SIZE];
    sprintf(buffer, "%d", data_size);
    send(client->connectionFd, buffer, REQ_BUF_SIZE, 0);
    if (data_size > 0 ) {
        send(client->connectionFd, data, data_size, 0);
    }
}

int subscribe(client_t* client, int c) {
    if (!is_subscribed(client, c)) {
        client->positions[c] = channels[c]->pos;
        printf("[%d] subbed to channel %d\n", client->client_id, c);
        return 0;
    } else return -1;
}

int unsubscribe(client_t* client, int c) {
    if (is_subscribed(client, c)) {
        client->positions[c] = -1;
        printf("%d subbed to channel %d\n", client->client_id, c);

        return 0;
    } else return -1;

}

int add_message(int channel, char * message, client_t* client) {
    message_t m;
    m.channel = channel;
    m.client_id = client->client_id;
    m.time = time(NULL);
    printf("[%d] posted %s to channel %d\n", client->client_id, message, channel);
    sprintf(m.message, message);

    message_t* m_ptr = message_put(channel, m);

    client->positions[channel] = (client->positions[channel] + 1) % CHANNAL_INIT_SIZE;

    buffer_add(m_ptr, mess_buffer);
    return 0;
}

void next_id(int c, client_t* client) {
    channel_t *channel = channels[c];
    if (channel->pos == client->positions[c]){
        return_data(client, NULL, 0);
        return;
    } else if (!is_subscribed(client, c)) {
        return_data(client, "-1", REQ_BUF_SIZE);
        return;
    }
    else {
        message_t message = channel->messages[client->positions[c]++];
        return_data(client, message.message, MESSAGE_SIZE);
        return;
    }
}

void list_sub(client_t* client) {
    char buffer[MESSAGE_SIZE];
    bool first = true;
    size_t pos = 0;
    for (int i = 0; i<MAX_CHANNELS;++i) {
        if (is_subscribed(client, i)) {
            if (first) {
                sprintf(buffer + pos, "%d", i);
                pos += 1;
                first = false;
            } else {
                sprintf(buffer + pos, ", %d", i);
                pos += 3;
            }
        }
    }
    if (pos == 0) {
        return_data(client, "None", REQ_BUF_SIZE);
    } else {
        return_data(client, buffer, strlen(buffer) + 1);
    }
}

void catch_up(client_t* client, int c) {
    if (c == -1) {
        while (client->que.head != NULL) {
            next_time(client, &client->que);
        }
    } else {

        channel_t* channel = channels[c];
        while (client->positions[c] != channel->pos) {
            next_id(c, client);
        }
    }
}

void remove_livefeed(client_t* client, int channel) {
    if (channel == -1) client->livefeed_all = false;
    else client->livefeeds[channel] = false;
}

void add_livefeed(client_t* client, int channel) {
    char buffer[BUFFER_SIZE];
    if (channel == -1) { 
        return_data(client, "0", REQ_BUF_SIZE);
        client->livefeed_all = true;
    }
    else if (is_subscribed(client, channel)) {
        client->livefeeds[channel] = true;
        catch_up(client, channel);

        return_data(client, "0", REQ_BUF_SIZE);
        printf("[%d] is now livefeeding channel %d\n", client->client_id, channel );
    }
    else {
        return_data(client, "1", REQ_BUF_SIZE);
    }
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

void exit_wait(client_t* client) {
    sem_wait(&client->exit_sem);
    send(client->connectionFd, "CLOSE", BUFFER_SIZE, 0);
    exit(0);
}

void client_init(client_t* client, int connectFd) {
    client->connectionFd = connectFd;
    client->free = 0;
    client->pid = getpid();
    client->buffer_pos = mess_buffer->writer_pos;
    client->que.head = NULL;
    client->que.tail = NULL;
    bzero(client->livefeeds, MAX_CHANNELS * sizeof(bool));
    sem_init(&client->buffer_sem, 1, 0);
    client->livefeed_all = false;
    for (int j = 0; j < MAX_CHANNELS; ++j) {
        client->livefeeds[j] = false;
        client->positions[j] = -1;
    }

    pthread_t exit_thread;
    pthread_create(&exit_thread, NULL, (void* (*) (void *)) exit_wait, client);
}

#define REQUESET_BITS 1

void chat_listen(int connectFd, client_t *client) {
    pthread_t thread;
    pthread_create(&thread, NULL, (void * (*) (void *) )message_reader, client);

    char req_buffer[REQ_BUF_SIZE];
    // char buffer[BUFFER_SIZE];
    char *tmp;

    printf("[%d] has joinded the server\n", client->client_id);

    while (1) {
        fflush(stdout);
        recv(client->connectionFd, req_buffer, REQ_BUF_SIZE, MSG_CONFIRM);
        send(client->connectionFd, "0", REQ_BUF_SIZE, 0);

        int request = req_buffer[0] - '0';

        if (request == Send) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            int mess_size = int_range(req_buffer, 4, 8, NULL);
            
            char buffer[mess_size];
            recv(client->connectionFd, buffer, MESSAGE_SIZE, 0);
            send(client->connectionFd, "0", REQ_BUF_SIZE, 0);

            add_message(channel, buffer, client);
        }
    
        else if (request == NextId) {
            int channel = int_range(req_buffer, 1, 4, NULL);

            if (channel != -1)
                next_id(channel, client);
            else 
                next_time(client, &client->que);
        }

        else if (request == LivefeedId) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            add_livefeed(client, channel);
        }

        else if (request == Sub) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            if (subscribe(client, channel) == -1) {
                return_data(client, "1", REQ_BUF_SIZE);
            } else {
                return_data(client, "0", REQ_BUF_SIZE);
            }
        }

        else if (request == UnSub) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            if (unsubscribe(client, channel) == -1) {
                return_data(client, "1", REQ_BUF_SIZE);
            } else {
                return_data(client, "0", REQ_BUF_SIZE);
            }
        }

        else if (request == List) {
            list_sub(client);
        }

        if (strcmp("CLOSE", req_buffer) == 0) {
            printf("[%d] left the left the server\n", client->client_id);
            client_close(client);
            return;
        }
    }
}


void incoming_connections(int listenFd) {
    pid_t pid;

    client_t *client_ptr;
    int connectFd;
    a:
    connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }

        
    client_ptr = client_add(); // Checks for avaliable client slot

    if (client_ptr == NULL) {
        send(connectFd, "SERVER FULL", BUFFER_SIZE, 0);
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
            client_init(client_ptr, connectFd);
            chat_listen(connectFd, client_ptr);
        }
    }
}

void incoming_connections_single_process(int listenFd) {
    pid_t pid;
    
    int connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }

    char buffer[2];
    sprintf(buffer, "%d", 0);
    send(connectFd, buffer, BUFFER_SIZE, 0);

    client_t * client = &clients[0];
    client_init(client, connectFd);

    printf("Client %d connected\n", client->client_id);
    chat_listen(connectFd, client);
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


int main(int argc, char const *argv[]) {
    signal(SIGINT, chat_shutdown);

    if (argc < 2) {
        printf("server takes in 1 inputs, you have %d\n", argc);
        return -1;
    }

    channel_init();
    clients_ready();
    que_shm_init();

    int port = atoi(argv[1]);
    printf("Creating server on port %d\n", port);

    int listenFd = socket_init(port);
    LISTENFD = listenFd;

    if (argc == 3) incoming_connections_single_process(listenFd);
    else  incoming_connections(listenFd);

    return 0;
}
