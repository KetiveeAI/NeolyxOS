/*
 * hello - the smallest NeolyxOS C program built on nxlibc.
 *
 * Prints a greeting, then opens a file with fopen and reports what it
 * found. Build it with the Makefile next to it; install the result as
 * /apps/hello and launch it from the app drawer. Output appears in the
 * kernel debug log (serial console) until the kernel gives programs a
 * standard output (ABI flag NXSYS_ABI_FLAG_FILEIO).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <stdio.h>
#include <string.h>
#include <errno.h>

int main(int argc, char **argv) {
    const char *self = (argc > 0 && argv[0] && argv[0][0] == '/') ? argv[0] : "/apps/hello";

    printf("[HELLO] Hello from NeolyxOS, built on nxlibc!\n");

    FILE *f = fopen(self, "rb");
    if (!f) {
        printf("[HELLO] cannot open %s: %s\n", self, strerror(errno));
        return 1;
    }
    unsigned char magic[4];
    size_t got = fread(magic, 1, sizeof magic, f);
    if (got != sizeof magic || fseek(f, 0, SEEK_END) != 0) {
        printf("[HELLO] cannot read %s: %s\n", self, ferror(f) ? strerror(errno) : "file too short");
        fclose(f);
        return 1;
    }
    long size = ftell(f);
    fclose(f);

    int elf = memcmp(magic, "\x7f" "ELF", 4) == 0;
    printf("[HELLO] %s: %ld bytes, %s\n", self, size, elf ? "an ELF program" : "not an ELF file");
    printf("[HELLO] done\n");
    return elf ? 0 : 1;
}
