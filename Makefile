# ============================================================
#  Compilers & Flags
# ============================================================
CC       ?= gcc
CXX      ?= g++

CFLAGS   ?= -Wall -O2
CXXFLAGS ?= -Wall -O2

# 자동 의존성 생성 (각 .o 빌드 시 .d 파일 자동 생성)
CFLAGS   += -MMD -MP
CXXFLAGS += -MMD -MP

LDFLAGS  ?=
LIBS_COMMON = -levent

# ============================================================
#  Object Grouping (모듈 단위)
# ============================================================

# 프레임 / ICD
FRAME_OBJS   = cmdRegistry.o icdCommand.o

# 네트워크 (TCP/UDP/UDS 공통)
NET_OBJS     = netCore.o netTcp.o netUdp.o netUds.o

# 이벤트 엔진 / IPC
ENGINE_OBJS  = eventEngine.o eventSource.o ioChannelUtil.o ipcUtil.o udsFrame.o

# UART / 센서
UART_OBJS    = uartConfig.o 

# 공통 (대부분의 서버/컨트롤러에서 사용)
COMMON_OBJS  = $(FRAME_OBJS) $(NET_OBJS) $(ENGINE_OBJS)

# ============================================================
#  Phony targets
# ============================================================
.PHONY: all clean gtest

# 기본 빌드: 주요 프로세스
all: trackingController sensorFusion acuCtrl imuReceiver gpsReceiver#tcpCln acuCtrlForTest

# ============================================================
acuCtrlForTest: acuCtrlForTest.o acuUtil.o $(COMMON_OBJS) $(UART_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# --- ACU Controller ---
acuCtrl: acuCtrl.o acuUtil.o $(COMMON_OBJS) $(UART_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# --- Sensor Fusion ---
sensorFusion: sensorFusion.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# --- Tracking Controller (TCP ↔ UDS 허브) ---
trackingController: trackingController.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# ============================================================
#  UDS / TCP / UDP Servers & Clients
# ============================================================

# --- UDS 서버 / 클라이언트 ---
udsSvr: udsSvr.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udsCln: udsCln.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# --- UDP 서버 / 클라이언트 ---
udpSvr: udpSvr.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udpCln: udpCln.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# --- TCP 서버 / 클라이언트 ---
tcpSvr: tcpSvr.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

tcpCln: tcpCln.o $(COMMON_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)


# ============================================================
#  UART / 센서 Apps
# ============================================================

# GPS 수신 프로세스
gpsReceiver: gpsReceiver.o r632Gps.o $(COMMON_OBJS) $(UART_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

# IMU 수신 프로세스
imuReceiver: imuReceiver.o mti670Imu.o $(COMMON_OBJS) $(UART_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)


# ============================================================
#  Common Objects
#  (필요하면 개별 규칙 추가 가능, 기본은 패턴 규칙 사용)
# ============================================================

frame.o: frame.c
	$(CC) $(CFLAGS) -c -o $@ $<

icdCommand.o: icdCommand.c
	$(CC) $(CFLAGS) -c -o $@ $<

netCore.o: netCore.c
	$(CC) $(CFLAGS) -c -o $@ $<

netTcp.o: netTcp.c
	$(CC) $(CFLAGS) -c -o $@ $<

netUdp.o: netUdp.c
	$(CC) $(CFLAGS) -c -o $@ $<

netUds.o: netUds.c
	$(CC) $(CFLAGS) -c -o $@ $<

eventEngine.o: eventEngine.c
	$(CC) $(CFLAGS) -c -o $@ $<

eventSource.o: eventSource.c
	$(CC) $(CFLAGS) -c -o $@ $<

ioChannelUtil.o: ioChannelUtil.c
	$(CC) $(CFLAGS) -c -o $@ $<

ipcUtil.o: ipcUtil.c
	$(CC) $(CFLAGS) -c -o $@ $<

udsFrame.o: udsFrame.c
	$(CC) $(CFLAGS) -c -o $@ $<

uartConfig.o: uartConfig.c
	$(CC) $(CFLAGS) -c -o $@ $<

r632Gps.o: r632Gps.c
	$(CC) $(CFLAGS) -c -o $@ $<

mti670Imu.o: mti670Imu.c
	$(CC) $(CFLAGS) -c -o $@ $<


# ============================================================
#  GoogleTest 설정
# ============================================================

GTEST_DIR         = /home/pcw1029/googletest
GTEST_INCLUDE_DIR = $(GTEST_DIR)/googletest/include
GTEST_LIB_DIR     = $(GTEST_DIR)/build/lib

GTEST_CXXFLAGS    = -I$(GTEST_INCLUDE_DIR)
GTEST_LDFLAGS     = -L$(GTEST_LIB_DIR) -lgtest -lgtest_main -pthread -Wl,-rpath,$(GTEST_LIB_DIR)

gtest: gpsUartRxGtest tcpSvrGtest udsSvrGtest udpSvrGtest mutexQueueGtest

# --- UDS Server GTest ---
udsSvrGtest: udsSvrGtest.o udsSvrNostandalone.o $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

udsSvrGtest.o: gtest/udsSvrGtest.cc
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

udsSvrNostandalone.o: udsSvr.c
	$(CC) $(CFLAGS) -DGOOGLE_TEST -c -o $@ $<

# --- TCP Server GTest ---
tcpSvrGtest: tcpSvrGtest.o tcpSvrNostandalone.o $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

tcpSvrGtest.o: gtest/tcpSvrGtest.cc
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

tcpSvrNostandalone.o: tcpSvr.c
	$(CC) $(CFLAGS) -DGOOGLE_TEST -c -o $@ $<

# --- UDP Server GTest ---
udpSvrGtest: udpSvrGtest.o udpSvrNostandalone.o $(COMMON_OBJS)
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

udpSvrGtest.o: gtest/udpSvrGtest.cc
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

udpSvrNostandalone.o: udpSvr.c
	$(CC) $(CFLAGS) -DGOOGLE_TEST -c -o $@ $<

# --- MutexQueue GTest ---
mutexQueueGtest: gtest/mutexQueueGtest.o mutexQueue.o
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(GTEST_LDFLAGS) $(LDFLAGS)

gtest/mutexQueueGtest.o: gtest/mutexQueueGtest.cc mutexQueue.h
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

# --- GPS UartRx GTest ---
gpsUartRxGtest: gpsUartRxGtest.o uartRxNostandalone.o
	$(CXX) $(CXXFLAGS) -DGOOGLE_TEST -o $@ $^ $(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

gpsUartRxGtest.o: gtest/gpsUartRxGtest.cc
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -c -o $@ $<

uartRxNostandalone.o: uartRx.c r632Gps.c
	$(CC) $(CFLAGS) -DGOOGLE_TEST -c -o $@ $<

# ============================================================
#  Generic Pattern Rules
# ============================================================

# C 소스 기본 규칙 (위에서 개별 규칙 없는 경우에 사용)
%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

# C++ / gtest용 기본 규칙 (위에서 개별 규칙 없는 경우에 사용)
%.o: %.cc
	$(CXX) $(CXXFLAGS) -c -o $@ $<

# ============================================================
#  Clean
# ============================================================

clean:
	rm -f *.o *.d \
		udsSvr udsCln udsSvrGtest \
		trackingCtrlApp tcpSvr tcpCln tcpSvrGtest \
		udpSvr udpCln udpSvrGtest \
		multicastSender multicastReceiver mCastReceiver \
		uartTxTest uartRx mutexQueueGtest \
		gpsUartRxGtest tcpUdsSvr gpsReceiver \
		trackingController sensorFusion imuReceiver acuCtrl

# ============================================================
#  Include auto-generated dependencies
# ============================================================

