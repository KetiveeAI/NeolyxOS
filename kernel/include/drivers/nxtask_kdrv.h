/*
 * NXTask Kernel Driver (nxtask.kdrv)
 * 
 * NeolyxOS Kernel Task Manager
 * 
 * Features:
 *   - Process enumeration and stats
 *   - CPU usage per process
 *   - Memory consumption tracking
 *   - Priority management
 *   - Scheduler statistics
 *   - Architecture-agnostic (x86_64, ARM64)
 * 
 * Copyright (c) 2025 KetiveeAI - KETIVEEAI License
 */

#ifndef NXTASK_KDRV_H
#define NXTASK_KDRV_H

#include <stdint.h>
#include <stddef.h>

/* ============ Driver Metadata ============ */

#define NXTASK_KDRV_VERSION    "1.0.0"
#define NXTASK_KDRV_ABI        1

/* ============ Process State ============ */

typedef enum {
    NXTASK_STATE_RUNNING = 0,
    NXTASK_STATE_READY,
    NXTASK_STATE_BLOCKED,
    NXTASK_STATE_SLEEPING,
    NXTASK_STATE_ZOMBIE,
    NXTASK_STATE_STOPPED,
} nxtask_state_t;

/* ============ Priority Levels ============ */

typedef enum {
    NXTASK_PRIO_IDLE = 0,       /* Lowest - only runs when nothing else */
    NXTASK_PRIO_LOW = 1,
    NXTASK_PRIO_BELOW_NORMAL = 2,
    NXTASK_PRIO_NORMAL = 3,     /* Default */
    NXTASK_PRIO_ABOVE_NORMAL = 4,
    NXTASK_PRIO_HIGH = 5,
    NXTASK_PRIO_REALTIME = 6,   /* Highest - preempts everything */
    NXTASK_PRIO_COUNT = 7,
} nxtask_priority_t;

/* ============ Process Flags ============ */

typedef enum {
    NXTASK_FLAG_KERNEL      = (1 << 0),  /* Kernel thread */
    NXTASK_FLAG_USER        = (1 << 1),  /* User process */
    NXTASK_FLAG_SYSTEM      = (1 << 2),  /* System service */
    NXTASK_FLAG_IDLE        = (1 << 3),  /* Idle process */
    NXTASK_FLAG_SUSPENDED   = (1 << 4),  /* Manually suspended */
    NXTASK_FLAG_CRITICAL    = (1 << 5),  /* Cannot be killed */
} nxtask_flags_t;

/* ============ CPU Usage Statistics ============ */

typedef struct {
    uint64_t        user_time_us;       /* Time in user mode */
    uint64_t        kernel_time_us;     /* Time in kernel mode */
    uint64_t        total_time_us;      /* Total CPU time */
    uint64_t        start_time_us;      /* Process start time */
    
    /* Per-second averages */
    uint32_t        cpu_percent_x10;    /* CPU % x10 (e.g., 125 = 12.5%) */
    uint32_t        cpu_percent_avg;    /* Average CPU % */
    
    /* Scheduling */
    uint32_t        context_switches;
    uint32_t        voluntary_switches;
    uint32_t        involuntary_switches;
    uint32_t        time_slice;
} nxtask_cpu_stats_t;

/* ============ Memory Statistics ============ */

typedef struct {
    uint64_t        virtual_size;       /* Virtual memory size */
    uint64_t        resident_size;      /* Physical memory used */
    uint64_t        shared_size;        /* Shared memory */
    uint64_t        text_size;          /* Code segment */
    uint64_t        data_size;          /* Data + BSS */
    uint64_t        stack_size;         /* Stack size */
    uint64_t        heap_size;          /* Heap size */
    
    /* Page faults */
    uint32_t        minor_faults;       /* No disk I/O needed */
    uint32_t        major_faults;       /* Required disk I/O */
} nxtask_mem_stats_t;

/* ============ I/O Statistics ============ */

typedef struct {
    uint64_t        read_bytes;
    uint64_t        write_bytes;
    uint64_t        read_ops;
    uint64_t        write_ops;
} nxtask_io_stats_t;

/* ============ Process Info ============ */

typedef struct {
    uint32_t            pid;
    uint32_t            ppid;           /* Parent PID */
    uint32_t            uid;
    uint32_t            gid;
    
    char                name[64];
    char                cmdline[256];
    
    nxtask_state_t      state;
    nxtask_priority_t   priority;
    uint32_t            nice;           /* -20 to +19 */
    uint32_t            flags;
    
    /* Core affinity (bitmask) */
    uint64_t            affinity_mask;
    uint32_t            running_on_cpu;
    
    /* Stats */
    nxtask_cpu_stats_t  cpu;
    nxtask_mem_stats_t  mem;
    nxtask_io_stats_t   io;
    
    /* Threads */
    uint32_t            thread_count;
} nxtask_info_t;

/* ============ System-wide Statistics ============ */

typedef struct {
    uint32_t        total_processes;
    uint32_t        running_processes;
    uint32_t        sleeping_processes;
    uint32_t        zombie_processes;
    
    uint32_t        total_threads;
    
    /* CPU */
    uint32_t        cpu_count;
    uint32_t        cpu_online;
    uint32_t        load_avg_1min;      /* x100 (e.g., 150 = 1.50) */
    uint32_t        load_avg_5min;
    uint32_t        load_avg_15min;
    
    /* Per-CPU usage */
    uint32_t        cpu_user_percent;
    uint32_t        cpu_kernel_percent;
    uint32_t        cpu_idle_percent;
    uint32_t        cpu_iowait_percent;
    
    /* Memory */
    uint64_t        mem_total;
    uint64_t        mem_used;
    uint64_t        mem_free;
    uint64_t        mem_cached;
    uint64_t        mem_buffers;
    
    /* Uptime */
    uint64_t        uptime_sec;
    uint64_t        idle_time_sec;
} nxtask_sysinfo_t;

/* ============ Kernel Driver API ============ */

/**
 * Initialize task manager
 */
int nxtask_kdrv_init(void);

/**
 * Shutdown
 */
void nxtask_kdrv_shutdown(void);

/**
 * Get total process count
 */
int nxtask_kdrv_count(void);

/**
 * Get list of all PIDs
 * Returns number of PIDs copied
 */
int nxtask_kdrv_list(uint32_t *pids, int max_count);

/**
 * Get process info by PID
 */
int nxtask_kdrv_info(uint32_t pid, nxtask_info_t *info);

/**
 * Get system-wide statistics
 */
int nxtask_kdrv_sysinfo(nxtask_sysinfo_t *info);

/**
 * Set process priority
 */
int nxtask_kdrv_set_priority(uint32_t pid, nxtask_priority_t priority);

/**
 * Set process nice value (-20 to +19)
 */
int nxtask_kdrv_set_nice(uint32_t pid, int nice);

/**
 * Set CPU affinity
 */
int nxtask_kdrv_set_affinity(uint32_t pid, uint64_t mask);

/**
 * Suspend process
 */
int nxtask_kdrv_suspend(uint32_t pid);

/**
 * Resume process
 */
int nxtask_kdrv_resume(uint32_t pid);

/**
 * Terminate process
 */
int nxtask_kdrv_kill(uint32_t pid, int signal);

/**
 * Get state name
 */
const char *nxtask_kdrv_state_name(nxtask_state_t state);

/**
 * Get priority name
 */
const char *nxtask_kdrv_priority_name(nxtask_priority_t prio);

/**
 * Debug output
 */
void nxtask_kdrv_debug(void);

/* ============ Architecture Detection ============ */

#if defined(__x86_64__) || defined(_M_X64)
    #define NXTASK_ARCH_X86_64  1
    #define NXTASK_ARCH_NAME    "x86_64"
#elif defined(__aarch64__) || defined(_M_ARM64)
    #define NXTASK_ARCH_ARM64   1
    #define NXTASK_ARCH_NAME    "ARM64"
#elif defined(__arm__) || defined(_M_ARM)
    #define NXTASK_ARCH_ARM32   1
    #define NXTASK_ARCH_NAME    "ARM32"
#else
    #define NXTASK_ARCH_UNKNOWN 1
    #define NXTASK_ARCH_NAME    "Unknown"
#endif

#endif /* NXTASK_KDRV_H */
