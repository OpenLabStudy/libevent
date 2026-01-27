# ============================================================
# Sensor Fusion
# ============================================================

SENSOR_FUSION_SRCS := \
 apps/sensorFusion/sensorFusion.c

SENSOR_FUSION_TARGET := sensorFusion

$(SENSOR_FUSION_TARGET): $(COMMON_SRCS) $(SENSOR_FUSION_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(SENSOR_FUSION_SRCS) \
	$(LDLIBS) \
	-o $@

