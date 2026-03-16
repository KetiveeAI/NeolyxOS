# libnximage - NeolyxOS Image Library
#
# Makefile for building libnximage.a static library

CC = gcc
CFLAGS = -Wall -Wextra -O2 -fno-builtin -fno-stack-protector -I.

# For NeolyxOS freestanding build
ifdef NEOLYXOS
    CFLAGS += -ffreestanding -mno-red-zone -D__NEOLYXOS__
endif

SOURCES = nximage.c nxpng.c nxnxi.c nxavif_stub.c
OBJECTS = $(SOURCES:.c=.o)
LIBRARY = libnximage.a

.PHONY: all clean test

all: $(LIBRARY)

$(LIBRARY): $(OBJECTS)
	ar rcs $@ $^
	@echo "Built $(LIBRARY)"

%.o: %.c nximage.h
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJECTS) $(LIBRARY)

# Quick test (requires test harness)
test: $(LIBRARY)
	@echo "Run test_nximage from desktop/test/"
