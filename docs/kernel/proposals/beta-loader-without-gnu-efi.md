# Proposal: build the UEFI loader without gnu-efi (S9)

Status: **proposal, not implemented.** No code changes with this document.
It asks for owner decision D8 and is part of the Beta ABI batch (D2,
2026-11-06). Stream: S9 of
`/mnt/project-files/beta-plan/parallel-workstreams.md`.
Checked against: `main` at c1b2f63 (PR #49).

## 1. Why

NeolyxOS code must be written from scratch, with no third-party code or
libraries. The UEFI loader (`boot/`, built into `BOOTX64.EFI`) is the one
place that still links third-party code: gnu-efi.

| What gnu-efi provides today | Where |
|-----------------------------|-------|
| UEFI type and protocol headers | `#include <efi.h>`, `<efilib.h>` from `/usr/include/efi` |
| Entry code and self-relocation (`crt0-efi-x86_64.o`, `_relocate`) | `boot/Makefile`, `LDFLAGS` |
| Linker script | `/usr/lib/elf_x86_64_efi.lds` |
| Library functions: `InitializeLib`, `Print` (66 calls), `StrCpy`, `CopyMem`, `SetMem`, `CompareMem`, `AllocatePool`, `FreePool`, `FileDevicePath` | `libefi.a`, `libgnuefi.a` |
| The `uefi_call_wrapper` calling-convention macro (126 calls) | `efi.h` with `-DEFI_FUNCTION_WRAPPER` |

The current `BOOTX64.EFI` shows the gnu-efi layout: a 12-byte placeholder
`.reloc` section, and the real relocations in ELF `.rela` and `.dynamic`
sections that gnu-efi's `_relocate` applies at run time.

There is a second reason. On the owner's machine (GCC 15), `boot/` does
not build and `build_all.sh` falls back to the committed `BOOTX64.EFI`.
The likely cause is the gnu-efi install (paths differ between
distributions); `make -C boot` would show it. A loader that needs only GCC
and binutils removes that dependency.

## 2. What changes and what does not

**Unchanged:** the boot flow (the same steps in the same order), the
files on the ESP (`\EFI\BOOT\BOOTX64.EFI`, `kernel.bin`, `desktop.elf`,
`diagnose.flg`), `NeolyxBootInfo` and the boot flags word, the
diagnostics page, the kernel. Nothing in userspace.

**Changed:** how `BOOTX64.EFI` is compiled and linked, and the small
library the loader calls.

## 3. Design

### 3.1 Own UEFI headers: `boot/include/nxefi/`

Written from the UEFI 2.10 specification, only what the loader uses:

- Base types (`UINTN`, `CHAR16`, `EFI_STATUS`, `EFI_HANDLE`,
  `EFI_GUID`), status codes, `EFI_TABLE_HEADER`.
- `EFI_SYSTEM_TABLE`, `EFI_BOOT_SERVICES`, `EFI_RUNTIME_SERVICES`. Every
  table keeps the spec's full field order, so offsets are right. Members
  the loader never calls are declared as plain pointers.
- Protocols: Simple Text Input and Output, Graphics Output, Loaded Image,
  Simple File System, File (with `EFI_FILE_INFO`), plus the memory
  descriptor and the ACPI configuration-table GUIDs.
- Calling convention: every function pointer is declared
  `__attribute__((ms_abi))` (`EFIAPI`). GCC then emits the Microsoft x64
  convention UEFI uses. No call wrapper is needed, so the 126
  `uefi_call_wrapper(f, n, ...)` calls become `f(...)`.

Header checks: a host test compiles the headers and checks
`offsetof`/`sizeof` of every table against the spec's values (for
example `BootServices` at offset 96 in `EFI_SYSTEM_TABLE`).

### 3.2 Own small library: `boot/lib/`

- `nx_print()`: formatted output to `ConOut` (UCS-2), with the formats
  the loader uses (`%s` for UCS-2, `%a` for ASCII, `%d`, `%u`, `%x`,
  `%lx`, `%p`, `%r` for an `EFI_STATUS` name). It replaces `Print`.
- `nx_memcpy`, `nx_memset`, `nx_memcmp`, `nx_strcpy16`, `nx_strcmp16`,
  `nx_strlen16`.
- Pool allocation and the file device path are called through Boot
  Services directly.
- The entry point stores `SystemTable`, `BootServices` and
  `RuntimeServices` in globals, which is all `InitializeLib` did for us.

### 3.3 Build: ELF to PE32+ with a from-scratch tool

1. GCC compiles with `-ffreestanding -fpie -fshort-wchar -mno-red-zone
   -fno-stack-protector -fno-asynchronous-unwind-tables`.
2. `ld -pie --no-dynamic-linker -T boot/nxefi.ld` links a static
   position-independent ELF. The linker script is our own: 4 KB-aligned
   `.text`, `.rodata`, `.data` and `.bss`, with entry `efi_main`.
3. A new host tool, `tools/elf2pe/elf2pe.c` (from scratch, about 600
   lines of C), writes the PE32+ image:
   - It copies the sections.
   - It turns every `R_X86_64_RELATIVE` relocation into a PE base
     relocation (`IMAGE_REL_BASED_DIR64`, grouped per 4 KB page in
     `.reloc`).
   - It sets subsystem 10 (EFI application) and machine `0x8664`.
   - It writes timestamp 0, so the same source gives the same file.

   The tool refuses any other relocation type, a writable executable
   section, or an entry point outside `.text`, and reports which one.

The firmware then relocates the image itself, as it does for any PE
image, so the loader needs no self-relocation code. A proper PE also
signs cleanly when Secure Boot signing comes later.

Considered and not chosen:
- `objcopy --target=efi-app-x86_64` together with our own
  self-relocation code. It is less work, but it produces gnu-efi's split
  layout (a placeholder `.reloc`, with the real relocations applied at
  run time), and it depends on binutils being built with that target.
- `clang` or `lld-link`. That would add a toolchain the owner does not
  use today.

### 3.4 Migration

The work is done in one PR per step:

1. Headers, library, linker script, `elf2pe`, and a "hello" EFI app that
   prints to `ConOut` and returns, booted on OVMF.
2. Move `main.c`, `graphics.c` and `boot_menu.c` to the new headers and
   library.
3. Delete the gnu-efi build and its include paths from `boot/Makefile`.
   `build_all.sh` keeps falling back to the committed `BOOTX64.EFI` only
   if GCC itself is missing.

## 4. Tests (QEMU, logs to `/mnt/project-files/beta-plan/S9/`)

- The link map and `nm` of the new image contain no gnu-efi symbols:
  `_relocate`, `InitializeLib`, `crt0` symbols or `Print`.
- `elf2pe` host tests:
  - Convert a known ELF, parse the PE back, and check the headers,
    section table, entry point, `SizeOfImage` and every relocation.
  - Feed it an ELF with a non-RELATIVE relocation and expect a refusal.
- Boot to the desktop on OVMF with q35 and with pc, both with and
  without `diagnose.flg`. The diagnostics page shows the same values as
  the gnu-efi build.
- Load the image at a different address: in the UEFI shell, `load` it
  after allocating the preferred range first. Relocation must work.
- Build on GCC 13 (cloud) and GCC 15 (owner), with the GCC 15 error
  flags.

## 5. Risks

- **Firmware strictness about PE layout** (header size, section
  alignment, `SizeOfImage`). OVMF is strict, which covers most cases.
  Real PCs come in the hardware rounds after about 2026-12-01 (owner's
  VM-first decision). The committed `BOOTX64.EFI` from the gnu-efi build
  stays in git history if a round fails.
- **Behavior differences in `Print`.** Only formatting changes, and the
  diagnostics page is compared side by side.

## 6. Schedule

The headers, library and `elf2pe` steps start after sign-off (D8 with the
batch on 2026-11-06). Steps 1-3 are done by 2026-11-24, after the PCI and
memory work S8 depends on.

## 7. Compatibility

No syscall, struct, ABI flag or `NeolyxBootInfo` change. The boot flow
change is in how the loader is built, which is why it needs the owner's
sign-off (D8).
