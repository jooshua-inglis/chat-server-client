#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <signal.h>
#include <semaphore.h>

#include "util.h"
#include "client.h"


int main(int argc, char **argv) {
    signal(SIGINT, handle_interrupt);

    if (argc != 3) {
        printf("server takes in 2 inputs, you have %d\n", argc - 1);
        return -1;
    }

    int port = atoi(argv[2]);
    char *serverName = argv[1];
    user_t user;

    user_int(&user);
    connect_to_server(&user, serverName, port);
    livefeed_init(&user);
    user_input(&user);
    quit(&user);

    return 0;
}
