/*
 * NeolyxOS open files and per-process descriptor tables (kernel-internal)
 *
 * An fd points at a reference-counted open file (nx_file_t). dup, dup2 and
 * (later) spawn_ex share one open file, so its offset and status flags are
 * shared; the close-on-exec flag belongs to the fd. Each process owns a
 * table of NX_FD_MAX entries, allocated on first use and freed with the
 * process. Spec: docs/kernel/proposals/beta-process-abi.md, section 4.1.
 *
 * The kernel is single-CPU and not preemptible in syscalls; callers that
 * can race an interrupt-time user (terminate() from a timer tick) hold
 * interrupts off.
 *
 * Not part of the user ABI (see core/nx_file.h for that).
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_NX_FDTAB_H
#define NEOLYX_NX_FDTAB_H

#include <stdint.h>
#include "core/nx_file.h"

struct process;

typedef struct nx_file {
    uint32_t type;          /* NX_FD_TYPE_* */
    uint32_t refs;          /* fds (in any process) that point here */
    uint32_t status;        /* access mode | NX_O_APPEND | NX_O_NONBLOCK */
    uint32_t reserved;
    void    *vf;            /* vfs_file_t * for NX_FD_TYPE_FILE / _DIR */
    void    *obj;           /* pipe or PTY end (later PRs) */
} nx_file_t;

typedef struct {
    nx_file_t *file;        /* NULL = free */
    uint32_t   flags;       /* NX_FD_CLOEXEC */
    uint32_t   reserved;
} nx_fd_entry_t;

typedef struct nx_fdtab {
    nx_fd_entry_t e[NX_FD_MAX];
} nx_fdtab_t;

/** The process's table; with create != 0 it is allocated if missing.
 *  NULL when the process has none (or allocation failed). */
nx_fdtab_t *nx_fd_table(struct process *proc, int create);

/** Wrap a VFS open file (takes ownership: closes it on failure).
 *  @return a new open file with one reference, or NULL (vf is closed) */
nx_file_t *nx_file_from_vfs(void *vf, uint32_t type, uint32_t status);

/** Add a reference. */
void nx_file_ref(nx_file_t *f);

/** Drop a reference; the last one closes the underlying object. */
void nx_file_unref(nx_file_t *f);

/**
 * Put f in the lowest free fd >= min_fd. On success the table holds the
 * caller's reference; on failure the caller still owns it.
 * @return fd, or -EMFILE / -ENOMEM
 */
int32_t nx_fd_install(struct process *proc, nx_file_t *f, uint32_t fd_flags, int32_t min_fd);

/** Open file behind fd, or NULL. No reference is taken. */
nx_file_t *nx_fd_get(struct process *proc, int64_t fd);

/** Close one fd. @return 0 or -EBADF */
int nx_fd_close(struct process *proc, int64_t fd);

/** Close every fd (exit, kill, fault). Logs how many were open. */
void nx_fd_close_all(struct process *proc);

/** Close the fds marked NX_FD_CLOEXEC (exec). */
void nx_fd_close_on_exec(struct process *proc);

/** Free the table itself (after nx_fd_close_all). */
void nx_fd_free_table(struct process *proc);

/** Open files in the whole system (sys.stats). */
uint32_t nx_file_open_count(void);

/** Map a VFS_ERR_* code (or any other negative) to -errno. */
int nx_errno_from_vfs(int64_t rc);

/**
 * Copy a path from user memory and resolve it against the caller's working
 * directory (section 4.2). out must hold 256 bytes.
 * @return length, or -EFAULT / -ENAMETOOLONG / -ENOENT (empty path)
 */
int nx_path_from_user(struct process *proc, const char *user_path, char *out);

/* ---- Pure helper (core/nx_path.c, also built by host tests) ---- */

#define NX_PATH_MAX 256     /* bytes including the NUL */

/**
 * Join path to cwd (when path is relative) and normalise: "." and empty
 * components dropped, ".." removes the previous component and stays at "/".
 * The result is absolute, has no trailing "/" (except "/" itself).
 * @param cap  size of out (at most NX_PATH_MAX is enough)
 * @return length of out, -ENOENT for an empty path, -ENAMETOOLONG when the
 *         result (plus its NUL) does not fit in cap, -EINVAL for NULL
 */
int nx_path_normalize(const char *cwd, const char *path, char *out, uint32_t cap);

#endif /* NEOLYX_NX_FDTAB_H */
