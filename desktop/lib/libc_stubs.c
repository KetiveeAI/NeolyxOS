/*
 * NeolyxOS - libc Stubs for Freestanding Userspace
 * 
 * Provides standard C library functions needed by libnxrender.a
 * when running in freestanding mode (no libc).
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include <stddef.h>

/* Use the existing nx_malloc implementation */
extern void* nx_malloc(uint64_t size);
extern void nx_mfree(void *ptr);

/* Standard malloc wrapper */
void* malloc(size_t size) {
    return nx_malloc((uint64_t)size);
}

/* Standard free wrapper */
void free(void *ptr) {
    nx_mfree(ptr);
}

/* Standard calloc */
void* calloc(size_t num, size_t size) {
    size_t total = num * size;
    void *ptr = nx_malloc(total);
    if (ptr) {
        /* Zero memory */
        uint8_t *p = (uint8_t*)ptr;
        for (size_t i = 0; i < total; i++) {
            p[i] = 0;
        }
    }
    return ptr;
}

/* Standard realloc - simple implementation */
void* realloc(void *ptr, size_t size) {
    (void)ptr;  /* Can't actually reallocate with simple allocator */
    return nx_malloc(size);  /* Just allocate new block */
}

/* String length */
size_t strlen(const char *s) {
    size_t len = 0;
    while (s && s[len]) len++;
    return len;
}

/* String copy */
char* strcpy(char *dst, const char *src) {
    char *d = dst;
    while (src && *src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dst;
}

/* String copy with length */
char* strncpy(char *dst, const char *src, size_t n) {
    char *d = dst;
    size_t i;
    for (i = 0; i < n && src && src[i]; i++) {
        d[i] = src[i];
    }
    for (; i < n; i++) {
        d[i] = '\0';
    }
    return dst;
}

/* String compare */
int strcmp(const char *s1, const char *s2) {
    while (*s1 && (*s1 == *s2)) {
        s1++;
        s2++;
    }
    return *(const unsigned char*)s1 - *(const unsigned char*)s2;
}

/* memcpy, memset, memcmp are defined in userspace_stubs.c */

/* ============ Math Functions for Freestanding Build ============ */

/* Software square root using Newton-Raphson */
float sqrtf(float x) {
    if (x < 0.0f) return 0.0f;
    if (x == 0.0f) return 0.0f;
    
    float guess = x / 2.0f;
    for (int i = 0; i < 10; i++) {  /* 10 iterations is enough precision */
        guess = (guess + x / guess) / 2.0f;
    }
    return guess;
}

double sqrt(double x) {
    return (double)sqrtf((float)x);
}

/* Software sine using Taylor series approximation */
float sinf(float x) {
    /* Normalize x to [-PI, PI] */
    const float PI = 3.14159265358979f;
    const float TWO_PI = 6.28318530717959f;
    
    while (x > PI) x -= TWO_PI;
    while (x < -PI) x += TWO_PI;
    
    /* Taylor series: sin(x) = x - x^3/3! + x^5/5! - x^7/7! + ... */
    float x2 = x * x;
    float x3 = x2 * x;
    float x5 = x3 * x2;
    float x7 = x5 * x2;
    
    return x - (x3 / 6.0f) + (x5 / 120.0f) - (x7 / 5040.0f);
}

/* Software cosine using Taylor series approximation */
float cosf(float x) {
    /* cos(x) = sin(x + PI/2) */
    const float HALF_PI = 1.57079632679490f;
    return sinf(x + HALF_PI);
}

/* sincosf - compute both sin and cos together */
void sincosf(float x, float *sinp, float *cosp) {
    if (sinp) *sinp = sinf(x);
    if (cosp) *cosp = cosf(x);
}

/* ============ Fortify Stubs (GCC hardening) ============ */

/* __memcpy_chk - checked memcpy for -D_FORTIFY_SOURCE */
void* __memcpy_chk(void *dst, const void *src, size_t n, size_t dst_size) {
    (void)dst_size;  /* We ignore the check in freestanding mode */
    uint8_t *d = (uint8_t*)dst;
    const uint8_t *s = (const uint8_t*)src;
    while (n--) *d++ = *s++;
    return dst;
}

/* abs/fabs for completeness */
int abs(int x) {
    return x < 0 ? -x : x;
}

float fabsf(float x) {
    return x < 0.0f ? -x : x;
}

