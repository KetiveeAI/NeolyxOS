# NeolyxOS crypto test - Makefile
#
# Builds cryptotest (see cryptotest.c) linked with lib/nxcrypto.
# Install it on the boot disk as /apps/cryptotest to run it from the
# desktop's app drawer.

CC = gcc
LD = ld

NXCRYPTO = ../../../lib/nxcrypto

CFLAGS = -I$(NXCRYPTO)/include -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -m64 -mno-red-zone -Wall -Wextra -O2 \
         -Werror=implicit-function-declaration -Werror=implicit-int \
         -Werror=int-conversion -Werror=incompatible-pointer-types

LDFLAGS = -nostdlib -static -T linker.ld

TARGET = cryptotest
OBJS = cryptotest.o

.PHONY: all clean $(NXCRYPTO)/build/libnxcrypto.a

all: $(TARGET)

$(NXCRYPTO)/build/libnxcrypto.a:
	$(MAKE) -C $(NXCRYPTO)

$(TARGET): $(OBJS) linker.ld $(NXCRYPTO)/build/libnxcrypto.a
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(NXCRYPTO)/build/libnxcrypto.a
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)
