# Proposal: boot info additions for shutdown and the boot volume (S9)

Status: **proposal, not implemented.** No code changes with this document.
It is part of the Beta ABI batch (D2, 2026-11-06), because it changes the
boot info struct the loader passes to the kernel. Stream: S9 of
`/mnt/project-files/beta-plan/parallel-workstreams.md`.
Checked against: `main` at c1b2f63 (PR #49).

## 1. Why

Two S9 items need facts that only the loader has:

| Item | Needs | Today |
|------|-------|-------|
| Clean shutdown (VM-first, now) | UEFI Runtime Services `ResetSystem(EfiResetShutdown)` | The loader has the EFI System Table and does not pass it. Shell `sys shutdown` (syscall 101) only halts the CPU with interrupts off (`sys_shutdown_impl`), so the VM window freezes and nothing is flushed; `power_shutdown()` resets through the keyboard controller instead of powering off |
| Boot volume by ESP GUID (on hold until the owner's ~Dec 1 decision) | Which partition the loader was started from | The kernel assumes the boot volume is AHCI port 0 |

Both fit in the unused `reserved[]` words of `NeolyxBootInfo`, so the
struct layout and `NEOLYX_BOOT_VERSION` (1) stay the same. Defining both
now keeps them in one batch instead of a second one in December.

## 2. Current use of `NeolyxBootInfo.reserved[]`

| Word | Use | Defined in |
|------|-----|------------|
| 0 | `"INST"` installed marker | `boot/main.c` |
| 1-3 | Desktop image address, size, `"DESK"` magic | `boot/main.c`, `boot_image.h` |
| 4 | Boot flags word (`"FLGS"` magic, pixel format, VERBOSE, WRITE_PROTECT) | `kernel/include/video/bootlog.h` |
| 5-7 | unused (0) | |

## 3. Proposal

### 3.1 New flag bits in the boot flags word (`reserved[4]`)

| Bit | Name | Meaning when set |
|-----|------|------------------|
| 2 | `NEOLYX_BOOT_FLAG_SYSTAB` | `reserved[5]` holds the EFI System Table address |
| 3 | `NEOLYX_BOOT_FLAG_ESP_GUID` | `reserved[6..7]` hold the ESP's partition GUID |

Bits 0, 1 and 8-15 keep their meaning. A loader that predates this leaves
the bits clear (or the whole word 0), and the kernel then behaves exactly
as today.

### 3.2 `reserved[5]`: EFI System Table (physical address)

- Set by the loader from `efi_main`'s `SystemTable` argument, which stays
  valid after `ExitBootServices` (only the boot-services parts become
  invalid).
- The kernel uses only `RuntimeServices` (for `ResetSystem`) and, later,
  `ConfigurationTable` (for example SMBIOS in system info). It never calls
  `SetVirtualAddressMap`, so runtime services stay in physical mode, which
  the UEFI specification allows. The kernel's identity map covers runtime
  regions below 4 GB today, and all RAM-type regions after D7.
- Validation before any use, each failure logged and the feature
  disabled:
  - The address is non-zero, page-reachable, and below the identity
    map's end.
  - `Hdr.Signature` is `0x5453595320494249` (`"IBI SYST"`), `HeaderSize`
    is at least the size the kernel reads, and `Revision` is 2.0 or
    later.
  - `RuntimeServices` passes the same checks with signature
    `0x56524553544E5552` (`"RUNTSERV"`).
  - The CRC32 of both table headers matches.
- Calls are made from one function, `uefi_rt_reset(type)`, with interrupts
  off, on the kernel's page tables, at most once per boot.

### 3.3 `reserved[6..7]`: ESP partition GUID

- The loader reads the device path of its own image (`LoadedImage`
  `DeviceHandle`). From the Hard Drive media node with GPT signature type
  it copies the 16-byte partition GUID: bytes 0-7 go in `reserved[6]` and
  bytes 8-15 in `reserved[7]`, in the on-disk byte order.
- An MBR or superfloppy boot volume (no GPT node) leaves the flag clear.
  The kernel then keeps today's rule (AHCI port 0).
- The kernel's later use is to scan the GPT of every block device (AHCI,
  NVMe and, with S8, USB) for that GUID and mount that partition as the
  boot volume. That is held until the owner's decision around Dec 1; only
  the field is defined now.

## 4. Shutdown with these fields (implemented after sign-off)

`sys_shutdown` (101) and `sys_reboot` (100) keep their numbers,
arguments and privilege (`PRIV_SYSTEM`); only what they do changes:

1. Stop accepting new process starts and log the request (audit).
2. Sync every mounted file system (`vfs` sync on each mount; FAT writes
   through today, so this is mostly a hook for NXFS and later caches).
3. Flush every block device's write cache (`block_flush`: ATA FLUSH CACHE
   EXT for AHCI, NVMe Flush). Errors are logged and do not stop the
   shutdown.
4. `ResetSystem(EfiResetShutdown)` for shutdown or
   `ResetSystem(EfiResetCold)` for reboot, when `SYSTAB` is valid.
5. Fallbacks, in order: ACPI S5 through the FADT `PM1a_CNT` block (for
   shutdown), the FADT reset register or keyboard controller (for
   reboot). As a last resort, halt with the message "It is now safe to
   turn off the computer" on the console and the boot log.

Done (QEMU): Shell `sys shutdown` ends the QEMU process on q35 and pc. A
file written just before it is on the disk image after the next boot.
`sys reboot` restarts into the loader.

## 5. Compatibility

- The `NeolyxBootInfo` layout, size and version don't change. Only
  reserved words and flag bits gain a meaning.
- A new kernel with an old loader keeps today's behavior: the flags are
  clear, shutdown falls back to ACPI or halt, and the boot volume is
  AHCI port 0.
- An old kernel with a new loader ignores the new words.
- No syscall number, argument or ABI flag changes; no `NX_ABI_MINOR`
  change. The loader change is two assignments plus the device path walk.
