/*
 * NeolyxOS x86_64 I/O Port Operations
 * 
 * Low-level port I/O for hardware access
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ARCH_IO_H
#define ARCH_IO_H

#include <stdint.h>

/* 8-bit I/O */
static inline void outb(uint16_t port, uint8_t val) {
    __asm__ volatile("outb %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint8_t inb(uint16_t port) {
    uint8_t ret;
    __asm__ volatile("inb %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 16-bit I/O */
static inline void outw(uint16_t port, uint16_t val) {
    __asm__ volatile("outw %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint16_t inw(uint16_t port) {
    uint16_t ret;
    __asm__ volatile("inw %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* 32-bit I/O */
static inline void outl(uint16_t port, uint32_t val) {
    __asm__ volatile("outl %0, %1" : : "a"(val), "Nd"(port));
}

static inline uint32_t inl(uint16_t port) {
    uint32_t ret;
    __asm__ volatile("inl %1, %0" : "=a"(ret) : "Nd"(port));
    return ret;
}

/* I/O delay */
static inline void io_wait(void) {
    outb(0x80, 0);
}

#endif /* ARCH_IO_H */
