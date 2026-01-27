# ============================================================
# Command / ICD / Protocol Layer
# ============================================================

PROTOCOL_SRCS := \
 protocol/cmdRegistry.c \
 protocol/icdCommand.c

PROTOCOL_OBJS := $(PROTOCOL_SRCS:.c=.o)

