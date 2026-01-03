/*
 * NeolyxOS Process Management
 * 
 * Process control, scheduling, and context switching.
 * 
 * Design:
 *   - Process Control Block (PCB) for each process
 *   - Round-robin scheduler initially
 *   - Kernel threads first, then user mode
 *   - Fork/exec support later
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_PROCESS_H
#define NEOLYX_PROCESS_H

#include <stdint.h>
#include <stddef.h>

/* ============ Constants ============ */

#define MAX_PROCESSES       256
#define PROCESS_NAME_MAX    64
#define KERNEL_STACK_SIZE   (16 * 1024)  /* 16 KB */
#define USER_STACK_SIZE     (64 * 1024)  /* 64 KB */

/* Process states */
typedef enum {
    PROC_STATE_CREATED = 0,
    PROC_STATE_READY,
    PROC_STATE_RUNNING,
    PROC_STATE_BLOCKED,
    PROC_STATE_SLEEPING,
    PROC_STATE_ZOMBIE,
    PROC_STATE_DEAD,
} process_state_t;

/* Process flags */
#define PROC_FLAG_KERNEL    0x0001  /* Kernel process (ring 0) */
#define PROC_FLAG_USER      0x0002  /* User process (ring 3) */
#define PROC_FLAG_SYSTEM    0x0004  /* System process (cannot be killed) */
#define PROC_FLAG_DETACHED  0x0008  /* No parent waiting */

/* ============ CPU Context ============ */

/* Saved registers for context switch */
typedef struct {
    /* General purpose registers */
    uint64_t r15, r14, r13, r12;
    uint64_t r11, r10, r9, r8;
    uint64_t rbp, rdi, rsi, rdx, rcx, rbx, rax;
    
    /* Segment registers (for user mode) */
    uint64_t ds, es, fs, gs;
    
    /* Interrupt frame */
    uint64_t rip;
    uint64_t cs;
    uint64_t rflags;
    uint64_t rsp;
    uint64_t ss;
} __attribute__((packed)) cpu_context_t;

/* ============ Process Control Block ============ */

typedef struct process {
    /* Identity */
    uint32_t pid;                       /* Process ID */
    uint32_t ppid;                      /* Parent PID */
    char name[PROCESS_NAME_MAX];        /* Process name */
    
    /* State */
    process_state_t state;
    uint32_t flags;
    int exit_code;
    
    /* Scheduling */
    int priority;                       /* 0 = highest, 255 = lowest */
    uint64_t time_slice;                /* Remaining time slice (ticks) */
    uint64_t total_time;                /* Total CPU time used */
    uint64_t start_time;                /* When process was created */
    
    /* Memory */
    uint64_t page_directory;            /* CR3 value (page table) */
    uint64_t kernel_stack;              /* Kernel stack base */
    uint64_t kernel_stack_top;          /* Kernel stack top (SP) */
    uint64_t user_stack;                /* User stack base */
    uint64_t user_stack_top;            /* User stack top */
    uint64_t heap_start;
    uint64_t heap_end;
    
    /* CPU Context */
    cpu_context_t context;
    
    /* File descriptors */
    void *fd_table[16];                 /* Open file handles */
    
    /* Working directory */
    char cwd[256];
    
    /* Linked list for scheduler */
    struct process *next;
    struct process *prev;
    
    /* Wait list (for blocked processes) */
    struct process *wait_next;
} process_t;

/* ============ Process API ============ */

/**
 * Initialize the process subsystem.
 */
void process_init(void);

/**
 * Create a new kernel thread.
 * 
 * @param name      Thread name
 * @param entry     Entry point function
 * @param arg       Argument to pass
 * @return Process ID or -1 on error
 */
typedef void (*thread_entry_t)(void *arg);
int32_t process_create_thread(const char *name, thread_entry_t entry, void *arg);

/**
 * Create a new user process (fork).
 * 
 * @return Child PID in parent, 0 in child, -1 on error
 */
int32_t process_fork(void);

/**
 * Replace current process with new executable (exec).
 * 
 * @param path Path to executable
 * @param argv Arguments
 * @param envp Environment
 * @return -1 on error (does not return on success)
 */
int process_exec(const char *path, char *const argv[], char *const envp[]);

/**
 * Exit current process.
 * 
 * @param exit_code Exit status
 */
void process_exit(int exit_code) __attribute__((noreturn));

/**
 * Wait for child process.
 * 
 * @param pid PID to wait for (-1 for any)
 * @param status Pointer to store exit status
 * @return PID of exited child or -1 on error
 */
int32_t process_wait(int32_t pid, int *status);

/**
 * Get current process.
 */
process_t *process_current(void);

/**
 * Get process by PID.
 */
process_t *process_get(uint32_t pid);

/**
 * Yield CPU to next process.
 */
void process_yield(void);

/**
 * Sleep for specified milliseconds.
 */
void process_sleep(uint32_t ms);

/**
 * Block current process.
 */
void process_block(void);

/**
 * Unblock a process.
 */
void process_unblock(process_t *proc);

/**
 * Kill a process.
 * 
 * @param pid Process to kill
 * @param signal Signal number
 * @return 0 on success, -1 on error
 */
int process_kill(uint32_t pid, int signal);

/* ============ Scheduler API ============ */

/**
 * Initialize the scheduler.
 */
void scheduler_init(void);

/**
 * Start the scheduler (called once from kernel_main).
 */
void scheduler_start(void) __attribute__((noreturn));

/**
 * Called by timer interrupt to switch processes.
 */
void scheduler_tick(void);

/**
 * Schedule next process (called from scheduler_tick or yield).
 */
void schedule(void);

/**
 * Add process to ready queue.
 */
void scheduler_add(process_t *proc);

/**
 * Remove process from ready queue.
 */
void scheduler_remove(process_t *proc);

/* ============ Context Switch ============ */

/**
 * Switch context from old to new process.
 * Implemented in assembly.
 */
extern void context_switch(cpu_context_t *old_ctx, cpu_context_t *new_ctx);

/**
 * Enter user mode (ring 3).
 * Implemented in assembly.
 */
extern void enter_usermode(uint64_t entry, uint64_t stack) __attribute__((noreturn));

#endif /* NEOLYX_PROCESS_H */
