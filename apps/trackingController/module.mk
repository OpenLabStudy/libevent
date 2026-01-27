# ============================================================
# Tracking Controller (Main Orchestrator)
# ============================================================

TRACKING_CONTROLLER_SRCS := \
 apps/trackingController/trackingController.c

TRACKING_CONTROLLER_TARGET := trackingController

$(TRACKING_CONTROLLER_TARGET): $(COMMON_SRCS) $(TRACKING_CONTROLLER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(TRACKING_CONTROLLER_SRCS) \
	$(LDLIBS) \
	-o $@

