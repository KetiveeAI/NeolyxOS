/*
 * NXTask Kernel Driver Implementation
 * 
 * Kernel Task Manager - Process monitoring and control
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#include "drivers/nxtask_kdrv.h"
#include "core/process.h"
#include <stdint.h>

/* External kernel interfaces */
extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);

/* External process table */
extern process_t *process_table[];
extern int process_table_size;
extern process_t *current_process;

/* ============ Helpers ============ */

static inline void nx_strcpy(char *d, const char *s, int max) {
    int i = 0;
    while (s[i] && i < max - 1) { d[i] = s[i]; i++; }
    d[i] = '\0';
}

static inline void nx_memset(void *p, int c, size_t n) {
    uint8_t *ptr = (uint8_t*)p;
    while (n--) *ptr++ = (uint8_t)c;
}

static void serial_dec(int val) {
    char buf[12];
    int i = 0;
    if (val < 0) { serial_putc('-'); val = -val; }
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i++] = '0' + (val % 10); val /= 10; }
    while (i > 0) serial_putc(buf[--i]);
}

static void serial_hex64(uint64_t val) {
    const char hex[] = "0123456789ABCDEF";
    serial_puts("0x");
    for (int i = 60; i >= 0; i -= 4) {
        serial_putc(hex[(val >> i) & 0xF]);
    }
}

/* ============ State ============ */

static struct {
    int             initialized;
    uint64_t        boot_time;
    
    /* CPU tracking */
    uint64_t        last_update_tick;
    uint64_t        total_cpu_ticks;
    uint64_t        idle_ticks;
} g_task = {0};

/* ============ Process State Conversion ============ */

static nxtask_state_t convert_state(int proc_state) {
    switch (proc_state) {
        case PROC_STATE_RUNNING:  return NXTASK_STATE_RUNNING;
        case PROC_STATE_READY:    return NXTASK_STATE_READY;
        case PROC_STATE_BLOCKED:  return NXTASK_STATE_BLOCKED;
        case PROC_STATE_SLEEPING: return NXTASK_STATE_SLEEPING;
        case PROC_STATE_ZOMBIE:   return NXTASK_STATE_ZOMBIE;
        default:                  return NXTASK_STATE_STOPPED;
    }
}

static nxtask_priority_t convert_priority(int proc_priority) {
    /* Map kernel priority (0-255) to our levels */
    if (proc_priority < 10) return NXTASK_PRIO_REALTIME;
    if (proc_priority < 40) return NXTASK_PRIO_HIGH;
    if (proc_priority < 80) return NXTASK_PRIO_ABOVE_NORMAL;
    if (proc_priority < 120) return NXTASK_PRIO_NORMAL;
    if (proc_priority < 160) return NXTASK_PRIO_BELOW_NORMAL;
    if (proc_priority < 200) return NXTASK_PRIO_LOW;
    return NXTASK_PRIO_IDLE;
}

/* ============ Public API ============ */

int nxtask_kdrv_init(void) {
    if (g_task.initialized) return 0;
    
    serial_puts("[NXTask] Initializing v" NXTASK_KDRV_VERSION "\n");
    serial_puts("[NXTask] Architecture: " NXTASK_ARCH_NAME "\n");
    
    nx_memset(&g_task, 0, sizeof(g_task));
    g_task.boot_time = pit_get_ticks();
    g_task.last_update_tick = g_task.boot_time;
    g_task.initialized = 1;
    
    serial_puts("[NXTask] Task manager ready\n");
    return 0;
}

void nxtask_kdrv_shutdown(void) {
    if (!g_task.initialized) return;
    serial_puts("[NXTask] Shutting down\n");
    g_task.initialized = 0;
}

int nxtask_kdrv_count(void) {
    int count = 0;
    for (int i = 0; i < process_table_size; i++) {
        if (process_table[i] && process_table[i]->state != PROC_STATE_DEAD) {
            count++;
        }
    }
    return count;
}

int nxtask_kdrv_list(uint32_t *pids, int max_count) {
    if (!pids || max_count <= 0) return 0;
    
    int count = 0;
    for (int i = 0; i < process_table_size && count < max_count; i++) {
        if (process_table[i] && process_table[i]->state != PROC_STATE_DEAD) {
            pids[count++] = process_table[i]->pid;
        }
    }
    return count;
}

int nxtask_kdrv_info(uint32_t pid, nxtask_info_t *info) {
    if (!info) return -1;
    
    nx_memset(info, 0, sizeof(*info));
    
    /* Find process */
    process_t *proc = NULL;
    for (int i = 0; i < process_table_size; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            proc = process_table[i];
            break;
        }
    }
    
    if (!proc) return -2;  /* Not found */
    
    /* Basic info */
    info->pid = proc->pid;
    info->ppid = 0;  /* Parent PID not in process_t */
    info->uid = 0;  /* UID not in process_t */
    info->gid = 0;  /* GID not in process_t */
    
    nx_strcpy(info->name, proc->name, 64);
    
    /* State and priority */
    info->state = convert_state(proc->state);
    info->priority = convert_priority(proc->priority);
    info->nice = 0;  /* TODO: implement nice */
    
    /* Flags */
    if (proc->flags & PROC_FLAG_KERNEL) info->flags |= NXTASK_FLAG_KERNEL;
    if (proc->flags & PROC_FLAG_USER) info->flags |= NXTASK_FLAG_USER;
    if (proc->flags & PROC_FLAG_SYSTEM) info->flags |= NXTASK_FLAG_SYSTEM;
    
    /* CPU stats */
    info->cpu.total_time_us = proc->total_time * 1000;  /* ticks to us */
    info->cpu.time_slice = proc->time_slice;
    info->cpu.context_switches = proc->total_time / 10;  /* Approximate */
    
    /* Calculate CPU percentage (rough estimate) */
    uint64_t uptime = pit_get_ticks() - g_task.boot_time;
    if (uptime > 0) {
        info->cpu.cpu_percent_x10 = (proc->total_time * 1000) / uptime;
    }
    
    /* Memory stats */
    info->mem.virtual_size = proc->heap_end - proc->heap_start + 
                             proc->user_stack_top - proc->user_stack + 4096;
    info->mem.stack_size = proc->user_stack_top - proc->user_stack;
    info->mem.heap_size = proc->heap_end - proc->heap_start;
    
    /* Thread count (single-threaded for now) */
    info->thread_count = 1;
    
    /* CPU affinity (default: all CPUs) */
    info->affinity_mask = 0xFFFFFFFFFFFFFFFF;
    info->running_on_cpu = 0;
    
    return 0;
}

int nxtask_kdrv_sysinfo(nxtask_sysinfo_t *info) {
    if (!info) return -1;
    
    nx_memset(info, 0, sizeof(*info));
    
    /* Process counts */
    for (int i = 0; i < process_table_size; i++) {
        process_t *proc = process_table[i];
        if (!proc) continue;
        
        info->total_processes++;
        
        switch (proc->state) {
            case PROC_STATE_RUNNING:
                info->running_processes++;
                break;
            case PROC_STATE_SLEEPING:
            case PROC_STATE_BLOCKED:
                info->sleeping_processes++;
                break;
            case PROC_STATE_ZOMBIE:
                info->zombie_processes++;
                break;
        }
    }
    
    info->total_threads = info->total_processes;  /* 1 thread per process for now */
    
    /* CPU info */
    info->cpu_count = 1;  /* TODO: detect multiple CPUs */
    info->cpu_online = 1;
    
    /* Load average - calculate from actual running processes */
    uint32_t running = info->running_processes;
    info->load_avg_1min = running * 100;
    info->load_avg_5min = running * 80;
    info->load_avg_15min = running * 60;
    
    /* CPU usage - based on running vs total processes */
    uint32_t total = info->total_processes;
    if (total > 0) {
        info->cpu_user_percent = (running * 100) / total;
        info->cpu_kernel_percent = 10;
        info->cpu_idle_percent = 100 - info->cpu_user_percent - info->cpu_kernel_percent;
    } else {
        info->cpu_user_percent = 0;
        info->cpu_kernel_percent = 5;
        info->cpu_idle_percent = 95;
    }
    info->cpu_iowait_percent = 0;
    
    /* Memory - get real stats from PMM */
    extern uint64_t pmm_get_total_memory(void);
    extern uint64_t pmm_get_free_memory(void);
    
    info->mem_total = pmm_get_total_memory();
    info->mem_free = pmm_get_free_memory();
    info->mem_used = info->mem_total - info->mem_free;
    
    /* Uptime */
    info->uptime_sec = (pit_get_ticks() - g_task.boot_time) / 1000;
    
    return 0;
}

int nxtask_kdrv_set_priority(uint32_t pid, nxtask_priority_t priority) {
    process_t *proc = NULL;
    for (int i = 0; i < process_table_size; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            proc = process_table[i];
            break;
        }
    }
    
    if (!proc) return -1;
    
    /* Map priority to kernel value */
    switch (priority) {
        case NXTASK_PRIO_REALTIME:     proc->priority = 0; break;
        case NXTASK_PRIO_HIGH:         proc->priority = 20; break;
        case NXTASK_PRIO_ABOVE_NORMAL: proc->priority = 60; break;
        case NXTASK_PRIO_NORMAL:       proc->priority = 100; break;
        case NXTASK_PRIO_BELOW_NORMAL: proc->priority = 140; break;
        case NXTASK_PRIO_LOW:          proc->priority = 180; break;
        case NXTASK_PRIO_IDLE:         proc->priority = 255; break;
        default:                       proc->priority = 100; break;
    }
    
    serial_puts("[NXTask] Set PID ");
    serial_dec(pid);
    serial_puts(" priority to ");
    serial_puts(nxtask_kdrv_priority_name(priority));
    serial_puts("\n");
    
    return 0;
}

int nxtask_kdrv_set_nice(uint32_t pid, int nice) {
    if (nice < -20) nice = -20;
    if (nice > 19) nice = 19;
    
    /* Map nice to priority */
    nxtask_priority_t prio = NXTASK_PRIO_NORMAL;
    if (nice <= -10) prio = NXTASK_PRIO_HIGH;
    else if (nice <= -5) prio = NXTASK_PRIO_ABOVE_NORMAL;
    else if (nice <= 5) prio = NXTASK_PRIO_NORMAL;
    else if (nice <= 10) prio = NXTASK_PRIO_BELOW_NORMAL;
    else prio = NXTASK_PRIO_LOW;
    
    return nxtask_kdrv_set_priority(pid, prio);
}

int nxtask_kdrv_set_affinity(uint32_t pid, uint64_t mask) {
    /* TODO: Implement CPU affinity */
    (void)pid;
    (void)mask;
    return 0;
}

int nxtask_kdrv_suspend(uint32_t pid) {
    process_t *proc = NULL;
    for (int i = 0; i < process_table_size; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            proc = process_table[i];
            break;
        }
    }
    
    if (!proc) return -1;
    if (proc->flags & PROC_FLAG_SYSTEM) return -2;  /* Cannot suspend system */
    
    proc->state = PROC_STATE_BLOCKED;
    proc->flags |= PROC_FLAG_SYSTEM;  /* Mark as suspended */
    
    serial_puts("[NXTask] Suspended PID ");
    serial_dec(pid);
    serial_puts("\n");
    
    return 0;
}

int nxtask_kdrv_resume(uint32_t pid) {
    process_t *proc = NULL;
    for (int i = 0; i < process_table_size; i++) {
        if (process_table[i] && process_table[i]->pid == pid) {
            proc = process_table[i];
            break;
        }
    }
    
    if (!proc) return -1;
    
    proc->state = PROC_STATE_READY;
    
    serial_puts("[NXTask] Resumed PID ");
    serial_dec(pid);
    serial_puts("\n");
    
    return 0;
}

int nxtask_kdrv_kill(uint32_t pid, int signal) {
    extern int process_kill(uint32_t pid, int signal);
    return process_kill(pid, signal);
}

const char *nxtask_kdrv_state_name(nxtask_state_t state) {
    switch (state) {
        case NXTASK_STATE_RUNNING:  return "Running";
        case NXTASK_STATE_READY:    return "Ready";
        case NXTASK_STATE_BLOCKED:  return "Blocked";
        case NXTASK_STATE_SLEEPING: return "Sleeping";
        case NXTASK_STATE_ZOMBIE:   return "Zombie";
        case NXTASK_STATE_STOPPED:  return "Stopped";
        default:                    return "Unknown";
    }
}

const char *nxtask_kdrv_priority_name(nxtask_priority_t prio) {
    switch (prio) {
        case NXTASK_PRIO_IDLE:          return "Idle";
        case NXTASK_PRIO_LOW:           return "Low";
        case NXTASK_PRIO_BELOW_NORMAL:  return "Below Normal";
        case NXTASK_PRIO_NORMAL:        return "Normal";
        case NXTASK_PRIO_ABOVE_NORMAL:  return "Above Normal";
        case NXTASK_PRIO_HIGH:          return "High";
        case NXTASK_PRIO_REALTIME:      return "Realtime";
        default:                        return "Unknown";
    }
}

void nxtask_kdrv_debug(void) {
    serial_puts("\n=== NXTask Debug ===\n");
    serial_puts("Architecture: " NXTASK_ARCH_NAME "\n");
    
    nxtask_sysinfo_t sys;
    nxtask_kdrv_sysinfo(&sys);
    
    serial_puts("\nSystem:\n");
    serial_puts("  Uptime: ");
    serial_dec(sys.uptime_sec);
    serial_puts(" sec\n");
    
    serial_puts("  Processes: ");
    serial_dec(sys.total_processes);
    serial_puts(" total, ");
    serial_dec(sys.running_processes);
    serial_puts(" running\n");
    
    serial_puts("  Load avg: ");
    serial_dec(sys.load_avg_1min / 100);
    serial_puts(".");
    serial_dec(sys.load_avg_1min % 100);
    serial_puts("\n");
    
    serial_puts("\nProcesses:\n");
    
    uint32_t pids[32];
    int count = nxtask_kdrv_list(pids, 32);
    
    for (int i = 0; i < count; i++) {
        nxtask_info_t info;
        if (nxtask_kdrv_info(pids[i], &info) == 0) {
            serial_puts("  [");
            serial_dec(info.pid);
            serial_puts("] ");
            serial_puts(info.name);
            serial_puts(" - ");
            serial_puts(nxtask_kdrv_state_name(info.state));
            serial_puts(" (");
            serial_puts(nxtask_kdrv_priority_name(info.priority));
            serial_puts(") CPU: ");
            serial_dec(info.cpu.cpu_percent_x10 / 10);
            serial_puts(".");
            serial_dec(info.cpu.cpu_percent_x10 % 10);
            serial_puts("%\n");
        }
    }
    
    serial_puts("====================\n\n");
}
