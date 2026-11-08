# nxtls - NeolyxOS TLS 1.3 client library (X.509, key schedule, records)
#
#   make           build build/libnxtls.a for NeolyxOS (freestanding, x86_64);
#                  link it together with lib/nxcrypto/build/libnxcrypto.a
#   make test      build the host tests and run them against the published
#                  vectors (downloads them once, see
#                  tests/host/crypto_fetch_vectors.sh)
#   make clean
#
# Freestanding like nxcrypto: no libc, no allocation, general-purpose
# registers only. -Werror on the warnings GCC 15 turns into errors.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

CC      ?= gcc
AR      ?= ar
HOSTCC  ?= gcc

CRYPTO  = ../nxcrypto
SRCS    = src/der.c src/names.c src/x509.c src/x509_verify.c \
          src/tls13_keys.c src/tls13_record.c
CRYPTO_SRCS = $(wildcard $(CRYPTO)/src/*.c)

STRICT  = -Werror=implicit-function-declaration -Werror=implicit-int \
          -Werror=int-conversion -Werror=incompatible-pointer-types

INCS    = -Iinclude -I$(CRYPTO)/include

CFLAGS_TARGET = -std=c11 -O2 -Wall -Wextra $(STRICT) $(INCS) \
                -ffreestanding -fno-builtin -fno-stack-protector -nostdlib \
                -fno-pie -m64 -mno-red-zone -mgeneral-regs-only

CFLAGS_HOST   = -std=c11 -O2 -Wall -Wextra $(STRICT) $(INCS)

OBJDIR  = build
OBJS    = $(SRCS:src/%.c=$(OBJDIR)/%.o)
LIB     = $(OBJDIR)/libnxtls.a
HDRS    = include/nxtls/*.h src/*.h $(CRYPTO)/include/nxcrypto.h $(CRYPTO)/include/nxcrypto/*.h

TESTDIR = ../../tests/host
TESTS   = crypto_x509_test crypto_x509_limbo_test crypto_tls13_trace_test

.PHONY: all test clean

all: $(LIB)

$(OBJDIR)/%.o: src/%.c $(HDRS)
	@mkdir -p $(OBJDIR)
	$(CC) $(CFLAGS_TARGET) -c $< -o $@

$(LIB): $(OBJS)
	$(AR) rcs $@ $^
	@echo "Built: $@"

$(OBJDIR)/host/%: $(TESTDIR)/%.c $(SRCS) $(CRYPTO_SRCS) $(HDRS) $(TESTDIR)/crypto_vec.h $(TESTDIR)/crypto_json.h $(TESTDIR)/crypto_pem.h
	@mkdir -p $(OBJDIR)/host
	$(HOSTCC) $(CFLAGS_HOST) -o $@ $< $(SRCS) $(CRYPTO_SRCS)

test: $(LIB) $(TESTS:%=$(OBJDIR)/host/%)
	@set -e; V=$$($(TESTDIR)/crypto_fetch_vectors.sh); \
	for t in $(TESTS); do $(OBJDIR)/host/$$t "$$V"; done

clean:
	rm -rf $(OBJDIR)
