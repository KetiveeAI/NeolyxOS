# NeolyxOS Desktop Crash Analysis

## Problem Summary
The desktop process crashes with a General Protection Fault at RIP=0x010149F4 during `desktop_init()`.

## Boot Sequence (Working)
1. ✅ Bootloader loads kernel
2. ✅ Kernel initializes (GDT, IDT, PMM, VMM, heap, drivers)
3. ✅ RAMFS mounted with desktop binary
4. ✅ Desktop ELF loaded from `/ramfs/bin/desktop`
5. ✅ Desktop heap set up (64MB at 0x013F7000-0x053F7000)
6. ✅ Heap region mapped with USER flag
7. ✅ Desktop process created (PID=2)
8. ✅ Transition to Ring 3 userspace successful
9. ✅ Desktop _start() executes
10. ✅ First syscall (SYS_FB_INFO) succeeds
11. ❌ **CRASH in desktop_init() at 0x010149F4**

## Crash Details
```
Exception: General Protection Fault
Vector: 13  Origin: USERSPACE (Ring 3)
Error Code: 0x0000000000000000
RIP: 0x00000000010149F4
CS:  0x000000000000002B (Ring 3 code segment)
RFLAGS: 0x0000000000000212
RSP: 0x000000000547EE38
```

## What We Know
- Crash happens BEFORE any BRK syscalls (no `[BRK]` messages in log)
- Crash happens after printing "[DESKTOP] Initializing desktop shell (userspace)..."
- This means the crash is in the early part of `desktop_init()` before `init_framebuffer()` calls `nx_malloc()`

## Likely Causes
1. **Invalid function pointer** - Calling through a NULL or invalid pointer
2. **Uninitialized global variable** - Accessing uninitialized BSS data
3. **Stack corruption** - Stack pointer or frame pointer issue
4. **Missing symbol** - Undefined reference that wasn't caught at link time

## Next Steps
1. Disassemble desktop binary at address 0x010149F4 to see what instruction is failing
2. Check desktop_shell.c line-by-line to find what happens before init_framebuffer()
3. Add more debug prints in desktop_init() to narrow down the exact line
4. Check if there are any function calls or global variable accesses before the crash

## Files to Check
- `neolyx-os/desktop/shell/desktop_shell.c` - desktop_init() function (line ~1829)
- `neolyx-os/desktop/shell/main.c` - _start() and desktop_init() call
- `neolyx-os/desktop/build/desktop.elf` - Binary to disassemble

## Heap Status
The heap IS properly initialized:
- Start: 0x013F7000
- End: 0x053F7000  
- Size: 64MB
- Mapped with USER flag: ✅
- syscall_set_desktop_heap() called: ✅
