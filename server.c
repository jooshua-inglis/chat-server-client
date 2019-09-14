#include "util.h"
#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <signal.h>



#define BUFFER_SIZE 32

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

void chat_listen(int listenFd) {
    char buffer[BUFFER_SIZE];

    for (;;) {
        int connectFd = accept(listenFd, NULL, NULL);
        if (connectFd == -1) {
            fprintf(stderr, "Failed to accept connection\n");
            close(listenFd);
            exit(1);
        }
        printf("connected\n");

        // sprintf(buffer, "connected");
        send(connectFd, "connected", BUFFER_SIZE, 0);

        while (1) {
            recv(connectFd, buffer, BUFFER_SIZE, MSG_CONFIRM);
            printf("Received \"%s\" from client!\n", buffer);
            // sprintf(buffer, "SUCCESS");
            // send(connectFd, buffer, BUFFER_SIZE, 0);
        }

        if (shutdown(connectFd, SHUT_RDWR) == -1) {
            fprintf(stderr, "Failed to shutdown socket\n");
            close(listenFd);
            close(connectFd);
            exit(1);
        }
        close(connectFd);
    }
    
}

void chat_shutdown() {
    printf("bye\n");
    close(LISTENFD);
    exit(0);
}

int main(int argc, char const *argv[])
{
    signal(SIGINT, chat_shutdown);
    if (argc != 2) {
        printf("server takes in 1 inputs, you have %d\n", argc);
        return -1;
    }

    int port = atoi(argv[1]);
    printf("Creating server on port %d\n", port);

    int listenFd = socket_init(port);

    LISTENFD = listenFd;
    chat_listen(listenFd);


    return 0;
}
