#include "util.h"
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


#define BUFFER_SIZE 32
int PARENT_ID;
int LISTENFD;

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

// int await_connection() {
//     const int size = 1;
//     const char *name = "con accept";

//     int shm_fd;
//     void *ptr;

//     shm_fd = shm_open(name, O_RDONLY, 0666);
//     ptr = mmap(0, size, PROT_READ, MAP_SHARED, shm_fd, 0);
//     while 
//         sleep(1);

//     }
//     shm_unlink();
// }


void chat_listen(int connectFd) {
    char buffer[BUFFER_SIZE];

    while (1) {
        recv(connectFd, buffer, BUFFER_SIZE, MSG_CONFIRM);
        printf("Received \"%s\" from client!\n", buffer);
        
        if (strcmp("CLOSE", buffer) == 0) {
            printf("Closing Process for user\n");
            close(connectFd);
            close(LISTENFD);
            return;
        }
        sprintf(buffer, "SUCCESS");
        send(connectFd, buffer, BUFFER_SIZE, 0);
    }
}

void incoming_connections(int listenFd) {
    a:
    printf("Waiting for another connection\n");
    int connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }
    printf("connected\n");
    send(connectFd, "connected", BUFFER_SIZE, 0);
    send(connectFd, "6", 8, 0);


    if (fork() != 0) {
        close(connectFd);
        goto a;
    } else {
        chat_listen(connectFd);
    }
    
} 

void chat_shutdown() {
    printf("bye\n");
    close(LISTENFD);
    exit(0);
}

int main(int argc, char const *argv[])
{
    PARENT_ID = getpid();
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
