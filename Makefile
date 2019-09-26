CC = c99
CFLAGS = -lrt -g -pthread 

all: server client 

server: server.c
	${CC} $@.c ${CFLAGS} ${serverObjects} -o $@

client: client.c
	${CC} $@.c ${CFLAGS} ${clientObjects} -o $@

scratch: scratch.c
	${CC} $@.c ${CFLAGS} ${clientObjects} -o $@

clean:
	rm -f *.o
	rm -f server client
