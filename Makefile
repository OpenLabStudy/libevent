# ============================================================
# === Compilers & Flags ===
# ============================================================
CC      	?= gcc
CXX     	?= g++
CFLAGS  	?= -Wall -O2
CXXFLAGS	?= -Wall -O2
LDFLAGS 	?=
LIBS_COMMON = -levent

# ============================================================
# === uartModule (Kbuild 스타일 포함)
# ============================================================
UART_MODULE_DIR := uartModule
include $(UART_MODULE_DIR)/Makefile
UART_OBJS := $(addprefix $(UART_MODULE_DIR)/, $(obj-y))

# ============================================================
# === netModule (Kbuild 스타일 포함) ★ NEW
# ============================================================
NET_MODULE_DIR := netModule
include $(NET_MODULE_DIR)/Makefile
NET_OBJS := $(addprefix $(NET_MODULE_DIR)/, $(obj-y))

# ============================================================
# === Files ===
# ============================================================
HDRS       = frame.h icdCommand.h sockSession.h
FRAME_OBJS = frame-io.o sockSession.o

# ============================================================
# === Phony targets ===
# ============================================================
.PHONY: all clean gtest

# 기본 빌드: udsSvr, udsCln
# ★ MOD: netModule 추가됨
all: tcpSvr tcpCln udpSvr udpCln udsSvr udsCln multicastSender multicastReceiver mCastReceiver uartTxTest uartRx

# ============================================================
# === Regular apps (netModule 통합)
# ============================================================
udsSvr: udsSvr.o $(FRAME_OBJS) $(NET_OBJS)
	$(CC) $(CFLAGS) -I$(NET_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udsCln: udsCln.o $(FRAME_OBJS) $(NET_OBJS)
	$(CC) $(CFLAGS) -I$(NET_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udpSvr: udpSvr.o $(FRAME_OBJS) $(NET_OBJS)
	$(CC) $(CFLAGS) -I$(NET_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udpCln: udpCln.o $(FRAME_OBJS) $(NET_OBJS)
	$(CC) $(CFLAGS) -I$(NET_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

tcpSvr: tcpSvr.o $(FRAME_OBJS) $(NET_OBJS)
	$(CC) $(CFLAGS) -I$(NET_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

tcpCln: tcpCln.o $(FRAME_OBJS) $(NET_OBJS)
	$(CC) $(CFLAGS) -I$(NET_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

udsSvr.o: udsSvr.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

udsCln.o: udsCln.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

udpSvr.o: udpSvr.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

udpCln.o: udpCln.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

tcpSvr.o: tcpSvr.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

tcpCln.o: tcpCln.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# === Multicast apps (standalone)
# ============================================================
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

# ============================================================
# === Uart apps (uartModule 통합 적용)
# ============================================================
uartRx: uartRx.o $(UART_OBJS)
	$(CC) $(CFLAGS) -I$(UART_MODULE_DIR) -o $@ $^ $(LIBS_COMMON) $(LDFLAGS)

uartRx.o: uartRx.c
	$(CC) $(CFLAGS) -I$(UART_MODULE_DIR) -c -o $@ $<

# ============================================================
# === Common objects
# ============================================================
frame-io.o: frame-io.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

sockSession.o: sockSession.c $(HDRS)
	$(CC) $(CFLAGS) -c -o $@ $<

# ============================================================
# === GoogleTest (생략: 기존 그대로 유지)
# ============================================================
# ...

# ============================================================
# === Clean ===
# ============================================================
clean:
	rm -f *.o udsSvr udsCln udsSvrGtest trackingCtrlApp tcpSvr tcpCln tcpSvrGtest \
		udpSvr udpCln multicastSender multicastReceiver mCastReceiver uartTxTest \
		uartRx mutexQueueGtest
	$(MAKE) -C $(UART_MODULE_DIR) clean-obj
	$(MAKE) -C $(NET_MODULE_DIR) clean-obj
