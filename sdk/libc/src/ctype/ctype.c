/*
 * nxlibc - <ctype.h>, "C" locale.
 *
 * One class byte per character 0-127; 128-255 and EOF have no class.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include <ctype.h>
#include <stdint.h>

#define C_CNTRL  0x01
#define C_SPACE  0x02   /* space, \t \n \v \f \r */
#define C_BLANK  0x04   /* space, \t */
#define C_UPPER  0x08
#define C_LOWER  0x10
#define C_DIGIT  0x20
#define C_XDIGIT 0x40   /* 0-9 a-f A-F */
#define C_PUNCT  0x80

static uint8_t class_of(int c) {
    if (c < 0 || c > 127) return 0;
    if (c < 32 || c == 127) {
        uint8_t k = C_CNTRL;
        if (c >= '\t' && c <= '\r') k |= C_SPACE;
        if (c == '\t') k |= C_BLANK;
        return k;
    }
    if (c == ' ') return C_SPACE | C_BLANK;
    if (c >= '0' && c <= '9') return C_DIGIT | C_XDIGIT;
    if (c >= 'A' && c <= 'Z') return (uint8_t)(C_UPPER | (c <= 'F' ? C_XDIGIT : 0));
    if (c >= 'a' && c <= 'z') return (uint8_t)(C_LOWER | (c <= 'f' ? C_XDIGIT : 0));
    return C_PUNCT;     /* every other printable ASCII character */
}

int isalnum(int c)  { return (class_of(c) & (C_UPPER | C_LOWER | C_DIGIT)) != 0; }
int isalpha(int c)  { return (class_of(c) & (C_UPPER | C_LOWER)) != 0; }
int isblank(int c)  { return (class_of(c) & C_BLANK) != 0; }
int iscntrl(int c)  { return (class_of(c) & C_CNTRL) != 0; }
int isdigit(int c)  { return (class_of(c) & C_DIGIT) != 0; }
int isgraph(int c)  { return (class_of(c) & (C_UPPER | C_LOWER | C_DIGIT | C_PUNCT)) != 0; }
int islower(int c)  { return (class_of(c) & C_LOWER) != 0; }
int isprint(int c)  { return c == ' ' || isgraph(c); }
int ispunct(int c)  { return (class_of(c) & C_PUNCT) != 0; }
int isspace(int c)  { return (class_of(c) & C_SPACE) != 0; }
int isupper(int c)  { return (class_of(c) & C_UPPER) != 0; }
int isxdigit(int c) { return (class_of(c) & C_XDIGIT) != 0; }
int tolower(int c)  { return isupper(c) ? c + ('a' - 'A') : c; }
int toupper(int c)  { return islower(c) ? c - ('a' - 'A') : c; }
