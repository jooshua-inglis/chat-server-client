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
#include <signal.h>
#include "util.h"
#include <stdbool.h>
#include <pthread.h>
#include <semaphore.h>
#include <sys/select.h>


int exiting = 0;

// ==============================================================================
//                              USER AND CONNECTIONS
// ==============================================================================

typedef struct next_job Next_job_t;
typedef struct list List_t;

struct list {
    Next_job_t* head;
    Next_job_t* tail;
};

typedef struct user {
    int chanels[256];
    int connectionFd;
    struct sockaddr_in* server_address;
    int address_size;
    int client_id;

    sem_t sem;
    List_t list;

    pthread_mutex_t port_mutex;
} User_t;


int connect_to_server(char *server_name, int port, User_t *user_ptr)
{
    struct sockaddr_in serverAddr;

    int sockFd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockFd == -1)
    {
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

    char buffer[BUFFER_SIZE];
    printf("Getting confimation");
    recv(sockFd, buffer, BUFFER_SIZE, 0);
    printf(" done\n");

    if (strcmp(buffer, "SERVER FULL") == 0) {
        printf("Server full");
        close(sockFd);
        exit(1);
    }

    
    printf("Connected to server\nYour id is %s\n", buffer);
    
    user_ptr->client_id = atoi(buffer);
    user_ptr->server_address = &serverAddr;
    user_ptr->address_size = sizeof(serverAddr);
    user_ptr->connectionFd = sockFd;

    return sockFd;
}

void user_int(User_t* user_ptr) {
    for (int i = 0; i < 256; i++) {
        user_ptr->chanels[i] = 0;
    }
    pthread_mutex_init(&user_ptr->port_mutex, NULL);
    pthread_mutex_unlock(&user_ptr->port_mutex);
}


// ==============================================================================
//                                    REQUESTS
// ==============================================================================


int send_data(User_t* user, char* data) {
    char buffer[BUFFER_SIZE];
    int sockFd = user->connectionFd;
    int err;

    snprintf(buffer, BUFFER_SIZE, data);

    pthread_mutex_lock(&user->port_mutex);

    if (send(sockFd, buffer, BUFFER_SIZE, 0) == -1) {
        printf("failed to send message\n");
        return -1;
    }
    recv(sockFd, buffer, BUFFER_SIZE, 0);

    pthread_mutex_unlock(&user->port_mutex);

    if (strcmp(buffer, "SUCCESS"))
    {
        printf("Failed to varify message\n");
        return -1;
    }
    return 0;
}

int recive_data(User_t* user, int cha, char* data) {

}


int subscription(int channelId, User_t* user, int request) {
    if (channelId > 255 || channelId < 0) {
        printf("Invalid channel: %d", channelId);
        return -2;
    }

    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "%d%03d", request, channelId);
    send_data(user, buffer);
    pthread_mutex_lock(&user->port_mutex);
    recv(user->connectionFd, buffer, BUFFER_SIZE, 0);
    if (strcmp(buffer, "SUCCESS") == 0) {
        pthread_mutex_unlock(&user->port_mutex);

        return 0;
    }
    pthread_mutex_unlock(&user->port_mutex);
    return -1;

}

void subscribe_to(int channelId, User_t* user) {
    int error = subscription(channelId, user, Sub);
    if (error == 0) {
        printf("Subscribed to channel %d\n", channelId);
    } else if(error == -1) {
        printf("Already subscribed to channel %d\n", channelId);
    }
}

void unsubscribe_from(int channelId, User_t* user) {
    int error = subscription(channelId, user, UnSub);
    if (error == 0) {
        printf("Unsubscribed to channel %d\n", channelId);
    } else if(error == -1) {
        printf("Not subscribed to channel %d\n", channelId);
    }
}

void list_channels(User_t* user) {
    // TODO implement list channels
}

// If channelId is -1 then get the message of all the channels
void get_next_message(int channelId, User_t* user) {
    char buffer[BUFFER_SIZE];
    sprintf(buffer, "%d%03d", NextId, channelId);
    send_data(user, buffer);
    pthread_mutex_lock(&user->port_mutex);
    recv(user->connectionFd, buffer, BUFFER_SIZE, 0);
    if (buffer[0] == '\0') {
        printf("\rsend nothing\n> ");
    } else {
        printf("\r%s\n> ", buffer);
    }
    pthread_mutex_unlock(&user->port_mutex);

    fflush(stdout);   
}

void send_message(int channel, User_t* user, char *message) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "%d%03d%s", Send, channel, message);
    send_data(user, buffer);
}


void live_feed(int channelId, User_t* user) {
    char buffer[BUFFER_SIZE];
    snprintf(buffer, BUFFER_SIZE, "%d%03d", LivefeedId, channelId);
    send_data(user,buffer);
    pthread_mutex_lock(&user->port_mutex);
    recv(user->connectionFd, buffer, BUFFER_SIZE, 0);
    printf("\r%s\n> ", buffer);
    fflush(stdout);
    pthread_mutex_unlock(&user->port_mutex);
}


// ============================================================================== //
//                                 THREADED REQUESTS                              //
// ============================================================================== //


struct next_job {
    int channel;
    int request;
    Next_job_t* next;
};

struct next_thr {
    sem_t* job_sem;
    List_t* job_list;
    User_t* user;
}; 


void thread_do(User_t* user) {
    sem_t* job_sem = &user->sem; 
    List_t* job_list = &user->list;

    int channelId, request;
    while(1) {
        start:
        sem_wait(job_sem);
        if (job_list->head == NULL) {
            printf("DEUBG job isn't on head\n");
            continue;
        }

        channelId = job_list->head->channel;
        request = job_list->head->request;
        if (request == NextId) { get_next_message(channelId, user); }
        else if (request == LivefeedId) { live_feed(channelId, user); }

        Next_job_t* old_job = job_list->head;
        job_list->head = job_list->head->next;
        free(old_job);
    }
}

void add_job(User_t* user, int channel, int request) {
    Next_job_t* job = malloc(sizeof(Next_job_t));
    job->channel = channel;
    job->request = request;
    job->next = NULL;
    List_t* list = &user->list;
    if (list->head == NULL) {
        list->tail = job;
        list->head = job;
    } else {
        list->tail->next = job;
        list->tail = job;
    }
    sem_post(&user->sem);
}

void pnext(User_t* user, int channel) {
    add_job(user, channel, NextId);
}

void plivefeed(User_t* user, int channel) {
    add_job(user, channel, LivefeedId);
}

void next_init(User_t* user) {
    sem_init(&user->sem, 0, 0);

    user->list.head = NULL;
    user->list.tail = NULL;

    pthread_t thread;
    pthread_create(&thread, NULL, (void * (*) (void * )) thread_do, user);
}

void quit(User_t* user);

void livefeed_listen(User_t* user) {
    char buffer[BUFFER_SIZE];
    while(1) {
        recv(user->connectionFd, buffer, BUFFER_SIZE, MSG_PEEK);
        if (strcmp(buffer, "CLOSE") == 0) {
            quit(user);
        }
        if (pthread_mutex_trylock(&user->port_mutex) == EBUSY) continue;
        int err = recv(user->connectionFd, buffer, BUFFER_SIZE, 0);
        printf("\r%s\n> ", buffer);
        fflush(stdout);
        pthread_mutex_unlock(&user->port_mutex);
    }
}

void livefeed_init(User_t* user) {
    pthread_t thread;
    pthread_create(&thread, NULL, (void * (*) (void * )) livefeed_listen, user);
}
 
// ======================================================================== //
//                                SHELL                                     //
// ======================================================================== //

void sigin_handler(int sig) {
    exiting = 1;
}

void quit(User_t* user) {
    printf("\rBye         \n");
    char buffer[10];
    send(user->connectionFd, "CLOSE", BUFFER_SIZE, 0);
    close(user->connectionFd);
    exit(0);
}

void get_inputs(char *buffer, int buffer_size)
{
    char c;
    int position = 0;
    while (1) {
        c = getchar();
        if (c == EOF || c == '\n') {
            buffer[position] = '\0';
            return;
        }
        else {
            buffer[position] = c;
        }
        position++;
    }

    if (position >= buffer_size) {
        return;
    }
}

int get_channel_id(char* param) {
    char *err;
    int channelId = strtod(param, &err);
    if (*err != '\0' || channelId < 0 || channelId > 255) {
        printf("Invalid channel: %s\n", param);
        return -1;
    }
    else
        return channelId;
}



void user_input(User_t *user_ptr)
{
    char cmd[100];
    next_init(user_ptr);

    while (1) {
        char com[100];
        printf("\r> ");
        fflush(stdout);
        get_inputs(com, 100);
        strtok(com, " ");

        if (exiting) quit(user_ptr);


        if (strcasecmp(com, "SUB") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                printf("SUB requires a channelid to connect to \n");
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    subscribe_to(id, user_ptr);
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
                    unsubscribe_from(id, user_ptr);
                }   
            }
        }
        else if (strcasecmp(com, "CHANNELS") == 0) {
            list_channels(user_ptr);
        }
        else if (strcasecmp(com, "NEXT") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                pnext(user_ptr, -1); // change id to something ele for next without id
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    pnext(user_ptr, id);
                }   
            }
        }
        else if (strcasecmp(com, "LIVEFEED") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                plivefeed(user_ptr, -1);
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    plivefeed(user_ptr, id);
                }   
            }
        } 
        else if (strcasecmp(com, "SEND") == 0) {
            char *channel = strtok(NULL, " ");
            char *message = strtok(NULL, " ");
            if (channel == NULL || message == NULL) {
                printf("invald arguments\n");
            }
            else {
                int id = get_channel_id(channel);
                if (id != -1) send_message(id, user_ptr, message);
            }
        }
        else if (strcasecmp(com, "BYE") == 0) {
            quit(user_ptr);
        }
        else if (strcasecmp(com, "TEST") == 0) {
            send_data(user_ptr, "0001test data");
        }
        else {
            printf("Not valid command\n");
        }
    }
}

int main(int argc, char **argv)
{   
    signal(SIGINT, sigin_handler);
    
    if (argc != 3)
    {
        printf("server takes in 2 inputs, you have %d\n", argc - 1);
        return -1;
    }

    int port = atoi(argv[2]);
    char *serverName = argv[1];
    User_t user;

    user_int(&user);
    connect_to_server(serverName, port, &user);
    livefeed_init(&user);
    user_input(&user);
    quit(&user);


    return 0;   
}
