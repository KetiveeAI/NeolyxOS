/*
 * NeolyxOS Pseudo-Terminal (PTY) Subsystem
 * 
 * Provides terminal multiplexing for Terminal.app and other terminal emulators.
 * 
 * Architecture:
 *   Terminal.app (userspace)
 *        ↓ syscalls (60-67)
 *   PTY Master/Slave pair
 *        ↓
 *   Shell process (/bin/nxsh)
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_PTY_H
#define NEOLYX_PTY_H

#include <stdint.h>
#include <stddef.h>

/* ============ PTY Configuration ============ */

#define PTY_MAX_PAIRS       32      /* Maximum concurrent PTYs */
#define PTY_BUFFER_SIZE     4096    /* Ring buffer size */
#define PTY_NAME_SIZE       16      /* "/dev/ptyN" */

/* ============ PTY IOCTL Commands ============ */

#define PTY_IOCTL_GET_SIZE      1   /* Get terminal size (rows, cols) */
#define PTY_IOCTL_SET_SIZE      2   /* Set terminal size */
#define PTY_IOCTL_GET_OWNER     3   /* Get owning process */
#define PTY_IOCTL_SET_ECHO      4   /* Enable/disable echo */
#define PTY_IOCTL_SET_RAW       5   /* Set raw mode (no line editing) */
#define PTY_IOCTL_FLUSH         6   /* Flush buffers */

/* ============ PTY Flags ============ */

#define PTY_FLAG_ECHO       0x01    /* Echo input back */
#define PTY_FLAG_RAW        0x02    /* Raw mode (no line buffering) */
#define PTY_FLAG_CRLF       0x04    /* Convert \n to \r\n */
#define PTY_FLAG_CTRLC      0x08    /* Handle Ctrl+C as SIGINT */

/* ============ Structures ============ */

/* Ring buffer for PTY I/O */
typedef struct {
    uint8_t data[PTY_BUFFER_SIZE];
    volatile uint32_t head;
    volatile uint32_t tail;
    volatile uint32_t count;
} pty_buffer_t;

/* PTY size (terminal dimensions) */
typedef struct {
    uint16_t rows;
    uint16_t cols;
    uint16_t xpixel;
    uint16_t ypixel;
} pty_winsize_t;

/* PTY pair (master + slave) */
typedef struct {
    int id;                     /* PTY pair ID (0 to PTY_MAX_PAIRS-1) */
    int master_fd;              /* Master file descriptor */
    int slave_fd;               /* Slave file descriptor */
    
    pty_buffer_t master_to_slave; /* Data from terminal to shell */
    pty_buffer_t slave_to_master; /* Data from shell to terminal */
    
    pty_winsize_t winsize;      /* Terminal dimensions */
    uint32_t flags;             /* PTY_FLAG_* */
    
    int32_t owner_pid;          /* Owning process (Terminal.app) */
    int32_t shell_pid;          /* Child shell process */
    
    int in_use;                 /* Is this pair allocated? */
    char name[PTY_NAME_SIZE];   /* Device name */
} pty_pair_t;

/* ============ PTY API ============ */

/**
 * Initialize PTY subsystem.
 */
void pty_init(void);

/**
 * Allocate a new PTY master/slave pair.
 * @param master_fd Output: master file descriptor
 * @param slave_fd Output: slave file descriptor
 * @return 0 on success, -errno on failure
 */
int pty_open(int *master_fd, int *slave_fd);

/**
 * Read from PTY (from shell to terminal).
 * @param fd Master or slave file descriptor
 * @param buf Buffer to read into
 * @param count Maximum bytes to read
 * @return Bytes read, or -errno on error
 */
ssize_t pty_read(int fd, void *buf, size_t count);

/**
 * Write to PTY (from terminal to shell or vice versa).
 * @param fd Master or slave file descriptor
 * @param buf Data to write
 * @param count Number of bytes
 * @return Bytes written, or -errno on error
 */
ssize_t pty_write(int fd, const void *buf, size_t count);

/**
 * Close a PTY file descriptor.
 * @param fd Master or slave file descriptor
 * @return 0 on success, -errno on failure
 */
int pty_close(int fd);

/**
 * Resize PTY (update terminal dimensions).
 * @param fd Master file descriptor
 * @param cols Number of columns
 * @param rows Number of rows
 * @return 0 on success, -errno on failure
 */
int pty_resize(int fd, int cols, int rows);

/**
 * PTY ioctl for miscellaneous control.
 * @param fd File descriptor
 * @param cmd PTY_IOCTL_* command
 * @param arg Command-specific argument
 * @return Command-specific return value
 */
int pty_ioctl(int fd, int cmd, void *arg);

/**
 * Spawn a shell process attached to PTY slave.
 * @param slave_fd Slave file descriptor (becomes stdin/stdout/stderr)
 * @param shell Path to shell binary (e.g., "/bin/nxsh")
 * @return Child PID on success, -errno on failure
 */
int pty_spawn_shell(int slave_fd, const char *shell);

/**
 * Poll PTY for data availability.
 * @param fd File descriptor to poll
 * @param timeout_ms Timeout in milliseconds (-1 = infinite)
 * @return 1 if data available, 0 if timeout, -errno on error
 */
int pty_poll(int fd, int timeout_ms);

/**
 * Get PTY pair by file descriptor.
 * @param fd Master or slave file descriptor
 * @return PTY pair pointer, or NULL if invalid
 */
pty_pair_t *pty_get_pair(int fd);

#endif /* NEOLYX_PTY_H */
