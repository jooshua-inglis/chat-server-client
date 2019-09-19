CC = c99
CFLAGS = -Wall -lrt -g
serverObjects = util.o
clientObjects = util.o 

all: server client 

server: util.o server.c
	${CC} $@.c ${CFLAGS} ${serverObjects} -o $@

client: util.o client.c
	${CC} $@.c ${CFLAGS} ${clientObjects} -o $@

util.o: util.h

clean:
	rm -f *.o
	rm -f server client
