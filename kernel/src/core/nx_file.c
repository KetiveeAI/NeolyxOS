/*
 * NeolyxOS open files, descriptor tables and file syscalls
 *
 * Spec: docs/kernel/proposals/beta-process-abi.md, sections 3, 4 and 6
 * (approved by the owner 2026-11-01).
 *
 * Moved here from syscall.c: open (20), close (21), read (22), write (23),
 * seek (24), stat (25), readdir (30), mkdir (31), rmdir (32), chdir (33),
 * getcwd (34), rename (35). New: dup (14), dup2 (15), fd.ctl (18),
 * fstat (26), unlink (27).
 *
 * Rules that hold for every handler here:
 *   - results are -errno only, never raw VFS codes (nx_errno_from_vfs);
 *   - paths may be relative and are resolved against the caller's own
 *     working directory (nx_path_from_user);
 *   - open/dup allocate fds from NX_FD_FIRST_AUTO (3) up, so 0-2 are only
 *     ever set on purpose (dup2, spawn_ex);
 *   - every pointer is validated before use.
 *
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#include "core/nx_fdtab.h"
#include "core/nx_file.h"
#include "core/process.h"
#include "core/syscall.h"
#include "fs/vfs.h"
#include "mm/kheap.h"

extern void serial_puts(const char *s);
extern void serial_putc(char c);

/* Open files alive in the system (sys.stats) */
static uint32_t g_open_files;

/* ============ Small helpers ============ */

static void log_dec(uint64_t v) {
    char buf[21];
    int i = 20;
    buf[i] = '\0';
    if (v == 0) buf[--i] = '0';
    while (v > 0 && i > 0) {
        buf[--i] = (char)('0' + (v % 10));
        v /= 10;
    }
    serial_puts(&buf[i]);
}

static void log_pid_msg(const process_t *proc, const char *msg) {
    serial_puts("[FD] PID ");
    log_dec(proc ? proc->pid : 0);
    serial_puts(msg);
}

static uint32_t str_len(const char *s, uint32_t max) {
    uint32_t n = 0;
    while (n < max && s[n]) n++;
    return n;
}

int nx_errno_from_vfs(int64_t rc) {
    switch (rc) {
    case VFS_OK:             return 0;
    case VFS_ERR_NOT_FOUND:  return -ENOENT;
    case VFS_ERR_PERM:       return -EACCES;
    case VFS_ERR_IO:         return -EIO;
    case VFS_ERR_NO_MEM:     return -ENOMEM;
    case VFS_ERR_EXIST:      return -EEXIST;
    case VFS_ERR_NOT_DIR:    return -ENOTDIR;
    case VFS_ERR_IS_DIR:     return -EISDIR;
    case VFS_ERR_NOT_EMPTY:  return -ENOTEMPTY;
    case VFS_ERR_BUSY:       return -EBUSY;
    case VFS_ERR_INVALID:    return -EINVAL;
    case VFS_ERR_NO_SPACE:   return -ENOSPC;
    case VFS_ERR_READ_ONLY:  return -EROFS;
    case VFS_ERR_NFILE:      return -ENFILE;
    default:                 return rc < 0 ? -EIO : 0;
    }
}

int nx_path_from_user(process_t *proc, const char *user_path, char *out) {
    char raw[NX_PATH_MAX];
    int n = syscall_strncpy_from_user(raw, user_path, sizeof(raw));
    if (n < 0) return n;                    /* -EFAULT or -ENAMETOOLONG */
    const char *cwd = (proc && proc->cwd[0] == '/') ? proc->cwd : "/";
    return nx_path_normalize(cwd, raw, out, NX_PATH_MAX);
}

/* ============ Open files ============ */

nx_file_t *nx_file_from_vfs(void *vf, uint32_t type, uint32_t status) {
    if (!vf) return NULL;
    nx_file_t *f = (nx_file_t *)kzalloc(sizeof(nx_file_t));
    if (!f) {
        serial_puts("[FD] ERROR: out of memory for an open file\n");
        vfs_close((vfs_file_t *)vf);
        return NULL;
    }
    f->type = type;
    f->refs = 1;
    f->status = status;
    f->vf = vf;
    g_open_files++;
    return f;
}

void nx_file_ref(nx_file_t *f) {
    if (f) f->refs++;
}

void nx_file_unref(nx_file_t *f) {
    if (!f) return;
    if (f->refs == 0) {
        serial_puts("[FD] BUG: open file released twice\n");
        return;
    }
    if (--f->refs > 0) return;

    if ((f->type == NX_FD_TYPE_FILE || f->type == NX_FD_TYPE_DIR) && f->vf) {
        if (vfs_close((vfs_file_t *)f->vf) != VFS_OK) {
            serial_puts("[FD] WARNING: VFS close failed\n");
        }
    }
    f->vf = NULL;
    if (g_open_files) g_open_files--;
    kfree(f);
}

uint32_t nx_file_open_count(void) {
    return g_open_files;
}

/* ============ Descriptor tables ============ */

nx_fdtab_t *nx_fd_table(process_t *proc, int create) {
    if (!proc) return NULL;
    if (!proc->fdtab && create) {
        proc->fdtab = (nx_fdtab_t *)kzalloc(sizeof(nx_fdtab_t));
        if (!proc->fdtab) log_pid_msg(proc, ": out of memory for the descriptor table\n");
    }
    return proc->fdtab;
}

static int32_t lowest_free(const nx_fdtab_t *t, int32_t min_fd) {
    if (min_fd < 0) min_fd = 0;
    for (int32_t fd = min_fd; fd < NX_FD_MAX; fd++) {
        if (!t->e[fd].file) return fd;
    }
    return -1;
}

int32_t nx_fd_install(process_t *proc, nx_file_t *f, uint32_t fd_flags, int32_t min_fd) {
    if (!proc || !f) return -EINVAL;
    nx_fdtab_t *t = nx_fd_table(proc, 1);
    if (!t) return -ENOMEM;
    int32_t fd = lowest_free(t, min_fd);
    if (fd < 0) {
        log_pid_msg(proc, ": descriptor table full\n");
        return -EMFILE;
    }
    t->e[fd].file = f;
    t->e[fd].flags = fd_flags & NX_FD_CLOEXEC;
    return fd;
}

nx_file_t *nx_fd_get(process_t *proc, int64_t fd) {
    if (!proc || !proc->fdtab || fd < 0 || fd >= NX_FD_MAX) return NULL;
    return proc->fdtab->e[fd].file;
}

int nx_fd_close(process_t *proc, int64_t fd) {
    nx_file_t *f = nx_fd_get(proc, fd);
    if (!f) return -EBADF;
    /* Clear the slot first so the fd can never be closed twice */
    proc->fdtab->e[fd].file = NULL;
    proc->fdtab->e[fd].flags = 0;
    nx_file_unref(f);
    return 0;
}

void nx_fd_close_all(process_t *proc) {
    if (!proc || !proc->fdtab) return;
    uint32_t closed = 0;
    for (int32_t fd = 0; fd < NX_FD_MAX; fd++) {
        if (nx_fd_close(proc, fd) == 0) closed++;
    }
    if (closed) {
        serial_puts("[PROC] Closed ");
        log_dec(closed);
        serial_puts(" open file(s) of PID ");
        log_dec(proc->pid);
        serial_puts("\n");
    }
}

void nx_fd_close_on_exec(process_t *proc) {
    if (!proc || !proc->fdtab) return;
    for (int32_t fd = 0; fd < NX_FD_MAX; fd++) {
        if (proc->fdtab->e[fd].file && (proc->fdtab->e[fd].flags & NX_FD_CLOEXEC)) {
            nx_fd_close(proc, fd);
        }
    }
}

void nx_fd_free_table(process_t *proc) {
    if (!proc || !proc->fdtab) return;
    nx_fd_close_all(proc);              /* no-op unless a path skipped it */
    kfree(proc->fdtab);
    proc->fdtab = NULL;
}

/* ============ Syscalls: descriptors ============ */

static vfs_file_t *file_vf(nx_file_t *f) {
    if (!f || (f->type != NX_FD_TYPE_FILE && f->type != NX_FD_TYPE_DIR)) return NULL;
    return (vfs_file_t *)f->vf;
}

/*
 * int open(const char *path, uint32_t flags, uint32_t mode)
 * Returns the lowest free fd from 3 up, or -errno.
 */
int64_t sys_open_impl(uint64_t path_ptr, uint64_t flags, uint64_t mode, uint64_t a4, uint64_t a5) {
    (void)mode; (void)a4; (void)a5;
    process_t *proc = process_current();
    if (!proc) {
        serial_puts("[FD] ERROR: open outside a process\n");
        return -EPERM;
    }
    if (flags & ~(uint64_t)NX_O_VALID) return -EINVAL;
    /* Access mode 0 has always opened for reading (nximage passes flags 0);
     * keep that, so the status below always holds a real access mode. */
    if ((flags & NX_O_ACCMODE) == 0) flags |= NX_O_RDONLY;

    char path[NX_PATH_MAX];
    int rc = nx_path_from_user(proc, (const char *)path_ptr, path);
    if (rc < 0) return rc;

    /* Check for a free slot first so a full table never creates a file */
    nx_fdtab_t *t = nx_fd_table(proc, 1);
    if (!t) return -ENOMEM;
    if (lowest_free(t, NX_FD_FIRST_AUTO) < 0) {
        log_pid_msg(proc, ": descriptor table full\n");
        return -EMFILE;
    }

    uint32_t vflags = (uint32_t)flags & (NX_O_ACCMODE | NX_O_CREATE | NX_O_TRUNC |
                                         NX_O_APPEND | NX_O_EXCL);
    int err = VFS_OK;
    vfs_file_t *vf = vfs_open_ex(path, vflags, &err);
    if (!vf) return nx_errno_from_vfs(err ? err : VFS_ERR_NOT_FOUND);

    uint32_t type = (vf->node && vf->node->type == VFS_DIRECTORY) ? NX_FD_TYPE_DIR : NX_FD_TYPE_FILE;
    nx_file_t *f = nx_file_from_vfs(vf, type,
                                    (uint32_t)flags & (NX_O_ACCMODE | NX_O_APPEND | NX_O_NONBLOCK));
    if (!f) return -ENOMEM;

    int32_t fd = nx_fd_install(proc, f, (flags & NX_O_CLOEXEC) ? NX_FD_CLOEXEC : 0,
                               NX_FD_FIRST_AUTO);
    if (fd < 0) nx_file_unref(f);
    return fd;
}

int64_t sys_close_impl(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    return nx_fd_close(process_current(), (int64_t)fd);
}

int64_t sys_read_impl(uint64_t fd, uint64_t buf_ptr, uint64_t count, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    nx_file_t *f = nx_fd_get(process_current(), (int64_t)fd);
    if (!f) return -EBADF;
    if (f->type == NX_FD_TYPE_DIR) return -EISDIR;
    vfs_file_t *vf = file_vf(f);
    if (!vf) return -EBADF;
    if (!(f->status & NX_O_RDONLY)) return -EBADF;      /* not open for reading */
    if (count == 0) return 0;
    if (count > 0x7FFFFFFFULL) count = 0x7FFFFFFFULL;
    if (syscall_validate_ptr((void *)buf_ptr, count, 1) != 0) return -EFAULT;

    int64_t r = vfs_read(vf, (void *)buf_ptr, count);
    return r < 0 ? nx_errno_from_vfs(r) : r;
}

int64_t sys_write_impl(uint64_t fd, uint64_t buf_ptr, uint64_t count, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    nx_file_t *f = nx_fd_get(process_current(), (int64_t)fd);
    if (!f) return -EBADF;
    if (f->type == NX_FD_TYPE_DIR) return -EISDIR;
    vfs_file_t *vf = file_vf(f);
    if (!vf) return -EBADF;
    if (!(f->status & NX_O_WRONLY)) return -EBADF;      /* not open for writing */
    if (count == 0) return 0;
    if (count > 0x7FFFFFFFULL) count = 0x7FFFFFFFULL;
    if (syscall_validate_ptr((void *)buf_ptr, count, 0) != 0) return -EFAULT;

    if ((f->status & NX_O_APPEND) && vf->node) vf->offset = vf->node->size;
    int64_t w = vfs_write(vf, (const void *)buf_ptr, count);
    return w < 0 ? nx_errno_from_vfs(w) : w;
}

int64_t sys_seek_impl(uint64_t fd, uint64_t offset, uint64_t whence, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    nx_file_t *f = nx_fd_get(process_current(), (int64_t)fd);
    if (!f) return -EBADF;
    vfs_file_t *vf = file_vf(f);
    if (!vf) return -ESPIPE;
    if (whence > VFS_SEEK_END) return -EINVAL;
    int64_t r = vfs_seek(vf, (int64_t)offset, (int)whence);
    return r < 0 ? nx_errno_from_vfs(r) : r;
}

/* int32_t dup(int32_t fd) */
int64_t sys_dup_impl(uint64_t fd, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    nx_file_t *f = nx_fd_get(proc, (int64_t)fd);
    if (!f) return -EBADF;
    nx_file_ref(f);
    int32_t nfd = nx_fd_install(proc, f, 0, NX_FD_FIRST_AUTO);
    if (nfd < 0) nx_file_unref(f);
    return nfd;
}

/* int32_t dup2(int32_t fd, int32_t new_fd) */
int64_t sys_dup2_impl(uint64_t fd, uint64_t new_fd, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    nx_file_t *f = nx_fd_get(proc, (int64_t)fd);
    if (!f) return -EBADF;
    if (new_fd >= NX_FD_MAX) return -EBADF;
    if (fd == new_fd) return (int64_t)new_fd;

    nx_fdtab_t *t = proc->fdtab;            /* exists: fd is open */
    nx_file_ref(f);
    nx_file_t *old = t->e[new_fd].file;
    t->e[new_fd].file = f;
    t->e[new_fd].flags = 0;
    if (old) nx_file_unref(old);
    return (int64_t)new_fd;
}

/* int64_t fd_ctl(int32_t fd, uint32_t cmd, uint64_t arg) */
int64_t sys_fd_ctl_impl(uint64_t fd, uint64_t cmd, uint64_t arg, uint64_t a4, uint64_t a5) {
    (void)a4; (void)a5;
    process_t *proc = process_current();
    nx_file_t *f = nx_fd_get(proc, (int64_t)fd);
    if (!f) return -EBADF;
    nx_fd_entry_t *e = &proc->fdtab->e[fd];

    switch (cmd) {
    case NX_FD_GET_FLAGS:
        return e->flags;
    case NX_FD_SET_FLAGS:
        if (arg & ~(uint64_t)NX_FD_CLOEXEC) return -EINVAL;
        e->flags = (uint32_t)arg;
        return 0;
    case NX_FD_GET_STATUS:
        return f->status;
    case NX_FD_SET_STATUS: {
        /* The access mode cannot change; libc passes it back unchanged */
        uint64_t change = arg & ~(uint64_t)NX_O_ACCMODE;
        if (change & ~(uint64_t)(NX_O_APPEND | NX_O_NONBLOCK)) return -EINVAL;
        if ((arg & NX_O_ACCMODE) && (arg & NX_O_ACCMODE) != (f->status & NX_O_ACCMODE)) {
            return -EINVAL;
        }
        f->status = (f->status & NX_O_ACCMODE) | (uint32_t)change;
        return 0;
    }
    case NX_FD_GET_TYPE:
        return f->type;
    default:
        return -EINVAL;
    }
}

/* ============ Syscalls: metadata ============ */

static void fill_stat(const vfs_node_t *node, stat_t *st) {
    st->st_ino = node->inode;
    st->st_mode = (node->permissions & 07777) |
                  (node->type == VFS_DIRECTORY ? NX_S_IFDIR : NX_S_IFREG);
    st->st_nlink = 1;
    st->st_uid = node->uid;
    st->st_gid = node->gid;
    st->st_size = node->size;
    st->st_atime = node->accessed;
    st->st_mtime = node->modified;
    st->st_ctime = node->created;
}

int64_t sys_stat_impl(uint64_t path_ptr, uint64_t stat_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    char path[NX_PATH_MAX];
    int rc = nx_path_from_user(process_current(), (const char *)path_ptr, path);
    if (rc < 0) return rc;

    vfs_node_t node;
    int vr = vfs_stat(path, &node);
    if (vr != VFS_OK) return nx_errno_from_vfs(vr);

    stat_t st;
    fill_stat(&node, &st);
    if (syscall_copy_to_user((void *)stat_ptr, &st, sizeof(st)) != 0) return -EFAULT;
    return 0;
}

/* int fstat(int32_t fd, stat_t *st) */
int64_t sys_fstat_impl(uint64_t fd, uint64_t stat_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    nx_file_t *f = nx_fd_get(process_current(), (int64_t)fd);
    if (!f) return -EBADF;

    stat_t st;
    vfs_file_t *vf = file_vf(f);
    if (vf && vf->node) {
        fill_stat(vf->node, &st);
    } else {
        /* Pipes, PTYs, log and null (later PRs): type bits only */
        st.st_ino = 0;
        st.st_mode = (f->type == NX_FD_TYPE_PIPE_READ || f->type == NX_FD_TYPE_PIPE_WRITE)
                     ? NX_S_IFIFO : NX_S_IFCHR;
        st.st_nlink = 1;
        st.st_uid = 0;
        st.st_gid = 0;
        st.st_size = 0;
        st.st_atime = 0;
        st.st_mtime = 0;
        st.st_ctime = 0;
    }
    if (syscall_copy_to_user((void *)stat_ptr, &st, sizeof(st)) != 0) return -EFAULT;
    return 0;
}

int64_t sys_readdir_impl(uint64_t path_ptr, uint64_t entries_ptr, uint64_t count, uint64_t read_ptr, uint64_t a5) {
    (void)a5;
    char path[NX_PATH_MAX];
    int rc = nx_path_from_user(process_current(), (const char *)path_ptr, path);
    if (rc < 0) return rc;
    if (count == 0 || count > 4096) return -EINVAL;
    if (syscall_validate_ptr((void *)entries_ptr, count * sizeof(vfs_dirent_t), 1) != 0) {
        return -EFAULT;
    }
    if (read_ptr && syscall_validate_ptr((void *)read_ptr, sizeof(uint32_t), 1) != 0) {
        return -EFAULT;
    }

    uint32_t nread = 0;
    int vr = vfs_readdir(path, (vfs_dirent_t *)entries_ptr, (uint32_t)count, &nread);
    if (read_ptr && syscall_copy_to_user((void *)read_ptr, &nread, sizeof(nread)) != 0) {
        return -EFAULT;
    }
    return nx_errno_from_vfs(vr);
}

/* ============ Syscalls: names ============ */

int64_t sys_mkdir_impl(uint64_t path_ptr, uint64_t mode, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    char path[NX_PATH_MAX];
    int rc = nx_path_from_user(process_current(), (const char *)path_ptr, path);
    if (rc < 0) return rc;
    return nx_errno_from_vfs(vfs_create(path, VFS_DIRECTORY, (uint32_t)mode & 07777));
}

/* Remove path after checking it is (want_dir) or is not a directory */
static int64_t remove_checked(uint64_t path_ptr, int want_dir) {
    char path[NX_PATH_MAX];
    int rc = nx_path_from_user(process_current(), (const char *)path_ptr, path);
    if (rc < 0) return rc;

    vfs_node_t node;
    int vr = vfs_stat(path, &node);
    if (vr != VFS_OK) return nx_errno_from_vfs(vr);
    int is_dir = node.type == VFS_DIRECTORY;
    if (want_dir && !is_dir) return -ENOTDIR;
    if (!want_dir && is_dir) return -EISDIR;
    return nx_errno_from_vfs(vfs_unlink(path));
}

/* int rmdir(const char *path) - directories only */
int64_t sys_rmdir_impl(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    return remove_checked(path_ptr, 1);
}

/* int unlink(const char *path) - files only; -EBUSY while open anywhere */
int64_t sys_unlink_impl(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    return remove_checked(path_ptr, 0);
}

/*
 * Rename or move a file or directory on one volume. Both paths are resolved
 * against the caller's cwd and checked by SIP in vfs_rename.
 */
int64_t sys_rename_impl(uint64_t old_ptr, uint64_t new_ptr, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    char old_path[NX_PATH_MAX];
    char new_path[NX_PATH_MAX];
    int rc = nx_path_from_user(proc, (const char *)old_ptr, old_path);
    if (rc < 0) return rc;
    rc = nx_path_from_user(proc, (const char *)new_ptr, new_path);
    if (rc < 0) return rc;
    return nx_errno_from_vfs(vfs_rename(old_path, new_path));
}

/* Working directory: per process (process_t.cwd) */
int64_t sys_chdir_impl(uint64_t path_ptr, uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a2; (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    if (!proc) return -EPERM;
    char path[NX_PATH_MAX];
    int rc = nx_path_from_user(proc, (const char *)path_ptr, path);
    if (rc < 0) return rc;

    vfs_node_t node;
    int vr = vfs_stat(path, &node);
    if (vr != VFS_OK) return nx_errno_from_vfs(vr);
    if (node.type != VFS_DIRECTORY) return -ENOTDIR;

    uint32_t len = str_len(path, sizeof(proc->cwd) - 1);
    for (uint32_t i = 0; i < len; i++) proc->cwd[i] = path[i];
    proc->cwd[len] = '\0';
    return 0;
}

int64_t sys_getcwd_impl(uint64_t buf_ptr, uint64_t size, uint64_t a3, uint64_t a4, uint64_t a5) {
    (void)a3; (void)a4; (void)a5;
    process_t *proc = process_current();
    if (!proc) return -EPERM;
    const char *cwd = proc->cwd[0] == '/' ? proc->cwd : "/";
    uint32_t len = str_len(cwd, sizeof(proc->cwd) - 1);
    if ((uint64_t)len + 1 > size) return -ERANGE;
    if (syscall_copy_to_user((void *)buf_ptr, cwd, len + 1) != 0) return -EFAULT;
    return len;
}
