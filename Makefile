# neolyx-os/Makefile
NASM = nasm
CC = i386-elf-gcc
LD = i386-elf-ld
CFLAGS = -ffreestanding -O2 -Wall -Wextra
LDFLAGS = -T kernel/link.ld

BOOTLOADER = bootloader/boot.asm
BOOTBIN = build/boot.bin
KERNEL_SRC = kernel/main.c
KERNEL_OBJ = build/kernel.o
KERNEL_BIN = build/kernel.bin
IMG = build/neolyx.img

all: $(IMG)

$(BOOTBIN): $(BOOTLOADER)
	$(NASM) $< -f bin -o $@

$(KERNEL_OBJ): $(KERNEL_SRC) kernel/kernel.h
	$(CC) $(CFLAGS) -c $< -o $@

$(KERNEL_BIN): $(KERNEL_OBJ)
	$(LD) $(LDFLAGS) -o $@ $<

$(IMG): $(BOOTBIN) $(KERNEL_BIN)
	cat $(BOOTBIN) $(KERNEL_BIN) > $(IMG)

clean:
	rm -f build/*.bin build/*.o build/neolyx.img 