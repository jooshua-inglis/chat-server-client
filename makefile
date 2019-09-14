CC = c99
CFLAGS = -Wall
serverObjects = util.o
clientObjects = util.o 

all: server client 

server: util.o server.c
	${CC} $@.c ${serverObjects} -o $@

client: util.o client.c
	${CC} $@.c -g ${clientObjects} -o $@

util.o: util.h

clean:
	rm -f *.o
	rm -f server client
