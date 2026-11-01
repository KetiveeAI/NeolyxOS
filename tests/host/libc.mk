# nxlibc host tests
#
#   make -f libc.mk run     build sdk/libc, then build and run every test
#                           twice: against today's kernel ABI and against
#                           the S3 file-I/O ABI (NXC_TEST_FILEIO=1)
#
# The tests link the very objects that go into libnxc.a for NeolyxOS
# (same compiler flags, same x86_64 code); only src/nx/syscall.c is
# replaced, by libc_host_sys.c, which answers system calls the way the
# NeolyxOS kernel does using Linux system calls. No host C library is
# linked, so a test can only pass on nxlibc's own code.
#
# Copyright (c) 2025 KetiveeAI
# SPDX-License-Identifier: MIT

ROOT    := $(abspath ../..)
LIBC    := $(ROOT)/sdk/libc
include $(LIBC)/nxlibc.mk

CC ?= gcc
LD ?= ld
AR ?= ar
OUT := build/libc

TEST_CFLAGS := $(NXLIBC_CFLAGS) -O1 -Wall -Wextra -Wno-format-truncation \
               -Werror=implicit-function-declaration -Werror=implicit-int \
               -Werror=int-conversion -Werror=incompatible-pointer-types

TESTS := libc_printf_test libc_strtod_test libc_malloc_test libc_stdio_test \
         libc_time_test libc_misc_test

.PHONY: run lib clean-libc
.SUFFIXES:

run: $(TESTS:%=$(OUT)/%) $(OUT)/libc_abi_check $(OUT)/libc_math_test
	@$(OUT)/libc_abi_check
	@$(OUT)/libc_math_test
	@echo "== allocator misuse must abort (status 134)"
	@$(OUT)/libc_malloc_test --double-free 2>/dev/null; test $$? -eq 134
	@$(OUT)/libc_malloc_test --bad-free 2>/dev/null; test $$? -eq 134
	@set -e; for t in $(TESTS); do \
	    echo "== $$t (kernel ABI 1.4)"; \
	    (cd $(OUT) && ./$$t 2>&1); \
	    echo "== $$t (S3 FILEIO ABI)"; \
	    (cd $(OUT) && NXC_TEST_FILEIO=1 ./$$t 2>&1); \
	done
	@echo "nxlibc host tests: ALL PASS"

lib:
	@$(MAKE) --no-print-directory -C $(LIBC)

$(OUT)/libnxc_host.a: lib
	@mkdir -p $(OUT)
	@rm -f $@
	@$(AR) rcs $@ $(filter-out %/nx/syscall.o,$(shell find $(LIBC)/build/obj -name '*.o' | sort))

$(OUT)/libc_host_sys.o: libc_host_sys.c
	@mkdir -p $(OUT)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(OUT)/%.o: %.c libc_test.h
	@mkdir -p $(OUT)
	$(CC) $(TEST_CFLAGS) -c $< -o $@

$(OUT)/libc_printf_test.o: libc_printf_expected.h
$(OUT)/libc_printf_test.o: TEST_CFLAGS += -Wno-format -Wno-format-overflow
$(OUT)/libc_misc_test.o: TEST_CFLAGS += -Wno-infinite-recursion

$(OUT)/%: $(OUT)/%.o $(OUT)/libc_host_sys.o $(OUT)/libnxc_host.a
	$(LD) -nostdlib -static -no-pie -z noexecstack -T $(LIBC)/crt/nxapp.ld \
	      -o $@ $(OUT)/libc_host_sys.o $< $(OUT)/libnxc_host.a

# Struct layouts, syscall numbers and errno values against the kernel's
# own headers. A host program: it only compares numbers.
$(OUT)/libc_abi_kernel.o: libc_abi_kernel.c libc_abi_list.h $(wildcard $(ROOT)/kernel/include/core/*.h $(ROOT)/kernel/include/core/*.def $(ROOT)/kernel/include/fs/vfs.h $(ROOT)/kernel/include/net/*.h)
	@mkdir -p $(OUT)
	$(CC) -std=c11 -m64 -ffreestanding -fno-pie -nostdinc -isystem $(NXLIBC_GCC_INCLUDE) \
	      -iquote $(ROOT)/kernel/include -iquote . -c $< -o $@

$(OUT)/libc_abi_check.o: libc_abi_check.c libc_abi_list.h $(LIBC)/include/nx/sys.h

$(OUT)/libc_abi_check: $(OUT)/libc_abi_check.o $(OUT)/libc_abi_kernel.o $(OUT)/libc_host_sys.o $(OUT)/libnxc_host.a
	$(LD) -nostdlib -static -no-pie -z noexecstack -T $(LIBC)/crt/nxapp.ld \
	      -o $@ $(OUT)/libc_host_sys.o $(OUT)/libc_abi_check.o $(OUT)/libc_abi_kernel.o $(OUT)/libnxc_host.a

clean-libc:
	rm -rf $(OUT)

# Math accuracy: a host program (host long double math as the reference)
# with nxlibc's math objects renamed to nx_*.
$(OUT)/nx_math.o: lib
	@mkdir -p $(OUT)
	@$(LD) -r -o $(OUT)/nx_math_all.o $(LIBC)/build/obj/math/math.o $(LIBC)/build/obj/math/kernels.o
	@objcopy --prefix-symbols=nx_ $(OUT)/nx_math_all.o $@

$(OUT)/libc_math_test: libc_math_test.c $(OUT)/nx_math.o
	$(CC) -std=gnu11 -O1 -Wall -Wextra -fno-pie -no-pie -o $@ $< $(OUT)/nx_math.o -lm
