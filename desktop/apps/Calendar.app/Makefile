# Calendar.app Makefile
# NeolyxOS Calendar Application with themes and clock faces

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2
LDFLAGS = 

SRCS = main.c regions.c animation.c render.c
OBJS = $(SRCS:.c=.o)
TARGET = calendar

INCLUDES = -I. -I../../REOX/reolab/runtime

.PHONY: all clean install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c calendar.h
	$(CC) $(CFLAGS) $(INCLUDES) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

install: $(TARGET)
	mkdir -p bin
	cp $(TARGET) bin/
