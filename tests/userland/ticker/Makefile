# NeolyxOS Ticker - Makefile
#
# Builds a small static userspace ELF that runs alongside the desktop,
# linked on nxlibc (sdk/libc). Install it on the boot disk as /apps/ticker
# to make it launchable from the desktop's app drawer (rebuild_image.sh
# does this). nxlibc links it with sdk/libc/crt/nxapp.ld at 0x1000000,
# the same layout linker.ld describes.

ROOT := $(abspath ../../..)
include $(ROOT)/sdk/libc/nxlibc.mk

CFLAGS = $(NXLIBC_CFLAGS) -O2 -Wall -Wextra \
         -Werror=implicit-function-declaration -Werror=implicit-int \
         -Werror=int-conversion -Werror=incompatible-pointer-types

TARGET = ticker
OBJS = ticker.o

.PHONY: all clean libc

all: $(TARGET)

libc:
	@$(MAKE) --no-print-directory -C $(NXLIBC_DIR)

$(TARGET): $(OBJS) libc
	$(LD) $(NXLIBC_LDFLAGS) -o $@ $(NXLIBC_CRT0) $(OBJS) $(NXLIBC_LIB)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
