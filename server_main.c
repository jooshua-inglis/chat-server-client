#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>

#include "util.h"
#include "server.h"

void chat_shutdown() {
    printf("\rShutting down server\n");
    close(LISTEN_FD);
    client_close_all();
    channel_close();
    unlink(SHARED_PROCESS_NAME);
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
    LISTEN_FD = listenFd;

    if (argc == 3) {
        debug = true;
        printf("Launched in debug mode\n");
        incoming_connections_single_process(listenFd);
    } else {
        incoming_connections(listenFd);
    }

    return 0;
}
