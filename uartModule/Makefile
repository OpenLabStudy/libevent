# ============================================================
# === uartModule/Makefile
# ============================================================

obj-uartModule-y += uartEvent.o uartManager.o

# 모듈 전용 빌드 타겟 (중복 방지)
all-uart: $(obj-uartModule-y)
	@echo "[uartModule] Build complete."

# .c → .o 규칙
%.o: %.c
	$(CC) $(CFLAGS) -I. -c -o $@ $<

# clean
clean-uart:
	rm -f $(obj-uartModule-y)
