# Proposal: `sys` capability tokens

Status: **proposal, not implemented**. No kernel file has been changed for this.
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
| Delete files | Syscall 32 (`rmdir` → `vfs_unlink`), but the FAT driver has no `unlink` op |
| Rename | None |

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

## Also needed for the spec (separate, smaller)

- FAT `unlink` and `rename` ops, so `remove` and `move` work on the boot volume.
- `info disk` (per-mount totals), `logs` (read the kernel log ring), a package service (`pkg`).

## Decision needed from the owner

This changes the kernel ABI and the boot-time trust model, so per CLAUDE.md it needs the owner's
OK before any kernel work starts.
