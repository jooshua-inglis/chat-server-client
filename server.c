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

#define BUFFER_SIZE 32
#define MAX_USES  256

int LISTENFD;

// ===========================================================================
//                                   CHAT LOG 
// ===========================================================================


#define SHARED_CHAT_NAME "CHAT%d"
#define MAX_CHANNELS 256
#define CHANNAL_INIT_SIZE 1024
#define MESSAGE_SIZE 1024 * 8

typedef struct message {
    int client_id;
    int channel;
    char message[MESSAGE_SIZE]; 
    int time;
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

void message_put(int channel , Message_t message) {
    printf("adding to chat\n");
    Channel_t* c = channels[channel];
    c->messages[c->pos] = message; 
    c->pos ++;
}

// ===========================================================================
//                                   CLIENT 
// ===========================================================================

typedef struct client {
    pid_t pid;
    int client_id;
    int connectionFd;
    bool free;
    int positions[MAX_CHANNELS];
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
        bzero(clients[i].positions, MAX_CHANNELS);
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

int add_message(int channel, char * message, Client_t * client) {
    Message_t m;
    m.channel = channel;
    m.client_id = client->client_id;
    sprintf(m.message, message);
    message_put(channel, m);
}

Message_t* next(int c, Client_t* client) {
    Channel_t *channel = channels[c];
    if (channel->pos == client->positions[c]){
        return NULL;
    }
    return &channel->messages[client->positions[c]++];
}


// ===========================================================================
//                               SERVER MAIN 
// ===========================================================================

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

enum Request {
    Send, NextId, LivefeedId, Next, Livefeed, Bye
};

#define REQUESET_BITS 1

void chat_listen(int connectFd, Client_t *client) {
    char buffer[BUFFER_SIZE];
    char *tmp;
    while (1) {
        recv(client->connectionFd, buffer, BUFFER_SIZE, MSG_CONFIRM);
        send(client->connectionFd, "SUCCESS", BUFFER_SIZE, 0);

        int request = buffer[0] - '0';

        if (request == Send) {
            char _channel[3];
            snprintf(_channel, 3 + 1, buffer + REQUESET_BITS);
            int channel = strtol(_channel, NULL, 0);
            printf("%s", _channel);
            char message[BUFFER_SIZE - REQUESET_BITS - 3];
            strcpy(message, buffer + REQUESET_BITS + 3);

            printf("adding to channel %d\n", channel);
            add_message(channel, message, client);
        }

        if (request == NextId) {
            char _channel[3];
            strncpy(_channel, buffer + REQUESET_BITS, 3);
            int channel = atoi(_channel);
            
            Message_t* message = next(channel, client);
            if (message == NULL) {
                sprintf(buffer, "");
            }else {
                sprintf(buffer, message->message);
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

    Client_t client;
    a:
    printf("Waiting for another connection\n");
    int connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }

    char buffer[2];
    sprintf(buffer, "%d", 0);
    send(connectFd, buffer, BUFFER_SIZE, 0);

    client.client_id = 0;
    client.connectionFd = connectFd;
    client.free = 0;
    client.pid = getpid();

    chat_listen(connectFd, &client);
}

void chat_shutdown() {
    printf("bye\n");
    close(LISTENFD);
    client_close_all();
    channel_close();
    unlink(SHARED_PRCOESS_NAME);
    unlink(SHARED_CHAT_NAME);
    exit(0);
}


int main(int argc, char const *argv[])
{
    shared_memory_init();
    channel_init();
    client_init();
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
