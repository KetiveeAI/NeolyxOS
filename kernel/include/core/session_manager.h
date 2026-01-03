/*
 * NeolyxOS Session Manager
 * 
 * Manages user sessions, GUI processes, and crash recovery.
 * Desktop GUI runs as a separate process - if it crashes,
 * kernel and games keep running, and GUI can be restarted.
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef NEOLYX_SESSION_MANAGER_H
#define NEOLYX_SESSION_MANAGER_H

#include <stdint.h>

/* Session types */
typedef enum {
    SESSION_TYPE_CONSOLE = 0,     /* Text console session */
    SESSION_TYPE_DESKTOP = 1,     /* Desktop GUI session (NXRender) */
    SESSION_TYPE_GAME    = 2,     /* Game/exclusive GPU session */
} session_type_t;

/* Session state */
typedef enum {
    SESSION_STATE_INACTIVE = 0,   /* Not started */
    SESSION_STATE_STARTING = 1,   /* Being initialized */
    SESSION_STATE_RUNNING  = 2,   /* Fully running */
    SESSION_STATE_STOPPING = 3,   /* Being shut down */
    SESSION_STATE_CRASHED  = 4,   /* Session crashed */
} session_state_t;

/* Session flags */
typedef enum {
    SESSION_FLAG_EXCLUSIVE_GPU  = 0x01,  /* Has exclusive GPU access */
    SESSION_FLAG_EXCLUSIVE_AUDIO = 0x02, /* Has exclusive audio access */
    SESSION_FLAG_AUTO_RESTART   = 0x04,  /* Auto-restart on crash */
    SESSION_FLAG_SYSTEM         = 0x08,  /* System session (can't be killed) */
} session_flags_t;

/* Session info */
typedef struct {
    uint32_t session_id;          /* Unique session ID */
    session_type_t type;          /* Session type */
    session_state_t state;        /* Current state */
    uint32_t flags;               /* Session flags */
    int32_t main_pid;             /* Main process PID (-1 if none) */
    int32_t display_id;           /* Display this session uses */
    uint32_t crash_count;         /* Number of times crashed */
    uint64_t start_time;          /* When session started */
    uint64_t last_crash_time;     /* When session last crashed */
} session_info_t;

#define MAX_SESSIONS 16

/* ============ Session Manager API ============ */

/*
 * session_manager_init - Initialize the session manager
 * 
 * Returns: 0 on success, -1 on error
 */
int session_manager_init(void);

/*
 * session_create - Create a new session
 * 
 * @type: Type of session to create
 * @flags: Session flags
 * 
 * Returns: Session ID on success, -1 on error
 */
int32_t session_create(session_type_t type, uint32_t flags);

/*
 * session_destroy - Destroy a session
 * 
 * @session_id: Session to destroy
 * 
 * Returns: 0 on success, -1 on error
 */
int session_destroy(uint32_t session_id);

/*
 * session_start - Start a session (launch its main process)
 * 
 * @session_id: Session to start
 * @executable: Path to executable to run
 * 
 * Returns: 0 on success, -1 on error
 */
int session_start(uint32_t session_id, const char *executable);

/*
 * session_restart - Restart a crashed or stopped session
 * 
 * @session_id: Session to restart
 * 
 * Returns: 0 on success, -1 on error
 */
int session_restart(uint32_t session_id);

/*
 * session_get_info - Get information about a session
 * 
 * @session_id: Session to query
 * @info: Output structure
 * 
 * Returns: 0 on success, -1 on error
 */
int session_get_info(uint32_t session_id, session_info_t *info);

/*
 * session_notify_crash - Called when a session's main process crashes
 * 
 * @pid: PID that crashed
 */
void session_notify_crash(int32_t pid);

/*
 * session_notify_exit - Called when a session's main process exits normally
 * 
 * @pid: PID that exited
 * @exit_code: Exit code of the process
 */
void session_notify_exit(int32_t pid, int exit_code);

/* ============ Desktop GUI Control ============ */

/*
 * session_start_desktop - Start the desktop GUI session
 * 
 * Convenience function to start NXRender desktop.
 * 
 * Returns: Session ID on success, -1 on error
 */
int32_t session_start_desktop(void);

/*
 * session_restart_desktop - Restart the desktop GUI
 * 
 * Called when desktop crashes or needs refresh.
 * Games and kernel keep running.
 * 
 * Returns: 0 on success, -1 on error
 */
int session_restart_desktop(void);

/*
 * session_get_desktop_state - Get desktop session state
 * 
 * Returns: Current state of desktop session
 */
session_state_t session_get_desktop_state(void);

/* ============ Game/Exclusive Mode Control ============ */

/*
 * session_request_exclusive - Request exclusive GPU/audio access
 * 
 * @session_id: Session requesting exclusive access
 * @flags: What to request exclusive access to
 * 
 * Returns: 0 on success, -1 if denied
 */
int session_request_exclusive(uint32_t session_id, uint32_t flags);

/*
 * session_release_exclusive - Release exclusive access
 * 
 * @session_id: Session releasing access
 * 
 * Returns: 0 on success, -1 on error
 */
int session_release_exclusive(uint32_t session_id);

/*
 * session_get_exclusive_holder - Get session holding exclusive access
 * 
 * @flags: Which exclusive access to check
 * 
 * Returns: Session ID, or -1 if none
 */
int32_t session_get_exclusive_holder(uint32_t flags);

/* ============ Debug/Info ============ */

/*
 * session_print_all - Print info about all sessions
 */
void session_print_all(void);

#endif /* NEOLYX_SESSION_MANAGER_H */
