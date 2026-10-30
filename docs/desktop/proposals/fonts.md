# Proposal: directory listing wrapper for the font registry

Area: fonts. Status: proposal. No kernel change was made.

## Need

User fonts belong in `/Library/Fonts`. Today the registry reads the font
list from `/Library/Fonts/fonts.index`, which it writes itself when it
installs a font. A `.ttf` file copied into the folder by other means, such
as a file manager or a package, is not picked up until something adds it to
the index.

## What exists

- **Kernel**: `SYS_READDIR` (30), implemented by `sys_readdir_impl(path,
  entries, count, read_ptr)`. It fills `vfs_dirent_t` records and writes the
  number read to `*read_ptr`.
- **Userspace**: `desktop/include/nxsyscall.h` has no `SYS_READDIR` define
  and no wrapper. Its `syscall3` passes only three arguments, while the
  call needs four. The comment in `kernel/include/core/syscall.h` describes
  the signature as `(int fd, struct dirent*)`, which differs from the
  implementation. `desktop/apps/Path.app/vfs_ops.c` uses a different number
  (51).

## Proposal

1. Settle the ABI: `SYS_READDIR` = 30 with
   `(const char *path, nx_dirent_t *entries, uint32_t count, uint32_t *nread)`,
   as implemented. Fix the comment in `syscall.h` to match.
2. Mirror the record in `nxsyscall.h` as `nx_dirent_t`, with the same field
   order and sizes as `vfs_dirent_t`, guarded by a `_Static_assert` on its
   size in a shared header.
3. Add a `syscall4` (rdi, rsi, r10, r8, the same registers `syscall3` uses
   for its first three arguments) and an `nx_readdir()` wrapper.
4. In the registry, list `*.ttf` files under `/Library/Fonts`. Any file
   that is not in the index is validated exactly like an install, then
   added as enabled. The index then keeps only the enabled/disabled state.

Until the project owner approves this, the registry keeps the index as the
source of truth. This document records why.
