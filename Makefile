CC = c99
CFLAGS = -Wall -g
LDLIBS = -lrt -pthread 

all: server client 

server: server.c util.c util.h mutex.c mutex.h

client: client.c util.c util.h mutex.c mutex.h

clean:
	rm -f server client
