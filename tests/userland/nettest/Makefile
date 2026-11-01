# NeolyxOS network test - Makefile
#
# Builds the network test (see nettest.c).
# Install it on the boot disk as /apps/nettest to make it launchable from
# the desktop's app drawer (rebuild_image.sh does this).

CC = gcc
LD = ld

CFLAGS = -I../../../kernel/include/core -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -m64 -mno-red-zone -Wall -Wextra -O2

LDFLAGS = -nostdlib -static -T linker.ld

TARGET = nettest
OBJS = nettest.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
