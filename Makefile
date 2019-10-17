CC = c99
CFLAGS = -lrt -g -pthread
clientObjects = util.o
serverObjects = util.o

all: server client 

util.o: util.c util.h
	${CC} util.c ${CFLAGS} -c -o $@


server: server.c util.o
	${CC} $@.c ${CFLAGS} ${serverObjects} -o $@

client: client.c util.o
	${CC} $@.c ${CFLAGS} ${clientObjects} -o $@

scratch: scratch.c util.o
	${CC} $@.c ${CFLAGS} ${clientObjects} -o $@


clean:
	rm -f *.o
	rm -f server client
