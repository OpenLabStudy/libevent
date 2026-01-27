# ============================================================
# Network Abstraction Layer
# ============================================================

NET_SRCS := \
 net/netCore.c \
 net/tcp/netTcp.c \
 net/udp/netUdp.c \
 net/uds/netUds.c

NET_OBJS := $(NET_SRCS:.c=.o)

