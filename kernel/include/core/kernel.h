// kernel/kernel.h
#ifndef KERNEL_H
#define KERNEL_H

#include <stdint.h>
#include <stddef.h>

#ifndef _SSIZE_T_DEFINED
#define _SSIZE_T_DEFINED
typedef long long ssize_t;
#endif

// Define off_t for freestanding environment
typedef int64_t off_t;

// Kernel version
#define KERNEL_VERSION "1.0.0"
#define KERNEL_ARCH "x86_64"

// Kernel main function
void kernel_main(void);

// Memory management
void init_memory(void);
void* memory_alloc(size_t size);
void memory_free(void* ptr);
void* memory_mmap(void* addr, size_t length, int prot, int flags, int fd, off_t offset);
int memory_munmap(void* addr, size_t length);

// Kernel memory allocation
// Implemented in src/mm/heap.c
void* kmalloc(size_t size);
void kfree(void* ptr);

// Architecture initialization
void init_architecture(void);

// System call initialization
void init_system_calls(void);

// Process management initialization
void init_process_management(void);

// Runtime system initialization
void init_runtime_system(void);

// Hardware drivers initialization
void init_hardware_drivers(void);

// Network subsystem initialization
void init_network_subsystem(void);

// Storage subsystem initialization
void init_storage_subsystem(void);

// System halt
void halt(void);

// File operations
struct file;
int file_open(const char* path, int flags, int mode);
int file_close(int fd);
ssize_t file_read(int fd, char* buf, size_t count);
ssize_t file_write(int fd, const char* buf, size_t count);

// Network operations
struct sockaddr;
typedef uint32_t socklen_t;
int network_init(void);
int network_socket(int domain, int type, int protocol);
int network_connect(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int network_bind(int sockfd, const struct sockaddr* addr, socklen_t addrlen);
int network_listen(int sockfd, int backlog);
int network_accept(int sockfd, struct sockaddr* addr, socklen_t* addrlen);
ssize_t network_send(int sockfd, const void* buf, size_t len, int flags);
ssize_t network_recv(int sockfd, void* buf, size_t len, int flags);

// Storage operations
int storage_init(void);

// Driver operations
int kernel_drivers_init(void);

// Video operations
void init_video(void);
void clear_screen(void);
void print(const char* str);
void print_char(char c);

// Keyboard operations
char keyboard_getchar(void);

// String operations
int strcmp(const char* s1, const char* s2);
int strncmp(const char* s1, const char* s2, size_t n);
size_t strlen(const char* s);
char* strcpy(char* dest, const char* src);
char* strncpy(char* dest, const char* src, size_t n);
char* strrchr(const char* s, int c);

// Memory operations
void* memset(void* s, int c, size_t n);
void* memcpy(void* dest, const void* src, size_t n);

#endif // KERNEL_H 