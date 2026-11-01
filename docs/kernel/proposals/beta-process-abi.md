# Proposal: process, file and terminal syscalls for the beta (S3)

Status: **proposal, not implemented.** No kernel code changes with this
document. It is part of the Beta ABI batch (owner decision D2, 2026-11-06).
Stream: S3 of `/mnt/project-files/beta-plan/parallel-workstreams.md`.
Checked against: `main` at 18512d2 (PR #45).

## 1. Why

nxlibc (S4), the userspace shell and Terminal.app (S5) and the out-of-process
Task Manager (S2) need things the kernel does not offer today:

| Need | Today on `main` |
|------|-----------------|
| Memory for a C allocator beyond `brk` | `mmap`/`munmap` (11, 12) declared in `syscall.h`, not registered: `-ENOSYS` |
| Information about an open file | `fstat` (26) declared, not registered |
| Delete a file | Only `rmdir` (32), which calls `vfs_unlink` and so deletes files too |
| Shorten a file, flush it | No syscall. FAT `truncate` only handles size 0 |
| Redirection and pipelines | No `dup`, `dup2` or `pipe` |
| Start a program with stdin/stdout/stderr and an environment | `spawn` (13) gives the child an empty fd table; `exec` (8) ignores `envp` |
| Wait for any child without blocking | `wait` (9) blocks; returns `-1` instead of an errno |
| More than 16 open files | `PROCESS_FD_MAX` 16; the VFS has one table of 64 open files for the whole system |
| A terminal | PTY numbers 60-67 declared, not registered. `kernel/src/io/pty.c` is an unused stub (not in the Makefile, fake `getpid`) |
| `cd` in a shell | `chdir`/`getcwd` use one global `cwd` in `syscall.c`, shared by every process; relative paths are not resolved anywhere |
| Useful error codes | `open` returns `-ENOENT` for every failure; `mkdir`, `rmdir` and `rename` return raw VFS codes (`VFS_ERR_NOT_FOUND` is `-1`, which reads as `-EPERM`) |
| Process list, kill, priority, OS name | None (see `docs/desktop/proposals/taskmgr.md`) |

This document fixes every number, struct and rule for those calls so S4, S5
and S2 can code against them now and test the ABI flags at run time.

## 2. Summary

| No. | Name | Purpose | Flag |
|-----|------|---------|------|
| 11 | `mmap` | Anonymous, private, zero-filled memory | FILEIO |
| 12 | `munmap` | Release it | FILEIO |
| 14 | `dup` | Duplicate an fd to the lowest free number | FILEIO |
| 15 | `dup2` | Duplicate an fd to a chosen number | FILEIO |
| 16 | `pipe` | Create a pipe (read end, write end) | FILEIO |
| 17 | `spawn_ex` | Start a program with an fd map, argv, envp, cwd and job | FILEIO |
| 18 | `fd.ctl` | Close-on-exec, non-blocking, append, fd type | FILEIO |
| 19 | `wait_ex` | Wait for a child, optionally without blocking | FILEIO |
| 26 | `fstat` | Stat an open fd | FILEIO |
| 27 | `unlink` | Delete a file (not a directory) | FILEIO |
| 28 | `ftruncate` | Set a file's length | FILEIO |
| 29 | spare | | |
| 36 | `fsync` | Put a file's data on the device | FILEIO |
| 37 | `statfs` | Volume size, free space, type | FILEIO |
| 38-39 | spare | | |
| 51 | `uname` | OS name, release, build, machine, host name | PROC |
| 52 | `sys.stats` | System counters (taskmgr.md) | PROC |
| 53 | `proc.list` | Process table snapshot (taskmgr.md) | PROC |
| 54 | `proc.info` | One process (taskmgr.md) | PROC |
| 55 | `proc.kill` | End a process or a job (taskmgr.md, extended) | PROC |
| 56 | `proc.setprio` | Normal or low priority (taskmgr.md, changed) | PROC |
| 58-59 | spare | | |
| 60 | `pty.open` | Create a PTY pair as two fds | PTY |
| 61 | `pty.set_size` | Set rows/columns | PTY |
| 62 | `pty.get_size` | Read rows/columns | PTY |
| 63 | `pty.set_mode` | Echo, canonical lines, Ctrl-C, CR/LF handling | PTY |
| 64 | `pty.get_mode` | Read the mode | PTY |
| 65 | `pty.set_fg` | Choose the job Ctrl-C ends | PTY |
| 66-69 | spare | | |
| 90-99 | reserved | Capabilities (`docs/desktop/proposals/sys-capabilities.md`), not in this batch | |

ABI flags (`nx_abi_info_t.flags`), each defined in its own header:

| Flag | Name | Set when |
|------|------|----------|
| 0x0040 | `NX_ABI_FLAG_FILEIO` | Every FILEIO call above and the changes in section 4 are merged |
| 0x0080 | `NX_ABI_FLAG_PTY` | 60-65 merged |
| 0x0100 | `NX_ABI_FLAG_PROC` | 51-56 merged |

Until its flag is set, a call in a group may already exist; clients must test
the flag, never the minor version, and must treat `-ENOSYS` as "not here
yet". `NX_ABI_MINOR` is changed only by S10 (1.5 for this batch).

The declared-but-unregistered meanings in `syscall.h` for 51 (`utsname_t`)
and 60-67 (`pty_read`, `pty_write`, `pty_close`, `pty_ioctl`,
`spawn_shell`, `pty_poll`) are replaced by this document. Nothing in the tree
calls them (checked with grep). PTY ends are ordinary fds, so `read` (22),
`write` (23) and `close` (21) work on them, and `spawn_ex` replaces
`spawn_shell`.

Handlers take at most five arguments (`nx_syscall_desc_t`), so calls with more
inputs take a struct pointer.

## 3. General rules

These hold for every call in this document.

- **Errors** are negative errno values from `syscall.h`. No call returns a raw
  VFS code. Once `NX_ABI_FLAG_FILEIO` is set this holds for every file,
  directory and path syscall, old and new (20-37), so nxlibc can stop
  translating codes by testing that flag. This document adds no error
  numbers; the networking stream's new socket errors (98, 101, 104, 106,
  107, 111, 113) are not reused here.
- **User pointers** are checked with `syscall_validate_ptr` and copied with
  `syscall_copy_from_user` / `syscall_copy_to_user` before use. A bad pointer
  is `-EFAULT`, and the call has no effect.
- **Versioned structs** start with `uint32_t size`. The caller sets `size` to
  the size it was built against. The kernel rejects sizes below the v1 size
  (`-EINVAL`), reads or writes `min(size, sizeof(v1))`, and sets `size` on
  output to the bytes it filled. New fields are only ever appended.
- **Reserved fields and unknown flag bits** must be 0, otherwise `-EINVAL`.
- **Paths** are at most 255 bytes plus the NUL (`-ENAMETOOLONG`). A path that
  does not start with `/` is resolved against the caller's working directory
  (section 4.2). SIP (`kernel/src/fs/sip.c`) checks the resolved path.
- **Limits are errors, never silent clamps.**
- **Logging.** Calls marked *audited* use `NX_FLAG_AUDIT` in
  `syscall_table.def` and log one outcome line. Every refusal for a privilege
  reason is logged, rate-limited to one line per process per second plus a
  count of suppressed lines.
- **Cleanup.** Everything a process owns from this document (fds, pipes,
  PTYs, mappings, job membership) is released by `terminate()`, so exit,
  kill, Ctrl-C and CPU faults all clean up the same way.

## 4. Changes to existing behaviour

These change calls that exist today. Each was checked against the callers in
the tree; none depends on the old behaviour.

### 4.1 64 descriptors per process, shared open files

- `PROCESS_FD_MAX` goes from 16 to 64 (`NX_FD_MAX`). The PCB's
  `void *fd_table[16]` is replaced by a pointer to a table allocated with the
  process (64 entries of {file, fd flags}). `process_t` is kernel-internal;
  no assembly uses its offsets.
- An fd points at a reference-counted **open file** (`nx_file_t`, internal)
  that holds the type (regular file, directory, pipe end, PTY master, PTY
  slave, log, null), the offset and the status flags. `dup`, `dup2` and
  `spawn_ex` share one open file, so the offset is shared, as a shell needs
  for `cmd >file 2>&1`.
- The VFS open-file table grows from 64 (`VFS_MAX_OPEN_FILES`) to 1024 for
  the whole system. When it is full, `open` returns `-ENFILE`.
- **fds 0, 1 and 2 are only ever set on purpose.** `open`, `dup`, `pipe`
  and `pty.open` return the lowest free fd **from 3 up**; only `dup2` and the
  `spawn_ex` fd map can place a file at 0-2. So when a parent gives a child no
  standard input or output, a later `open` can never become stdout and
  `printf` can never land in a user file; writes to an empty fd 1 fail with
  `-EBADF`. This changes today's behaviour (a new process's first `open`
  returns 0); `fdtest` is updated in the same PR to expect 3.
- A process created by `spawn` (13) still starts with an empty table.

### 4.2 Working directory per process, relative paths

- `chdir` (33) and `getcwd` (34) use the caller's own `process_t.cwd`, which
  already exists and is set to `/` at spawn. Today they change one global
  string for every process.
- Every path argument (`open`, `stat`, `readdir`, `mkdir`, `rmdir`,
  `rename`, `chdir`, `spawn`, `exec`, and the new calls) may be relative. The
  kernel joins it to the cwd and normalises `.`, `..` and repeated `/` before
  SIP and the VFS see it. `..` at `/` stays at `/`. A result longer than 255
  bytes is `-ENAMETOOLONG`.
- A child started by `spawn` (13) gets the caller's cwd (today: `/`).
  `spawn_ex` can set it. `exec` keeps it.

### 4.3 Errors from the existing file calls

| Call | Today | Proposed |
|------|-------|----------|
| `open` (20) | `-ENOENT` for every failure | `-ENOENT`, `-EACCES` (SIP), `-EISDIR` (write to a directory), `-EEXIST` (`NX_O_EXCL`), `-EMFILE`, `-ENFILE`, `-EIO`, `-EINVAL` (unknown flag bits) |
| `mkdir` (31), `rename` (35) | raw `VFS_ERR_*` | mapped: `NOT_FOUND`→`ENOENT`, `PERM`→`EACCES`, `IO`→`EIO`, `NO_MEM`→`ENOMEM`, `EXIST`→`EEXIST`, `NOT_DIR`→`ENOTDIR`, `IS_DIR`→`EISDIR`, `NOT_EMPTY`→`ENOTEMPTY`, `BUSY`→`EBUSY`, `INVALID`→`EINVAL`, `NO_SPACE`→`ENOSPC`, `READ_ONLY`→`EROFS` |
| `rmdir` (32) | deletes files too; raw codes | `-ENOTDIR` for a file (use `unlink`); mapped codes |
| `read`/`write`/`seek` (22-24) | raw codes from `vfs_*` | mapped codes; `seek` on a pipe or PTY is `-ESPIPE` |
| `stat` (25), `readdir` (30), `chdir` (33), `getcwd` (34) | `stat`/`chdir`: `-ENOENT` for every failure; `readdir`: raw codes | mapped codes (`-ENOENT`, `-ENOTDIR`, `-EACCES`, `-EIO`, `-ERANGE` for `getcwd`) |
| `wait` (9) | `-1` when there is no child | `-ECHILD` |

New open flags (existing values unchanged):

```c
#define NX_O_RDONLY    0x0001   /* existing */
#define NX_O_WRONLY    0x0002   /* existing */
#define NX_O_RDWR      0x0003   /* existing */
#define NX_O_CREATE    0x0100   /* existing */
#define NX_O_TRUNC     0x0200   /* existing */
#define NX_O_APPEND    0x0400   /* existing */
#define NX_O_EXCL      0x0800   /* new: with CREATE, -EEXIST if the path exists */
#define NX_O_CLOEXEC   0x1000   /* new: the fd is closed by exec */
#define NX_O_NONBLOCK  0x2000   /* new: pipes and PTYs return -EAGAIN instead of blocking */
```

### 4.4 `exec` (8) and the program entry

- `exec` honours `envp` and closes fds marked close-on-exec.
- `exec`, `spawn` and `spawn_ex` share new argument limits (today 16
  arguments of 255 bytes): at most 256 `argv` entries and 256 `envp`
  entries, each string at most 4096 bytes, all strings together at most
  64 KiB including NULs (`-E2BIG`). The 512 KiB user stack holds this.
- Program entry: `RDI` = argc, `RSI` = argv (unchanged), and new `RDX` =
  envp. The stack layout above `RSP` is unchanged: fake return address,
  argc, `argv[0..argc-1]`, NULL, then the envp pointers inline, so envp is
  always `&argv[argc + 1]`, followed by a NULL; the strings sit above. Today
  that envp list is just the NULL. `RDX` holds the same address as
  `&argv[argc + 1]`. Today's crt0 ignores `RDX`, so old programs keep
  working.
- The kernel does not interpret the environment.

### 4.5 `obj.wait` (147) can wait on fds

`nx_wait_item_t.reserved` (must be 0 today) becomes `kind`:

```c
typedef struct {
    nx_handle_t handle;     /* kind 0: a handle. kind 1: an fd number */
    uint32_t    signals;
    uint32_t    observed;
    uint32_t    kind;       /* was reserved: NX_WAIT_KIND_HANDLE 0, NX_WAIT_KIND_FD 1 */
} nx_wait_item_t;           /* still 16 bytes */
```

Signals on fd items:

| Signal | Pipe read end, PTY master or slave | Pipe write end | Regular file, log, null |
|--------|-------------------------------------|----------------|-------------------------|
| `NX_SIGNAL_READABLE` (0x1) | data buffered, or end of file | never | always |
| `NX_SIGNAL_WRITABLE` (0x2) | PTY: room for 1 byte or more | room, or no reader left | always |
| `NX_SIGNAL_PEER_CLOSED` (0x4) | the other side is fully closed | no reader left | never |
| `NX_SIGNAL_RESIZED` (0x8, new) | PTY slave: size changed since the last `pty.get_size` on it | never | never |

This lets Terminal.app wait on its window channel and its PTY master in one
call, and lets the shell wait on several pipes, without polling. Old callers
pass `kind == 0` and see no change. Bad fd in an item: `-EBADF`.

### 4.6 Small fixes with no new number

- `info` (50): `num_processes` is the real count of processes that are not
  `DEAD` (today hard-coded 1); `uptime_ms` is scaled by `PIT_TICK_RATE`.

## 5. Memory: `mmap` (11), `munmap` (12)

Header: `kernel/include/core/nx_file.h`.

```c
#define NX_PROT_READ   0x1
#define NX_PROT_WRITE  0x2      /* a writable page is also readable on x86 */
#define NX_MAP_ANON    0x1      /* required: anonymous private memory */

void *mmap(uint64_t hint, uint64_t len, uint32_t prot, uint32_t flags);
int   munmap(uint64_t addr, uint64_t len);
```

**`mmap`** maps `len` bytes rounded up to whole pages of zero-filled private
memory and returns its address (page aligned), or a negative errno. Return
values from -4095 to -1 are errors; no mapping is ever placed there.

- `hint` must be 0 (the kernel chooses; no fixed mappings in the beta).
- `flags` must be exactly `NX_MAP_ANON`. File-backed and shared mappings are
  out of scope (shared memory is `shm.*`).
- `prot` is `NX_PROT_READ` or `NX_PROT_READ | NX_PROT_WRITE`. 0 and any
  execute request are `-EINVAL`. Note: the kernel does not enable the NX page
  bit today (paging is S9's), so mapped pages are not hardware-protected
  against execution; refusing `PROT_EXEC` keeps the promise for later.
- Errors: `-EINVAL` (len 0, bad flags/prot/hint), `-ENOMEM` (no free range
  large enough, or the caller's memory class limit or the PMM reserve
  refuses the pages; logged like `brk`).

**Where.** No memory-layout change: mappings live in the existing private
user window (16 MiB to 256 MiB), in the **mmap area** between the heap
reserve and the stack:

```
USER_WINDOW_BASE 0x0100_0000  image (code, data, BSS)
                              heap: brk grows up to heap_max (image end + 64 MiB)
heap_max + 1 guard page       mmap area start
                              ... mappings, first fit, lowest address ...
stack bottom - 1 guard page   mmap area end
USER_STACK_TOP - 512 KiB      stack (512 KiB) up to USER_STACK_TOP
```

For an image under 40 MiB the area is at least 128 MiB. The kernel keeps a
per-process page bitmap of the area (allocated on the first `mmap`, about
6 KiB), so placement and partial unmaps need no region list. A larger area
needs the direct-map work in decision D7 (S9) and can come in the second
batch without changing these calls.

**`munmap`** removes every mapped page in `[addr, addr + len)` (len rounded
up) and returns the frames to the PMM. `addr` must be page aligned and the
whole range inside the mmap area (`-EINVAL`; this protects the image, heap
and stack). Pages in the range that are not mapped are skipped. Partial
unmaps are allowed, so an allocator can trim. Returns 0.

`exec` and `terminate()` drop every mapping with the address space.
`sys.stats` reports `mmap_bytes` system-wide, and `proc.info` includes them
in `resident_bytes`.

## 6. Descriptors and files

Header: `kernel/include/core/nx_file.h`.

### 6.1 `dup` (14), `dup2` (15)

```c
int32_t dup(int32_t fd);
int32_t dup2(int32_t fd, int32_t new_fd);
```

- `dup` returns the lowest free fd from 3 up that shares `fd`'s open file. `-EBADF`,
  `-EMFILE`.
- `dup2` makes `new_fd` (0-63) share `fd`'s open file and returns `new_fd`.
  An open `new_fd` is closed first; if `fd == new_fd` it returns `new_fd`
  and changes nothing. `-EBADF` for a bad `fd` or `new_fd` out of range.
- The new fd never has close-on-exec set.

### 6.2 `pipe` (16)

```c
#define NX_PIPE_BUF  4096        /* buffer size; writes up to this size are atomic */

int pipe(int32_t fds[2], uint32_t flags);   /* flags: NX_O_CLOEXEC | NX_O_NONBLOCK */
```

`fds[0]` is the read end and `fds[1]` the write end. Returns 0.

- `read` blocks until at least one byte is buffered, then returns what is
  there (up to `count`). When every write end is closed and the buffer is
  empty it returns 0 (end of file).
- `write` blocks until all bytes are written. A write of up to
  `NX_PIPE_BUF` bytes is never interleaved with another writer. When no read
  end is left it returns `-EPIPE` (no signals exist) and writes nothing.
- With `NX_O_NONBLOCK`, `read` returns `-EAGAIN` instead of blocking and
  `write` returns the bytes that fit, or `-EAGAIN` if none fit.
- A process blocked in a pipe that is killed or interrupted by Ctrl-C ends
  normally; the other end sees the close.
- Limits: 256 pipes in the system (`-ENFILE`); the fd limit bounds them per
  process. Each pipe uses one 4 KiB page from the PMM, freed when both ends
  are closed. `-ENOMEM` when no page is available.

### 6.3 `fd.ctl` (18)

```c
int64_t fd_ctl(int32_t fd, uint32_t cmd, uint64_t arg);

#define NX_FD_GET_FLAGS   1   /* returns fd flags */
#define NX_FD_SET_FLAGS   2   /* arg: fd flags */
#define NX_FD_GET_STATUS  3   /* returns access mode | NX_O_APPEND | NX_O_NONBLOCK */
#define NX_FD_SET_STATUS  4   /* arg: only NX_O_APPEND and NX_O_NONBLOCK may change */
#define NX_FD_GET_TYPE    5   /* returns NX_FD_TYPE_* */

#define NX_FD_CLOEXEC     0x1 /* fd flag */

#define NX_FD_TYPE_FILE        1
#define NX_FD_TYPE_DIR         2
#define NX_FD_TYPE_PIPE_READ   3
#define NX_FD_TYPE_PIPE_WRITE  4
#define NX_FD_TYPE_PTY_MASTER  5
#define NX_FD_TYPE_PTY_SLAVE   6
#define NX_FD_TYPE_LOG         7
#define NX_FD_TYPE_NULL        8
```

Status flags belong to the open file and so are shared by its duplicates;
fd flags belong to the one fd. `isatty()` in nxlibc is
`fd_ctl(fd, NX_FD_GET_TYPE, 0) == NX_FD_TYPE_PTY_SLAVE`. Errors: `-EBADF`,
`-EINVAL` (unknown command or bits).

### 6.4 `fstat` (26)

```c
int fstat(int32_t fd, stat_t *st);   /* stat_t as today in syscall.h, 56 bytes */
```

Same `stat_t` as `stat` (25). `st_mode` type bits: `0040000` directory,
`0100000` regular file (as today), new `0010000` pipe, `0020000` character
device (PTY ends, log, null). For pipes and PTYs `st_size`, `st_ino` and the
times are 0. `-EBADF`, `-EFAULT`.

### 6.5 `unlink` (27)

```c
int unlink(const char *path);
```

Deletes a file. `-EISDIR` for a directory (use `rmdir`), `-ENOENT`,
`-EACCES` (SIP), `-EBUSY` when the file is open in any process (the FAT
driver creates a node per lookup and cannot keep a deleted file alive until
its last close; this differs from POSIX on purpose and is documented for
nxlibc). Logged like `rmdir` today.

### 6.6 `ftruncate` (28)

```c
int ftruncate(int32_t fd, uint64_t length);
```

Sets the length of a regular file open for writing. Growing fills with
zeros; shrinking frees the clusters past the end. `-EBADF` (not open for
writing), `-EINVAL` (not a regular file), `-EFBIG` (above the filesystem's
limit, 4 GiB - 1 on FAT32), `-ENOSPC`, `-EIO`, `-EROFS`. The offset of the
open file does not move. S3 extends `fat_vfs_truncate`, which only handles
length 0 today.

### 6.7 `fsync` (36)

```c
int fsync(int32_t fd);
```

When it returns 0, every byte written through any fd to this file, and the
file's directory entry, are on the device. The FAT driver writes through
today, so this calls the mount's `sync` and then the block device's cache
flush (ATA FLUSH CACHE, NVMe Flush) where the driver offers one; a driver
without one is logged once per device. Pipes, PTYs, log and null return 0.
`-EBADF`, `-EIO`.

### 6.8 `statfs` (37)

```c
#define NX_STATFS_READONLY  0x1

typedef struct {
    uint32_t size;            /* bytes the kernel filled */
    uint32_t version;         /* 1 */
    uint32_t block_size;      /* bytes */
    uint32_t flags;           /* NX_STATFS_* */
    uint64_t total_blocks;
    uint64_t free_blocks;     /* UINT64_MAX when the filesystem cannot tell */
    char     fs_type[16];     /* "fat32", "nxfs", "ramfs", NUL-terminated */
    char     mount_path[64];  /* NUL-terminated */
} nx_statfs_t;                /* v1: 120 bytes */

int statfs(const char *path, nx_statfs_t *buf);
```

Describes the volume that holds `path`. The Files app and a `df` command use
it. `-ENOENT`, `-EFAULT`, `-EINVAL` (size).

## 7. Starting programs: `spawn_ex` (17), `wait_ex` (19)

Header: `kernel/include/core/nx_proc.h`.

```c
#define NX_SPAWN_WAITABLE    0x1  /* same as SPAWN_WAITABLE: keep the child for wait/wait_ex */
#define NX_SPAWN_NEW_JOB     0x2  /* the child starts a new job: job id = child's pid */
#define NX_SPAWN_LOG_STDIO   0x4  /* unmapped fd 0 = null, unmapped fd 1 and 2 = kernel log */
#define NX_SPAWN_FD_MAX      16   /* fd map entries */

typedef struct {
    int32_t child_fd;         /* 0..63, each at most once */
    int32_t parent_fd;        /* an open fd of the caller */
} nx_spawn_fd_t;

typedef struct {
    uint32_t size;            /* sizeof(nx_spawn_args_t) the caller was built with */
    uint32_t flags;           /* NX_SPAWN_* */
    uint64_t path;            /* user pointer: program path */
    uint64_t argv;            /* user pointer: NULL-terminated string array, or 0 */
    uint64_t envp;            /* user pointer: NULL-terminated "KEY=VALUE" array, or 0 */
    uint64_t fd_map;          /* user pointer: nx_spawn_fd_t[fd_count], or 0 */
    uint32_t fd_count;        /* 0..NX_SPAWN_FD_MAX */
    uint32_t job;             /* 0 = caller's job; else a job id (see below). 0 with NEW_JOB */
    uint64_t cwd;             /* user pointer: working directory, or 0 = caller's */
} nx_spawn_args_t;            /* v1: 56 bytes */

int32_t spawn_ex(const nx_spawn_args_t *args);   /* audited */
```

Returns the child's PID or a negative errno. The child:

- starts with an empty fd table, then gets each `child_fd` sharing the
  caller's `parent_fd` open file (as `dup2`, so offsets are shared and
  close-on-exec is clear). Nothing else is inherited: no other fds, no
  handles, no mappings. Handles go through channels as today;
- with `NX_SPAWN_LOG_STDIO`, fd 0 is null (reads return 0, writes are
  dropped) and fds 1 and 2 are the kernel log, unless the map sets them.
  Log lines are prefixed `[pid name]`, split at 512 bytes, and limited to
  16 KiB per second per process; the rest is dropped and counted in one
  line. This lets the desktop give apps a working `printf` without a
  terminal;
- gets `argv`, `envp` under the limits of section 4.4. With no `argv` the
  child gets `{ path }` as today;
- is a child of the caller (`ppid`), detached unless `NX_SPAWN_WAITABLE`,
  in the foreground class like `spawn`;
- has the job given by `job` and `NX_SPAWN_NEW_JOB` (section 7.1).

All checks happen before the child exists: on any error no process is
created and nothing leaks. Errors: `-EFAULT`, `-EINVAL` (size, flags, a
`child_fd` twice or out of range, `job` with `NEW_JOB`), `-EBADF` (a
`parent_fd`), `-E2BIG`, `-ENOENT`, `-ENOEXEC`, `-ENOMEM`, `-EAGAIN`
(process table full), `-EPERM` (job rule).

`spawn` (13) is unchanged apart from section 4: empty fd table, caller's
job, caller's cwd. It stays for old callers.

### 7.1 Jobs

A job is a group of processes that Ctrl-C and `proc.kill` can end together,
for example both sides of `ls | more`. It is the minimum a shell needs
without signals or sessions.

- Every process has `job` (a PID number). Processes started by the kernel
  have job = their own PID. `spawn` and `spawn_ex` without `NX_SPAWN_NEW_JOB`
  and with `job == 0` put the child in the caller's job.
- `NX_SPAWN_NEW_JOB` makes the child's job its own PID.
- An explicit `job` J must be the job of a live child of the caller
  (`-EPERM` otherwise), so a process can only group its own children. A
  shell starts the first stage of a pipeline with `NEW_JOB` and the rest
  with `job` = that PID.
- A job id stays valid while any process of the job is alive.

### 7.2 `wait_ex` (19)

```c
#define NX_WAIT_NOHANG  0x1

int32_t wait_ex(int32_t pid, int32_t *status, uint32_t flags);
```

`pid` > 0 waits for that child, -1 for any waitable child. Returns the PID
and stores the exit status, or 0 with `NX_WAIT_NOHANG` when no child has
exited yet. `-ECHILD` when there is no such waitable child, `-EINVAL`,
`-EFAULT`. Exit statuses the kernel sets: `exit` code as given; 128 + vector
after a CPU exception (as today); 129 hangup (PTY master closed); 130 Ctrl-C;
137 `proc.kill` KILL; 143 TERM deadline passed.

## 8. Terminals: PTY (60-65)

Header: `kernel/include/core/nx_pty.h`. New code in `kernel/src/core/nx_pty.c`;
the unused stub `kernel/src/io/pty.c` and `kernel/include/io/pty.h` are
deleted in the same PR.

```c
#define NX_PTY_MAX        32      /* PTY pairs in the system */
#define NX_PTY_PROC_MAX   8       /* PTY pairs a process may create */
#define NX_PTY_BUF        4096    /* bytes per direction */
#define NX_PTY_LINE_MAX   1024    /* canonical line length */

/* Mode bits (pty.set_mode / pty.get_mode). Default: all set. */
#define NX_PTY_ECHO       0x01    /* echo input to the master */
#define NX_PTY_CANON      0x02    /* line editing; reads return whole lines */
#define NX_PTY_ISIG       0x04    /* Ctrl-C ends the foreground job */
#define NX_PTY_ONLCR      0x08    /* output: "\n" -> "\r\n" */
#define NX_PTY_ICRNL      0x10    /* input: "\r" -> "\n" */

typedef struct {
    uint16_t rows;
    uint16_t cols;
    uint16_t xpixel;          /* 0 if unknown */
    uint16_t ypixel;          /* 0 if unknown */
} nx_pty_size_t;              /* 8 bytes */

int pty_open(int32_t fds[2], uint32_t flags);      /* fds[0] master, fds[1] slave; NX_O_CLOEXEC | NX_O_NONBLOCK */
int pty_set_size(int32_t fd, const nx_pty_size_t *size);
int pty_get_size(int32_t fd, nx_pty_size_t *size);
int pty_set_mode(int32_t fd, uint32_t mode);
int64_t pty_get_mode(int32_t fd);                  /* returns the mode */
int pty_set_fg(int32_t fd, uint32_t job);          /* 0 = no foreground job */
```

`fd` may be either end. Model: Terminal.app holds the master; the shell and
its jobs get the slave as fds 0-2 through `spawn_ex`.

- **Master `write`** is keyboard input. With `NX_PTY_CANON` the kernel edits
  the line: Backspace (0x08, 0x7F) erases one character (one UTF-8 sequence),
  Ctrl-U (0x15) erases the line, Enter ends it, Ctrl-D (0x04) at the start of
  a line makes the next slave `read` return 0. Lines longer than
  `NX_PTY_LINE_MAX` stop taking characters until Enter. Without
  `NX_PTY_CANON` bytes go straight to the slave. With `NX_PTY_ECHO` input is
  echoed to the master's output.
- **Slave `read`** returns one line at most in canonical mode, otherwise
  what is buffered. It blocks like a pipe.
- **Slave `write`** is program output; **master `read`** returns it, after
  `NX_PTY_ONLCR`. Writes block while the buffer is full.
- **Ctrl-C** (0x03) with `NX_PTY_ISIG`: the input line is discarded, `^C` and
  a newline are echoed, and if a foreground job is set, every non-system
  process in it ends with status 130 and the kernel logs
  `[PTY] pty<N> Ctrl-C: ended job <J> (<n> processes)`. With no foreground
  job only the line is discarded. Programs cannot catch Ctrl-C in the beta
  (no signals); a shell that wants to survive Ctrl-C must not set its own job
  as the foreground.
- **`pty.set_fg`**: J must be 0, the caller's job, or the job of a child of
  the caller (`-EPERM`), and the caller must hold an fd of this PTY
  (`-EBADF`). The shell sets the job before it waits and 0 after.
- **Size**: `pty.set_size` stores the size and raises `NX_SIGNAL_RESIZED`
  on the slave (section 4.5); `pty.get_size` clears it. Initial size 24 x 80.
  `rows` and `cols` must be 1..1000 (`-EINVAL`).
- **Closing.** When the last master fd closes: buffered output is dropped,
  slave reads return remaining input then 0, slave writes return `-EIO`,
  the foreground job ends with status 129, and the slave sees
  `NX_SIGNAL_PEER_CLOSED`. When the last slave fd closes: master reads
  return the remaining output then 0, and the master sees
  `NX_SIGNAL_PEER_CLOSED`. The pair is freed when both sides are closed.
- `seek` is `-ESPIPE`. `pty.*` on a non-PTY fd is `-ENOTTY`.
- Limits: `-ENFILE` when 32 pairs exist, `-EMFILE` when the caller already
  created 8 or has fewer than two free fds. Two 4 KiB buffers per pair from
  the PMM.

## 9. Processes: `uname` (51), `sys.stats` (52), `proc.*` (53-56)

Header: `kernel/include/core/nx_proc.h`. Numbers, structs and semantics are
those of `docs/desktop/proposals/taskmgr.md`, with the changes listed here.

### 9.1 `uname` (51)

```c
typedef struct {
    uint32_t size;            /* bytes the kernel filled */
    uint32_t version;         /* 1 */
    char sysname[32];         /* "NeolyxOS" */
    char release[32];         /* e.g. "0.9.0-beta.1", from S10's version header */
    char build[64];           /* build id, from S10's version header */
    char machine[16];         /* "x86_64" */
    char nodename[64];        /* host name; "neolyx" until Settings can set one */
} nx_uname_t;                 /* v1: 216 bytes; all strings NUL-terminated */

int uname(nx_uname_t *buf);
```

Replaces the unused `utsname_t` (five 65-byte fields) in `syscall.h`.
Until S10 adds the version header, `release` is `"1.0.0"` as `info` (50)
reports today.

### 9.2 `sys.stats` (52)

`nx_sys_stats_t` as in taskmgr.md (192 bytes) with these fields appended,
so v1 is 216 bytes:

```c
    uint32_t open_files;      /* open files in the system */
    uint32_t pipes;           /* live pipes */
    uint32_t ptys;            /* live PTY pairs */
    uint32_t reserved2;
    uint64_t mmap_bytes;      /* bytes mapped by mmap, all processes */
```

The new counters let tests and the Task Manager check that a killed process
gave back its pipes, PTYs and mappings.

### 9.3 `proc.list` (53), `proc.info` (54)

As taskmgr.md. In `nx_proc_entry_t` the `reserved` field becomes
`uint32_t job` (still 120 bytes). `resident_bytes` includes mmap pages.
`priority` stays informational (64 system, 112 foreground, 144 background,
from `sched_queue.h`).

### 9.4 `proc.kill` (55, audited)

As taskmgr.md (mode 0 TERM with `grace_ms`, mode 1 KILL), plus:

- `NX_PROC_KILL_JOB` (0x100) or-ed into `mode`: `pid` is a job id and every
  process of the job that the caller may end is ended. Returns the number
  ended, or `-ESRCH` if none.
- Exit statuses: KILL 137, TERM when the deadline passes 143.
- `PROC_FLAG_TERM_PENDING` (0x0010) is new in `process_t.flags`; a process
  sees it through `proc.info` on itself.

### 9.5 `proc.setprio` (56, audited)

Changed from taskmgr.md. The scheduler has run queues per class
(`sched_queue.h`), not numeric priorities, and `proc.set_class` (153) moves
apps between foreground and background on focus. So:

```c
#define NX_PRIO_NORMAL  0   /* the class follows focus (proc.set_class) */
#define NX_PRIO_LOW     1   /* pinned to the background queue, whatever the focus */

int proc_setprio(uint32_t pid, uint32_t level);
```

There is no level above normal: the system queue is for system processes
only. `-EINVAL` for another level, `-ESRCH`, `-EACCES`, `-EPERM` per the
kill rule.

### 9.6 Who may end or lower whom

As taskmgr.md: a user process may end or lower its own descendants; a
system process any non-system process; nobody a system process, kernel
thread, pid 1 or the idle process. One change is needed because S2 moves the
Task Manager out of the desktop: as a normal app it is not the parent of the
apps it lists. Proposed (decision Q2):

```c
#define NX_SPAWN_GRANT_PROC_ADMIN  0x8   /* spawn_ex flag; only a system caller may set it */
```

A process started with it may end and lower any non-system process, as a
system process may. It is stored as one bit in the PCB, never inherited,
logged at spawn and on every use, and folds into the capability set of
90-99 when that lands. A non-system caller setting it gets `-EPERM`.

## 10. Security

- Every pointer, size, count, flag and reserved field is checked (section 3).
- Fds cross processes only through an explicit `spawn_ex` map; never through
  `spawn`, `exec` of another program's choosing, or channels (fd passing over
  channels is not in this batch).
- Resource limits: 64 fds per process, 1024 open files, 256 pipes, 32 PTYs
  (8 per creator), the mmap area, the memory class limits. Each refusal is an
  error code and a log line, rate-limited.
- Process control follows section 9.6. Ctrl-C ends only the PTY's
  foreground job, which only a holder of that PTY can set, and only to its
  own job or a child's.
- The log sink is rate-limited per process so an app cannot flood the boot
  log.
- Audited: `spawn_ex`, `proc.kill`, `proc.setprio`, Ctrl-C and hangup job
  ends, `NX_SPAWN_GRANT_PROC_ADMIN` grants and uses.
- SIP checks the resolved path for every create, write, truncate, delete and
  rename, as today.

## 11. Out of scope for the beta

`fork` (stays `-ENOSYS`; use `spawn_ex`), signals, threads, sessions and
job stop/continue (Ctrl-Z), file-backed or shared `mmap`, `mprotect`,
sockets in the fd table (networking stream), fd passing over channels,
users and uids, capabilities 90-99 (second batch at the earliest), delete on
last close.

## 12. Stale numbers in the tree

These must change before the numbers they collide with are registered. The
S3 PR that registers the number fixes or reports each one.

| File | Problem | Owner |
|------|---------|-------|
| `tests/userland/include/syscall.h` (used by `nxsh`, `init`, `nxrender_test`) | `SYS_WRITE` 24 (is `seek`), `SYS_SEEK` 25 (is `stat`), `SYS_STAT` 26 (becomes `fstat`) | S3 fixes it in PR 1 |
| `desktop/apps/Path.app/vfs_ops.c` (not built into the image) | Invented numbers: open 5, read 0, write 1, stat 40, readdir 51, mkdir 53, rmdir 54, unlink 55, rename 56. 54 and 55 become `proc.info` and `proc.kill` | S2 (Files app), told before PR 5 |
| `kernel/include/core/syscall.h` | 51 `utsname_t`, 60-67 PTY prototypes | S3 replaces them with this document |

## 13. Delivery after sign-off

Each step is one PR with a QEMU log in `/mnt/project-files/beta-plan/S3/`
and keeps `fdtest`, `ipc_test`, `shmtest` and `classtest` passing.

| PR | Content | Test | Target |
|----|---------|------|--------|
| 0 | No ABI: open-file object, 64-entry fd table behind a pointer, VFS table 1024, errno mapping helper, pipe and PTY ring buffers and line discipline as kernel-independent code with host tests | `tests/host/nx_pty_ldisc_test.c`, `fdtest` | before sign-off |
| 1 | Section 4 (cwd, errors, flags, `wait` errno), `dup`, `dup2`, `fd.ctl`, `fstat`, `unlink`, `ftruncate`, `fsync`, `statfs` | `fdtest` extended | 2026-11-10 |
| 2 | `mmap`, `munmap` | `tests/userland/mmaptest` | 2026-11-12 |
| 3 | `pipe`, `spawn_ex`, jobs, `wait_ex`, exec envp, `obj.wait` fd items; sets FILEIO | `tests/userland/pipetest` | 2026-11-17 |
| 4 | PTY 60-65; sets PTY | `tests/userland/ptytest` | 2026-11-21 (S5 needs it) |
| 5 | `uname`, `sys.stats`, `proc.*`; sets PROC | `tests/userland/proctest` | 2026-11-25 |

Done when (from the S3 plan): `mmaptest`, `pipetest`, `ptytest` and
`proctest` print PASS in QEMU; the four existing tests still pass; and a
killed process gives back its pipes, PTYs and mappings, checked with
`mem.stats` (156) and the `sys.stats` counters of section 9.2.

## 14. Decisions for the owner

| # | Question | Recommendation |
|---|----------|----------------|
| Q1 | mmap memory inside today's 240 MiB user window (no layout change), or a new region that needs a memory-layout change | Inside the window for the beta; revisit with D7 |
| Q2 | How the out-of-process Task Manager may end apps it did not start | `NX_SPAWN_GRANT_PROC_ADMIN`, set only by the desktop when it launches Task Manager (section 9.6) |
| Q3 | Ctrl-C ends the foreground job at once and programs cannot catch it in the beta | Yes; catchable interrupts need signals or a notification handle, after the beta |
| Q4 | `obj.wait` items can name fds (the reserved field becomes `kind`) | Yes |
| Q5 | Existing file calls return proper errno values, resolve relative paths against a per-process cwd, and `open` allocates from fd 3 so 0-2 are only set on purpose (section 4) | Yes; nothing in the tree depends on the old behaviour except `fdtest`, which is updated |
| Q6 | Deleting an open file fails with `-EBUSY` instead of POSIX delete-on-last-close | Yes for the beta (FAT) |
