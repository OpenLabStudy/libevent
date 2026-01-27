# ============================================================
# IMU Receiver (Xsens MTi-670)
# ============================================================

IMU_RECEIVER_SRCS := \
 apps/imuReceiver/imuReceiver.c \
 apps/imuReceiver/mti670Imu.c

IMU_RECEIVER_TARGET := imuReceiver

$(IMU_RECEIVER_TARGET): $(COMMON_SRCS) $(IMU_RECEIVER_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(IMU_RECEIVER_SRCS) \
	$(LDLIBS) \
	-o $@

