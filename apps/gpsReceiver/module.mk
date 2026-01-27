# ============================================================
# GPS Receiver (Hemisphere R632)
# ============================================================

GPS_RECEIVER_SRCS := \
 apps/gpsReceiver/gpsReceiver.c \
 apps/gpsReceiver/r632Gps.c

GPS_RECEIVER_TARGET := gpsReceiver

$(GPS_RECEIVER_TARGET): $(COMMON_SRCS) $(GPS_RECEIVER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(GPS_RECEIVER_SRCS) \
	$(LDLIBS) \
	-o $@
