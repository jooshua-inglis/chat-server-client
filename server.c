#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdbool.h>
#include <time.h>
#include <pthread.h>
#include <semaphore.h>
#include <signal.h>

#include "util.h"
#include "server.h"
#include "mutex.h"

int LISTEN_FD;
bool debug = false;

// ===========================================================================
//                                   CHAT LOG
// ===========================================================================

channel_t* channels[MAX_CHANNELS];

channel_t* channel_shm_init(int i) {
    channel_t* channel;
    char name[8];
    sprintf(name, SHARED_CHAT_NAME, i);
    int shm_sg = shm_open(name, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_sg, sizeof(channel_t));
    channel = mmap(0, sizeof(channel_t), PROT_WRITE, MAP_SHARED, shm_sg, 0);
    sprintf(channel->shm_name, "%s", name);
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
        rw_mutex_init(&channels[i]->mutex);
    }
}

message_t* message_put(int channel , message_t message) {
    channel_t* c = channels[channel];
    message.pos = c->pos;

    write_lock(&c->mutex);
    c->messages[c->pos] = message;
    write_unlock(&c->mutex);

    int pos = c->pos;
    c->pos = (c->pos + 1) % CHANNEL_SIZE;
    return &c->messages[pos];
}

void increment_channel(client_t* client, int c) {
    client->positions[c] = (client->positions[c]+1)%CHANNEL_SIZE;

}

message_t* get_next_message(client_t* client, int c, bool incr) {
    channel_t* channel = channels[c];

    message_t* output = &channel->messages[client->positions[c]];
    if (incr) {
        increment_channel(client, c);
    }
    return output;
}


// ===========================================================================
//                                   CLIENT
// ===========================================================================


client_t *clients;
int processes_mem;

void client_shm_init() {
    processes_mem = shm_open(SHARED_PROCESS_NAME, O_CREAT | O_RDWR, 0666);
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
        if (clients[i].free) {
            return &clients[i];
        }
    }
    return NULL;
}

void client_close(client_t* client) {
    close(client->connectionFd);
    close(LISTEN_FD);
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


bool is_livefeed(client_t* client, int channel) {
    return client->livefeed_all || client->livefeeds[channel];
}

bool _message_read(int mess_pos, int read_pos, int write_pos) {
    if (read_pos == write_pos) {
        return true;
    }
    else if (write_pos > read_pos) {
        return !(read_pos <= mess_pos && mess_pos < write_pos);
    }
    else {
        return write_pos <= mess_pos && mess_pos < read_pos;
    }
}


bool message_read(client_t* client, message_t* message) {
    int mess_pos = message->pos;
    int read_pos = client->positions[message->channel];
    int write_pos = channels[message->channel]->pos;

    return _message_read(mess_pos, read_pos, write_pos);
}


// ===========================================================================
//                                   MESSAGE QUE
// ===========================================================================


struct message_buffer* mess_buffer;

void que_shm_init() {
    int shm_sg = shm_open(MESSAGE_QUE_NAME, O_CREAT | O_RDWR, 0666);
    ftruncate(shm_sg, sizeof(struct message_buffer));
    mess_buffer = mmap(0, sizeof(struct message_buffer), PROT_WRITE, MAP_SHARED, shm_sg, 0);
    mess_buffer->writer_pos = 0;
    rw_mutex_init(&mess_buffer->lock);
}

void buffer_add(message_t* message, struct message_buffer* buffer, int channel) {
    write_lock(&mess_buffer->lock);

    struct new_message new = {channel, message->time};
    buffer->buffer[buffer->writer_pos++] = new;

    write_unlock(&mess_buffer->lock);

    for (int i = 0; i < MAX_USES; ++i) {
        if (clients[i].pid != 0) sem_post(&clients[i].buffer_sem);
    }
}



void que_add(client_t* client) {
    message_que_t* que = &client->que;
    message_node_t* node = malloc(sizeof(message_node_t));

    read_lock(&mess_buffer->lock);

    struct new_message new = mess_buffer->buffer[client->buffer_pos++];
    int c = new.channel;
    int time = new.time;

    read_unlock(&mess_buffer->lock);
    channel_t* channel = channels[c];
    read_lock(&channel->mutex);

    if (channel->pos == client->positions[c]) {
        read_unlock(&channel->mutex);
        return;
    }

    node->channel = channel;
    node->channel_id = c;
    node->time = time;
    node->next = NULL;

    if (is_livefeed(client, c) && is_subscribed(client, c)) {
        char buffer[MESSAGE_SIZE + 5];
        sprintf(buffer, "%d: %s", c, get_next_message(client, c, true)->message);
        send(client->connectionFd, buffer, MESSAGE_SIZE + 5, 0);
    }
    else if (que->head == NULL) {
        que->head = node;
        que->tail = node;
    } 
    else {
        que->tail->next = node;
        que->tail = node;
    }

    read_unlock(&channel->mutex);
}

void message_reader(client_t* client) {
    while(1) {
        sem_wait(&client->buffer_sem);
        if (client->buffer_pos == mess_buffer->writer_pos) {
            continue;
        }
        else {
            que_add(client);
        }
    }
}


// ===========================================================================
//                                   REQUESTS
// ===========================================================================


void return_data(client_t* client, char* data, int data_size, int code) {
    char buffer[REQ_BUF_SIZE];
    sprintf(buffer, "%05d%04d", data_size, code);
    send(client->connectionFd, buffer, REQ_BUF_SIZE, 0);
    if (data_size > 0 ) {
        send(client->connectionFd, data, data_size, 0);
    }
}

int subscribe(client_t* client, int c) {
    int code;
    if (!is_subscribed(client, c)) {
        client->positions[c] = channels[c]->pos;
        printf("[%d] subbed to channel %d\n", client->client_id, c);
        code = 0;
    } else {
        code = -1;
    }
    return_data(client, NULL, 0, code);
    return code;
}

int unsubscribe(client_t* client, int c) {
    int code;
    if (is_subscribed(client, c)) {
        client->positions[c] = -1;
        printf("%d subbed to channel %d\n", client->client_id, c);
        code = 0;
    } else {
        code = -1;
    }
    return_data(client, NULL, 0, code);
    return code;
}

int add_message(client_t *client, int channel, char *message) {
    message_t m;
    m.channel = channel;
    m.client_id = client->client_id;
    m.time = time(NULL);
    printf("[%d] posted %s to channel %d\n", client->client_id, message, channel);
    sprintf(m.message, "%s", message);

    message_t* m_ptr = message_put(channel, m);

    if (is_subscribed(client, channel) && !debug) {
        client->positions[channel] = (client->positions[channel] + 1) % CHANNEL_SIZE;
    }

    if (client->positions[channel] == (channels[channel]->pos+1)%CHANNEL_SIZE) {
        increment_channel(client, channel);
    }

    buffer_add(m_ptr, mess_buffer, channel);

    return_data(client, NULL, 0, 0);
    return 0;
}

void next_time(client_t* client, message_que_t *m) {
    if (m->head == NULL ) {
        return_data(client, NULL, 0, 2);
        return;
    }
    message_node_t* node = m->head;
    read_lock(&node->channel->mutex);
    
    message_t* message = get_next_message(client, node->channel_id, false);

    if (!is_subscribed(client, node->channel_id) || message->time != node->time || message_read(client, message)) {
        m->head = node->next;
        next_time(client, m);
        read_unlock(&node->channel->mutex);
        free(node);
        return;
    } else {
        char buffer[MESSAGE_SIZE + 5];
        sprintf(buffer, "%d: %s", node->channel_id, message->message);
        return_data(client, buffer, MESSAGE_SIZE + 6, 0);
        increment_channel(client, message->channel);
        m->head = node->next;
        read_unlock(&node->channel->mutex);
        free(node);
        return;
    }
}

/*
 * Is not sync protected 
 */
void next_id(client_t *client, int c) {
    channel_t *channel = channels[c];
    if (channel->pos == client->positions[c]) { // call caught up
        return_data(client, NULL, 0, 2);
    } else if (!is_subscribed(client, c)) { // Not subbed
        return_data(client, NULL, 0, 1);
    } else {
        message_t* message = get_next_message(client, c, true);
        return_data(client, message->message, MESSAGE_SIZE, 0);
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
        return_data(client, NULL, 0, 1);
    } else {
        return_data(client, buffer, strlen(buffer) + 1, 0);
    }
}

void catch_up_all(client_t* client) {
    while (client->que.head != NULL) {
        message_node_t* node = client->que.head;
        read_lock(&node->channel->mutex);

        message_t* message = get_next_message(client, node->channel_id, false);

        if (!is_subscribed(client, node->channel_id) || message->time != node->time || message_read(client, message)) {
            client->que.head = node->next;
            free(node);
            read_unlock(&node->channel->mutex);
        } else {
            char buffer[MESSAGE_SIZE + 5];
            sprintf(buffer, "%d: %s", message->channel, message->message);
            send(client->connectionFd, buffer, MESSAGE_SIZE + 5, 0);
            increment_channel(client, message->channel);
            client->que.head = node->next;
            read_unlock(&node->channel->mutex);
            free(node);
        }
    }
}

void catch_up(client_t* client, int c) {
    channel_t* channel = channels[c];
    read_lock(&channel->mutex);

    while (client->positions[c] != channel->pos) {
        char buffer[MESSAGE_SIZE + 5];
        sprintf(buffer, "%d: %s", c, get_next_message(client, c, true)->message);

        send(client->connectionFd, buffer, MESSAGE_SIZE+5, 0);
    }
    read_unlock(&channel->mutex);
    
}

void add_livefeed(client_t* client, int channel) {
    if (channel == -1) {
        return_data(client, NULL, 0, 0);
        catch_up_all(client);
        client->livefeed_all = true;
    } else if (is_subscribed(client, channel)) {
        if (client->livefeeds[channel]) {
            return_data(client, NULL, 0, 2);
            return;
        }
        client->livefeeds[channel] = true;
        return_data(client, NULL, 0, 0);
        
        catch_up(client, channel);

        printf("[%d] is now livefeeding channel %d\n", client->client_id, channel );
    } else {
        return_data(client, NULL, 0, 1);
    }
}

void stop(client_t* client) {
    for (int i = 0; i < MAX_CHANNELS; ++i) {
        client->livefeeds[i] = false;
    }
    return_data(client, NULL, 0, 0);
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

void exit_wait(client_t* client) {
    sem_wait(&client->exit_sem);
    send(client->connectionFd, "CLOSE", REQ_BUF_SIZE, 0);
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

void chat_listen(client_t *client) {
    pthread_t thread;
    pthread_create(&thread, NULL, (void * (*) (void *) )message_reader, client);

    char req_buffer[REQ_BUF_SIZE];
    printf("[%d] has joined the server\n", client->client_id);

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

            add_message(client, channel, buffer);
        }

        else if (request == Next) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            if (channel != -1) {
                read_lock(&channels[channel]->mutex);
                next_id(client, channel); 
                read_unlock(&channels[channel]->mutex);

            } else {
                next_time(client, &client->que);
            }
        }

        else if (request == Livefeed) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            add_livefeed(client, channel);
        }

        else if (request == Sub) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            subscribe(client, channel);
        }

        else if (request == UnSub) {
            int channel = int_range(req_buffer, 1, 4, NULL);
            unsubscribe(client, channel);
        }

        else if (request == List) {
            list_sub(client);
        }
        else if (request == Stop) {
            stop(client);
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

    client_ptr = client_add();

    if (client_ptr == NULL) {
        send(connectFd, "SERVER FULL", REQ_BUF_SIZE, 0);
        close(connectFd);
        goto a;
    }
    else {
        char buffer[2];
        sprintf(buffer, "%d", client_ptr->client_id);
        send(connectFd, buffer, REQ_BUF_SIZE, 0);

        pid = fork();
        if (pid != 0) {
            close(connectFd);
            goto a;
        }
        else {
            client_init(client_ptr, connectFd);
            chat_listen(client_ptr);
        }
    }
}

void incoming_connections_single_process(int listenFd) {
    int connectFd = accept(listenFd, NULL, NULL);

    if (connectFd == -1) {
        fprintf(stderr, "Failed to accept connection\n");
    }

    char buffer[2];
    sprintf(buffer, "%d", 0);
    send(connectFd, buffer, REQ_BUF_SIZE, 0);

    client_t * client = &clients[0];
    client_init(client, connectFd);

    printf("Client %d connected\n", client->client_id);
    chat_listen(client);
}

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

#ifndef SERVER_MAIN
#define SERVER_MAIN

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

#endif