# NeolyxOS Hello World - Makefile
#
# Builds a minimal userspace ELF executable

CC = gcc
LD = ld

CFLAGS = -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -m64 -Wall -Wextra -O2

LDFLAGS = -nostdlib -static -T linker.ld

TARGET = hello
SRCS = hello.c
OBJS = $(SRCS:.c=.o)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $^
	@echo "Built: $(TARGET)"
	@file $(TARGET)
	@ls -lh $(TARGET)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

# Show ELF info
info: $(TARGET)
	readelf -h $(TARGET)
	readelf -l $(TARGET)
