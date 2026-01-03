// kernel/src/drivers/serial.h
// Serial port driver header
// NeolyxOS - Copyright (c) 2024 KetiveeAI

#ifndef SERIAL_H
#define SERIAL_H

#include <stdint.h>

// Initialize serial port (COM1)
void serial_init(void);

// Write a single character to serial
void serial_putc(char c);

// Write a string to serial
void serial_puts(const char* s);

// Write a hex value to serial
void serial_puthex(uint64_t val);

// Debug print with prefix
void serial_debug(const char* msg);

#endif // SERIAL_H
