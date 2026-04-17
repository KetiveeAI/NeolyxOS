# NeolyxOS Userland NXRender Test Makefile

CC = gcc
LD = ld

CFLAGS = -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -mno-red-zone -m64 -fno-pie \
         -I../include -I../../../gui/nxrender_c/include \
         -Wall -Wextra -std=c11

LDFLAGS = -T linker.ld -nostdlib

TARGET = nxrender_test

NXRENDER_SRCS = ../../../gui/nxrender_c/src/nxgfx/nx_bezier.c \
                ../../../gui/nxrender_c/src/nxgfx/nx_scanline.c \
                ../../../gui/nxrender_c/src/nxgfx/nx_math.c \
                ../../../gui/nxrender_c/src/nxgfx/nxsvg_parser.c \
                ../../../gui/nxrender_c/src/nxserver/nxserver_compositor.c

TEST_SRCS = nxrender_test.c

all: $(TARGET)

$(TARGET): $(TEST_SRCS) $(NXRENDER_SRCS)
	$(CC) $(CFLAGS) -c $(TEST_SRCS) -o nxrender_test_main.o
	$(CC) $(CFLAGS) -c ../../../gui/nxrender_c/src/nxgfx/nx_bezier.c -o nx_bezier.o
	$(CC) $(CFLAGS) -c ../../../gui/nxrender_c/src/nxgfx/nx_scanline.c -o nx_scanline.o
	$(CC) $(CFLAGS) -c ../../../gui/nxrender_c/src/nxgfx/nx_math.c -o nx_math.o
	$(CC) $(CFLAGS) -c ../../../gui/nxrender_c/src/nxgfx/nxsvg_parser.c -o nxsvg_parser.o
	$(CC) $(CFLAGS) -c ../../../gui/nxrender_c/src/nxserver/nxserver_compositor.c -o nxserver_compositor.o
	
	$(LD) $(LDFLAGS) -o $@ nxrender_test_main.o nx_bezier.o nx_scanline.o nx_math.o nxsvg_parser.o nxserver_compositor.o

clean:
	rm -f *.o $(TARGET)

.PHONY: all clean
