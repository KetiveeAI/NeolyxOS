# Archive-Pie.app - NeolyxOS Archive Manager
# Build with nxrender_c and nxzip

CC = gcc
CFLAGS = -Wall -Wextra -std=c11 -O2 \
         -I../../include \
         -I../../../gui/nxrender_c/include \
         -I../../../lib/nxzip/include \
         -I./include

LDFLAGS = -L../../../gui/nxrender_c -lnxrender \
          -L../../../lib/nxzip -lnxzip \
          -lm -lSDL2

SRCS = src/main.c \
       src/archive_core.c \
       src/archive_ui.c

OBJS = $(SRCS:.c=.o)
TARGET = bin/archive

.PHONY: all clean run

all: bin $(TARGET)

bin:
	mkdir -p bin

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
	@echo "Built: $@"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET)
	rm -rf bin

run: $(TARGET)
	./$(TARGET)
