CFLAGS  = -Wall -O2
LDFLAGS = -levent

all:   tcpServer udsServer udsClient

eventServer: eventServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ eventServer.c $(LDFLAGS)

eventClient: eventClient.c protocol.h
	$(CC) $(CFLAGS) -o $@ eventClient.c $(LDFLAGS)

trackingCtrlApp: trackingCtrlApp.c protocol.h
	$(CC) $(CFLAGS) -o $@ trackingCtrlApp.c $(LDFLAGS)

tcpServer: tcpServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ tcpServer.c $(LDFLAGS)

udsServer: udsServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ udsServer.c $(LDFLAGS)

udsClient: udsClient.c protocol.h
	$(CC) $(CFLAGS) -o $@ udsClient.c $(LDFLAGS)

clean:
	rm -f eventServer eventClient trackingCtrlApp udsServer udsClient tcpServer

