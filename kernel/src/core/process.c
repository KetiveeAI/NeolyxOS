/*
 * NeolyxOS Process Management Implementation
 *
 * Process control, scheduler, and context switching.
 *
 * Model:
 *   - Every user process owns a private address space (mm/addrspace.c) and
 *     a private 16KB kernel stack. Syscalls and interrupts from ring 3 run
 *     on that kernel stack.
 *   - Switching is a kernel-stack switch (sched_switch in arch.S). CR3, the
 *     TSS RSP0 / syscall stack pointer and the FXSAVE area follow the
 *     process being resumed.
 *   - The kernel itself is not preemptible: syscalls run with interrupts
 *     masked and only give up the CPU at explicit points (sleep, yield,
 *     wait, exit). The timer preempts code running in ring 3, and the idle
 *     thread.
 *   - The boot context becomes the idle thread (PID 0) in scheduler_run().
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/process.h"
#include "core/syscall.h"
#include "core/elf.h"
#include "mm/kheap.h"
#include "mm/pmm.h"
#include "mm/addrspace.h"
#include "fs/vfs.h"
#include "core/panic.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void tss_set_kernel_stack(uint64_t stack);
extern uint64_t g_kernel_rsp0;

/* arch.S */
extern void sched_switch(uint64_t *save_rsp, uint64_t new_rsp);
extern void user_first_entry(void);
extern void kthread_first_entry(void);
extern void enter_user_mode_args(uint64_t stack, uint64_t entry,
                                 uint64_t arg0, uint64_t arg1) __attribute__((noreturn));

/* ============ Tunables ============ */

#define SCHED_TIME_SLICE    10                  /* Ticks (PIT runs at ~1000Hz) */
#define SPAWN_FILE_MAX      (64ULL * 1024 * 1024)
#define FXSAVE_SIZE         512

/* ============ Process Table ============ */

/* Exported for nxtask_kdrv */
process_t *process_table[MAX_PROCESSES];
int process_table_size = MAX_PROCESSES;
static uint32_t pid_cursor = 1;

/* Current running process - exported */
process_t *current_process = NULL;

/* Idle process (PID 0): the boot context once scheduler_run() starts */
static process_t *idle_process = NULL;

/* Ready queue (doubly linked list) */
static process_t *ready_head = NULL;
static process_t *ready_tail = NULL;

/* Processes in PROC_STATE_SLEEPING (lets the timer skip the scan) */
static uint32_t sleeping_count = 0;

/* Zombies whose kernel stacks still need freeing */
static uint32_t dead_pending = 0;

/* Clean FPU/SSE image every new process starts from */
static uint8_t fpu_template[FXSAVE_SIZE] __attribute__((aligned(16)));
static int fpu_ready = 0;

static int scheduler_running = 0;

/* ============ Helpers ============ */

static void *proc_memset(void *s, int c, size_t n) {
    uint8_t *p = (uint8_t *)s;
    while (n--) *p++ = (uint8_t)c;
    return s;
}

static void proc_memcpy(void *dst, const void *src, size_t n) {
    uint8_t *d = (uint8_t *)dst;
    const uint8_t *s = (const uint8_t *)src;
    while (n--) *d++ = *s++;
}

static void proc_strcpy(char *dst, const char *src, int max) {
    int i = 0;
    while (src[i] && i < max - 1) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static int proc_strlen(const char *s) {
    int n = 0;
    while (s[n]) n++;
    return n;
}

static void serial_dec(uint32_t val) {
    char buf[12];
    int i = 11;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0 && i >= 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

static void serial_sdec(int32_t val) {
    if (val < 0) { serial_putc('-'); val = -val; }
    serial_dec((uint32_t)val);
}

static void serial_hex64(uint64_t val) {
    const char *hex = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

static inline uint64_t irq_save(void) {
    uint64_t flags;
    __asm__ volatile("pushfq; popq %0; cli" : "=r"(flags) : : "memory");
    return flags;
}

static inline void irq_restore(uint64_t flags) {
    if (flags & 0x200) __asm__ volatile("sti" : : : "memory");
}

static inline void fpu_save(uint8_t *area) {
    __asm__ volatile("fxsave (%0)" : : "r"(area) : "memory");
}

static inline void fpu_restore(const uint8_t *area) {
    __asm__ volatile("fxrstor (%0)" : : "r"(area) : "memory");
}

/* Name used in logs: basename of a path */
static const char *path_basename(const char *path) {
    const char *base = path;
    for (const char *p = path; *p; p++) {
        if (*p == '/' && p[1]) base = p + 1;
    }
    return base;
}

/* ============ Process Table Operations ============ */

/*
 * Allocate a PCB and a free PID slot (1..MAX_PROCESSES-1, reused after
 * a process is freed). Returns NULL when the table is full.
 */
static process_t *alloc_process(void) {
    uint32_t pid = 0;
    for (uint32_t n = 0; n < MAX_PROCESSES - 1; n++) {
        uint32_t candidate = pid_cursor + n;
        if (candidate >= MAX_PROCESSES) candidate -= (MAX_PROCESSES - 1);
        if (process_table[candidate] == NULL) {
            pid = candidate;
            break;
        }
    }
    if (pid == 0) {
        serial_puts("[PROC] ERROR: process table full\n");
        return NULL;
    }

    process_t *proc = (process_t *)kzalloc(sizeof(process_t));
    if (!proc) {
        serial_puts("[PROC] ERROR: cannot allocate PCB\n");
        return NULL;
    }

    proc->pid = pid;
    proc->state = PROC_STATE_CREATED;
    proc->priority = 128;
    proc->time_slice = SCHED_TIME_SLICE;
    process_table[pid] = proc;

    pid_cursor = pid + 1;
    if (pid_cursor >= MAX_PROCESSES) pid_cursor = 1;
    return proc;
}

/* Release kernel stack and FPU area (safe once the process never runs again) */
static void release_stacks(process_t *proc) {
    if (proc->stack_released) return;
    if (proc->kernel_stack) {
        pmm_free_pages(proc->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
        proc->kernel_stack = 0;
        proc->kernel_stack_top = 0;
    }
    if (proc->fpu_alloc) {
        kfree(proc->fpu_alloc);
        proc->fpu_alloc = NULL;
        proc->fpu_state = NULL;
    }
    proc->stack_released = 1;
}

static void free_process(process_t *proc) {
    if (!proc || proc == idle_process) return;

    if (proc->pid < MAX_PROCESSES && process_table[proc->pid] == proc) {
        process_table[proc->pid] = NULL;
    }
    release_stacks(proc);
    if (proc->as) {
        addrspace_destroy(proc->as);
        proc->as = NULL;
    }
    kfree(proc);
}

/* ============ Ready Queue Operations ============ */

void scheduler_add(process_t *proc) {
    if (!proc || proc == idle_process) return;
    if (proc->state == PROC_STATE_DEAD || proc->state == PROC_STATE_ZOMBIE) return;

    /* Already queued? (prev set, or it is the head) */
    if (proc->prev || ready_head == proc) return;

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
    if (!proc->prev && ready_head != proc) return;  /* Not queued */

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

/* ============ Reaping ============ */

/*
 * Free kernel stacks of processes that exited. Runs on another process's
 * stack, so the dead stack is no longer in use. Zombies nobody can wait
 * for (detached, or parent gone) are freed entirely.
 */
static void reap_dead(void) {
    if (dead_pending == 0) return;

    for (uint32_t i = 1; i < MAX_PROCESSES; i++) {
        process_t *p = process_table[i];
        if (!p || p == current_process || p->state != PROC_STATE_ZOMBIE) continue;
        if (p->stack_released) continue;

        release_stacks(p);
        if (dead_pending) dead_pending--;

        process_t *parent = (p->ppid < MAX_PROCESSES) ? process_table[p->ppid] : NULL;
        int parent_alive = parent && parent != p &&
                           parent->state != PROC_STATE_ZOMBIE &&
                           parent->state != PROC_STATE_DEAD;
        if ((p->flags & PROC_FLAG_DETACHED) || p->ppid == 0 || !parent_alive) {
            free_process(p);
        }
    }
}

/* ============ Context Switch ============ */

/*
 * Switch from current_process to next. Called with interrupts disabled.
 * Returns when the calling process is scheduled again.
 */
static void switch_to(process_t *next) {
    process_t *prev = current_process;

    if (prev->fpu_state && prev->state != PROC_STATE_ZOMBIE) {
        fpu_save(prev->fpu_state);
    }

    current_process = next;
    if (next->kernel_stack_top) {
        tss_set_kernel_stack(next->kernel_stack_top);
        g_kernel_rsp0 = next->kernel_stack_top;
    }
    addrspace_activate(next->as);

    sched_switch(&prev->ksp, next->ksp);

    /* Running as prev again */
    if (current_process->fpu_state) {
        fpu_restore(current_process->fpu_state);
    }
    reap_dead();
}

/* ============ First run of new processes ============ */

/* Called from user_first_entry (arch.S) on the new process's kernel stack */
void process_first_run(void) {
    process_t *p = current_process;

    if (p->fpu_state) fpu_restore(p->fpu_state);
    reap_dead();

    serial_puts("[PROC] Starting '");
    serial_puts(p->name);
    serial_puts("' (PID ");
    serial_dec(p->pid);
    serial_puts(") at ");
    serial_hex64(p->user_entry);
    serial_puts("\n");

    enter_user_mode_args(p->user_rsp, p->user_entry, p->user_argc, p->user_argv);
}

/* Called from kthread_first_entry (arch.S) */
void kthread_start(thread_entry_t entry, void *arg) {
    reap_dead();
    __asm__ volatile("sti");
    entry(arg);
    process_exit(0);
}

/*
 * Build the initial kernel stack so sched_switch() "returns" into
 * entry_stub with the given R12/R13 values.
 */
static void prepare_kernel_stack(process_t *proc, void (*entry_stub)(void),
                                 uint64_t r12, uint64_t r13) {
    uint64_t *sp = (uint64_t *)proc->kernel_stack_top;
    *--sp = 0;                          /* Alignment slot */
    *--sp = (uint64_t)entry_stub;       /* RET target */
    *--sp = 0;                          /* RBP */
    *--sp = 0;                          /* RBX */
    *--sp = r12;                        /* R12 */
    *--sp = r13;                        /* R13 */
    *--sp = 0;                          /* R14 */
    *--sp = 0;                          /* R15 */
    proc->ksp = (uint64_t)sp;
}

static int alloc_kernel_stack(process_t *proc) {
    proc->kernel_stack = pmm_alloc_pages(KERNEL_STACK_SIZE / PAGE_SIZE);
    if (!proc->kernel_stack) {
        serial_puts("[PROC] ERROR: cannot allocate kernel stack\n");
        return -1;
    }
    if (proc->kernel_stack + KERNEL_STACK_SIZE > 0x100000000ULL ||
        (proc->kernel_stack < USER_WINDOW_END &&
         proc->kernel_stack + KERNEL_STACK_SIZE > USER_WINDOW_BASE)) {
        serial_puts("[PROC] ERROR: kernel stack outside kernel-reachable memory\n");
        pmm_free_pages(proc->kernel_stack, KERNEL_STACK_SIZE / PAGE_SIZE);
        proc->kernel_stack = 0;
        return -1;
    }
    proc->kernel_stack_top = proc->kernel_stack + KERNEL_STACK_SIZE;
    return 0;
}

static int alloc_fpu_area(process_t *proc) {
    proc->fpu_alloc = kmalloc(FXSAVE_SIZE + 16);
    if (!proc->fpu_alloc) {
        serial_puts("[PROC] ERROR: cannot allocate FPU area\n");
        return -1;
    }
    proc->fpu_state = (uint8_t *)(((uint64_t)proc->fpu_alloc + 15) & ~15ULL);
    proc_memcpy(proc->fpu_state, fpu_template, FXSAVE_SIZE);
    return 0;
}

/* ============ Idle Process ============ */

void process_init(void) {
    serial_puts("[PROC] Initializing process management...\n");

    proc_memset(process_table, 0, sizeof(process_table));
    ready_head = ready_tail = NULL;
    sleeping_count = 0;
    dead_pending = 0;
    pid_cursor = 1;

    idle_process = (process_t *)kzalloc(sizeof(process_t));
    if (!idle_process) {
        kernel_panic("Failed to create idle process", 0);
    }

    idle_process->pid = 0;
    process_table[0] = idle_process;
    proc_strcpy(idle_process->name, "idle", PROCESS_NAME_MAX);
    proc_strcpy(idle_process->cwd, "/", 256);
    idle_process->flags = PROC_FLAG_KERNEL | PROC_FLAG_SYSTEM;
    idle_process->state = PROC_STATE_RUNNING;

    current_process = idle_process;

    serial_puts("[PROC] Ready (idle = boot context)\n");
}

void process_fpu_init(void) {
    uint64_t cr0, cr4;

    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 &= ~((uint64_t)1 << 2);     /* EM: no x87 emulation */
    cr0 &= ~((uint64_t)1 << 3);     /* TS: no lazy-switch trap */
    cr0 |= ((uint64_t)1 << 1);      /* MP */
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));

    __asm__ volatile("mov %%cr4, %0" : "=r"(cr4));
    cr4 |= ((uint64_t)1 << 9);      /* OSFXSR: FXSAVE/FXRSTOR + SSE */
    cr4 |= ((uint64_t)1 << 10);     /* OSXMMEXCPT */
    __asm__ volatile("mov %0, %%cr4" : : "r"(cr4));

    /* Clean x87 state, default MXCSR (all exceptions masked) */
    uint32_t mxcsr = 0x1F80;
    __asm__ volatile("fninit");
    __asm__ volatile("ldmxcsr %0" : : "m"(mxcsr));

    /* New programs start from the all-zero default image (arch.S), not a
     * snapshot of the boot context, so no kernel data in XMM/x87 registers
     * reaches userspace. */
    extern const uint8_t nx_fpu_default_state[];
    proc_memcpy(fpu_template, nx_fpu_default_state, FXSAVE_SIZE);
    fpu_ready = 1;

    serial_puts("[PROC] FPU/SSE enabled, per-process FXSAVE ready\n");
}

int32_t process_create_thread(const char *name, thread_entry_t entry, void *arg) {
    if (!entry) return -1;

    uint64_t flags = irq_save();
    process_t *proc = alloc_process();
    if (!proc) { irq_restore(flags); return -1; }

    proc_strcpy(proc->name, name ? name : "thread", PROCESS_NAME_MAX);
    proc->flags = PROC_FLAG_KERNEL;
    proc->ppid = current_process ? current_process->pid : 0;
    proc->start_time = pit_get_ticks();
    proc_strcpy(proc->cwd, "/", 256);

    if (alloc_kernel_stack(proc) != 0) {
        free_process(proc);
        irq_restore(flags);
        return -1;
    }
    prepare_kernel_stack(proc, kthread_first_entry, (uint64_t)entry, (uint64_t)arg);

    scheduler_add(proc);
    irq_restore(flags);

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

/* ============ Program Images ============ */

/*
 * Lay out argv on a new process's stack, top of the user window.
 *
 * NeolyxOS entry ABI: RDI = argc, RSI = argv, and RSP points at a zero
 * return address with RSP % 16 == 8, exactly as if _start had been
 * called. Above it: argc, argv[0..argc-1], NULL, envp NULL, strings.
 */
static int setup_user_stack(addrspace_t *as, int argc, const char *const *argv,
                            uint64_t *out_rsp, uint64_t *out_argv) {
    uint64_t str_va[PROCESS_ARGV_MAX];
    uint64_t sp = USER_STACK_TOP;

    for (int i = argc - 1; i >= 0; i--) {
        int len = proc_strlen(argv[i]);
        if (len >= PROCESS_ARG_LEN) return -1;
        sp -= (uint64_t)len + 1;
        if (addrspace_copy_in(as, sp, argv[i], (uint64_t)len + 1) != 0) return -1;
        str_va[i] = sp;
    }
    sp &= ~15ULL;

    /* ret, argc, argv[argc+1], envp[1] */
    uint64_t frame[PROCESS_ARGV_MAX + 4];
    int n = 0;
    frame[n++] = 0;                         /* Fake return address */
    frame[n++] = (uint64_t)argc;
    for (int i = 0; i < argc; i++) frame[n++] = str_va[i];
    frame[n++] = 0;                         /* argv terminator */
    frame[n++] = 0;                         /* empty envp */

    uint64_t rsp = ((sp - (uint64_t)n * 8) & ~15ULL) - 8;
    if (rsp < USER_STACK_TOP - USER_STACK_SIZE_MAX) return -1;
    if (addrspace_copy_in(as, rsp, frame, (uint64_t)n * 8) != 0) return -1;

    *out_rsp = rsp;
    *out_argv = rsp + 16;
    return 0;
}

/*
 * Build a complete user image (code, data, BSS, stack, argv) in a fresh
 * address space. On success returns the space and fills the out values.
 */
static addrspace_t *build_image(const void *image, uint64_t size,
                                int argc, const char *const *argv,
                                elf_info_t *info, uint64_t *rsp, uint64_t *uargv) {
    if (argc < 0 || argc > PROCESS_ARGV_MAX || (argc > 0 && !argv)) {
        serial_puts("[PROC] ERROR: bad argument vector\n");
        return NULL;
    }
    for (int i = 0; i < argc; i++) {
        if (!argv[i]) return NULL;
    }

    addrspace_t *as = addrspace_create();
    if (!as) return NULL;

    if (elf_load_user(as, image, size, info) != 0) {
        addrspace_destroy(as);
        return NULL;
    }
    if (addrspace_map_alloc(as, USER_STACK_TOP - USER_STACK_SIZE_MAX,
                            USER_STACK_SIZE_MAX, AS_MAP_WRITE) != 0) {
        serial_puts("[PROC] ERROR: cannot map user stack\n");
        addrspace_destroy(as);
        return NULL;
    }
    if (setup_user_stack(as, argc, argv, rsp, uargv) != 0) {
        serial_puts("[PROC] ERROR: arguments do not fit on the user stack\n");
        addrspace_destroy(as);
        return NULL;
    }
    return as;
}

/* Heap: from the end of the image up to the reserve, never into the stack */
static void init_heap(process_t *proc, const elf_info_t *info) {
    uint64_t limit = USER_STACK_TOP - USER_STACK_SIZE_MAX - PAGE_SIZE;
    proc->heap_start = info->brk;
    proc->heap_end = info->brk;
    proc->heap_mapped = info->brk;
    proc->heap_max = info->brk + USER_HEAP_RESERVE;
    if (proc->heap_max > limit) proc->heap_max = limit;
}

int32_t process_spawn_image(const char *name, const void *image, uint64_t size,
                            int argc, const char *const *argv, uint32_t flags) {
    if (!image || size == 0) return -EINVAL;
    if (!fpu_ready) {
        serial_puts("[PROC] ERROR: spawn before process_fpu_init\n");
        return -EINVAL;
    }

    elf_info_t info;
    uint64_t user_rsp = 0, user_argv = 0;
    addrspace_t *as = build_image(image, size, argc, argv, &info, &user_rsp, &user_argv);
    if (!as) return -ENOEXEC;

    uint64_t irq = irq_save();
    process_t *proc = alloc_process();
    if (!proc) {
        irq_restore(irq);
        addrspace_destroy(as);
        return -EAGAIN;
    }

    proc_strcpy(proc->name, name ? name : "user", PROCESS_NAME_MAX);
    proc->flags = PROC_FLAG_USER | (flags & (PROC_FLAG_SYSTEM | PROC_FLAG_DETACHED));
    proc->ppid = current_process ? current_process->pid : 0;
    proc->start_time = pit_get_ticks();
    proc_strcpy(proc->cwd, "/", 256);
    proc->as = as;

    if (alloc_kernel_stack(proc) != 0 || alloc_fpu_area(proc) != 0) {
        free_process(proc);     /* Also destroys the address space */
        irq_restore(irq);
        return -ENOMEM;
    }

    init_heap(proc, &info);
    proc->user_entry = info.entry_point;
    proc->user_rsp = user_rsp;
    proc->user_argc = (uint64_t)argc;
    proc->user_argv = user_argv;
    proc->user_stack = USER_STACK_TOP - USER_STACK_SIZE_MAX;
    proc->user_stack_top = USER_STACK_TOP;
    proc->page_directory = as->pml4_phys;
    prepare_kernel_stack(proc, user_first_entry, 0, 0);

    scheduler_add(proc);
    irq_restore(irq);

    serial_puts("[PROC] Spawned '");
    serial_puts(proc->name);
    serial_puts("' PID ");
    serial_dec(proc->pid);
    serial_puts(" (parent ");
    serial_dec(proc->ppid);
    serial_puts("), entry ");
    serial_hex64(info.entry_point);
    serial_puts(", image ");
    serial_hex64(info.base_addr);
    serial_puts("-");
    serial_hex64(info.top_addr);
    serial_puts(", ");
    serial_dec((uint32_t)as->pages_mapped);
    serial_puts(" pages\n");

    return (int32_t)proc->pid;
}

/*
 * Read a whole file into physically contiguous kernel pages (outside the
 * kernel heap, which is too small for large programs).
 * Returns 0 and fills *phys / *pages / *size, or a negative errno.
 */
static int read_program_file(const char *path, uint64_t *phys, uint64_t *pages, uint64_t *size) {
    vfs_file_t *file = vfs_open(path, VFS_O_RDONLY);
    if (!file) return -ENOENT;

    if (!file->node || file->node->type != VFS_FILE) {
        vfs_close(file);
        return -EACCES;
    }
    uint64_t fsize = file->node->size;
    if (fsize == 0 || fsize > SPAWN_FILE_MAX) {
        serial_puts("[PROC] ERROR: program file size not acceptable\n");
        vfs_close(file);
        return -ENOEXEC;
    }

    uint64_t npages = (fsize + PAGE_SIZE - 1) / PAGE_SIZE;
    uint64_t buf = pmm_alloc_pages(npages);
    if (!buf) {
        vfs_close(file);
        return -ENOMEM;
    }
    if (buf + npages * PAGE_SIZE > 0x100000000ULL) {
        pmm_free_pages(buf, npages);
        vfs_close(file);
        return -ENOMEM;
    }

    uint64_t done = 0;
    while (done < fsize) {
        int64_t n = vfs_read(file, (uint8_t *)buf + done, fsize - done);
        if (n <= 0) break;
        done += (uint64_t)n;
    }
    vfs_close(file);

    if (done != fsize) {
        serial_puts("[PROC] ERROR: short read of program file\n");
        pmm_free_pages(buf, npages);
        return -EIO;
    }

    *phys = buf;
    *pages = npages;
    *size = fsize;
    return 0;
}

int32_t process_spawn_file(const char *path, int argc, const char *const *argv,
                           uint32_t flags) {
    if (!path || !path[0]) return -EINVAL;

    serial_puts("[PROC] Spawn from disk: ");
    serial_puts(path);
    serial_puts("\n");

    uint64_t buf = 0, pages = 0, size = 0;
    int err = read_program_file(path, &buf, &pages, &size);
    if (err != 0) {
        serial_puts("[PROC] Spawn failed: cannot read ");
        serial_puts(path);
        serial_puts(" (");
        serial_sdec(err);
        serial_puts(")\n");
        return err;
    }

    /* Default argv: just the program path */
    const char *default_argv[1] = { path };
    if (argc == 0 || !argv) {
        argc = 1;
        argv = default_argv;
    }

    int32_t pid = process_spawn_image(path_basename(path), (const void *)buf, size,
                                      argc, argv, flags);
    pmm_free_pages(buf, pages);
    return pid;
}

/* ============ Exit / Kill ============ */

/*
 * Common teardown. The process must not be on the ready queue afterwards
 * and must not be the one whose CR3 is loaded (caller handles current).
 */
/*
 * Close every file a process still has open. Called from terminate(), so it
 * runs for normal exit, kill and CPU faults alike. Each slot is cleared
 * before vfs_close() so a descriptor can never be closed twice.
 */
static void close_files(process_t *proc) {
    uint32_t closed = 0;
    for (int fd = 0; fd < PROCESS_FD_MAX; fd++) {
        vfs_file_t *file = (vfs_file_t *)proc->fd_table[fd];
        if (!file) continue;
        proc->fd_table[fd] = NULL;
        if (vfs_close(file) != VFS_OK) {
            serial_puts("[PROC] WARNING: close failed for fd ");
            serial_dec((uint32_t)fd);
            serial_puts(" of PID ");
            serial_dec(proc->pid);
            serial_puts("\n");
        }
        closed++;
    }
    if (closed) {
        serial_puts("[PROC] Closed ");
        serial_dec(closed);
        serial_puts(" open file(s) of PID ");
        serial_dec(proc->pid);
        serial_puts("\n");
    }
}

static void terminate(process_t *proc, int exit_code) {
    proc->exit_code = exit_code;

    /* A process's files are its own: nothing it left open outlives it */
    close_files(proc);

    scheduler_remove(proc);
    if (proc->state == PROC_STATE_SLEEPING && sleeping_count) sleeping_count--;
    proc->state = PROC_STATE_ZOMBIE;

    if (proc->as) {
        addrspace_destroy(proc->as);
        proc->as = NULL;
        proc->page_directory = 0;
    }

    /* Orphans are adopted by nobody: they are freed when they exit */
    for (uint32_t i = 1; i < MAX_PROCESSES; i++) {
        process_t *c = process_table[i];
        if (c && c != proc && c->ppid == proc->pid) {
            c->ppid = 0;
        }
    }

    /* Wake a parent blocked in wait() */
    process_t *parent = (proc->ppid < MAX_PROCESSES) ? process_table[proc->ppid] : NULL;
    if (parent && parent != proc && parent->state == PROC_STATE_BLOCKED) {
        scheduler_add(parent);
    }

    dead_pending++;
}

void process_exit(int exit_code) {
    __asm__ volatile("cli");
    process_t *self = current_process;

    if (!self || self == idle_process) {
        kernel_panic("Exit with no process", 0);
    }

    serial_puts("[PROC] Process ");
    serial_dec(self->pid);
    serial_puts(" ('");
    serial_puts(self->name);
    serial_puts("') exited with code ");
    serial_sdec(exit_code);
    serial_puts("\n");

    /* Leave the dying address space before freeing it */
    addrspace_activate(NULL);
    terminate(self, exit_code);

    schedule();

    /* A zombie is never scheduled again */
    kernel_panic("Zombie process resumed", self->pid);
}

void process_exit_on_fault(uint64_t vector) {
    process_t *self = current_process;
    serial_puts("[PROC] Terminating PID ");
    serial_dec(self ? self->pid : 0);
    serial_puts(" after CPU exception ");
    serial_dec((uint32_t)vector);
    serial_puts("\n");
    process_exit(128 + (int)vector);
}

int process_kill(uint32_t pid, int signal) {
    process_t *proc = process_get(pid);
    if (!proc || proc == idle_process) return -1;
    if (proc->state == PROC_STATE_ZOMBIE || proc->state == PROC_STATE_DEAD) return -1;

    if (proc->flags & PROC_FLAG_SYSTEM) {
        serial_puts("[PROC] Cannot kill system process\n");
        return -1;
    }

    serial_puts("[PROC] Killing PID ");
    serial_dec(pid);
    serial_puts("\n");

    if (proc == current_process) {
        process_exit(128 + signal);
    }

    uint64_t flags = irq_save();
    terminate(proc, 128 + signal);
    irq_restore(flags);
    return 0;
}

/* ============ Blocking ============ */

void process_yield(void) {
    schedule();
}

void process_sleep(uint32_t ms) {
    process_t *self = current_process;

    if (ms > 60000u) ms = 60000u;          /* bound a hostile request */

    if (!scheduler_running || !self || self == idle_process) {
        /* No scheduler yet: calibrated busy-wait (~100k loops per ms).
         * The PIT may not be programmed or unmasked this early, so HLT
         * could wait forever; once the scheduler runs, sleep blocks. */
        uint64_t delay_loops = (uint64_t)ms * 100000ULL;
        for (volatile uint64_t i = 0; i < delay_loops; i++) {
            __asm__ volatile("pause");
        }
        return;
    }

    uint64_t flags = irq_save();
    self->wake_tick = pit_get_ticks() + (ms ? ms : 1);
    self->state = PROC_STATE_SLEEPING;
    sleeping_count++;
    schedule();
    irq_restore(flags);
}

void process_block(void) {
    if (!current_process || current_process == idle_process) return;

    uint64_t flags = irq_save();
    current_process->state = PROC_STATE_BLOCKED;
    schedule();
    irq_restore(flags);
}

void process_unblock(process_t *proc) {
    if (!proc || proc->state != PROC_STATE_BLOCKED) return;

    uint64_t flags = irq_save();
    scheduler_add(proc);
    irq_restore(flags);
}

/* ============ Scheduler ============ */

void scheduler_init(void) {
    serial_puts("[SCHED] Initializing scheduler...\n");
    serial_puts("[SCHED] Ready (round-robin, preempts ring 3 only)\n");
}

/*
 * Pick the next process and switch to it. Safe to call from syscalls and
 * from the timer IRQ. Returns when the caller is scheduled again (never
 * for a zombie).
 */
void schedule(void) {
    uint64_t flags = irq_save();
    process_t *self = current_process;
    process_t *next = ready_head;

    if (next) {
        scheduler_remove(next);
        if (self->state == PROC_STATE_RUNNING && self != idle_process) {
            scheduler_add(self);
        }
    } else if (self->state == PROC_STATE_RUNNING) {
        /* Nothing else to run: keep going */
        self->time_slice = SCHED_TIME_SLICE;
        irq_restore(flags);
        return;
    } else {
        next = idle_process;
    }

    if (next == self) {
        self->state = PROC_STATE_RUNNING;
        irq_restore(flags);
        return;
    }

    next->state = PROC_STATE_RUNNING;
    next->time_slice = SCHED_TIME_SLICE;
    switch_to(next);

    irq_restore(flags);
}

/* Move sleepers whose deadline passed back to the ready queue */
static void wake_sleepers(void) {
    if (sleeping_count == 0) return;

    uint64_t now = pit_get_ticks();
    for (uint32_t i = 1; i < MAX_PROCESSES && sleeping_count; i++) {
        process_t *p = process_table[i];
        if (p && p->state == PROC_STATE_SLEEPING && now >= p->wake_tick) {
            sleeping_count--;
            scheduler_add(p);
        }
    }
}

void scheduler_tick_irq(int from_user) {
    if (!scheduler_running || !current_process) return;

    wake_sleepers();

    if (current_process == idle_process) {
        if (ready_head) schedule();
        return;
    }

    current_process->total_time++;

    /* Kernel code is never preempted; only ring 3 and idle */
    if (!from_user) return;

    if (current_process->time_slice > 0) {
        current_process->time_slice--;
    }
    if (current_process->time_slice == 0) {
        if (ready_head) {
            schedule();
        } else {
            current_process->time_slice = SCHED_TIME_SLICE;
        }
    }
}

/* Legacy hook (timer.c): wakes sleepers but never preempts kernel code */
void scheduler_tick(void) {
    scheduler_tick_irq(0);
}

void scheduler_run(void) {
    __asm__ volatile("cli");

    if (!idle_process) {
        kernel_panic("No idle process", 0);
    }

    serial_puts("[SCHED] Boot context becomes idle; starting scheduler\n");
    current_process = idle_process;
    idle_process->state = PROC_STATE_RUNNING;
    scheduler_running = 1;

    for (;;) {
        __asm__ volatile("cli");
        if (ready_head) {
            schedule();
        }
        /* Atomically enable interrupts and wait for the next one */
        __asm__ volatile("sti; hlt" : : : "memory");
    }
}

void scheduler_start(void) {
    scheduler_run();
}

/* ============ Fork/Exec/Wait Implementation ============ */

/*
 * fork() is not supported: user processes have private address spaces and
 * the kernel has no copy-on-write. Use spawn (SYS_SPAWN) instead.
 */
int32_t process_fork(void) {
    serial_puts("[PROC] fork() is not supported; use spawn\n");
    return -ENOSYS;
}

/*
 * Exec replaces the current process image with a program from disk.
 * argv/envp must be kernel pointers (the syscall layer copies them in).
 * Returns a negative errno on failure, and does not return on success.
 */
int process_exec(const char *path, char *const argv[], char *const envp[]) {
    (void)envp;
    process_t *self = current_process;
    if (!self || !path) return -EINVAL;
    if (self == idle_process || !(self->flags & PROC_FLAG_USER) || !fpu_ready) {
        serial_puts("[PROC] exec is only available to user processes\n");
        return -EPERM;
    }

    serial_puts("[PROC] Exec: ");
    serial_puts(path);
    serial_puts("\n");

    int argc = 0;
    if (argv) {
        while (argv[argc]) {
            if (++argc > PROCESS_ARGV_MAX) return -E2BIG;
        }
    }
    const char *default_argv[1] = { path };
    const char *const *use_argv = (const char *const *)argv;
    if (argc == 0) {
        argc = 1;
        use_argv = default_argv;
    }

    uint64_t buf = 0, pages = 0, size = 0;
    int err = read_program_file(path, &buf, &pages, &size);
    if (err != 0) return err;

    elf_info_t info;
    uint64_t user_rsp = 0, user_argv = 0;
    addrspace_t *as = build_image((const void *)buf, size, argc, use_argv,
                                  &info, &user_rsp, &user_argv);
    pmm_free_pages(buf, pages);
    if (!as) return -ENOEXEC;

    /* Point of no return: swap images */
    __asm__ volatile("cli");
    addrspace_t *old = self->as;
    self->as = as;
    self->page_directory = as->pml4_phys;
    addrspace_activate(as);
    if (old) addrspace_destroy(old);

    init_heap(self, &info);
    self->user_entry = info.entry_point;
    self->user_stack = USER_STACK_TOP - USER_STACK_SIZE_MAX;
    self->user_stack_top = USER_STACK_TOP;
    proc_strcpy(self->name, path_basename(path), PROCESS_NAME_MAX);
    if (self->fpu_state) {
        proc_memcpy(self->fpu_state, fpu_template, FXSAVE_SIZE);
        fpu_restore(self->fpu_state);
    }

    serial_puts("[PROC] PID ");
    serial_dec(self->pid);
    serial_puts(" now runs ");
    serial_puts(self->name);
    serial_puts("\n");

    /* Abandon the syscall frame: the next kernel entry starts at the top */
    enter_user_mode_args(user_rsp, info.entry_point, (uint64_t)argc, user_argv);
}

/*
 * Wait for child process to exit.
 * Returns: PID of exited child, or -1 on error.
 */
int32_t process_wait(int32_t pid, int *status) {
    process_t *self = current_process;
    if (!self || self == idle_process) return -1;

    uint64_t flags = irq_save();
    for (;;) {
        int has_child = 0;

        for (uint32_t i = 1; i < MAX_PROCESSES; i++) {
            process_t *child = process_table[i];
            if (!child || child == self || child->ppid != self->pid) continue;
            if (pid >= 0 && child->pid != (uint32_t)pid) continue;

            has_child = 1;
            if (child->state == PROC_STATE_ZOMBIE && child->stack_released) {
                int32_t child_pid = (int32_t)child->pid;
                if (status) *status = child->exit_code;
                free_process(child);
                irq_restore(flags);
                return child_pid;
            }
        }

        if (!has_child) {
            irq_restore(flags);
            return -1;
        }

        /* Sleep until a child exits (terminate() requeues us) */
        self->state = PROC_STATE_BLOCKED;
        schedule();
        reap_dead();
    }
}
