# Clock.app Makefile
# NeolyxOS Clock Application with NTP support

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS = 

# Source files
SRCS = main.c ntp_client.c
OBJS = $(SRCS:.c=.o)
TARGET = clock

# Includes
INCLUDES = -I. -I../../REOX/reolab/runtime

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c clock.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	cp $(TARGET) bin/

# For kernel-mode build (freestanding)
kernel:
	$(CC) -ffreestanding -nostdlib -mno-red-zone -Wall -Wextra -m64 \
		-fno-pie -no-pie $(INCLUDES) \
		-c main.c -o main.o
	$(CC) -ffreestanding -nostdlib -mno-red-zone -Wall -Wextra -m64 \
		-fno-pie -no-pie $(INCLUDES) \
		-c ntp_client.c -o ntp_client.o
