CFLAGS  = -Wall -O2
LDFLAGS = -levent

all: tcpSvr tcpCln tcpClient

eventServer: eventServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ eventServer.c $(LDFLAGS)

eventClient: eventClient.c protocol.h
	$(CC) $(CFLAGS) -o $@ eventClient.c $(LDFLAGS)

trackingCtrlApp: trackingCtrlApp.c protocol.h
	$(CC) $(CFLAGS) -o $@ trackingCtrlApp.c $(LDFLAGS)

tcpServer: tcpServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ tcpServer.c $(LDFLAGS)

tcpClient: tcpClient.c protocol.h
	$(CC) $(CFLAGS) -o $@ tcpClient.c $(LDFLAGS)

udsServer: udsServer.c protocol.h
	$(CC) $(CFLAGS) -o $@ udsServer.c $(LDFLAGS)

udsClient: udsClient.c protocol.h
	$(CC) $(CFLAGS) -o $@ udsClient.c $(LDFLAGS)

tcpSvr: tcpSvr.c frame.h icdCommand.h frame-io.c frame-io.c tcpSession.c tcpSession.h
	$(CC) $(CFLAGS) -o $@ tcpSvr.c frame-io.c tcpSession.c $(LDFLAGS)

tcpCln: tcpCln.c frame.h icdCommand.h frame-io.c frame-io.c  tcpSession.c tcpSession.h
	$(CC) $(CFLAGS) -o $@ tcpCln.c frame-io.c tcpSession.c $(LDFLAGS)	

clean:
	rm -f eventServer eventClient trackingCtrlApp udsServer udsClient tcpServer tcpClient tcpSvr tcpCln

