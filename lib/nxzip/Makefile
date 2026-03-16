# NXZIP - NeolyxOS Compression Library Makefile

CC = gcc
AR = ar
CFLAGS = -Wall -Wextra -std=c11 -O2 -Iinclude
ARFLAGS = rcs

# Output
LIB_NAME = libnxzip.a
TARGET = $(LIB_NAME)

# Sources
SRCS = src/crc32.c \
       src/deflate.c \
       src/archive.c

OBJS = $(SRCS:.c=.o)

# Test binary
TEST_BIN = nxzip_test

.PHONY: all clean test install

all: $(TARGET)

$(TARGET): $(OBJS)
	$(AR) $(ARFLAGS) $@ $^
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(TEST_BIN)

# Test program
test: $(TARGET) test_nxzip.c
	$(CC) $(CFLAGS) -o $(TEST_BIN) test_nxzip.c -L. -lnxzip
	./$(TEST_BIN)

# Install headers and library
install: $(TARGET)
	mkdir -p /usr/local/include/nxzip
	mkdir -p /usr/local/lib
	cp include/nxzip.h /usr/local/include/nxzip/
	cp $(TARGET) /usr/local/lib/
	@echo "Installed nxzip to /usr/local"

# Info
info:
	@echo "NXZIP - NeolyxOS Compression Library"
	@echo "Build: $(TARGET)"
	@echo "Sources: $(SRCS)"
