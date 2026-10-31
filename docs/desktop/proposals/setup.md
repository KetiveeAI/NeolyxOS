# Proposal: accounts service for first-run setup (Setup flow area)

Status: proposal. It needs the owner's approval because it adds syscalls (a
public ABI) and touches boot flow. Nothing in this proposal is implemented in
the kernel.

## Problem

The userspace Setup Assistant collects the first administrator account (full
name, account name, password, hint), but it has nowhere safe to send it:

- `kernel/include/core/user_auth.h` declares `user_create()`,
  `user_login()` and `session_unlock()`, but they are kernel-internal. The
  syscall table (`kernel/include/core/syscall_table.def`) has no account,
  auth or session entries.
- `desktop/shell/lock_screen.c` declares `extern int session_unlock(const char *)`,
  but the desktop links a stub, `void session_unlock(void)`, in
  `shell/userspace_stubs.c`. The signatures do not match and nothing checks a
  password.
- `kernel/src/core/user_auth.c` `hash_password()` is a placeholder XOR/add
  mix, not a cryptographic hash. The root account is created with an all-zero
  hash, meaning no password.
- `kernel/src/core/first_boot.c` runs its own first-boot wizard from
  `boot_mode_init()`, inside the kernel. That puts GUI code in the kernel,
  against the rule in CLAUDE.md, and it duplicates this assistant.

Until there is an answer, `setup_accounts_create()` returns
`SETUP_ERR_UNSUPPORTED` and the assistant:

- wipes the password,
- records `account.pending_creation = true`,
- tells the user plainly that no password was set.

## Proposed kernel interface

These are new syscalls. The numbers are left to the kernel owner; the names
follow the existing dotted style.

| Name | Args | Priv | Result |
|------|------|------|--------|
| `account.create` | `const nx_account_req_t *req` | PRIV_USER while the system is in first-run state; otherwise admin only | 0 or -EEXIST / -EINVAL / -EPERM / -ENOSPC |
| `account.verify` | `const char *name, const uint8_t *pw, uint32_t pw_len` | PRIV_USER, rate-limited | 0 or -EACCES (constant time) |
| `account.set_password` | `uint32_t uid, const uint8_t *old, uint32_t old_len, const uint8_t *new, uint32_t new_len` | owner or admin | 0 / -EACCES |
| `account.info` | `uint32_t uid, nx_account_info_t *out` | PRIV_USER (public fields only) | 0 / -ENOENT |
| `session.unlock` | `const uint8_t *pw, uint32_t pw_len` | session owner | 0 / -EACCES |

```c
typedef struct {
    uint32_t abi_version;          /* 1 */
    char     name[32];             /* validated again in kernel: ^[a-z][a-z0-9._-]{0,30}$ */
    char     full_name[64];
    char     hint[64];             /* optional, must not contain the password */
    const uint8_t *password;       /* user pointer, copied in then wiped by the kernel */
    uint32_t password_len;         /* 8..63 */
    uint32_t flags;                /* NX_ACCOUNT_ADMIN */
} nx_account_req_t;
```

Kernel requirements:

1. Copy the password from user memory with bounds checks, derive the stored
   hash with a salted slow KDF, then wipe the kernel copy. Use PBKDF2-HMAC-SHA256
   with at least 210k iterations, or Argon2id if the kernel crypto lands; use a
   16-byte random salt per account. Replace `hash_password()` in user_auth.c.
2. Allow `account.create` without admin rights only while the system is in
   first-run state (no admin account exists yet). Once the first admin exists,
   unprivileged creation is refused.
3. Persist accounts to a protected store, for example
   `/System/Security/accounts.nlc`, readable only by the kernel. Write it
   atomically with a temporary file and a rename.
4. Write an audit log line for create, verify failures, password changes and
   unlocks. Log the account name, never the password.
5. Rate-limit and lock out after repeated failures (the
   `failed_attempts` field already exists).

## Userspace side (already shaped for this)

- `setup_accounts_create()` in `desktop/shell/views/setup_store.c` is the one
  place to switch over to `account.create`. It already validates input,
  never logs the password and leaves the wipe to its caller. The assistant
  wipes on every path.
- With the service in place, `account.pending_creation` lets Settings or the
  login window offer "Finish creating your account" to systems that ran setup
  before the service existed.
- `lock_screen.c` should call `session.unlock` through a typed wrapper in
  `nxsyscall.h`, replacing the mismatched stub.

## Decisions for the owner

1. **Pending accounts.** On today's real system, setup completes without
   creating an account and writes the completed marker, so setup does not
   reopen at every boot. The other choice is to withhold the marker until an
   account exists, so setup reruns at every boot. Which do you want?
2. **Kernel first-boot wizard.** Retire `first_boot_run_wizard()` (kernel GUI)
   in favor of this userspace assistant? This changes boot flow.
3. **KDF choice and iteration count** for the kernel hash.
4. **Accent color.** NXUI has no `nxui_set_accent()`. Should NXUI add one
   (Glass/NXUI area), so the accent choice applies live like appearance does?
