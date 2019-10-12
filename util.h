enum Request {
    Send, Sub, UnSub, NextId, LivefeedId, NextNotUsed, LivefeedNotUSed, Bye
};

#define BUFFER_SIZE 64
#define REQ_BUF_SIZE 10
#define MESSAGE_SIZE 1024
