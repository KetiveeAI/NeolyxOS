# Calculator.app Makefile - NeolyxOS Calculator written in REOX
#
# Targets:
#   all / bin/calculator   REOX -> C -> freestanding NeolyxOS ELF (via sdk/reox.mk)
#   gen                    only generate src/calculator.c from src/calculator.rx
#   legacy                 the previous C implementation (legacy/, reference only)
#   clean
#
# Copyright (c) 2025 KetiveeAI

include ../../../sdk/reox.mk

APP_NAME  := calculator
BIN_DIR   := bin
BIN       := $(BIN_DIR)/$(APP_NAME)

REOX_SRCS  := src/calculator.rx
REOX_GEN_C := $(REOX_SRCS:.rx=.c)
APP_OBJS   := $(REOX_GEN_C:.c=.o)

# Keep the generated C: it is a build artefact but useful for review.
.SECONDARY: $(REOX_GEN_C)
.PRECIOUS: $(REOX_GEN_C)

.PHONY: all gen legacy clean info

all: $(BIN)

gen: $(REOX_GEN_C)

$(BIN): $(APP_OBJS) $(REOX_SUPPORT_OBJS) $(REOX_LDLIBS)
	@mkdir -p $(BIN_DIR)
	$(LD) $(NEOLYX_USER_LDFLAGS) -o $@ $(APP_OBJS) $(REOX_SUPPORT_OBJS) $(REOX_LDLIBS)
	@echo "Built $(BIN) (REOX runtime library: $(if $(filter 1,$(REOX_RUNTIME_AVAILABLE)),$(REOX_RUNTIME_LIB),not linked - $(REOX_RUNTIME_LIB) is absent))"

%.o: %.c
	$(CC) $(REOX_CFLAGS) -c $< -o $@

legacy:
	$(MAKE) -C legacy standalone

clean:
	rm -f $(APP_OBJS) $(REOX_GEN_C) $(BIN)
	rm -rf $(REOX_BUILD_DIR)
	-$(MAKE) -C legacy clean >/dev/null 2>&1

info: reox-info
	@echo "REOX_SRCS = $(REOX_SRCS)"

# ---- host-side engine test -------------------------------------------------
# Compiles the generated C on the development host (no NeolyxOS syscalls)
# together with NxUI and the host nxrender library, and checks the engine
# through the same hooks the buttons use.
HOST_CC      ?= gcc
HOST_CFLAGS  := -Wall -Wextra -std=c11 -O1 -g \
                -I$(REOX_SDK_DIR)/nxui -I$(NXRENDER_DIR)/include $(REOX_RUNTIME_INCLUDES)
HOST_NXRENDER := $(NXRENDER_DIR)/libnxrender.a
TEST_BIN := $(REOX_BUILD_DIR)/host/engine_test

.PHONY: test
test: $(TEST_BIN)
	$(TEST_BIN)

$(TEST_BIN): test/engine_test.c $(REOX_GEN_C) $(REOX_SDK_DIR)/nxui/nxui.c $(HOST_NXRENDER)
	@mkdir -p $(dir $@)
	$(HOST_CC) $(HOST_CFLAGS) -Dmain=calc_main -c $(REOX_GEN_C) -o $(REOX_BUILD_DIR)/host/calculator.o
	$(HOST_CC) $(HOST_CFLAGS) -c $(REOX_SDK_DIR)/nxui/nxui.c -o $(REOX_BUILD_DIR)/host/nxui.o
	$(HOST_CC) $(HOST_CFLAGS) test/engine_test.c $(REOX_BUILD_DIR)/host/calculator.o \
	    $(REOX_BUILD_DIR)/host/nxui.o $(HOST_NXRENDER) -lm -o $@

$(HOST_NXRENDER):
	$(MAKE) -C $(NXRENDER_DIR)
