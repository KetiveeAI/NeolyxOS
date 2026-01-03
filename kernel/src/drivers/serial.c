// kernel/src/drivers/serial.c
// Serial port driver for debugging output (COM1)
// NeolyxOS - Copyright (c) 2025 KetiveeAI

#include <stdint.h>

#define COM1_PORT 0x3F8

// Port I/O functions
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile ("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile ("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

// Check if serial port is ready to transmit
static int serial_is_transmit_empty(void) {
    return inb(COM1_PORT + 5) & 0x20;
}

// Initialize serial port
void serial_init(void) {
    outb(COM1_PORT + 1, 0x00);    // Disable all interrupts
    outb(COM1_PORT + 3, 0x80);    // Enable DLAB (set baud rate divisor)
    outb(COM1_PORT + 0, 0x03);    // Set divisor to 3 (lo byte) 38400 baud
    outb(COM1_PORT + 1, 0x00);    //                  (hi byte)
    outb(COM1_PORT + 3, 0x03);    // 8 bits, no parity, one stop bit
    outb(COM1_PORT + 2, 0xC7);    // Enable FIFO, clear them, with 14-byte threshold
    outb(COM1_PORT + 4, 0x0B);    // IRQs enabled, RTS/DSR set
    outb(COM1_PORT + 4, 0x1E);    // Set in loopback mode, test the serial chip
    outb(COM1_PORT + 0, 0xAE);    // Test serial chip (send byte 0xAE and check if serial returns same byte)
    
    // Check if serial is faulty (i.e: not same byte as sent)
    if(inb(COM1_PORT + 0) != 0xAE) {
        return; // Serial port is faulty
    }
    
    // If serial is not faulty set it in normal operation mode
    outb(COM1_PORT + 4, 0x0F);
}

// Write a single character to serial
void serial_putc(char c) {
    while (serial_is_transmit_empty() == 0);
    outb(COM1_PORT, c);
}

// Write a string to serial
void serial_puts(const char* s) {
    while (*s) {
        if (*s == '\n') {
            serial_putc('\r');
        }
        serial_putc(*s++);
    }
}

// Write a hex value to serial
void serial_puthex(uint64_t val) {
    static const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

// Debug print with prefix
void serial_debug(const char* msg) {
    serial_puts("[DEBUG] ");
    serial_puts(msg);
    serial_puts("\n");
}

// Check if data is available to read
int serial_data_ready(void) {
    return inb(COM1_PORT + 5) & 0x01;
}

// Read a single character from serial (blocking)
char serial_getc(void) {
    while (!serial_data_ready());
    return inb(COM1_PORT);
}

// Read a single character (non-blocking, returns -1 if no data)
int serial_getc_nonblock(void) {
    if (serial_data_ready()) {
        return inb(COM1_PORT);
    }
    return -1;
}

