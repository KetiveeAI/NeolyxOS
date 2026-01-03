#include "utils/log.h"
#include "core/kernel.h"
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

// Define uintptr_t if not available
#ifndef UINTPTR_MAX
typedef uint64_t uintptr_t;
#endif

static int log_initialized = 0;

// Forward declarations for helper functions
static void int_to_string(int value, char* buffer, size_t buffer_size);
static void uint_to_string(unsigned int value, char* buffer, size_t buffer_size);
static void uint_to_hex_string(unsigned int value, char* buffer, size_t buffer_size);
static void ptr_to_hex_string(void* ptr, char* buffer, size_t buffer_size) __attribute__((unused));

// Simple string formatting for basic types
static void format_string(char* buffer, size_t buffer_size, const char* fmt, va_list args) {
    size_t pos = 0;
    const char* p = fmt;
    
    while (*p && pos < buffer_size - 1) {
        if (*p == '%') {
            p++;
            switch (*p) {
                case 's': {
                    const char* str = va_arg(args, const char*);
                    if (str) {
                        while (*str && pos < buffer_size - 1) {
                            buffer[pos++] = *str++;
                        }
                    }
                    break;
                }
                case 'd': {
                    int val = va_arg(args, int);
                    char num_buf[32];
                    int_to_string(val, num_buf, sizeof(num_buf));
                    const char* num_str = num_buf;
                    while (*num_str && pos < buffer_size - 1) {
                        buffer[pos++] = *num_str++;
                    }
                    break;
                }
                case 'u': {
                    unsigned int val = va_arg(args, unsigned int);
                    char num_buf[32];
                    uint_to_string(val, num_buf, sizeof(num_buf));
                    const char* num_str = num_buf;
                    while (*num_str && pos < buffer_size - 1) {
                        buffer[pos++] = *num_str++;
                    }
                    break;
                }
                case 'x': {
                    unsigned int val = va_arg(args, unsigned int);
                    char num_buf[32];
                    uint_to_hex_string(val, num_buf, sizeof(num_buf));
                    const char* num_str = num_buf;
                    while (*num_str && pos < buffer_size - 1) {
                        buffer[pos++] = *num_str++;
                    }
                    break;
                }
                case 'p': {
                    void* ptr = va_arg(args, void*);
                    char num_buf[32];
                    ptr_to_hex_string(ptr, num_buf, sizeof(num_buf));
                    const char* num_str = num_buf;
                    while (*num_str && pos < buffer_size - 1) {
                        buffer[pos++] = *num_str++;
                    }
                    break;
                }
                case '%': {
                    buffer[pos++] = '%';
                    break;
                }
                default: {
                    buffer[pos++] = '%';
                    if (*p) buffer[pos++] = *p;
                    break;
                }
            }
            p++;
        } else {
            buffer[pos++] = *p++;
        }
    }
    buffer[pos] = '\0';
}

// Helper functions for number formatting
static void int_to_string(int value, char* buffer, size_t buffer_size) __attribute__((unused));
static void int_to_string(int value, char* buffer, size_t buffer_size) {
    if (value < 0) {
        buffer[0] = '-';
        value = -value;
        buffer++;
        buffer_size--;
    }
    uint_to_string((unsigned int)value, buffer, buffer_size);
}

static void uint_to_string(unsigned int value, char* buffer, size_t buffer_size) __attribute__((unused));
static void uint_to_string(unsigned int value, char* buffer, size_t buffer_size) {
    char temp[32];
    int i = 0;
    
    if (value == 0) {
        temp[i++] = '0';
    } else {
        while (value > 0 && i < 31) {
            temp[i++] = '0' + (value % 10);
            value /= 10;
        }
    }
    
    // Reverse the string
    int j = 0;
    while (i > 0 && j < (int)buffer_size - 1) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
}

static void uint_to_hex_string(unsigned int value, char* buffer, size_t buffer_size) __attribute__((unused));
static void uint_to_hex_string(unsigned int value, char* buffer, size_t buffer_size) {
    char temp[32];
    int i = 0;
    
    if (value == 0) {
        temp[i++] = '0';
    } else {
        while (value > 0 && i < 31) {
            int digit = value % 16;
            temp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            value /= 16;
        }
    }
    
    // Reverse the string
    int j = 0;
    while (i > 0 && j < (int)buffer_size - 1) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
}

static void ptr_to_hex_string(void* ptr, char* buffer, size_t buffer_size) {
    (void)ptr; // Suppress unused parameter warning
    uintptr_t addr = (uintptr_t)ptr;
    char temp[32];
    int i = 0;
    
    if (addr == 0) {
        temp[i++] = '0';
    } else {
        while (addr > 0 && i < 31) {
            int digit = addr % 16;
            temp[i++] = (digit < 10) ? ('0' + digit) : ('a' + digit - 10);
            addr /= 16;
        }
    }
    
    // Reverse the string and add 0x prefix
    buffer[0] = '0';
    buffer[1] = 'x';
    int j = 2;
    while (i > 0 && j < (int)buffer_size - 1) {
        buffer[j++] = temp[--i];
    }
    buffer[j] = '\0';
}

/* Serial output - safe in graphics mode */
extern void serial_puts(const char *s);

void klog_init(const char* log_path) {
    (void)log_path;
    log_initialized = 1;
    serial_puts("[KLOG] Log initialized\n");
}

void klog(const char* msg) {
    if (!log_initialized) return;
    serial_puts("[KLOG] ");
    serial_puts(msg);
    serial_puts("\n");
}

// Formatted logging function
void klogf(const char* fmt, ...) {
    if (!log_initialized) return;
    
    char buffer[512];
    va_list args;
    va_start(args, fmt);
    format_string(buffer, sizeof(buffer), fmt, args);
    va_end(args);
    
    serial_puts("[KLOG] ");
    serial_puts(buffer);
    serial_puts("\n");
}

void klog_close(void) {
    log_initialized = 0;
    serial_puts("[KLOG] Log closed\n");
} 