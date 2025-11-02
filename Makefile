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
# === Include Paths
# ============================================================
INCLUDES = -I. -IuartModule -InetModule -InetModule/core -InetModule/protocols -Igtest
CFLAGS  += $(INCLUDES)
CXXFLAGS += $(INCLUDES)

# ============================================================
# === uartModule (Kbuild 스타일 포함)
# ============================================================
UART_MODULE_DIR := uartModule
include $(UART_MODULE_DIR)/Makefile
UART_OBJS := $(addprefix $(UART_MODULE_DIR)/, $(obj-uartModule-y))

# ============================================================
# === netModule (Kbuild 스타일 포함)
# ============================================================
NET_MODULE_DIR := netModule
include $(NET_MODULE_DIR)/Makefile
NET_OBJS := $(addprefix $(NET_MODULE_DIR)/, $(obj-netModule-y))

# ============================================================
# === GoogleTest 설정
# ============================================================
GTEST_DIR          = /home/pcw1029/googletest
GTEST_INCLUDE_DIR  = $(GTEST_DIR)/googletest/include
GTEST_LIB_DIR      = $(GTEST_DIR)/build/lib
GTEST_CXXFLAGS     = -I$(GTEST_INCLUDE_DIR)
GTEST_LDFLAGS      = -L$(GTEST_LIB_DIR) -lgtest -lgtest_main -pthread -Wl,-rpath,$(GTEST_LIB_DIR)

# ============================================================
# === Phony targets ===
# ============================================================
.PHONY: all clean all-uart all-net gtest clean-gtest

# 기본 빌드
all: tcpSvr tcpCln udsSvr udsCln # udpSvr udpCln

# ============================================================
# === Regular apps (netModule 통합)
# ============================================================
tcpSvr: tcpSvr.o $(NET_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON)

tcpCln: tcpCln.o $(NET_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON)

udpSvr: udpSvr.o $(NET_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON)

udpCln: udpCln.o $(NET_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON)

udsSvr: udsSvr.o $(NET_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON)

udsCln: udsCln.o $(NET_OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LIBS_COMMON)

# ============================================================
# === GoogleTest (개별 빌드: TCP / UDP / UDS)
# ============================================================
GTEST_SRCS = gtest/tcpSvrGtest.cc
GTEST_OBJS = $(GTEST_SRCS:.cpp=.o)

gtest: tcpSvrGtest udpSvrGtest udsSvrGtest

tcpSvrGtest: gtest/tcpSvrGtest.o $(NET_OBJS)
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -o $@ $^ \
		$(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

udpSvrGtest: gtest/udpSvrGtest.o $(NET_OBJS)
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -o $@ $^ \
		$(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

udsSvrGtest: gtest/udsSvrGtest.o $(NET_OBJS)
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -DGOOGLE_TEST -o $@ $^ \
		$(LIBS_COMMON) $(GTEST_LDFLAGS) $(LDFLAGS)

# 개별 오브젝트 빌드 규칙
gtest/%.o: gtest/%.cpp
	$(CXX) $(CXXFLAGS) $(GTEST_CXXFLAGS) -I$(NET_MODULE_DIR) -DGOOGLE_TEST -c -o $@ $<

# ============================================================
# === Clean ===
# ============================================================
clean:
	@echo "[CLEAN] Removing top-level targets..."
	rm -f *.o udsSvr udsCln tcpSvr tcpCln udpSvr udpCln \
	      tcpSvrGtest udpSvrGtest udsSvrGtest \
	      multicastSender multicastReceiver mCastReceiver \
	      uartTxTest uartRx mutexQueueGtest
	@$(MAKE) -s -C $(UART_MODULE_DIR) clean-uart
	@$(MAKE) -s -C $(NET_MODULE_DIR) clean-net

clean-gtest:
	@echo "[CLEAN] Removing GTest objects..."
	rm -f gtest/*.o tcpSvrGtest udpSvrGtest udsSvrGtest
