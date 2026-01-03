/*
 * NeolyxOS Session Manager Implementation
 * 
 * Manages user sessions, GUI processes, and crash recovery.
 * Desktop GUI runs as a separate process - if it crashes,
 * kernel and games keep running, and GUI can be restarted.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#include <stdint.h>
#include "../../include/core/session_manager.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void serial_putc(char c);
extern uint64_t pit_get_ticks(void);
extern void *kmalloc(uint64_t size);
extern void kfree(void *ptr);

/* Process management */
extern int process_spawn(const char *path, int flags);
extern int process_kill(int pid, int signal);
extern int process_wait(int pid);

/* ============ Static State ============ */

static session_info_t g_sessions[MAX_SESSIONS];
static int g_session_count = 0;
static int32_t g_desktop_session = -1;
static uint32_t g_next_session_id = 1;

/* Exclusive access tracking */
static int32_t g_exclusive_gpu_holder = -1;
static int32_t g_exclusive_audio_holder = -1;

/* Default paths */
static const char *DESKTOP_EXECUTABLE = "/System/NXRender/desktop";

/* ============ Utility Functions ============ */

static void serial_dec(uint64_t val) {
    char buf[21];
    int i = 20;
    buf[i--] = '\0';
    if (val == 0) { serial_putc('0'); return; }
    while (val > 0) { buf[i--] = '0' + (val % 10); val /= 10; }
    serial_puts(&buf[i + 1]);
}

static session_info_t *find_session(uint32_t session_id) {
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].session_id == session_id) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

static session_info_t *find_session_by_pid(int32_t pid) {
    for (int i = 0; i < g_session_count; i++) {
        if (g_sessions[i].main_pid == pid) {
            return &g_sessions[i];
        }
    }
    return NULL;
}

/* ============ Session Manager API Implementation ============ */

int session_manager_init(void) {
    serial_puts("[SESSION] Initializing session manager...\n");
    
    /* Clear all sessions */
    for (int i = 0; i < MAX_SESSIONS; i++) {
        g_sessions[i].session_id = 0;
        g_sessions[i].state = SESSION_STATE_INACTIVE;
        g_sessions[i].main_pid = -1;
    }
    
    g_session_count = 0;
    g_desktop_session = -1;
    g_exclusive_gpu_holder = -1;
    g_exclusive_audio_holder = -1;
    
    serial_puts("[SESSION] Session manager ready\n");
    
    return 0;
}

int32_t session_create(session_type_t type, uint32_t flags) {
    if (g_session_count >= MAX_SESSIONS) {
        serial_puts("[SESSION] Error: Max sessions reached\n");
        return -1;
    }
    
    session_info_t *session = &g_sessions[g_session_count];
    
    session->session_id = g_next_session_id++;
    session->type = type;
    session->state = SESSION_STATE_INACTIVE;
    session->flags = flags;
    session->main_pid = -1;
    session->display_id = 0;  /* Primary display by default */
    session->crash_count = 0;
    session->start_time = 0;
    session->last_crash_time = 0;
    
    g_session_count++;
    
    serial_puts("[SESSION] Created session ");
    serial_dec(session->session_id);
    serial_puts(" type=");
    switch (type) {
        case SESSION_TYPE_CONSOLE: serial_puts("CONSOLE"); break;
        case SESSION_TYPE_DESKTOP: serial_puts("DESKTOP"); break;
        case SESSION_TYPE_GAME:    serial_puts("GAME"); break;
    }
    serial_puts("\n");
    
    return session->session_id;
}

int session_destroy(uint32_t session_id) {
    session_info_t *session = find_session(session_id);
    if (!session) {
        return -1;
    }
    
    /* Can't destroy system sessions */
    if (session->flags & SESSION_FLAG_SYSTEM) {
        serial_puts("[SESSION] Error: Can't destroy system session\n");
        return -1;
    }
    
    /* Release any exclusive access */
    session_release_exclusive(session_id);
    
    /* Kill main process if running */
    if (session->main_pid >= 0) {
        process_kill(session->main_pid, 9);  /* SIGKILL */
    }
    
    /* Mark as inactive */
    session->state = SESSION_STATE_INACTIVE;
    session->main_pid = -1;
    
    serial_puts("[SESSION] Destroyed session ");
    serial_dec(session_id);
    serial_puts("\n");
    
    return 0;
}

int session_start(uint32_t session_id, const char *executable) {
    session_info_t *session = find_session(session_id);
    if (!session) {
        return -1;
    }
    
    if (session->state == SESSION_STATE_RUNNING) {
        serial_puts("[SESSION] Session already running\n");
        return -1;
    }
    
    serial_puts("[SESSION] Starting session ");
    serial_dec(session_id);
    serial_puts(": ");
    serial_puts(executable);
    serial_puts("\n");
    
    session->state = SESSION_STATE_STARTING;
    session->start_time = pit_get_ticks();
    
    /* Spawn the main process */
    int pid = process_spawn(executable, 0);
    if (pid < 0) {
        serial_puts("[SESSION] Error: Failed to spawn process\n");
        session->state = SESSION_STATE_INACTIVE;
        return -1;
    }
    
    session->main_pid = pid;
    session->state = SESSION_STATE_RUNNING;
    
    serial_puts("[SESSION] Session ");
    serial_dec(session_id);
    serial_puts(" running with PID ");
    serial_dec(pid);
    serial_puts("\n");
    
    return 0;
}

int session_restart(uint32_t session_id) {
    session_info_t *session = find_session(session_id);
    if (!session) {
        return -1;
    }
    
    serial_puts("[SESSION] Restarting session ");
    serial_dec(session_id);
    serial_puts("...\n");
    
    /* Kill existing process if any */
    if (session->main_pid >= 0) {
        process_kill(session->main_pid, 9);
        session->main_pid = -1;
    }
    
    /* Small delay for cleanup */
    /* In production: proper wait for process exit */
    
    /* Restart based on type */
    const char *executable = NULL;
    switch (session->type) {
        case SESSION_TYPE_DESKTOP:
            executable = DESKTOP_EXECUTABLE;
            break;
        case SESSION_TYPE_CONSOLE:
            executable = "/System/Console/shell";
            break;
        default:
            serial_puts("[SESSION] Error: Can't auto-restart this session type\n");
            return -1;
    }
    
    session->state = SESSION_STATE_INACTIVE;
    return session_start(session_id, executable);
}

int session_get_info(uint32_t session_id, session_info_t *info) {
    session_info_t *session = find_session(session_id);
    if (!session || !info) {
        return -1;
    }
    
    *info = *session;
    return 0;
}

void session_notify_crash(int32_t pid) {
    session_info_t *session = find_session_by_pid(pid);
    if (!session) {
        return;
    }
    
    serial_puts("\n[SESSION] !!! CRASH DETECTED !!!\n");
    serial_puts("[SESSION] Session ");
    serial_dec(session->session_id);
    serial_puts(" (PID ");
    serial_dec(pid);
    serial_puts(") crashed!\n");
    
    session->state = SESSION_STATE_CRASHED;
    session->crash_count++;
    session->last_crash_time = pit_get_ticks();
    session->main_pid = -1;
    
    /* Release any exclusive access */
    session_release_exclusive(session->session_id);
    
    /* Check if auto-restart is enabled */
    if (session->flags & SESSION_FLAG_AUTO_RESTART) {
        serial_puts("[SESSION] Auto-restart enabled, restarting...\n");
        session_restart(session->session_id);
    } else {
        serial_puts("[SESSION] Session stopped. Manual restart required.\n");
    }
    
    serial_puts("[SESSION] Kernel and other sessions continue running.\n\n");
}

void session_notify_exit(int32_t pid, int exit_code) {
    session_info_t *session = find_session_by_pid(pid);
    if (!session) {
        return;
    }
    
    serial_puts("[SESSION] Session ");
    serial_dec(session->session_id);
    serial_puts(" exited with code ");
    serial_dec(exit_code);
    serial_puts("\n");
    
    session->state = SESSION_STATE_INACTIVE;
    session->main_pid = -1;
    
    /* Release any exclusive access */
    session_release_exclusive(session->session_id);
}

/* ============ Desktop GUI Control ============ */

int32_t session_start_desktop(void) {
    /* Check if desktop already exists */
    if (g_desktop_session >= 0) {
        session_info_t *session = find_session(g_desktop_session);
        if (session && session->state == SESSION_STATE_RUNNING) {
            serial_puts("[SESSION] Desktop already running\n");
            return g_desktop_session;
        }
    }
    
    serial_puts("[SESSION] Starting desktop GUI...\n");
    
    /* Create desktop session with auto-restart */
    int32_t session_id = session_create(SESSION_TYPE_DESKTOP, 
        SESSION_FLAG_AUTO_RESTART | SESSION_FLAG_SYSTEM);
    
    if (session_id < 0) {
        return -1;
    }
    
    g_desktop_session = session_id;
    
    /* Start the desktop executable */
    if (session_start(session_id, DESKTOP_EXECUTABLE) < 0) {
        session_destroy(session_id);
        g_desktop_session = -1;
        return -1;
    }
    
    return session_id;
}

int session_restart_desktop(void) {
    if (g_desktop_session < 0) {
        serial_puts("[SESSION] No desktop session to restart\n");
        return session_start_desktop() >= 0 ? 0 : -1;
    }
    
    serial_puts("[SESSION] Restarting desktop GUI...\n");
    serial_puts("[SESSION] Games and kernel will continue running.\n");
    
    return session_restart(g_desktop_session);
}

session_state_t session_get_desktop_state(void) {
    if (g_desktop_session < 0) {
        return SESSION_STATE_INACTIVE;
    }
    
    session_info_t *session = find_session(g_desktop_session);
    if (!session) {
        return SESSION_STATE_INACTIVE;
    }
    
    return session->state;
}

/* ============ Game/Exclusive Mode Control ============ */

int session_request_exclusive(uint32_t session_id, uint32_t flags) {
    session_info_t *session = find_session(session_id);
    if (!session) {
        return -1;
    }
    
    serial_puts("[SESSION] Session ");
    serial_dec(session_id);
    serial_puts(" requesting exclusive access\n");
    
    /* Check GPU */
    if (flags & SESSION_FLAG_EXCLUSIVE_GPU) {
        if (g_exclusive_gpu_holder >= 0 && g_exclusive_gpu_holder != (int32_t)session_id) {
            serial_puts("[SESSION] GPU already held by session ");
            serial_dec(g_exclusive_gpu_holder);
            serial_puts(", denied\n");
            return -1;
        }
        g_exclusive_gpu_holder = session_id;
        serial_puts("[SESSION] Granted exclusive GPU access\n");
        
        /* Tell desktop to step back */
        if (g_desktop_session >= 0 && g_desktop_session != (int32_t)session_id) {
            serial_puts("[SESSION] Desktop stepping back for exclusive GPU mode\n");
            /* In production: signal desktop to stop rendering */
        }
    }
    
    /* Check audio */
    if (flags & SESSION_FLAG_EXCLUSIVE_AUDIO) {
        if (g_exclusive_audio_holder >= 0 && g_exclusive_audio_holder != (int32_t)session_id) {
            serial_puts("[SESSION] Audio already held by session ");
            serial_dec(g_exclusive_audio_holder);
            serial_puts(", denied\n");
            return -1;
        }
        g_exclusive_audio_holder = session_id;
        serial_puts("[SESSION] Granted exclusive audio access\n");
    }
    
    session->flags |= flags;
    
    return 0;
}

int session_release_exclusive(uint32_t session_id) {
    session_info_t *session = find_session(session_id);
    if (!session) {
        return -1;
    }
    
    if (g_exclusive_gpu_holder == (int32_t)session_id) {
        g_exclusive_gpu_holder = -1;
        serial_puts("[SESSION] Released exclusive GPU access\n");
        
        /* Tell desktop it can render again */
        if (g_desktop_session >= 0) {
            serial_puts("[SESSION] Desktop can resume rendering\n");
            /* In production: signal desktop to resume */
        }
    }
    
    if (g_exclusive_audio_holder == (int32_t)session_id) {
        g_exclusive_audio_holder = -1;
        serial_puts("[SESSION] Released exclusive audio access\n");
    }
    
    session->flags &= ~(SESSION_FLAG_EXCLUSIVE_GPU | SESSION_FLAG_EXCLUSIVE_AUDIO);
    
    return 0;
}

int32_t session_get_exclusive_holder(uint32_t flags) {
    if ((flags & SESSION_FLAG_EXCLUSIVE_GPU) && g_exclusive_gpu_holder >= 0) {
        return g_exclusive_gpu_holder;
    }
    if ((flags & SESSION_FLAG_EXCLUSIVE_AUDIO) && g_exclusive_audio_holder >= 0) {
        return g_exclusive_audio_holder;
    }
    return -1;
}

/* ============ Debug/Info ============ */

void session_print_all(void) {
    serial_puts("\n=== Sessions ===\n");
    serial_puts("Count: ");
    serial_dec(g_session_count);
    serial_puts("\n");
    
    for (int i = 0; i < g_session_count; i++) {
        session_info_t *s = &g_sessions[i];
        if (s->state == SESSION_STATE_INACTIVE && s->session_id == 0) {
            continue;
        }
        
        serial_puts("  [");
        serial_dec(s->session_id);
        serial_puts("] ");
        
        switch (s->type) {
            case SESSION_TYPE_CONSOLE: serial_puts("CONSOLE "); break;
            case SESSION_TYPE_DESKTOP: serial_puts("DESKTOP "); break;
            case SESSION_TYPE_GAME:    serial_puts("GAME    "); break;
        }
        
        switch (s->state) {
            case SESSION_STATE_INACTIVE: serial_puts("INACTIVE"); break;
            case SESSION_STATE_STARTING: serial_puts("STARTING"); break;
            case SESSION_STATE_RUNNING:  serial_puts("RUNNING "); break;
            case SESSION_STATE_STOPPING: serial_puts("STOPPING"); break;
            case SESSION_STATE_CRASHED:  serial_puts("CRASHED "); break;
        }
        
        if (s->main_pid >= 0) {
            serial_puts(" PID=");
            serial_dec(s->main_pid);
        }
        
        if (s->flags & SESSION_FLAG_EXCLUSIVE_GPU) {
            serial_puts(" [GPU]");
        }
        
        serial_puts("\n");
    }
    
    serial_puts("Desktop session: ");
    if (g_desktop_session >= 0) {
        serial_dec(g_desktop_session);
    } else {
        serial_puts("none");
    }
    serial_puts("\n");
    
    serial_puts("Exclusive GPU: ");
    if (g_exclusive_gpu_holder >= 0) {
        serial_puts("session ");
        serial_dec(g_exclusive_gpu_holder);
    } else {
        serial_puts("none");
    }
    serial_puts("\n");
    
    serial_puts("================\n\n");
}
