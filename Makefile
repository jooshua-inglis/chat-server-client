CC = c99
CFLAGS = -lrt -g -pthread -Wall

all: server client 

server: server_main.c server.c server.h util.c

client: client_main.c client.c util.c

scratch: scratch.c util.c util.h

clean:
	rm -f server client
