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
//                                   CLIENT 
// ===========================================================================

typedef struct client {
    pid_t pid;
    int client_id;
    int connectionFd;
    bool free;
} Client_t;


#define SHARED_PRCOESS_NAME "PROCESSES"
Client_t *clients;
int processes_mem;

void shared_memory_init() {
    processes_mem = shm_open(SHARED_PRCOESS_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(processes_mem, MAX_USES * sizeof(int));
    clients = mmap(0, MAX_USES * sizeof(int), PROT_WRITE, MAP_SHARED, processes_mem, 0);
}

void shared_memory_close() {
    unlink(SHARED_PRCOESS_NAME);
}

void client_init() {
    for (int i = 0; i < MAX_USES; ++i) { 
        clients[i].free = true; 
        clients[i].client_id = i;
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
    // close(LISTENFD);
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


void chat_listen(int connectFd, Client_t *client) {
    char buffer[BUFFER_SIZE];

    while (1) {
        recv(client->connectionFd, buffer, BUFFER_SIZE, MSG_CONFIRM);
        printf("Received \"%s\" from client!\n", buffer);
        
        if (strcmp("CLOSE", buffer) == 0) {
            printf("Closing Process for user\n");
            client_close(client);
            return;
        }
        sprintf(buffer, "SUCCESS");
        send(client->connectionFd, buffer, BUFFER_SIZE, 0);
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

void chat_shutdown() {
    printf("bye\n");
    close(LISTENFD);
    client_close_all();
    shared_memory_close();
    exit(0);
}


int main(int argc, char const *argv[])
{
    shared_memory_init();
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
    incoming_connections(listenFd);
    return 0;
}
