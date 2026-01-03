# NeolyxOS Init Process Makefile

CC = gcc
LD = ld

CFLAGS = -ffreestanding -nostdlib -nostdinc -fno-builtin -fno-stack-protector \
         -mno-red-zone -m64 -fno-pie -I../include -Wall -Wextra

LDFLAGS = -T linker.ld -nostdlib

TARGET = init
OBJS = init.o

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean
