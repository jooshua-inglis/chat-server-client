#ifndef CHAT_SERVER_H
#define CHAT_SERVER_H

#define MESSAGE_QUE_NAME "MESSAGE_QUE"
#define SHARED_CHAT_NAME "CHAT%d"
#define SHARED_PROCESS_NAME "PROCESSES"

#include <stdbool.h>

extern int LISTEN_FD;
extern bool debug;


void client_close_all();

void channel_close();

void channel_init();

void clients_ready();

void que_shm_init();

void incoming_connections_single_process(int listenFd);

void incoming_connections(int listenFd);

int socket_init(int port);

#endif //CHAT_SERVER_H
