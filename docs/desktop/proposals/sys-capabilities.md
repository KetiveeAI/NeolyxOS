# Proposal: `sys` capability tokens

Status: **capability tokens are a proposal, not implemented.** System Integrity Protection and
FAT delete/rename landed on 2026-10-31 (see "Done" below).
Client side: `desktop/shell/views/shell_view.c` (Shell). Command spec: NeoLyx OS Command Specification v1.0.

## Why

The command spec says `sys <command>` runs one command with system rights, `sys enter` opens a
system shell, and that `sys` maps to **capability tokens**, not UID 0. The kernel cannot do this today:

| Piece | Today |
|-------|-------|
| Privilege check in the dispatcher | `nx_syscall.c` checks `syscall_has_priv(sc->priv)` |
| Privilege state | One global `current_priv` in `syscall.c`, the same for every process |
| Elevation | `syscall_elevate()` sets it to `PRIV_SYSTEM` with no check (a `TODO`) and nothing calls it |
| Accounts / passwords | None (`setup_accounts_create` returns `UNSUPPORTED`) |
| Delete files | Syscall 32 (`rmdir` → `vfs_unlink`) with FAT `unlink` (done) |
| Rename | Syscall 35 (`rename` → `vfs_rename`) with FAT `rename` (done) |

So the Shell refuses every privileged path. `sys reboot` and `sys shutdown` are sent to the kernel
after the user types REBOOT or SHUTDOWN, and the kernel returns `-EPERM`. The Shell never claims
rights it does not have, and `sys enter` stays refused until this lands (owner decision, 2026-10-31).

## Proposed design

1. **Per-process capability set** in the PCB: a 64-bit mask (`CAP_REBOOT`, `CAP_SYS_FILES`,
   `CAP_PROC_KILL`, `CAP_PKG`, `CAP_SERVICE`, `CAP_USERS`, `CAP_NET_ADMIN`, `CAP_TIME`, ...).
   It replaces the global `current_priv`. The dispatcher checks `sc->cap` against the caller's mask.
2. **Tokens, not modes.** `sys_cap_request(caps, reason, ttl_ms)` asks the kernel for a token.
   The kernel hands the request to a trusted authenticator (the accounts service, or the lock screen
   prompt, both in userspace and signed) and grants a token only after the user authenticates.
   Tokens are bound to the pid, expire (default 5 min for `sys enter`, one call for `sys <cmd>`),
   and can be dropped (`sys_cap_drop`, used by `sys exit`).
3. **Audit.** Every request, grant, denial, use and drop goes to the kernel audit log with
   pid, caps and reason (`nx_audit_log` already exists).
4. **Children do not inherit** a token unless it was spawned with `SPAWN_INHERIT_CAPS`, so
   `run` from a system shell cannot hand rights to arbitrary programs by accident.
5. **SIP stays above caps.** `sip_check_path` keeps protecting the core even with `CAP_SYS_FILES`.
   That matches the spec's "`sys remove /system/core` needs typed intent".

## New syscalls (numbers to be assigned in `syscall_table.def`)

| Name | Args | Notes |
|------|------|-------|
| `cap.request` | caps mask, reason string, ttl ms | Blocks until the user authenticates or cancels. Audited |
| `cap.drop` | caps mask | Always allowed |
| `cap.query` | out: mask, expiry | What the Shell's `sys status` shows |

## Done: System Integrity Protection (2026-10-31)

`kernel/src/fs/sip.c` is now loaded at boot (`sip_init()` in `nx_kernel_start`, after the volumes
are mounted and before the desktop starts). Every create, write, delete and rename through the VFS
is checked. Paths are normalized (`//`, `.`, `..`) and matched case-insensitively on whole path
components, so `/system/x`, `/Users/../System/x` and `//System` are all caught.

| Path | User process | System process (desktop) | Kernel |
|------|--------------|--------------------------|--------|
| `/System`, `/EFI`, `/kernel.bin`, `/apps`, `/Kernel`, `/Boot`, `/bin`, `/sbin`, `/ramfs`, `/INSTALLED.MRK`, `/NvVars` | read only | read only | full |
| `/System/Config` (settings, setup marker) | read only | read/write | full |
| `/Library` | read only | read/write | full |
| `/Applications` | read only (needs admin, no accounts yet) | read/write | full |
| `/Users`, `/home`, `/tmp` (contents) | read/write | read/write | full |
| Those folders themselves, and other entries in `/` | cannot delete | cannot delete | full |

Every refusal is logged to serial as `[SIP] *** VIOLATION #n ***` with path, operation, pid and
protection flags. Recovery mode turns SIP off (unchanged). Known limit: Shell runs inside the
desktop process, so the kernel sees it as a system process; Shell refuses system paths itself
until apps run as their own processes.

## Also needed for the spec (separate, smaller)

- `info disk` (per-mount totals), `logs` (read the kernel log ring), a package service (`pkg`).
- FAT free-cluster count in FSInfo is not updated on create/delete (fsck auto-corrects it).

## Decision needed from the owner

Capability tokens change the kernel ABI and the trust model, and need an accounts service to
authenticate against, so they still need the owner's OK before work starts.
