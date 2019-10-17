CC = c99
CFLAGS = -lrt -g -pthread -Wall
clientObjects = util.o
serverObjects = util.o
scratchObjects =  util.o

all: server client 

util.o: util.c util.h
	${CC} util.c ${CFLAGS} -c -o $@


server: server.c util.o
	${CC} $@.c ${CFLAGS} ${serverObjects} -o $@

client: client.c util.o
	${CC} $@.c ${CFLAGS} ${clientObjects} -o $@

scratch: scratch.c util.o
	${CC} $@.c ${CFLAGS} ${scratchObjects} -o $@


clean:
	rm -f *.o
	rm -f server client
