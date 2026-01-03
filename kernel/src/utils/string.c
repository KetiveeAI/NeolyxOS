#include <utils/string.h>

// Memory operations
void* memcpy(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    for (size_t i = 0; i < n; i++) {
        d[i] = s[i];
    }
    return dest;
}

void* memmove(void* dest, const void* src, size_t n) {
    uint8_t* d = (uint8_t*)dest;
    const uint8_t* s = (const uint8_t*)src;
    if (d < s) {
        for (size_t i = 0; i < n; i++) {
            d[i] = s[i];
        }
    } else {
        for (size_t i = n; i > 0; i--) {
            d[i-1] = s[i-1];
        }
    }
    return dest;
}

void* memset(void* s, int c, size_t n) {
    uint8_t* p = (uint8_t*)s;
    for (size_t i = 0; i < n; i++) {
        p[i] = (uint8_t)c;
    }
    return s;
}

int memcmp(const void* s1, const void* s2, size_t n) {
    const uint8_t* p1 = (const uint8_t*)s1;
    const uint8_t* p2 = (const uint8_t*)s2;
    for (size_t i = 0; i < n; i++) {
        if (p1[i] != p2[i]) {
            return p1[i] - p2[i];
        }
    }
    return 0;
}

// String operations
char* strcpy(char* dest, const char* src) {
    char* d = dest;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncpy(char* dest, const char* src, size_t n) {
    char* d = dest;
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        d[i] = src[i];
    }
    for (; i < n; i++) {
        d[i] = '\0';
    }
    return dest;
}

char* strcat(char* dest, const char* src) {
    char* d = dest;
    while (*d) d++;
    while (*src) {
        *d++ = *src++;
    }
    *d = '\0';
    return dest;
}

char* strncat(char* dest, const char* src, size_t n) {
    char* d = dest;
    while (*d) d++;
    size_t i;
    for (i = 0; i < n && src[i]; i++) {
        d[i] = src[i];
    }
    d[i] = '\0';
    return dest;
}

int strcmp(const char* s1, const char* s2) {
    while (*s1 && *s2 && *s1 == *s2) {
        s1++;
        s2++;
    }
    return *s1 - *s2;
}

int strncmp(const char* s1, const char* s2, size_t n) {
    for (size_t i = 0; i < n; i++) {
        if (s1[i] != s2[i]) {
            return s1[i] - s2[i];
        }
        if (s1[i] == '\0') break;
    }
    return 0;
}

size_t strlen(const char* s) {
    size_t len = 0;
    while (s[len]) len++;
    return len;
}

char* strchr(const char* s, int c) {
    while (*s) {
        if (*s == (char)c) {
            return (char*)s;
        }
        s++;
    }
    return NULL;
}

char* strrchr(const char* s, int c) {
    char* last = NULL;
    while (*s) {
        if (*s == (char)c) {
            last = (char*)s;
        }
        s++;
    }
    return last;
}

char* strstr(const char* haystack, const char* needle) {
    if (!*needle) return (char*)haystack;
    
    for (const char* h = haystack; *h; h++) {
        const char* n = needle;
        const char* h2 = h;
        while (*n && *h2 && *n == *h2) {
            n++;
            h2++;
        }
        if (!*n) return (char*)h;
    }
    return NULL;
}

// Utility functions
void strrev(char* str) {
    size_t len = strlen(str);
    for (size_t i = 0; i < len / 2; i++) {
        char temp = str[i];
        str[i] = str[len - 1 - i];
        str[len - 1 - i] = temp;
    }
}

int strtol(const char* str, char** endptr, int base) {
    // Simple implementation for base 10
    if (base != 10) return 0;
    
    int result = 0;
    int sign = 1;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t') str++;
    
    // Handle sign
    if (*str == '-') {
        sign = -1;
        str++;
    } else if (*str == '+') {
        str++;
    }
    
    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    if (endptr) *endptr = (char*)str;
    return sign * result;
}

unsigned long strtoul(const char* str, char** endptr, int base) {
    // Simple implementation for base 10
    if (base != 10) return 0;
    
    unsigned long result = 0;
    
    // Skip whitespace
    while (*str == ' ' || *str == '\t') str++;
    
    // Skip sign for unsigned
    if (*str == '+' || *str == '-') str++;
    
    // Convert digits
    while (*str >= '0' && *str <= '9') {
        result = result * 10 + (*str - '0');
        str++;
    }
    
    if (endptr) *endptr = (char*)str;
    return result;
} 