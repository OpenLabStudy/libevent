CFLAGS  = -Wall -O2
LDFLAGS = -levent

all: eventServer eventClient

eventServer: eventServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ eventServer.c $(LDFLAGS)

eventClient: eventClient.c protocol.h
	$(CC) $(CFLAGS) -o $@ eventClient.c $(LDFLAGS)

clean:
	rm -f eventServer eventClient

