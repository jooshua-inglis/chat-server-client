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

#define BUFFER_SIZE 32

int exiting = 0;

typedef struct user {
    int chanels[256];
    int connectionFd;
    struct sockaddr_in* server_address;
    int address_size;
    int client_id;
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
    char id[8];
    printf("Getting confimation and ");
    recv(sockFd, buffer, BUFFER_SIZE, 0);
    printf("id");
    recv(sockFd, id, 8, 0);
    printf(" done\n");

    printf("%s\n", buffer);
    if (strcmp(buffer, "connected") == 0) {
        printf("Connected to server\nYour id is %s\n", id);
    }

    user_ptr->server_address = &serverAddr;
    user_ptr->address_size = sizeof(serverAddr);

    return sockFd;
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

void init_user(User_t * user) {

}

void subscribe_to(int channelId, User_t* user) {
    // TODO implemnt subscribe_to
}

void unsubscribe_from(int channelId, User_t* user) {
    // TODO implemnt unsubscribe_from
}

void list_channels(User_t* user) {
    // TODO implement list channels
}

// If channelId is -1 then get the message of all the channels
void get_next_message(int channelId, User_t* user) {
    // TODO implement list get_next_message
}

int send_data(User_t* user, char* data) {
    char buffer[BUFFER_SIZE];
    int sockFd = user->connectionFd;
    struct sockaddr_in *serverAddr = user->server_address;

    sprintf(buffer, data);
    send(sockFd, buffer, BUFFER_SIZE, 0);
    recv(sockFd, buffer, BUFFER_SIZE, 0);

    if (strcmp(buffer, "SUCCESS"))
    {
        printf("Failed to send message");
        return -1;
    }
    return 0;
}

void send_message(int channel, User_t* user, char *message)
{

}


void live_feed(int channelId, User_t* user) {

}

void sigin_handler(int sig) {
    exiting = 1;
}

void quit(User_t* user) {
    printf("\nBye\n");
    send_data(user, "CLOSE");
    shutdown(user->connectionFd, 2);
    close(user->connectionFd);
    exit(0);
}

void user_int(User_t* user_ptr) {
    for (int i = 0; i < 256; i++) {
        user_ptr->chanels[i] = 0;
    }
}


void user_input(User_t *user_ptr)
{
    char cmd[100];

    init_user(user_ptr);

    while (1) {
        char com[100];
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
                    printf("Subscribed to %d\n", id);
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
                    printf("Unsubscribing from to channel %d\n", id);
                }   
            }
        }
        else if (strcasecmp(com, "CHANNELS") == 0) {
            list_channels(user_ptr);
        }
        else if (strcasecmp(com, "NEXT") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                get_next_message(-1, user_ptr);
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    get_next_message(id, user_ptr);
                }   
            }
        }
        else if (strcasecmp(com, "LIVEFEED") == 0) {
            char *param = strtok(NULL, " ");
            if (param == NULL) {
                live_feed(-1, user_ptr);
            }
            else {
                int id = get_channel_id(param);
                if (id != -1) {
                    live_feed(id, user_ptr);
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
        else if (strcasecmp("^C", com) == 0) {
            quit(user_ptr);
        }
        else if (strcasecmp(com, "TEST") == 0) {
            send_data(user_ptr, "test data");
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

    user.connectionFd = connect_to_server(serverName, port, &user);
    send_data(&user, "test");
    user_input(&user);
    quit(&user);


    return 0;   
}
