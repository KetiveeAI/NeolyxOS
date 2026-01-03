/*
 * NeolyxOS Userspace Syscall Interface
 * 
 * This header provides syscall wrappers for user programs.
 * Include this to access NeolyxOS kernel services.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_SYSCALL_H
#define NEOLYX_SYSCALL_H

/* ============ Type Definitions ============ */

typedef unsigned char      uint8_t;
typedef unsigned short     uint16_t;
typedef unsigned int       uint32_t;
typedef unsigned long long uint64_t;
typedef signed int         int32_t;
typedef long long          int64_t;
typedef long long          ssize_t;
typedef unsigned long long size_t;

/* ============ Syscall Numbers ============ */

/* Process Control */
#define SYS_EXIT        1
#define SYS_SLEEP       2
#define SYS_TIME        3
#define SYS_YIELD       4
#define SYS_GETPID      5
#define SYS_GETPPID     6
#define SYS_FORK        7
#define SYS_EXEC        8
#define SYS_WAIT        9

/* Memory */
#define SYS_BRK         10

/* File I/O */
#define SYS_OPEN        20
#define SYS_CLOSE       21
#define SYS_READ        22
#define SYS_WRITE       24
#define SYS_SEEK        25
#define SYS_STAT        26

/* Directory Operations */
#define SYS_READDIR     30
#define SYS_MKDIR       31
#define SYS_RMDIR       32
#define SYS_CHDIR       33
#define SYS_GETCWD      34

/* File Descriptors */
#define STDIN_FD        0
#define STDOUT_FD       1
#define STDERR_FD       2

/* Open Flags */
#define O_RDONLY        0x0001
#define O_WRONLY        0x0002
#define O_RDWR          0x0003
#define O_CREATE        0x0100
#define O_TRUNC         0x0200
#define O_APPEND        0x0400

/* Seek Modes */
#define SEEK_SET        0
#define SEEK_CUR        1
#define SEEK_END        2

/* ============ Syscall Wrappers ============ */

static inline long _syscall0(long num) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long _syscall1(long num, long arg1) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num), "D" (arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long _syscall2(long num, long arg1, long arg2) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num), "D" (arg1), "S" (arg2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline long _syscall3(long num, long arg1, long arg2, long arg3) {
    long ret;
    __asm__ volatile (
        "syscall"
        : "=a" (ret)
        : "a" (num), "D" (arg1), "S" (arg2), "d" (arg3)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* ============ Process Functions ============ */

static inline void exit(int code) {
    _syscall1(SYS_EXIT, code);
    while (1) { }  /* Never returns */
}

static inline void sleep(uint32_t ms) {
    _syscall1(SYS_SLEEP, ms);
}

static inline uint64_t time(void) {
    return _syscall0(SYS_TIME);
}

static inline void yield(void) {
    _syscall0(SYS_YIELD);
}

static inline int32_t getpid(void) {
    return _syscall0(SYS_GETPID);
}

static inline int32_t getppid(void) {
    return _syscall0(SYS_GETPPID);
}

static inline int32_t fork(void) {
    return _syscall0(SYS_FORK);
}

static inline int exec(const char *path, char **argv, char **envp) {
    return _syscall3(SYS_EXEC, (long)path, (long)argv, (long)envp);
}

static inline int32_t wait(int32_t pid, int *status) {
    return _syscall2(SYS_WAIT, pid, (long)status);
}

/* ============ File I/O Functions ============ */

static inline int open(const char *path, int flags) {
    return _syscall2(SYS_OPEN, (long)path, flags);
}

static inline int close(int fd) {
    return _syscall1(SYS_CLOSE, fd);
}

static inline ssize_t read(int fd, void *buf, size_t count) {
    return _syscall3(SYS_READ, fd, (long)buf, count);
}

static inline ssize_t write(int fd, const void *buf, size_t count) {
    return _syscall3(SYS_WRITE, fd, (long)buf, count);
}

/* ============ Directory Functions ============ */

static inline int mkdir(const char *path, int mode) {
    return _syscall2(SYS_MKDIR, (long)path, mode);
}

static inline int rmdir(const char *path) {
    return _syscall1(SYS_RMDIR, (long)path);
}

static inline int chdir(const char *path) {
    return _syscall1(SYS_CHDIR, (long)path);
}

static inline int getcwd(char *buf, size_t size) {
    return _syscall2(SYS_GETCWD, (long)buf, size);
}

/* ============ Utility Functions ============ */

static inline int strlen(const char *s) {
    int len = 0;
    while (s[len]) len++;
    return len;
}

static inline void print(const char *msg) {
    write(STDOUT_FD, msg, strlen(msg));
}

static inline void println(const char *msg) {
    print(msg);
    print("\n");
}

#endif /* NEOLYX_SYSCALL_H */
