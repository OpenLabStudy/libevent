# ============================
# netModule/Makefile
# ============================

include core/Makefile
include protocols/Makefile

# core / protocols 개별 오브젝트 통합
obj-y += $(addprefix core/, $(core-y))
obj-y += $(addprefix protocols/, $(protocols-y))

# ----------------------------
# 기본 빌드 규칙 (gcc 컴파일)
# ----------------------------
# 1) 기본 타겟 정의
all: $(obj-y)
	@echo "[netModule] Build complete."

# 2) .c -> .o 규칙 (자동)
%.o: %.c
	$(CC) $(CFLAGS) -I. -c -o $@ $<

# 3) 클린 규칙
clean-obj:
	rm -f $(obj-y)
