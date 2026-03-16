# 🚀 Neolyx OS – Developer Instructions & IDE Guide

Neolyx is a **completely custom, non-Linux-based operating system** designed for speed, stability, game support, and developer power — with a locked core system and a fully modular terminal.

---

## 📁 Project Folder Structure

```
neolyx-os/
├── bootloader/     # Bootloader written in x86 ASM
├── kernel/         # Kernel (in C or Rust)
├── build/          # Linker script, image builder
├── iso/            # Output ISO file
├── shell/          # NeoShell (custom CLI engine)
├── apps/           # Default apps like O2N
├── pkg/            # Python/rust runtime & package manager
├── sdk/            # Dev toolchain (C, Python, Rust)
├── fs/             # Custom file system (NXFS)
└── docs/           # Architecture, API, design details
```

---

## ⚖️ Build & Run Instructions (QEMU)

1. Install tools: `nasm`, `i386-elf-gcc`, `qemu`, `make`
2. Compile bootloader:
   ```bash
   nasm bootloader/boot.asm -f bin -o build/boot.bin
   ```
3. Compile kernel:
   ```bash
   i386-elf-gcc -ffreestanding -c kernel/main.c -o build/kernel.o
   i386-elf-ld -T kernel/link.ld -o build/kernel.bin build/kernel.o
   ```
4. Combine:
   ```bash
   cat build/boot.bin build/kernel.bin > build/neolyx.img
   ```
5. Run:
   ```bash
   qemu-system-i386 -drive format=raw,file=build/neolyx.img
   ```

---

## ⚙️ Neolyx Design Principles

- **No Linux dependencies**: Only use our custom code.
- **Locked **`` directory: Can’t be changed at runtime.
- **Modular CLI**: NeoShell supports apps like `sudo`, `neo install`, etc.
- **Developer-first**: Python, Rust, WASM, and static compilation supported.
- **Custom FS**: `NXFS` handles file storage. Stored in `/system/fs/`.
- **Kernel in C**: Initially in C for control, later port to Rust/C++
- **Graphics**: Native VESA, Vulkan planned (no X11/Wayland).

---

## ⚠️ Error Handling Rules

If any tool or code fails:

- Always check:
  - `logs/` directory (create if missing)
  - Boot errors in QEMU (use `-d int` debug flag)
- If image doesn't boot:
  - Check bootloader 510-byte size + `0xAA55` signature
  - Make sure the linker script base address is correct (`0x100000`)
- If kernel panics:
  - Check pointer addresses in `main.c`
  - Use simple memory models before paging
- If filesystem breaks:
  - Make sure `/fs/driver.c` or `nxfs.c` is loaded early

---

## 🧠 Cursor IDE or AI Assistants Rulebook

- Only use files **within **``**, **``**, **``**, **``**, **``
- NEVER add Linux system headers or calls like `#include <linux/...>`
- NEVER suggest external APIs not written or planned in Neolyx
- Kernel must start from `kernel/main.c` and link via `kernel/link.ld`
- All paths must follow the **Neolyx folder layout**
- Assistants should default to C language for kernel
- If confused, refer to this file as truth

---

## 📦 Package System Plan (`.nxpkg`)

- All apps must be packaged in `/store/` using signed `.nxpkg` format
- Each package must contain:
  ```
  appname/
  ├── manifest.json
  ├── bin/
  ├── ui/
  └── permissions.cfg
  ```
- Installed into `/app/appname/` at runtime

---

## 📂 Shell Commands Plan (`NeoShell`)

- Built-in commands:
  - `neo install <pkg>` – install .nxpkg
  - `neo status` – system monitor
  - `neo reboot` – restart system
  - `sudo` – custom user privilege escalation
  - `edit` – launch in-terminal file editor
- All stored in `/shell/commands/`

---

## 🔒 Permissions Model

- Root user can only access `system/` via `neosh` with password
- All apps installed by user live in `/app/` (sandboxed)
- System daemons stored in `/service/` and launched during boot

---

## 🕒 Future Goals

- Build O2N system center GUI (in `/app/o2n/`)
- Add `NXFS` file format
- Add GUI: initial framebuffer/VESA mode
- Add networking
- Add Neolyx Store backend

---

🧠 **All Neolyx contributors must read and follow this document.**\
For questions, contact project owner or chat with the Neolyx AI.

