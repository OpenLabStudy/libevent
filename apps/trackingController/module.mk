# ============================================================================
# trackingController
# ============================================================================

TRACKING_CONTROLLER_SRCS := \
 apps/trackingController/trackingController.c

TRACKING_CONTROLLER_TARGET := trackingController

$(TRACKING_CONTROLLER_TARGET): $(COMMON_SRCS) $(TRACKING_CONTROLLER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(TRACKING_CONTROLLER_SRCS) \
	$(LDLIBS) \
	-o $@

# ============================================================================
# keyboardReceiver
# ============================================================================

KEYBOARD_RECEIVER_SRCS := \
 apps/trackingController/keyboardReceiver.c

KEYBOARD_RECEIVER_TARGET := keyboardReceiver

$(KEYBOARD_RECEIVER_TARGET): $(COMMON_SRCS) $(KEYBOARD_RECEIVER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(KEYBOARD_RECEIVER_SRCS) \
	$(LDLIBS) \
	-o $@

# ============================================================================
# azElSender
# ============================================================================

AZEL_SENDER_SRCS := \
 apps/trackingController/azElSender.c

AZEL_SENDER_TARGET := azElSender

$(AZEL_SENDER_TARGET): $(COMMON_SRCS) $(AZEL_SENDER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(AZEL_SENDER_SRCS) \
	$(LDLIBS) \
	-o $@
