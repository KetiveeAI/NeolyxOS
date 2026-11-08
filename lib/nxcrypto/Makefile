# nxcrypto - NeolyxOS cryptography library
#
#   make           build libnxcrypto.a for NeolyxOS (freestanding, x86_64)
#   make test      build the host tests and run them against the published
#                  vectors (downloads them once, see
#                  tests/host/crypto_fetch_vectors.sh)
#   make clean
#
# The library is freestanding: no libc, no allocation, general-purpose
# registers only (so the kernel can link it too). -Werror on the warnings
# GCC 15 turns into errors keeps the owner's toolchain building.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

CC      ?= gcc
AR      ?= ar
HOSTCC  ?= gcc

SRCS = src/util.c src/sha256.c src/sha512.c src/hash.c src/hmac.c \
       src/fe25519.c src/x25519.c src/ed25519.c \
       src/chacha20.c src/poly1305.c src/chachapoly.c src/aes.c src/gcm.c

STRICT  = -Werror=implicit-function-declaration -Werror=implicit-int \
          -Werror=int-conversion -Werror=incompatible-pointer-types

CFLAGS_TARGET = -std=c11 -O2 -Wall -Wextra $(STRICT) -Iinclude \
                -ffreestanding -fno-builtin -fno-stack-protector -nostdlib \
                -fno-pie -m64 -mno-red-zone -mgeneral-regs-only

CFLAGS_HOST   = -std=c11 -O2 -Wall -Wextra $(STRICT) -Iinclude

OBJDIR  = build
OBJS    = $(SRCS:src/%.c=$(OBJDIR)/%.o)
LIB     = $(OBJDIR)/libnxcrypto.a

TESTDIR = ../../tests/host
TESTS   = crypto_sha2_test crypto_curve25519_test crypto_aead_test

.PHONY: all test clean vectors ct-test

all: $(LIB)

$(OBJDIR)/%.o: src/%.c include/nxcrypto.h include/nxcrypto/*.h src/internal.h src/fe25519.h
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS_TARGET) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $^
	@echo "Built: $@"

$(OBJDIR)/host/%: $(TESTDIR)/%.c $(SRCS) $(TESTDIR)/crypto_vec.h include/nxcrypto.h include/nxcrypto/*.h src/internal.h src/fe25519.h
	@mkdir -p $(OBJDIR)/host
	$(HOSTCC) $(CFLAGS_HOST) -o $@ $< $(SRCS)

vectors:
	@$(TESTDIR)/crypto_fetch_vectors.sh > /dev/null

test: $(LIB) $(TESTS:%=$(OBJDIR)/host/%) vectors
	@set -e; V=$$($(TESTDIR)/crypto_fetch_vectors.sh); \
	for t in $(TESTS); do $(OBJDIR)/host/$$t "$$V"; done

clean:
	rm -rf $(OBJDIR)

# Constant-time check: secrets are marked undefined for Valgrind memcheck,
# which then reports any branch or memory index that depends on them.
ct-test: $(OBJDIR)/host/crypto_ct_test
	valgrind -q --error-exitcode=1 --track-origins=yes $(OBJDIR)/host/crypto_ct_test
