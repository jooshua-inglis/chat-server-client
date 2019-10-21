#include <stdio.h>
#include <stdlib.h>
#include <strings.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <errno.h>
#include <pthread.h>
#include <semaphore.h>

#include "util.h"
#include "client.h"


int exiting = 0;

// ==============================================================================
//                              USER AND CONNECTIONS
// ==============================================================================



int connect_to_server(user_t *user_ptr, char *server_name, int port) {
    struct sockaddr_in serverAddr;

    int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd == -1) {
        fprintf(stderr, "Failed to create socket\n");
        exit(1);
    }


    bzero(&serverAddr, sizeof(serverAddr));
    inet_pton(AF_INET, server_name, &serverAddr.sin_addr);
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(port);


    if (connect(sockFd, (struct sockaddr *) &serverAddr, sizeof(serverAddr)) == -1) {
        fprintf(stderr, "Failed to connect initi\n");
        close(sockFd);
        exit(1);
    }

    char buffer[REQ_BUF_SIZE];
    printf("Getting confirmation");
    recv(sockFd, buffer, REQ_BUF_SIZE, 0);
    printf(" done\n");

    if (strcmp(buffer, "SERVER FULL") == 0) {
        printf("Server full");
        close(sockFd);
        exit(1);
    }

    printf("Connected to server\nYour id is %s\n", buffer);
    user_ptr->connectionFd = sockFd;

    return sockFd;
}

void user_int(user_t* user) {
    for (int i = 0; i < 256; i++) {
        user->channels[i] = 0;
    }
    pthread_mutex_init(&user->port_mutex, NULL);
    pthread_mutex_unlock(&user->port_mutex);
}


// ==============================================================================
//                                    REQUESTS
// ==============================================================================


int send_request(user_t* user, char* data) {
    int sockFd = user->connectionFd;

    if (send(sockFd, data, REQ_BUF_SIZE, 0) == -1) {
        printf("failed to send message\n");
        return -1;
    }
    recv(sockFd, data, REQ_BUF_SIZE, 0);

    if (strcmp(data, "0") != 0) {
        printf("Failed to make request\n");
        close(sockFd);
        exit(0);
    }
    return 0;
}


struct request_details {
    int request;
    int channel;
    char* data;
    int data_size;
};

/**
 * request is the request type, channel is the channel the user wants, data is the data
 * to be sent, NULL if no data, data_size is the size of data
 * 
 * Returns the return code from request of the data to be send back from the server, 0 if no data
 */
int request(user_t* user, struct request_details details, size_t* size) {
    char buffer[REQ_BUF_SIZE];
    snprintf(buffer, REQ_BUF_SIZE, "%d%03d%03d", details.request, details.channel, details.data_size);
    send_request(user, buffer);
    if (details.data_size > 0 && details.data != NULL) {
        send(user->connectionFd, details.data, details.data_size, 0);
    }

    recv(user->connectionFd, buffer, REQ_BUF_SIZE, 0);
    if (size != NULL) {
        *size = int_range(buffer, 0, 5, NULL);
    }
    return int_range(buffer, 5, 9, NULL);
}

int subscription(user_t *user, int channelId, int req) {
    if (channelId > 255 || channelId < 0) {
        printf("Invalid channel: %d", channelId);
        return -2;
    }
    struct request_details details;
    details.request = req;
    details.channel = channelId;
    details.data_size = 0;

    return request(user, details, NULL);
}

void subscribe(user_t *user, int channelId) {
    int error = subscription(user, channelId, Sub);
    if (error == 0) {
        printf("Subscribed to channel %d\n", channelId);
    } else if(error == -1) {
        printf("Already subscribed to channel %d\n", channelId);
    }
}

void unsubscribe(user_t *user, int channelId) {
    int error = subscription(user, channelId, UnSub);
    if (error == 0) {
        printf("Unsubscribed to channel %d\n", channelId);
    } else if (error == -1) {
        printf("Not subscribed to channel %d\n", channelId);
    }
}

void list(user_t* user) {
    struct request_details details;
    details.request = List;
    details.data_size = 0;
    details.data = NULL;

    size_t size;
    if (request(user, details, &size) == 0) {
        char buffer[size];
        recv(user->connectionFd, buffer, size, 0);
        printf("\rSubscribed to %s\n> ", buffer);
    } else {
        printf("\rNo subscriptions\n> ");
    }
}

// If channelId is -1 then get the message of all the channels
void next(user_t *user, int channelId) {
    struct request_details details;
    details.request = Next;
    details.channel = channelId;
    details.data_size = 0;

    size_t size;
    int code = request(user, details, &size);

    if (code == 2) {
        printf("\rAll caught up\n> ");
    } else if (code == 1) {
        printf("\rNot Subbed\n> ");
    } else {
        char buffer[size];
        recv(user->connectionFd, buffer, size, 0);
        printf("\r%s\n> ", buffer);
    }
    fflush(stdout);
}

void send_message(int channel, user_t* user, char *message) {
    int length = (int) strlen(message);
    char buffer[MESSAGE_SIZE];

    for (int i = 0; i < length; i+= MESSAGE_SIZE) {
        snprintf(buffer, MESSAGE_SIZE, "%s", message+i);
        struct request_details details;
        details.request = Send;
        details.channel = channel;
        details.data = buffer;
        details.data_size = MESSAGE_SIZE;
        request(user, details, NULL);
    }
}


void livefeed(int channelId, user_t* user) {
    struct request_details details;
    details.channel = channelId;
    details.request = Livefeed;
    details.data_size = 0;

    int code = request(user, details , NULL);

    if (code == 0) {
        if (channelId == -1) {
            printf("\rLivefeeding all\n> ");
        } else {
            printf("\rLivefeeding %d\n> ", channelId);
        }
    } else if (code == 1) {
        printf("\rYou are not subbed to channel %d\n> ", channelId);
    } else if (code == 2) {
        printf("\rYou are already livefeeding channel %d\n> ", channelId);
    }
    fflush(stdout);
}

void stop(user_t* user) {
    struct request_details details = {.request = Stop, .data_size = 0 };
    request(user, details, NULL);
}


// ============================================================================== //
//                                 THREADED REQUESTS                              //
// ============================================================================== //

void request_que_worker(user_t* user) {
    sem_t* job_sem = &user->sem;
    list_t* job_list = &user->list;

    int channelId, request;
    while(exiting == 0) {
        sem_wait(job_sem);
        if (job_list->head == NULL) {
            printf("DEBUG job isn't on head\n");
            continue;
        }

        channelId = job_list->head->channel;
        request = job_list->head->request;
        pthread_mutex_lock(&user->port_mutex);
        if (request == Next) {
            next(user, channelId);
        } else if (request == Livefeed) {
            livefeed(channelId, user);
        }
        pthread_mutex_unlock(&user->port_mutex);

        next_job_t* old_job = job_list->head;
        job_list->head = job_list->head->next;
        free(old_job);
    }
}

void que_request(user_t* user, int channel, int request) {
    next_job_t* job = malloc(sizeof(next_job_t));
    job->channel = channel;
    job->request = request;
    job->next = NULL;
    list_t* list = &user->list;
    if (list->head == NULL) {
        list->tail = job;
        list->head = job;
    } else {
        list->tail->next = job;
        list->tail = job;
    }
    sem_post(&user->sem);
}

void que_next(user_t* user, int channel) {
    que_request(user, channel, Next);
}

void que_livefeed(user_t* user, int channel) {
    que_request(user, channel, Livefeed);
}

void request_que_init(user_t* user) {
    sem_init(&user->sem, 0, 0);

    user->list.head = NULL;
    user->list.tail = NULL;

    pthread_t thread;
    pthread_create(&thread, NULL, (void *(*)(void *)) request_que_worker, user);
}

void livefeed_listen(user_t* user) {
    char buffer[MESSAGE_SIZE + 5];
    while(exiting == 0) {
        recv(user->connectionFd, buffer, MESSAGE_SIZE + 5, MSG_PEEK);
        if (strcmp(buffer, "CLOSE") == 0) {
            quit(user);
        }
        if (pthread_mutex_trylock(&user->port_mutex) == EBUSY) {
            continue;
        }
        recv(user->connectionFd, buffer, MESSAGE_SIZE + 5, 0);

        printf("\r%s\n> ", buffer);
        fflush(stdout);
        pthread_mutex_unlock(&user->port_mutex);
    }
}

void livefeed_init(user_t* user) {
    pthread_t thread;
    pthread_create(&thread, NULL, (void * (*) (void * )) livefeed_listen, user);
}

// ======================================================================== //
//                                SHELL                                     //
// ======================================================================== //

void handle_interrupt(int sig) {
    exiting = 1;
}

void quit(user_t* user) {
    printf("\rBye         \n");
    send(user->connectionFd, "CLOSE", REQ_BUF_SIZE, 0);
    close(user->connectionFd);
    exit(0);
}

char* get_inputs() {
    char c;
    int position = 0;
    size_t buff_size = 100;
    char* buffer = malloc(100 * sizeof(char));
    while (1) {
        c = (char) getchar();
        if (position >= buff_size) {
            buff_size += 100;
            buffer = realloc(buffer, buff_size * sizeof(char));
        }
        if (c == EOF || c == '\n') {
            buffer[position] = '\0';
            return buffer;
        } else {
            buffer[position] = c;
        }
        position++;
    }
}

int get_channel_id(char* param) {
    char *err;
    int channelId = (int) strtod(param, &err);
    if (*err != '\0' || channelId < 0 || channelId > 255) {
        printf("Invalid channel: %s\n", param);
        return -1;
    }
    else {
        return channelId;
    }
}

void user_input(user_t *user) {
    request_que_init(user);
    char* com;

    while (exiting == 0) {
        printf("\r> ");
        fflush(stdout);
        pthread_mutex_unlock(&user->port_mutex);

        com = get_inputs();
        pthread_mutex_lock(&user->port_mutex);
        strtok(com, " ");

        if (exiting) quit(user);

        if (strcasecmp(com, "SUB") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                printf("SUB requires a channel id to connect to \n");
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    subscribe(user, id);
                }
            }
        }
        else if (strcasecmp(com, "UNSUB") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                printf("UNSUB requires a channelid to connect to \n");
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    unsubscribe(user, id);
                }
            }
        }
        else if (strcasecmp(com, "CHANNELS") == 0) {
            list(user);
        }
        else if (strcasecmp(com, "NEXT") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                que_next(user, -1); // change id to something ele for next without id
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    que_next(user, id);
                }
            }
        }
        else if (strcasecmp(com, "LIVEFEED") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                que_livefeed(user, -1);
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    que_livefeed(user, id);
                }
            }
        }
        else if (strcasecmp(com, "SEND") == 0) {
            char* channel = strtok(NULL, " ");
            char* message = strtok(NULL, "");

            if (channel == NULL || message == NULL) {
                printf("Invalid arguments\n");
            }
            else {
                int id = get_channel_id(channel);
                if (id != -1) send_message(id, user, message);
            }
        }
        else if (strcasecmp(com, "LIST") == 0) {
            list(user);
        }
        else if (strcasecmp(com, "BYE") == 0) {
            quit(user);
        }
        else if (strcasecmp(com, "STOP") == 0) {
            stop(user);
        }
        else {
            printf("Not valid command\n");
        }

        free(com);
    }
}