# ============================================================
# === netModule/Makefile
# ============================================================

# 현재 이 Makefile이 위치한 디렉토리 절대경로 계산
NETMODULE_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

# 하위 모듈 포함 (절대경로 방식)
include $(NETMODULE_DIR)/core/Makefile
include $(NETMODULE_DIR)/protocols/Makefile
# core / protocols 오브젝트 병합
obj-netModule-y += $(addprefix core/, $(core-y))
obj-netModule-y += $(addprefix protocols/, $(protocols-y))

# 모듈 전용 빌드 타겟
all-net: $(obj-netModule-y)
	@echo "[netModule] Build complete."

# .c → .o 규칙
%.o: %.c
	$(CC) $(CFLAGS) -I. -Icore -Iprotocols -c -o $@ $<

# clean
clean-net:
	rm -f $(obj-netModule-y)
