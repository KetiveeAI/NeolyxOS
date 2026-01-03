# NeolyxOS GUI Bootloader Makefile - Using gnu-efi
ARCH = x86_64
GNUEFI_DIR = /usr/lib

CC = gcc
LD = ld
OBJCOPY = objcopy

CFLAGS = -I/usr/include/efi -I/usr/include/efi/$(ARCH) -I. \
         -DEFI_FUNCTION_WRAPPER \
         -ffreestanding -fno-stack-protector -fpic -fshort-wchar \
         -mno-red-zone -Wall -Wno-unused-variable -c

LDFLAGS = -nostdlib -znocombreloc -T /usr/lib/elf_$(ARCH)_efi.lds \
          -shared -Bsymbolic \
          -L$(GNUEFI_DIR) \
          $(GNUEFI_DIR)/crt0-efi-$(ARCH).o

LIBS = -lefi -lgnuefi

OBJS = main.o graphics.o boot_menu.o

all: BOOTX64.EFI
	@echo "✓ NeolyxOS GUI Bootloader built: BOOTX64.EFI"

main.o: main.c graphics.h boot_menu.h
	$(CC) $(CFLAGS) main.c -o main.o

graphics.o: graphics.c graphics.h logo.h
	$(CC) $(CFLAGS) graphics.c -o graphics.o

boot_menu.o: boot_menu.c boot_menu.h graphics.h
	$(CC) $(CFLAGS) boot_menu.c -o boot_menu.o

bootloader.so: $(OBJS)
	$(LD) $(LDFLAGS) $(OBJS) -o bootloader.so $(LIBS)

BOOTX64.EFI: bootloader.so
	$(OBJCOPY) -j .text -j .sdata -j .data -j .dynamic \
	           -j .dynsym -j .rel -j .rela -j .reloc \
	           --target=efi-app-$(ARCH) bootloader.so BOOTX64.EFI

clean:
	rm -f *.o *.so *.EFI

install: BOOTX64.EFI
	@echo "Copy BOOTX64.EFI to EFI/BOOT/ on disk image"

.PHONY: all clean install