# ============================================================
# ACU Controller
# ============================================================

ACUCTRL_SRCS := \
 apps/acuCtrl/acuCtrl.c \
 apps/acuCtrl/acuUtil.c

ACUCTRL_TARGET := acuCtrl

$(ACUCTRL_TARGET): $(COMMON_SRCS) $(ACUCTRL_SRCS)
	$(CC) $(CFLAGS) $(INCLUDES) \
	$(COMMON_SRCS) $(ACUCTRL_SRCS) \
	$(LDLIBS) \
	-o $@
