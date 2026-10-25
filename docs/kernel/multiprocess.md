# Multiple user processes

The desktop is no longer the only program in ring 3. Every user process has
its own address space and kernel stack. The kernel schedules processes
round-robin, and programs can be loaded from disk with `sys_spawn` and
`sys_exec`.

## Address spaces

| Range | What lives there | Shared? |
|-------|------------------|---------|
| 0 – 16MB | Kernel image, boot data | Identity map, supervisor only |
| 16MB – 256MB | **User window**: program image, heap, stack | Private per process |
| 256MB – 4GB | Kernel heap, page tables, kernel stacks, MMIO | Identity map, supervisor only |
| Framebuffer | Linear framebuffer | Identity map with `PAGE_USER` (desktop compositor) |

- `kernel/src/mm/addrspace.c` builds a private PML4 → PDPT → PD for each
  process. The PD entries for the window point to private page tables, and
  all other entries are copied from the kernel tables.
- The physical range 16MB – 256MB is reserved in `pmm_init()` and never
  allocated, so the kernel never reaches a frame through an identity address
  that a process has remapped. It is also unmapped from the kernel-only
  tables, so a stray kernel access faults instead of corrupting memory.
- User frames come from the PMM (below 4GB, outside the window). The kernel
  writes them through their physical address, for example when it loads an
  ELF into a process that is not running.
- The kernel heap (16MB) is allocated from the PMM. It used to sit at the
  fixed address 0x2000000, inside the window.

Layout inside the window, for each process:

```
0x01000000  program image (link address, see tests/userland/ticker/linker.ld)
   brk      heap, grown on demand by sys_brk (up to 64MB)
0x0FF7F000  user stack (512KB, mapped eagerly)
0x0FFFF000  guard page (unmapped)
```

## Scheduling

- Each process has a 16KB kernel stack. SYSCALL and ring-3 interrupts run on
  it (`g_kernel_rsp0` and TSS.RSP0 follow the running process).
- `sched_switch` (arch.S) switches kernel stacks. CR3, RSP0 and the 512-byte
  FXSAVE area are switched with it.
- The kernel is not preemptible. Syscalls run with interrupts masked and give
  up the CPU only in `sleep`, `yield`, `wait` and `exit`. The timer (PIT,
  1000Hz, programmed just before the scheduler starts) preempts ring-3 code
  every 10 ticks.
- After boot, the boot context becomes the idle thread (PID 0, `sti; hlt`).
- A process that faults in ring 3 is terminated. Other processes keep
  running. A fault in a system process (the desktop) is still reported to
  the fault router as a service fault.

## Entry ABI

A new program starts at `e_entry` with:

- `RDI = argc`, `RSI = argv`
- `RSP % 16 == 8`, and `[RSP]` holds a zero return address, so a C
  `void _start(long argc, char **argv)` behaves as if it had been called
- above that are `argc`, then `argv[0..argc-1]`, then NULL, then an empty envp

`SYSCALL` preserves every register except `RAX` (the result), `RCX` and
`R11`. Arguments go in `RDI, RSI, RDX, R10, R8`.

Programs must be static `ET_EXEC` ELF64 files, linked inside the window.
`PT_INTERP` and `ET_DYN` files are rejected.

## Syscalls

| # | Call | Notes |
|---|------|-------|
| 13 | `spawn(path, argv, flags)` | **New.** Starts a program from disk as a new process and returns its PID. `flags & SPAWN_WAITABLE` keeps the child for `wait`. Otherwise the child is detached and freed when it exits. Every call is audit-logged. |
| 8 | `exec(path, argv, envp)` | Replaces the caller's image. Returns a negative errno on failure, and does not return on success. `envp` is ignored for now. |
| 9 | `wait(pid, status*)` | Blocks until a waitable child exits. |
| 2 | `sleep(ms)` | Really sleeps; other processes run in the meantime. |
| 4 | `yield()` | Gives up the rest of the time slice. |
| 10 | `brk(addr)` | Per process. Maps zero-filled pages on demand. |
| 7 | `fork()` | Returns `-ENOSYS`. Use `spawn`. |

User pointers are checked against the caller's page tables (present, user,
and writable where needed) before the kernel touches them.

## Launching from the desktop

The app drawer lists every file in `/apps` on the boot disk (FAT, 8.3 names)
ahead of the built-in entries. Selecting one calls `nx_spawn()`. The image
build installs the sample program `tests/userland/ticker` as `/apps/ticker`.

## Known limitations

- The file descriptor table is still global, shared by all processes.
- Input is not routed by focus. A second program that polls input competes
  with the desktop for events.
- Any user process can write the framebuffer, which is mapped with
  `PAGE_USER`.
- Kernel code reachable from syscalls must not use SSE. Only the per-process
  FXSAVE area is switched, and the 3D and audio-spatial code does use XMM
  registers.
- Pages freed by shrinking the heap stay mapped until the process exits.
- Single CPU only (the syscall scratch slot and the scheduler assume this).
