# NeolyxOS libnxwindow test - Makefile
#
# Builds the libnxwindow test (see nxwtest.c) against sdk/nxwindow.
# Install it on the boot disk as /apps/nxwtest to make it launchable from
# the desktop's app drawer (rebuild_image.sh does this).

CC = gcc
LD = ld

ROOT := ../../..
NXWINDOW_DIR := $(ROOT)/sdk/nxwindow
NXWINDOW_LIB := $(NXWINDOW_DIR)/libnxwindow.a

CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -m64 -mno-red-zone -Wall -Wextra -O2 \
         -fno-tree-loop-distribute-patterns \
         -I$(NXWINDOW_DIR) -I$(ROOT)/desktop/include -I$(ROOT)/kernel/include/core

LDFLAGS = -nostdlib -static -T linker.ld

TARGET = nxwtest
OBJS = nxwtest.o

.PHONY: all clean $(NXWINDOW_LIB)

all: $(TARGET)

$(NXWINDOW_LIB):
	$(MAKE) -C $(NXWINDOW_DIR)

$(TARGET): $(OBJS) $(NXWINDOW_LIB) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(NXWINDOW_LIB)
	@echo "Built: $(TARGET)"

%.o: %.c $(NXWINDOW_DIR)/nxwindow.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
