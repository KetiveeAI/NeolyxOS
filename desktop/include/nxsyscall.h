/*
 * NeolyxOS Userspace System Call Interface
 * 
 * Provides userspace wrappers for kernel syscalls.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NXSYSCALL_H
#define NXSYSCALL_H

#include <stdint.h>

/* ============ Syscall Numbers (MUST MATCH kernel/include/core/syscall.h) ============ */

/* Process Control */
#define SYS_EXIT        1
#define SYS_SLEEP       2   /* Kernel: line 36 */
#define SYS_TIME        3   /* Kernel: line 37 (use SYS_TIME, not SYS_GETTIME) */
#define SYS_YIELD       4
#define SYS_GETPID      5

/* Memory Management */
#define SYS_BRK         10
#define SYS_MMAP        11
#define SYS_MUNMAP      12

/* File I/O (VFS-backed) */
#define SYS_OPEN        20
#define SYS_CLOSE       21
#define SYS_READ        22
#define SYS_WRITE       23
#define SYS_STAT        25

/* Framebuffer (120-122) */
#define SYS_FB_MAP      120
#define SYS_FB_INFO     121
#define SYS_FB_FLIP     122

/* Input (123-124) */
#define SYS_INPUT_POLL      123
#define SYS_INPUT_REGISTER  124

/* ============ Data Structures ============ */

/* Framebuffer info structure - MUST MATCH KERNEL'S fb_info_t EXACTLY! */
typedef struct {
    uint64_t base_addr;     /* Physical/mapped address of framebuffer */
    uint32_t width;         /* Width in pixels */
    uint32_t height;        /* Height in pixels */
    uint32_t pitch;         /* Bytes per row */
    uint32_t bpp;           /* Bits per pixel (usually 32) */
    uint32_t format;        /* Pixel format (0=BGRA, 1=RGBA) */
    uint64_t size;          /* Total framebuffer size in bytes */
} fb_info_t;

/* Input event structure - MUST MATCH KERNEL'S input_event_t EXACTLY! */
typedef struct {
    uint32_t type;          /* 0=keyboard, 1=mouse_move, 2=mouse_button */
    uint32_t timestamp;     /* Milliseconds since boot */
    union {
        struct {
            uint8_t scancode;
            uint8_t ascii;
            uint8_t pressed;    /* 1=pressed, 0=released */
            uint8_t modifiers;  /* shift, ctrl, alt flags */
        } keyboard;
        struct {
            int16_t dx;         /* Delta X movement */
            int16_t dy;         /* Delta Y movement */
            uint8_t buttons;    /* Button state bitmap */
            uint8_t _pad[3];
        } mouse;
    };
} input_event_t;

/* Input event types */
#define INPUT_TYPE_KEYBOARD     0
#define INPUT_TYPE_MOUSE_MOVE   1
#define INPUT_TYPE_MOUSE_BUTTON 2

/* ============ Syscall Macros ============ */

/* Inline syscall with 0-3 arguments */
static inline int64_t syscall0(int64_t num) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall1(int64_t num, int64_t a1) {
    int64_t ret;
    /* Force num into a register that compiler can't reuse */
    register int64_t syscall_num __asm__("rax") = num;
    register int64_t arg1 __asm__("rdi") = a1;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "r"(syscall_num), "r"(arg1)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall2(int64_t num, int64_t a1, int64_t a2) {
    int64_t ret;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2)
        : "rcx", "r11", "memory"
    );
    return ret;
}

static inline int64_t syscall3(int64_t num, int64_t a1, int64_t a2, int64_t a3) {
    int64_t ret;
    register int64_t r10 __asm__("r10") = a3;
    __asm__ volatile(
        "syscall"
        : "=a"(ret)
        : "a"(num), "D"(a1), "S"(a2), "r"(r10)
        : "rcx", "r11", "memory"
    );
    return ret;
}

/* ============ API Wrappers ============ */

/* Get framebuffer info */
static inline int nx_fb_info(fb_info_t *info) {
    return (int)syscall1(SYS_FB_INFO, (int64_t)info);
}

/* Map framebuffer to userspace (returns address) */
static inline void* nx_fb_map(void) {
    return (void*)syscall0(SYS_FB_MAP);
}

/* Flip framebuffer (kernel does VSync wait + copy) */
static inline int nx_fb_flip(void *backbuffer) {
    return (int)syscall1(SYS_FB_FLIP, (int64_t)backbuffer);
}

/* Poll for input events */
static inline int nx_input_poll(input_event_t *event) {
    /* SYS_INPUT_POLL takes (ptr, max_events) */
    return (int)syscall2(SYS_INPUT_POLL, (int64_t)event, 1);
}

/* Register for input events */
static inline int nx_input_register(int type) {
    return (int)syscall1(SYS_INPUT_REGISTER, type);
}

/* Memory allocation via brk() syscall - Windows/macOS style heap management.
 * Uses kernel's 64MB heap reserve set up at boot.
 * This replaces the old 512KB static buffer. */

/* Internal: call brk syscall */
static inline int64_t nx_brk(uint64_t addr) {
    return (int64_t)syscall1(SYS_BRK, addr);
}

/* Simple sbrk-style allocator using brk syscall */
static inline void* nx_alloc(uint64_t size) {
    static uint64_t heap_end = 0;
    static int initialized = 0;
    
    /* First call: get current break from kernel */
    if (!initialized) {
        int64_t result = nx_brk(0);
        if (result < 0) {
            return (void*)0;  /* brk not available */
        }
        heap_end = (uint64_t)result;
        initialized = 1;
    }
    
    if (size == 0) return (void*)0;
    
    /* Align size to 16 bytes (standard malloc alignment) */
    size = (size + 15) & ~15;
    
    /* Request more heap from kernel */
    uint64_t new_end = heap_end + size;
    int64_t result = nx_brk(new_end);
    
    if (result < 0 || (uint64_t)result < new_end) {
        return (void*)0;  /* Allocation failed */
    }
    
    void *ptr = (void*)heap_end;
    heap_end = new_end;
    return ptr;
}

static inline void nx_free(void *ptr) {
    /* Simple allocator doesn't track individual allocations.
     * For proper free(), would need a more complex allocator (dlmalloc, etc.)
     * This is acceptable for desktop shell - Windows Explorer works similarly. */
    (void)ptr;
}

/* Get system time (milliseconds since boot) */
static inline uint64_t nx_gettime(void) {
    return (uint64_t)syscall0(SYS_TIME);
}

/* Sleep for milliseconds */
static inline int nx_sleep(uint64_t ms) {
    return (int)syscall1(SYS_SLEEP, ms);
}

/* Exit process */
static inline void nx_exit(int code) {
    syscall1(SYS_EXIT, code);
    /* Should not return */
    while(1) {}
}

/* Yield to scheduler */
static inline void nx_yield(void) {
    syscall0(SYS_YIELD);
}

/* Debug print to serial (uses SYS_DEBUG_PRINT syscall 127) */
static inline void nx_debug_print(const char *msg) {
    /* SYS_DEBUG_PRINT = 127 - prints directly to serial console */
    syscall1(127, (int64_t)msg);
}

/* ============ RTC (Real-Time Clock) ============ */
/* Syscall numbers - MUST MATCH kernel */
#define SYS_RTC_GET     130
#define SYS_RTC_SET     131
#define SYS_RTC_UNIX    132

/* RTC time structure - MUST MATCH kernel's rtc_time_t */
typedef struct {
    uint16_t year;
    uint8_t  month;
    uint8_t  day;
    uint8_t  hour;
    uint8_t  minute;
    uint8_t  second;
    uint8_t  weekday;
} __attribute__((packed)) rtc_time_t;

/* Get real-time clock value */
static inline int nx_rtc_get(rtc_time_t *time) {
    return (int)syscall1(SYS_RTC_GET, (int64_t)time);
}

/* Set real-time clock value (requires privilege) */
static inline int nx_rtc_set(const rtc_time_t *time) {
    return (int)syscall1(SYS_RTC_SET, (int64_t)time);
}

/* Get Unix timestamp (seconds since 1970-01-01) */
static inline uint64_t nx_rtc_unix(void) {
    return (uint64_t)syscall0(SYS_RTC_UNIX);
}

#endif /* NXSYSCALL_H */
