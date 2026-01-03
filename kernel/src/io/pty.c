/*
 * NeolyxOS Pseudo-Terminal (PTY) Implementation
 * 
 * Provides terminal multiplexing for Terminal.app.
 * Uses ring buffers for bidirectional I/O between master and slave.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "io/pty.h"
#include "core/syscall.h"
#include "core/process.h"

/* ============ External Dependencies ============ */

extern void serial_puts(const char *s);
extern void *kmalloc(size_t size);
extern void kfree(void *ptr);

/* Stub for process_current_pid if not available */
static int stub_getpid(void) {
    return 1; /* Default to PID 1 */
}
#define process_current_pid stub_getpid

/* ============ PTY State ============ */

static pty_pair_t pty_pairs[PTY_MAX_PAIRS];
static int pty_initialized = 0;
static int next_fd = 100;  /* Start PTY file descriptors at 100+ */

/* ============ Ring Buffer Operations ============ */

static void buffer_init(pty_buffer_t *buf) {
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
}

static int buffer_empty(pty_buffer_t *buf) {
    return buf->count == 0;
}

static int buffer_full(pty_buffer_t *buf) {
    return buf->count >= PTY_BUFFER_SIZE;
}

static int buffer_write_byte(pty_buffer_t *buf, uint8_t byte) {
    if (buffer_full(buf)) return -EAGAIN;
    
    buf->data[buf->head] = byte;
    buf->head = (buf->head + 1) % PTY_BUFFER_SIZE;
    buf->count++;
    return 0;
}

static int buffer_read_byte(pty_buffer_t *buf, uint8_t *byte) {
    if (buffer_empty(buf)) return -EAGAIN;
    
    *byte = buf->data[buf->tail];
    buf->tail = (buf->tail + 1) % PTY_BUFFER_SIZE;
    buf->count--;
    return 0;
}

static void buffer_flush(pty_buffer_t *buf) {
    buffer_init(buf);
}

/* ============ PTY Implementation ============ */

void pty_init(void) {
    if (pty_initialized) return;
    
    serial_puts("[PTY] Initializing pseudo-terminal subsystem\n");
    
    for (int i = 0; i < PTY_MAX_PAIRS; i++) {
        pty_pairs[i].id = i;
        pty_pairs[i].in_use = 0;
        pty_pairs[i].master_fd = -1;
        pty_pairs[i].slave_fd = -1;
        pty_pairs[i].owner_pid = -1;
        pty_pairs[i].shell_pid = -1;
    }
    
    pty_initialized = 1;
    serial_puts("[PTY] Ready (32 pairs available)\n");
}

pty_pair_t *pty_get_pair(int fd) {
    /* Find pair by master or slave fd */
    for (int i = 0; i < PTY_MAX_PAIRS; i++) {
        if (pty_pairs[i].in_use) {
            if (pty_pairs[i].master_fd == fd || pty_pairs[i].slave_fd == fd) {
                return &pty_pairs[i];
            }
        }
    }
    return NULL;
}

static int is_master(pty_pair_t *pair, int fd) {
    return pair->master_fd == fd;
}

int pty_open(int *master_fd, int *slave_fd) {
    if (!pty_initialized) pty_init();
    if (!master_fd || !slave_fd) return -EINVAL;
    
    /* Find free pair */
    pty_pair_t *pair = NULL;
    for (int i = 0; i < PTY_MAX_PAIRS; i++) {
        if (!pty_pairs[i].in_use) {
            pair = &pty_pairs[i];
            break;
        }
    }
    
    if (!pair) {
        serial_puts("[PTY] No free PTY pairs\n");
        return -ENOMEM;
    }
    
    /* Initialize pair */
    pair->in_use = 1;
    pair->master_fd = next_fd++;
    pair->slave_fd = next_fd++;
    pair->owner_pid = process_current_pid();
    pair->shell_pid = -1;
    pair->flags = PTY_FLAG_ECHO | PTY_FLAG_CRLF | PTY_FLAG_CTRLC;
    
    /* Default terminal size */
    pair->winsize.cols = 80;
    pair->winsize.rows = 24;
    pair->winsize.xpixel = 640;
    pair->winsize.ypixel = 384;
    
    /* Clear buffers */
    buffer_init(&pair->master_to_slave);
    buffer_init(&pair->slave_to_master);
    
    /* Set device name */
    pair->name[0] = '/';
    pair->name[1] = 'd';
    pair->name[2] = 'e';
    pair->name[3] = 'v';
    pair->name[4] = '/';
    pair->name[5] = 'p';
    pair->name[6] = 't';
    pair->name[7] = 'y';
    pair->name[8] = '0' + (pair->id / 10);
    pair->name[9] = '0' + (pair->id % 10);
    pair->name[10] = '\0';
    
    *master_fd = pair->master_fd;
    *slave_fd = pair->slave_fd;
    
    serial_puts("[PTY] Opened ");
    serial_puts(pair->name);
    serial_puts("\n");
    
    return 0;
}

ssize_t pty_read(int fd, void *buf, size_t count) {
    if (!buf || count == 0) return -EINVAL;
    
    pty_pair_t *pair = pty_get_pair(fd);
    if (!pair) return -EBADF;
    
    /* Master reads from slave_to_master (shell output) */
    /* Slave reads from master_to_slave (terminal input) */
    pty_buffer_t *buffer = is_master(pair, fd) 
        ? &pair->slave_to_master 
        : &pair->master_to_slave;
    
    uint8_t *data = (uint8_t *)buf;
    size_t read = 0;
    
    while (read < count) {
        uint8_t byte;
        if (buffer_read_byte(buffer, &byte) != 0) {
            break;  /* Buffer empty */
        }
        data[read++] = byte;
    }
    
    return (ssize_t)read;
}

ssize_t pty_write(int fd, const void *buf, size_t count) {
    if (!buf || count == 0) return -EINVAL;
    
    pty_pair_t *pair = pty_get_pair(fd);
    if (!pair) return -EBADF;
    
    /* Master writes to master_to_slave (terminal input) */
    /* Slave writes to slave_to_master (shell output) */
    pty_buffer_t *buffer = is_master(pair, fd)
        ? &pair->master_to_slave
        : &pair->slave_to_master;
    
    const uint8_t *data = (const uint8_t *)buf;
    size_t written = 0;
    
    while (written < count) {
        uint8_t byte = data[written];
        
        /* Handle special characters from master */
        if (is_master(pair, fd)) {
            /* Handle Ctrl+C */
            if ((pair->flags & PTY_FLAG_CTRLC) && byte == 0x03) {
                /* Signal shell process if running */
                if (pair->shell_pid > 0) {
                    /* TODO: process_signal(pair->shell_pid, SIGINT); */
                }
            }
            
            /* Echo back if enabled */
            if (pair->flags & PTY_FLAG_ECHO) {
                buffer_write_byte(&pair->slave_to_master, byte);
            }
            
            /* Convert \n to \r\n */
            if ((pair->flags & PTY_FLAG_CRLF) && byte == '\n') {
                buffer_write_byte(&pair->master_to_slave, '\r');
            }
        }
        
        if (buffer_write_byte(buffer, byte) != 0) {
            break;  /* Buffer full */
        }
        written++;
    }
    
    return (ssize_t)written;
}

int pty_close(int fd) {
    pty_pair_t *pair = pty_get_pair(fd);
    if (!pair) return -EBADF;
    
    /* Kill shell if closing master */
    if (is_master(pair, fd) && pair->shell_pid > 0) {
        /* TODO: process_signal(pair->shell_pid, SIGTERM); */
        pair->shell_pid = -1;
    }
    
    /* Mark fd as closed */
    if (is_master(pair, fd)) {
        pair->master_fd = -1;
    } else {
        pair->slave_fd = -1;
    }
    
    /* Free pair if both ends closed */
    if (pair->master_fd == -1 && pair->slave_fd == -1) {
        serial_puts("[PTY] Closed ");
        serial_puts(pair->name);
        serial_puts("\n");
        
        pair->in_use = 0;
        pair->owner_pid = -1;
        buffer_flush(&pair->master_to_slave);
        buffer_flush(&pair->slave_to_master);
    }
    
    return 0;
}

int pty_resize(int fd, int cols, int rows) {
    pty_pair_t *pair = pty_get_pair(fd);
    if (!pair) return -EBADF;
    
    pair->winsize.cols = cols;
    pair->winsize.rows = rows;
    pair->winsize.xpixel = cols * 8;
    pair->winsize.ypixel = rows * 16;
    
    /* TODO: Signal shell SIGWINCH */
    
    return 0;
}

int pty_ioctl(int fd, int cmd, void *arg) {
    pty_pair_t *pair = pty_get_pair(fd);
    if (!pair) return -EBADF;
    
    switch (cmd) {
        case PTY_IOCTL_GET_SIZE:
            if (arg) {
                *(pty_winsize_t *)arg = pair->winsize;
            }
            return 0;
            
        case PTY_IOCTL_SET_SIZE:
            if (arg) {
                pty_winsize_t *ws = (pty_winsize_t *)arg;
                return pty_resize(fd, ws->cols, ws->rows);
            }
            return -EINVAL;
            
        case PTY_IOCTL_GET_OWNER:
            return pair->owner_pid;
            
        case PTY_IOCTL_SET_ECHO:
            if (arg && *(int *)arg) {
                pair->flags |= PTY_FLAG_ECHO;
            } else {
                pair->flags &= ~PTY_FLAG_ECHO;
            }
            return 0;
            
        case PTY_IOCTL_SET_RAW:
            if (arg && *(int *)arg) {
                pair->flags |= PTY_FLAG_RAW;
                pair->flags &= ~(PTY_FLAG_ECHO | PTY_FLAG_CRLF);
            } else {
                pair->flags &= ~PTY_FLAG_RAW;
            }
            return 0;
            
        case PTY_IOCTL_FLUSH:
            buffer_flush(&pair->master_to_slave);
            buffer_flush(&pair->slave_to_master);
            return 0;
            
        default:
            return -EINVAL;
    }
}

int pty_spawn_shell(int slave_fd, const char *shell) {
    pty_pair_t *pair = pty_get_pair(slave_fd);
    if (!pair) return -EBADF;
    if (pair->shell_pid > 0) return -EBUSY;
    
    serial_puts("[PTY] Spawning shell: ");
    serial_puts(shell ? shell : "/bin/nxsh");
    serial_puts("\n");
    
    /* TODO: Implement fork/exec when process subsystem is ready */
    /* For now, return a placeholder PID */
    pair->shell_pid = 2;  /* Placeholder */
    
    return pair->shell_pid;
}

int pty_poll(int fd, int timeout_ms) {
    pty_pair_t *pair = pty_get_pair(fd);
    if (!pair) return -EBADF;
    
    /* Check if data available */
    pty_buffer_t *buffer = is_master(pair, fd)
        ? &pair->slave_to_master
        : &pair->master_to_slave;
    
    if (!buffer_empty(buffer)) {
        return 1;  /* Data available */
    }
    
    if (timeout_ms == 0) {
        return 0;  /* Non-blocking, no data */
    }
    
    /* TODO: Implement proper waiting with timeout */
    /* For now, just return 0 */
    (void)timeout_ms;
    
    return 0;
}

/*
 * Note: Syscall registration should be done in syscall.c's syscall_init()
 * Add these lines there:
 *
 *   extern int64_t sys_pty_open_impl(int *master_fd, int *slave_fd);
 *   syscall_table[SYS_PTY_OPEN] = (void*)pty_open;
 *   // etc.
 *
 * For now, PTY functions are called directly by the kernel terminal.
 */

/* Make PTY handlers available for syscall.c to register */
int64_t pty_syscall_open(int *master_fd, int *slave_fd) {
    return pty_open(master_fd, slave_fd);
}

int64_t pty_syscall_read(int fd, void *buf, size_t count) {
    return pty_read(fd, buf, count);
}

int64_t pty_syscall_write(int fd, const void *buf, size_t count) {
    return pty_write(fd, buf, count);
}

int64_t pty_syscall_close(int fd) {
    return pty_close(fd);
}

int64_t pty_syscall_resize(int fd, int cols, int rows) {
    return pty_resize(fd, cols, rows);
}

int64_t pty_syscall_ioctl(int fd, int cmd, void *arg) {
    return pty_ioctl(fd, cmd, arg);
}

int64_t pty_syscall_spawn(int slave_fd, const char *shell) {
    return pty_spawn_shell(slave_fd, shell);
}

int64_t pty_syscall_poll(int fd, int timeout_ms) {
    return pty_poll(fd, timeout_ms);
}
