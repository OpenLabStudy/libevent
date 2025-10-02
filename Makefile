# === Compilers & Flags ===
CC      	?= gcc
CXX     	?= g++
CFLAGS  	?= -Wall -O2
CXXFLAGS	?= -Wall -O2
LDFLAGS 	?=
LIBS_COMMON = -levent

# === Files ===
HDRS       = frame.h icdCommand.h sockSession.h
FRAME_OBJS = frame-io.o sockSession.o

# === Phony targets ===
.PHONY: all clean gtest

# 기본 빌드: udsSvr, udsCln
all: tcpSvr tcpCln udpSvr udpCln udsSvr udsCln  multicastSender multicastReceiver mCastReceiver uartTxTest uartRx

# === Regular apps ===
udsSvr: udsSvr.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udsCln: udsCln.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udsSvr.o: udsSvr.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

udsCln.o: udsCln.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<


udpSvr: udpSvr.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udpCln: udpCln.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udpSvr.o: udpSvr.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

udpCln.o: udpCln.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<



tcpSvr: tcpSvr.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

tcpSvr.o: tcpSvr.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

tcpCln: tcpCln.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

tcpCln.o: tcpCln.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<


# === Multicast apps (standalone) ===
multicastSender: multicastSender.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

multicastSender.o: multicastSender.c
	$(CC) $(CFLAGS) -c -o $@ $<	

multicastReceiver: multicastReceiver.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

multicastReceiver.o: multicastReceiver.c
	$(CC) $(CFLAGS) -c -o $@ $<

mCastReceiver: mCastReceiver.o $(FRAME_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

mCastReceiver.o: mCastReceiver.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# === Uart apps (standalone) ===
uartRx: uartRx.o
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

uartRx.o: uartRx.c
	$(CC) $(CFLAGS) -c -o $@ $<

uartTxTest: uartTxTest.o 
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

uartTxTest.o: uartTxTest.c
	$(CC) $(CFLAGS) -c -o $@ $<


# === Common objects ===
frame-io.o: frame-io.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

sockSession.o: sockSession.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# gtest 전용 빌드
# === GoogleTest Paths (given) ===
GTEST_DIR          	= /home/pcw1029/googletest
GTEST_INCLUDE_DIR  	= $(GTEST_DIR)/googletest/include
GTEST_LIB_DIR      	= $(GTEST_DIR)/build/lib

# Include/Link flags for GoogleTest
GTEST_CXXFLAGS 		= -I$(GTEST_INCLUDE_DIR)
GTEST_LDFLAGS 		= -L$(GTEST_LIB_DIR) -lgtest -lgtest_main -pthread -Wl,-rpath,$(GTEST_LIB_DIR)

gtest: mutexQueueGtest #udsSvrGtest tcpSvrGtest mutexQueueGtest
# === GoogleTest target (NO -DUDS_SVR_STANDALONE) ===

# 테스트 소스는 udsSvrGtest.cpp로 가정
udsSvrGtest: udsSvrGtest.o $(FRAME_OBJS) udsSvrNostandalone.o
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

udsSvrGtest.o: ./gtest/udsSvrGtest.cc $(HDRS)
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

# 테스트용: STANDALONE 미정의로 udsSvr.c 빌드
udsSvrNostandalone.o: udsSvr.c $(HDRS)
	$(CC) $(CFLAGS) -DGOOGLE_TEST -c -o $@ $<


tcpSvrGtest: tcpSvrGtest.o $(FRAME_OBJS) tcpSvrNostandalone.o
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

tcpSvrGtest.o: ./gtest/tcpSvrGtest.cc $(HDRS)
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

# 테스트용: STANDALONE 미정의로 udsSvr.c 빌드
tcpSvrNostandalone.o: tcpSvr.c $(HDRS)
	$(CC) $(CFLAGS) -DGOOGLE_TEST -c -o $@ $<


# === MutexQueue common object ===
mutexQueue.o: mutexQueue.c mutexQueue.h
	$(CC) $(CFLAGS) -c -o $@ $<

# === MutexQueue GoogleTest ===
mutexQueueGtest: gtest/mutexQueueGtest.o mutexQueue.o
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(GTEST_LDFLAGS) $(LDFLAGS)

mutexQueueGtest.o: ./gtest/mutexQueueGtest.cc mutexQueue.h
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<



	


# === Clean ===
clean:
	rm -f *.o udsSvr udsCln udsSvrGtest trackingCtrlApp tcpSvr tcpCln tcpSvrGtest \
		udpSvr udpCln multicastSender multicastReceiver mCastReceiver uartTxTest \
		uartRx mutexQueueGtest