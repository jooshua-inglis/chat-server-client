#ifndef CHAT_UTIL_H
#define CHAT_UTIL_H

enum Request {
    Send, Sub, UnSub, List, Next, Livefeed, Stop, Bye
};

#define REQ_BUF_SIZE 10
#define MESSAGE_SIZE 1024

int int_range(char* message, int start, int finnish, int* error);

#endif