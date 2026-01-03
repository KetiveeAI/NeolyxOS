#ifndef STRING_H
#define STRING_H

#include <stddef.h>
#include <stdint.h>

// Standard string functions for freestanding environment
void* memcpy(void* dest, const void* src, size_t n);
void* memmove(void* dest, const void* src, size_t n);
void* memset(void* s, int c, size_t n);
int memcmp(const void* s1, const void* s2, size_t n);

char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strcat(char* dest, const char* src);
char* strncat(char* dest, const char* src, size_t n);
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
size_t strlen(const char* s);
char* strchr(const char* s, int c);
char* strrchr(const char* s, int c);
char* strstr(const char* haystack, const char* needle);

// Additional utility functions
void strrev(char* str);
int strtol(const char* str, char** endptr, int base);
unsigned long strtoul(const char* str, char** endptr, int base);

#endif // STRING_H 