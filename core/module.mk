# ============================================================
# Core Event / IO Engine
# ============================================================

CORE_SRCS := \
 core/eventEngine.c \
 core/eventSource.c \
 core/ioChannelUtil.c

CORE_OBJS := $(CORE_SRCS:.c=.o)

