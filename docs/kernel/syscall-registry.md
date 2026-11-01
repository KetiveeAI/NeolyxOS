# NeolyxOS syscall and ABI flag registry

**Owner:** stream S10 (release, CI and integration).
**Created:** 2026-11-01, from section 4 of the public beta plan, approved by
the project owner on 2026-11-01 (decision D1).

This file is the only place a syscall number or an ABI capability flag is
allocated. If a number or flag is not listed here as yours, do not use it.

## Rules

1. A stream uses numbers and flags only from its own block below.
2. Need more numbers or a new flag: ask S10. S10 updates this file in a
   one-line PR, and the number is yours once that PR merges.
3. In `kernel/include/core/syscall_table.def` and
   `kernel/include/core/syscall.h`, add lines only inside your block, in
   number order, under the `/* --- Sx: <stream> --- */` comment for it.
4. New syscall code goes in a new per-area file (the pattern of `nx_fb.c`,
   `nx_ipc.c`, `nx_mem.c`), not in `kernel/src/core/syscall.c`.
5. Each ABI flag is defined in its stream's own header, as `NX_ABI_FLAG_IPC`
   is in `kernel/include/core/nx_ipc.h`. One flag per feature group.
6. Applications test the ABI flag (`abi.query`, syscall 0), never the minor
   version.
7. Only S10 changes `NX_ABI_MINOR` in `kernel/include/core/nx_abi.h`. It goes
   up once per batch the owner has signed off, when the batch's last part
   merges.
8. A syscall that adds or changes kernel ABI needs the owner's sign-off
   before code that uses it merges (see "ABI batches" below). A registry
   entry reserves a number; it is not that sign-off.
9. Numbers are never reused. A removed syscall stays listed as retired.

## Table size

`SYS_MAX` in `syscall.h` is 256, equal to `NX_SYSCALL_MAX` in `nx_abi.h`,
which sizes the dispatch table in `kernel/src/core/nx_syscall.c`. Raising it
from 160 changed table sizes only, with no behaviour change. A registered
number at or above `NX_SYSCALL_MAX` fails to compile.

## Syscall numbers

State: **in use** means registered in `syscall_table.def` on `main`;
**declared** means a `#define` exists in `syscall.h` but nothing is
registered (calls return `-ENOSYS`); **reserved** means allocated to a stream,
nothing in the code yet.

| Numbers | Owner | Use | State on `main` |
|---------|-------|-----|-----------------|
| 0 | existing | `abi.query` | in use |
| 1-10 | existing | exit, sleep, time, yield, getpid, getppid, fork, exec, wait, brk | in use |
| 11-12 | S3 | `mmap`, `munmap` (anonymous memory) | declared |
| 13 | existing | `spawn` | in use |
| 14-19 | S3 | `dup`, `dup2`, `pipe`, `spawn_ex` (fd map, argv, envp), spares | reserved |
| 20-25 | existing | open, close, read, write, seek, stat | in use |
| 26-29 | S3 | `fstat` (26, declared), `unlink`, `ftruncate`, spare | 26 declared, rest reserved |
| 30-35 | existing | readdir, mkdir, rmdir, chdir, getcwd, rename | in use |
| 36-39 | S3 | `fsync`, `statfs`, spares | reserved |
| 40-47 | existing (N) | socket, bind, listen, accept, connect, send, recv, closesock | in use |
| 48-49 | N | socket options or poll, if needed | reserved |
| 50 | existing | `info` | in use |
| 51-56 | S3 | `uname` (51, declared), `proc.list`, `sys.stats`, `proc.kill`, `proc.setprio`, spare | 51 declared, rest reserved |
| 57 | S6 | `random.get` (kernel entropy, `kernel/src/security/random.c`) | reserved |
| 58-59 | S3 | spares | reserved |
| 60-69 | S3 | PTY (60-67 as declared in `syscall.h`), spares | 60-67 declared, rest reserved |
| 70-89 | unallocated | threads and later work, not used in the beta | free |
| 90-99 | S3 | process capabilities (`docs/desktop/proposals/sys-capabilities.md`), stretch | reserved |
| 100-101 | existing | `reboot`, `shutdown` | in use |
| 102-109 | S9 | `mount`, `umount` (102-103, declared), power and boot-volume info | 102-103 declared, rest reserved |
| 110-119 | existing | NXGame bridge | in use |
| 120-124 | existing | `fb.map`, `fb.info`, `fb.flip`, `input.poll`, `input.register` | in use |
| 125-126 | S1 | input focus or routing, if needed | reserved |
| 127-128 | existing | `debug.print`, `cursor.set` | in use |
| 129 | S1 | input focus or routing, if needed | reserved |
| 130-132 | existing | `rtc.get`, `rtc.set`, `rtc.unix` | in use |
| 133 | N | `net.if_list` (ABI 1.4) | in use (see note 1) |
| 134-139 | N | DNS resolve, later network calls | reserved |
| 140-156 | existing | handles, shm, channels, `obj.wait`, `fb.flip_rects`, ports, app classes, memory pressure, `mem.stats` | in use |
| 157-167 | S1 | display and input security, compositor | reserved (note 1) |
| 168-175 | S9 | display backend and GPU (virtio-gpu stretch) | reserved |
| 176-255 | unallocated | S10 allocates on request | free |

**Note 1 (`net.if_list`).** `net.if_list` first merged as syscall 157 with
flag `0x0010` (PR #45, ABI 1.4), before this registry existed, which
collided with S1's block. PR #46 moved it to 133 and `NX_ABI_FLAG_NET` to
`0x0020` before any kernel outside the repository shipped either value, so
`NX_ABI_MINOR` stayed 4. 157 and `0x0010` now belong to S1. No released
kernel ever had `net.if_list` at 157, so rule 9 (no reuse) does not apply.

## ABI capability flags

`nx_abi_info_t.flags` is a `uint16_t`, returned by `abi.query`.

| Flag | Owner | Meaning | State on `main` |
|------|-------|---------|-----------------|
| 0x0001 | existing | `NX_ABI_FLAG_SHM`: shm and handles (`nx_ipc.h`) | in use |
| 0x0002 | existing | `NX_ABI_FLAG_IPC`: channels, `obj.wait`, ports (`nx_ipc.h`) | in use |
| 0x0004 | existing | `NX_ABI_FLAG_FB_RECTS`: `fb.flip_rects` (`nx_fb.h`) | in use |
| 0x0008 | existing | `NX_ABI_FLAG_MEMCLASS`: app classes and memory pressure (`nx_mem.h`) | in use |
| 0x0010 | S1 | display and input security | reserved |
| 0x0020 | N | `NX_ABI_FLAG_NET`: network info (`net.if_list`, resolve) | in use (`nx_net.h`) |
| 0x0040 | S3 | FILEIO: mmap, fstat, unlink, dup, pipe, spawn_ex | reserved |
| 0x0080 | S3 | PTY | reserved |
| 0x0100 | S3 | PROC: proc.list, sys.stats, proc.kill, setprio | reserved |
| 0x0200 | S6 | RANDOM | reserved |
| 0x0400 | S9 | PLATFORM: mount, power, boot volume | reserved |
| 0x0800-0x8000 | unallocated | S10 allocates on request | free |

Five flags are left. Ask for one only for a feature group an app needs to
detect separately.

## ABI versions

| Minor | Contents | State |
|-------|----------|-------|
| 1.0 | Base syscalls | released in tree |
| 1.1 | Handles, shm, channels, ports | merged |
| 1.2 | `fb.flip_rects` | merged |
| 1.3 | App classes, memory pressure, `mem.stats` | merged |
| 1.4 | `net.if_list` (133) | merged |
| 1.5 | Beta ABI batch (S1, S3, S6, S9, N, S4 headers) | proposals due 2026-11-05, owner review 2026-11-06 |
| 1.6 | Second, smaller batch, only if needed | around 2026-11-24 |

## ABI batches

- Every stream that adds or changes a syscall, a kernel struct shared with
  userspace, the window protocol, the boot info struct or the memory layout
  writes a proposal under `docs/kernel/proposals/` or
  `docs/desktop/proposals/` and opens it as a doc-only PR titled `[ABI]`.
- S10 collects them into one Beta ABI batch for the owner on 2026-11-06.
  Later findings go into a second batch around 2026-11-24. No third batch
  except for bugs.
- Code that uses a new number merges only after the owner signs off its
  proposal.

## Changing this file

Only S10 edits this file. A request names the stream, the count of numbers
(or the flag) and a one-line use. S10 answers with a PR that changes only the
affected rows.
