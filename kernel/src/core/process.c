/*
 * NeolyxOS Process Management Implementation
 * 
 * Process control, scheduler, and context switching.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/process.h"
#include "mm/kheap.h"
#include "mm/pmm.h"
#include "core/panic.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/* ============ Process Table ============ */

/* Exported for nxtask_kdrv */
process_t *process_table[MAX_PROCESSES];
int process_table_size = MAX_PROCESSES;
static uint32_t next_pid = 1;

/* Current running process - exported */
process_t *current_process = NULL;

/* Idle process (runs when nothing else can) */
static process_t *idle_process = NULL;

/* Ready queue (doubly linked list) */
static process_t *ready_head = NULL;
static process_t *ready_tail = NULL;

/* Scheduler lock */
static volatile int scheduler_lock = 0;

/* ============ Helpers ============ */

static void *proc_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void proc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 11;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

static void serial_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* Assembly functions for user mode transition (arch.S)
 * enter_user_mode takes: rdi=user_stack, rsi=entry_point */
extern void enter_user_mode(uint64_t stack, uint64_t entry) __attribute__((noreturn));

/* ============ Process Table Operations ============ */

static process_t *alloc_process(void) {
    process_t *proc = (process_t *)kzalloc(sizeof(process_t));
    if (!proc) return NULL;
    
    proc->pid = next_pid++;
    proc->state = PROC_STATE_CREATED;
    proc->priority = 128;  /* Default priority */
    proc->time_slice = 10; /* 10 ticks = 10ms */
    
    if (proc->pid < MAX_PROCESSES) {
        process_table[proc->pid] = proc;
    }
    
    return proc;
}

static void free_process(process_t *proc) {
    if (!proc) return;
    
    if (proc->pid < MAX_PROCESSES) {
        process_table[proc->pid] = NULL;
    }
    
    /* Free stacks */
    if (proc->kernel_stack) {
        pmm_free_pages(proc->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
    }
    if (proc->user_stack) {
        pmm_free_pages(proc->user_stack, USER_STACK_SIZE / PAGE_SIZE);
    }
    
    kfree(proc);
}

/* ============ Ready Queue Operations ============ */

void scheduler_add(process_t *proc) {
    if (!proc || proc->state == PROC_STATE_DEAD) return;
    
    proc->state = PROC_STATE_READY;
    proc->next = NULL;
    proc->prev = ready_tail;
    
    if (ready_tail) {
        ready_tail->next = proc;
    } else {
        ready_head = proc;
    }
    ready_tail = proc;
}

void scheduler_remove(process_t *proc) {
    if (!proc) return;
    
    if (proc->prev) {
        proc->prev->next = proc->next;
    } else {
        ready_head = proc->next;
    }
    
    if (proc->next) {
        proc->next->prev = proc->prev;
    } else {
        ready_tail = proc->prev;
    }
    
    proc->next = NULL;
    proc->prev = NULL;
}

/* ============ Idle Process ============ */

static void idle_thread(void *arg) {
    (void)arg;
    while (1) {
        __asm__ volatile("hlt");
    }
}

/* ============ Process API ============ */

void process_init(void) {
    serial_puts("[PROC] Initializing process management...\n");
    
    proc_memset(process_table, 0, sizeof(process_table));
    
    /* Create idle process (PID 0) */
    idle_process = alloc_process();
    if (!idle_process) {
        kernel_panic("Failed to create idle process", 0);
    }
    
    idle_process->pid = 0;
    process_table[0] = idle_process;
    proc_strcpy(idle_process->name, "idle", PROCESS_NAME_MAX);
    idle_process->flags = PROC_FLAG_KERNEL | PROC_FLAG_SYSTEM;
    idle_process->state = PROC_STATE_READY;
    
    /* Allocate idle stack */
    idle_process->kernel_stack = pmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!idle_process->kernel_stack) {
        kernel_panic("Failed to allocate idle stack", 0);
    }
    idle_process->kernel_stack_top = idle_process->kernel_stack + KERNEL_STACK_SIZE;
    
    /* Set up idle context */
    idle_process->context.rip = (uint64_t)idle_thread;
    idle_process->context.rsp = idle_process->kernel_stack_top - 8;
    idle_process->context.cs = 0x08;
    idle_process->context.ss = 0x10;
    idle_process->context.rflags = 0x202;  /* IF enabled */
    
    current_process = idle_process;
    
    serial_puts("[PROC] Ready (idle created)\n");
}

int32_t process_create_thread(const char *name, thread_entry_t entry, void *arg) {
    process_t *proc = alloc_process();
    if (!proc) return -1;
    
    proc_strcpy(proc->name, name ? name : "thread", PROCESS_NAME_MAX);
    proc->flags = PROC_FLAG_KERNEL;
    proc->ppid = current_process ? current_process->pid : 0;
    proc->start_time = pit_get_ticks();
    proc_strcpy(proc->cwd, "/", 256);
    
    /* Allocate kernel stack */
    proc->kernel_stack = pmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!proc->kernel_stack) {
        free_process(proc);
        return -1;
    }
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
    
    /* Set up initial context */
    uint64_t *stack = (uint64_t *)proc->kernel_stack_top;
    
    /* Push argument */
    stack--;
    *stack = (uint64_t)arg;
    
    /* Set up context for context_switch */
    proc->context.rip = (uint64_t)entry;
    proc->context.rsp = (uint64_t)stack;
    proc->context.rdi = (uint64_t)arg;  /* First argument */
    proc->context.cs = 0x08;  /* Kernel code */
    proc->context.ss = 0x10;  /* Kernel data */
    proc->context.rflags = 0x202;  /* IF enabled */
    
    /* Add to ready queue */
    scheduler_add(proc);
    
    serial_puts("[PROC] Created thread '");
    serial_puts(proc->name);
    serial_puts("' (PID ");
    serial_dec(proc->pid);
    serial_puts(")\n");
    
    return proc->pid;
}

process_t *process_current(void) {
    return current_process;
}

process_t *process_get(uint32_t pid) {
    if (pid >= MAX_PROCESSES) return NULL;
    return process_table[pid];
}

void process_yield(void) {
    schedule();
}

void process_exit(int exit_code) {
    if (!current_process) {
        kernel_panic("Exit with no process", 0);
    }
    
    current_process->exit_code = exit_code;
    current_process->state = PROC_STATE_ZOMBIE;
    
    serial_puts("[PROC] Process ");
    serial_dec(current_process->pid);
    serial_puts(" exited with code ");
    serial_dec(exit_code);
    serial_puts("\n");
    
    /* TODO: Notify parent, clean up children */
    
    schedule();
    
    /* Should not reach here */
    while (1) __asm__ volatile("hlt");
}

void process_sleep(uint32_t ms) {
    /*
     * CRITICAL: Do NOT enable interrupts (STI) during syscall handling!
     * Timer interrupts during kernel syscall execution cause a GPF because
     * the ISR tries to IRET when the stack is set up for SYSRET.
     * 
     * Use pure busy-wait for now. This is inefficient but stable.
     * TODO: Proper solution is to switch to a dedicated kernel thread
     * that can safely receive interrupts, or use a timer callback.
     */
    
    /* Pure busy-wait - ~100k loops ≈ 1ms on QEMU */
    uint64_t delay_loops = (uint64_t)ms * 100000ULL;
    for (volatile uint64_t i = 0; i < delay_loops; i++) {
        __asm__ volatile("pause");
    }
}

void process_block(void) {
    if (!current_process) return;
    
    current_process->state = PROC_STATE_BLOCKED;
    schedule();
}

void process_unblock(process_t *proc) {
    if (!proc || proc->state != PROC_STATE_BLOCKED) return;
    
    scheduler_add(proc);
}

int process_kill(uint32_t pid, int signal) {
    process_t *proc = process_get(pid);
    if (!proc) return -1;
    
    if (proc->flags & PROC_FLAG_SYSTEM) {
        serial_puts("[PROC] Cannot kill system process\n");
        return -1;
    }
    
    proc->state = PROC_STATE_ZOMBIE;
    proc->exit_code = 128 + signal;
    
    return 0;
}

/* ============ Scheduler ============ */

void scheduler_init(void) {
    serial_puts("[SCHED] Initializing scheduler...\n");
    /* Already done in process_init */
    serial_puts("[SCHED] Ready (round-robin)\n");
}

void schedule(void) {
    if (scheduler_lock) return;
    scheduler_lock = 1;
    
    process_t *old = current_process;
    process_t *next = NULL;
    
    /* Find next ready process */
    if (ready_head) {
        next = ready_head;
        scheduler_remove(next);
    } else {
        next = idle_process;
    }
    
    if (next == old) {
        scheduler_lock = 0;
        return;
    }
    
    /* Put old process back in queue if still runnable */
    if (old && old->state == PROC_STATE_RUNNING) {
        old->state = PROC_STATE_READY;
        scheduler_add(old);
    }
    
    /* Switch to new process */
    next->state = PROC_STATE_RUNNING;
    next->time_slice = 10;  /* Reset time slice */
    current_process = next;
    
    scheduler_lock = 0;
    
    /* Perform context switch */
    context_switch(&old->context, &next->context);
}

void scheduler_tick(void) {
    if (!current_process) return;
    
    current_process->total_time++;
    
    if (current_process->time_slice > 0) {
        current_process->time_slice--;
    }
    
    /* Time slice exhausted - schedule next */
    if (current_process->time_slice == 0) {
        schedule();
    }
}

void scheduler_start(void) {
    serial_puts("[SCHED] Starting scheduler...\n");
    
    if (!idle_process) {
        kernel_panic("No idle process", 0);
    }
    
    current_process = idle_process;
    current_process->state = PROC_STATE_RUNNING;
    
    /* Enable interrupts and run */
    __asm__ volatile("sti");
    
    /* Jump to idle */
    idle_thread(NULL);
    
    /* Never reached */
    while (1) __asm__ volatile("hlt");
}

/* ============ Fork/Exec/Wait Implementation ============ */

/*
 * Fork creates a copy of the current process.
 * Returns: child PID in parent, 0 in child, -1 on error.
 */
int32_t process_fork(void) {
    if (!current_process) return -1;
    
    serial_puts("[PROC] Forking process ");
    serial_dec(current_process->pid);
    serial_puts("...\n");
    
    /* Allocate new process */
    process_t *child = alloc_process();
    if (!child) {
        serial_puts("[PROC] Fork failed: out of memory\n");
        return -1;
    }
    
    /* Copy process name with "(fork)" suffix */
    proc_strcpy(child->name, current_process->name, PROCESS_NAME_MAX - 7);
    int len = 0;
    while (child->name[len]) len++;
    proc_strcpy(&child->name[len], " (fork)", 8);
    
    /* Set parent */
    child->ppid = current_process->pid;
    child->flags = current_process->flags & ~PROC_FLAG_SYSTEM;  /* Child is not system */
    child->priority = current_process->priority;
    child->start_time = pit_get_ticks();
    
    /* Copy working directory */
    proc_strcpy(child->cwd, current_process->cwd, 256);
    
    /* Allocate kernel stack */
    child->kernel_stack = pmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!child->kernel_stack) {
        free_process(child);
        return -1;
    }
    child->kernel_stack_top = child->kernel_stack + KERNEL_STACK_SIZE;
    
    /* For user processes, allocate user stack */
    if (current_process->flags & PROC_FLAG_USER) {
        child->user_stack = pmm_alloc_pages(USER_STACK_SIZE / PAGE_SIZE);
        if (!child->user_stack) {
            free_process(child);
            return -1;
        }
        child->user_stack_top = child->user_stack + USER_STACK_SIZE;
        
        /* Copy user stack contents */
        uint8_t *src = (uint8_t *)current_process->user_stack;
        uint8_t *dst = (uint8_t *)child->user_stack;
        for (uint64_t i = 0; i < USER_STACK_SIZE; i++) {
            dst[i] = src[i];
        }
    }
    
    /* Copy CPU context */
    child->context = current_process->context;
    
    /* Child returns 0 from fork */
    child->context.rax = 0;
    
    /* Use child's kernel stack */
    child->context.rsp = child->kernel_stack_top - 8;
    
    /* Copy heap bounds */
    child->heap_start = current_process->heap_start;
    child->heap_end = current_process->heap_end;
    
    /* TODO: Copy page tables for proper memory isolation */
    /* For now, share page directory (CoW not implemented) */
    child->page_directory = current_process->page_directory;
    
    /* Copy file descriptors (shallow copy for now) */
    for (int i = 0; i < 16; i++) {
        child->fd_table[i] = current_process->fd_table[i];
    }
    
    /* Add child to ready queue */
    scheduler_add(child);
    
    serial_puts("[PROC] Fork complete: child PID ");
    serial_dec(child->pid);
    serial_puts("\n");
    
    /* Return child PID in parent */
    return child->pid;
}

/*
 * Exec replaces current process image with new executable.
 * Returns: -1 on error (never returns on success).
 */
int process_exec(const char *path, char *const argv[], char *const envp[]) {
    if (!current_process || !path) return -1;
    
    serial_puts("[PROC] Exec: ");
    serial_puts(path);
    serial_puts("\n");
    
    /* Include ELF loader */
    extern int elf_load_file(const char *path, void *info);
    
    /* ELF info structure (must match elf.h) */
    struct {
        uint64_t entry_point;
        uint64_t base_addr;
        uint64_t top_addr;
        uint64_t brk;
        int      is_valid;
        int      is_dynamic;
        char     interp[256];
    } elf_info;
    
    /* Load ELF file */
    if (elf_load_file(path, &elf_info) != 0) {
        serial_puts("[PROC] Failed to load ELF file\n");
        return -1;
    }
    
    if (!elf_info.is_valid) {
        serial_puts("[PROC] Invalid ELF file\n");
        return -1;
    }
    
    serial_puts("[PROC] ELF loaded, entry point: ");
    serial_hex64(elf_info.entry_point);
    serial_puts("\n");
    
    /* Allocate user stack if not already present */
    if (!(current_process->flags & PROC_FLAG_USER)) {
        current_process->user_stack = pmm_alloc_pages(USER_STACK_SIZE / PAGE_SIZE);
        if (!current_process->user_stack) {
            serial_puts("[PROC] Failed to allocate user stack\n");
            return -1;
        }
        current_process->user_stack_top = current_process->user_stack + USER_STACK_SIZE;
        current_process->flags |= PROC_FLAG_USER;
    }
    
    /* Set up the stack with argv and envp */
    uint64_t *stack_ptr = (uint64_t *)(current_process->user_stack_top);
    
    /* Count arguments */
    int argc = 0;
    if (argv) {
        while (argv[argc]) argc++;
    }
    
    int envc = 0;
    if (envp) {
        while (envp[envc]) envc++;
    }
    
    /* Push NULL terminator for envp */
    stack_ptr--;
    *stack_ptr = 0;
    
    /* Push envp pointers (in reverse) */
    uint64_t *envp_start = stack_ptr;
    for (int i = envc - 1; i >= 0; i--) {
        /* Copy env string to stack */
        int len = 0;
        while (envp[i][len]) len++;
        len++;  /* Include null terminator */
        
        stack_ptr = (uint64_t *)((uint8_t *)stack_ptr - ((len + 7) & ~7));  /* 8-byte align */
        char *env_str = (char *)stack_ptr;
        for (int j = 0; j < len; j++) {
            env_str[j] = envp[i][j];
        }
        
        /* Push pointer to this string */
        envp_start--;
        *envp_start = (uint64_t)env_str;
    }
    
    /* Push NULL terminator for argv */
    envp_start--;
    *envp_start = 0;
    
    /* Push argv pointers (in reverse) */
    uint64_t *argv_start = envp_start;
    for (int i = argc - 1; i >= 0; i--) {
        /* Copy arg string to stack */
        int len = 0;
        while (argv[i][len]) len++;
        len++;  /* Include null terminator */
        
        stack_ptr = (uint64_t *)((uint8_t *)stack_ptr - ((len + 7) & ~7));  /* 8-byte align */
        char *arg_str = (char *)stack_ptr;
        for (int j = 0; j < len; j++) {
            arg_str[j] = argv[i][j];
        }
        
        /* Push pointer to this string */
        argv_start--;
        *argv_start = (uint64_t)arg_str;
    }
    
    /* Push argc */
    argv_start--;
    *argv_start = argc;
    
    /* Update process state */
    current_process->heap_start = elf_info.brk;
    current_process->heap_end = elf_info.brk;
    
    /* Copy program name to process name */
    const char *prog_name = path;
    const char *p = path;
    while (*p) {
        if (*p == '/') prog_name = p + 1;
        p++;
    }
    proc_strcpy(current_process->name, prog_name, PROCESS_NAME_MAX);
    
    /* Set up context for user mode */
    current_process->context.rip = elf_info.entry_point;
    current_process->context.rsp = (uint64_t)argv_start;
    current_process->context.rdi = argc;                      /* First arg: argc */
    current_process->context.rsi = (uint64_t)(argv_start + 1); /* Second arg: argv */
    current_process->context.rdx = (uint64_t)envp_start;       /* Third arg: envp */
    current_process->context.cs = 0x1B;   /* User code segment (Ring 3) */
    current_process->context.ss = 0x23;   /* User data segment (Ring 3) */
    current_process->context.ds = 0x23;
    current_process->context.es = 0x23;
    current_process->context.rflags = 0x202;  /* IF enabled */
    
    serial_puts("[PROC] Entering user mode at ");
    serial_hex64(elf_info.entry_point);
    serial_puts("\n");
    
    /* Set TSS kernel stack for this process */
    extern void tss_set_kernel_stack(uint64_t stack);
    tss_set_kernel_stack(current_process->kernel_stack_top);
    
    /* Enter user mode (does not return) - uses arch.S enter_user_mode */
    enter_user_mode((uint64_t)argv_start, elf_info.entry_point);
    
    /* Should never reach here */
    return -1;
}

/*
 * Wait for child process to exit.
 * Returns: PID of exited child, or -1 on error.
 */
int32_t process_wait(int32_t pid, int *status) {
    if (!current_process) return -1;
    
    serial_puts("[PROC] Waiting for PID ");
    serial_dec((uint32_t)(pid < 0 ? -1 : pid));
    serial_puts("\n");
    
    /* Find matching zombie child */
    while (1) {
        for (uint32_t i = 1; i < MAX_PROCESSES; i++) {
            process_t *child = process_table[i];
            if (!child) continue;
            
            /* Check if this is our child */
            if (child->ppid != current_process->pid) continue;
            
            /* Check PID filter (-1 means any child) */
            if (pid >= 0 && child->pid != (uint32_t)pid) continue;
            
            /* Check if zombie */
            if (child->state == PROC_STATE_ZOMBIE) {
                int32_t child_pid = child->pid;
                
                if (status) {
                    *status = child->exit_code;
                }
                
                serial_puts("[PROC] Reaped child ");
                serial_dec(child_pid);
                serial_puts("\n");
                
                /* Free child resources */
                free_process(child);
                
                return child_pid;
            }
        }
        
        /* No zombie found - block or return */
        if (pid < 0) {
            /* Wait for any - check if we have any children */
            int has_children = 0;
            for (uint32_t i = 1; i < MAX_PROCESSES; i++) {
                process_t *child = process_table[i];
                if (child && child->ppid == current_process->pid) {
                    has_children = 1;
                    break;
                }
            }
            
            if (!has_children) {
                return -1;  /* No children to wait for */
            }
            
            /* Block and retry */
            current_process->state = PROC_STATE_BLOCKED;
            schedule();
        } else {
            /* Wait for specific PID */
            process_t *target = process_get((uint32_t)pid);
            if (!target || target->ppid != current_process->pid) {
                return -1;  /* Not our child */
            }
            
            /* Block and retry */
            current_process->state = PROC_STATE_BLOCKED;
            schedule();
        }
    }
}

/* Create a user process (wrapper for fork + exec pattern) */
int32_t process_create_user(const char *name, uint64_t entry_point, uint64_t user_stack) {
    process_t *proc = alloc_process();
    if (!proc) return -1;
    
    proc_strcpy(proc->name, name ? name : "user", PROCESS_NAME_MAX);
    proc->flags = PROC_FLAG_USER;
    proc->ppid = current_process ? current_process->pid : 0;
    proc->start_time = pit_get_ticks();
    proc_strcpy(proc->cwd, "/", 256);
    
    /* Allocate kernel stack (for syscalls/interrupts) */
    proc->kernel_stack = pmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!proc->kernel_stack) {
        free_process(proc);
        return -1;
    }
    
    /* DEBUG: Log kernel stack allocation and check for overlap with ELF */
    serial_puts("[PROC_DEBUG] Kernel stack allocated at 0x");
    serial_hex64(proc->kernel_stack);
    serial_puts("\n");
    if (proc->kernel_stack >= 0x1000000 && proc->kernel_stack < 0x1480000) {
        serial_puts("[PROC_DEBUG] WARNING: Kernel stack IN ELF REGION! CORRUPTION LIKELY!\n");
    }
    
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
    
    /* Set user stack */
    proc->user_stack_top = user_stack;
    
    /* Set up context for user mode */
    proc->context.rip = entry_point;
    proc->context.rsp = user_stack;
    proc->context.cs = 0x1B;   /* User code segment (Ring 3) */
    proc->context.ss = 0x23;   /* User data segment (Ring 3) */
    proc->context.ds = 0x23;
    proc->context.es = 0x23;
    proc->context.rflags = 0x202;  /* IF enabled */
    
    /* Add to ready queue */
    scheduler_add(proc);
    
    serial_puts("[PROC] Created user process '");
    serial_puts(proc->name);
    serial_puts("' (PID ");
    serial_dec(proc->pid);
    serial_puts(")\n");
    
    return proc->pid;
}