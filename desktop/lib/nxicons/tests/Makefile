# NeolyxOS - NXIcons host tests (ASan/UBSan), see ../../nxcanvas/tests/Makefile
HOSTCC ?= gcc
OUT    ?= out
ENG    = ../../nxcanvas
CFLAGS = -std=c11 -O1 -g -Wall -Wextra -Werror=implicit-function-declaration \
         -fsanitize=address,undefined -fno-omit-frame-pointer -D_POSIX_C_SOURCE=200809L
ENGINE_SRCS = $(wildcard $(ENG)/nxc_*.c)
.PHONY: run clean
run:
	@mkdir -p $(OUT)/bin
	$(HOSTCC) $(CFLAGS) -o $(OUT)/bin/test_icons test_icons.c $(ENG)/tests/nxc_test.c $(ENGINE_SRCS) ../nxicons.c ../nxapp_icons.c -fsanitize=address,undefined -lm
	NXC_TEST_OUT=$(OUT) $(OUT)/bin/test_icons
clean:
	rm -rf $(OUT)
