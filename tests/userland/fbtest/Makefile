# NeolyxOS fb.flip_rects test - Makefile
#
# Builds the fb.flip_rects privilege test (see fbtest.c).
# Install it on the boot disk as /apps/fbtest to make it launchable from
# the desktop's app drawer (rebuild_image.sh does this).

CC = gcc
LD = ld

CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -m64 -mno-red-zone -Wall -Wextra -O2

LDFLAGS = -nostdlib -static -T linker.ld

TARGET = fbtest
OBJS = fbtest.o

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS) linker.ld
	$(LD) $(LDFLAGS) -o $@ $(OBJS)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
