# NeolyxOS crypto test - Makefile
#
# Builds cryptotest (see cryptotest.c) linked with lib/nxtls and
# lib/nxcrypto. Install it on the boot disk as /apps/cryptotest to run it
# from the desktop's app drawer.
#
# The X.509 part uses a real certificate chain taken from the x509-limbo
# vectors at build time (gen_x509_fixture.py), so the published vectors
# are downloaded first (tests/host/crypto_fetch_vectors.sh).

CC = gcc
LD = ld

NXCRYPTO = ../../../lib/nxcrypto
NXTLS    = ../../../lib/nxtls
FETCH    = ../../host/crypto_fetch_vectors.sh

CFLAGS = -I$(NXCRYPTO)/include -I$(NXTLS)/include -I. \
         -ffreestanding -nostdlib -fno-builtin -fno-stack-protector \
         -fno-pie -no-pie -m64 -mno-red-zone -Wall -Wextra -O2 \
         -Werror=implicit-function-declaration -Werror=implicit-int \
         -Werror=int-conversion -Werror=incompatible-pointer-types

LDFLAGS = -nostdlib -static -T linker.ld

TARGET = cryptotest
OBJS = cryptotest.o
LIBS = $(NXTLS)/build/libnxtls.a $(NXCRYPTO)/build/libnxcrypto.a

.PHONY: all clean $(LIBS)

all: $(TARGET)

$(NXCRYPTO)/build/libnxcrypto.a:
	$(MAKE) -C $(NXCRYPTO)

$(NXTLS)/build/libnxtls.a:
	$(MAKE) -C $(NXTLS)

x509_fixture.h: gen_x509_fixture.py
	V=$$($(FETCH)) && python3 -I gen_x509_fixture.py "$$V/x509-limbo/limbo.json" $@

cryptotest.o: x509_fixture.h

$(TARGET): $(OBJS) linker.ld $(LIBS)
	$(LD) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)
	@echo "Built: $(TARGET)"

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET) x509_fixture.h
