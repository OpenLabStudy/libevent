# =========================
# Toolchain
# =========================
CC      := gcc
CFLAGS  := -Wall -Wextra -g
LDLIBS  := -levent -lm

# =========================
# Include paths
# =========================
INCLUDES := \
    -Icore \
    -Inet \
    -Inet/tcp \
    -Inet/udp \
    -Inet/uds \
    -Iprotocol \
    -Idrivers/uart \
    -Iapps

# =========================
# Include module definitions
# =========================
include core/module.mk
include net/module.mk
include protocol/module.mk
include drivers/module.mk

# 공통 소스 묶기
COMMON_SRCS := \
 $(CORE_SRCS) \
 $(NET_SRCS) \
 $(PROTOCOL_SRCS) \
 $(DRIVERS_SRCS)

include apps/acuCtrl/module.mk
include apps/gpsReceiver/module.mk
include apps/imuReceiver/module.mk
include apps/sensorFusion/module.mk
include apps/trackingController/module.mk


define BUILD_START
	@echo ">>>>>>>>>"
	@echo "Start building $(1)"
endef

define BUILD_DONE
	@echo "<<<<<<<<<<"
	@echo "Finished building $(1)"
endef

acuCtrl_build:
	$(call BUILD_START,acuCtrl)
	@$(MAKE) acuCtrl
	$(call BUILD_DONE,acuCtrl)

gpsReceiver_build:
	$(call BUILD_START,gpsReceiver)
	@$(MAKE) gpsReceiver
	$(call BUILD_DONE,gpsReceiver)

imuReceiver_build:
	$(call BUILD_START,imuReceiver)
	@$(MAKE) imuReceiver
	$(call BUILD_DONE,imuReceiver)

sensorFusion_build:
	$(call BUILD_START,sensorFusion)
	@$(MAKE) sensorFusion
	$(call BUILD_DONE,sensorFusion)

trackingController_build:
	$(call BUILD_START,trackingController)
	@$(MAKE) trackingController
	$(call BUILD_DONE,trackingController)


ALL_APPS := \
    acuCtrl \
    gpsReceiver \
    imuReceiver \
    sensorFusion \
    trackingController

ALL_BUILD := \
    acuCtrl_build \
    gpsReceiver_build \
    imuReceiver_build \
    sensorFusion_build \
    trackingController_build

all: $(ALL_BUILD)
	@echo "======================================"
	@echo "All processes build completed"
	@echo "======================================"



# =========================
# Clean
# =========================
clean:
	rm -f $(ALL_APPS)
	rm -f */*.o */*/*.o
