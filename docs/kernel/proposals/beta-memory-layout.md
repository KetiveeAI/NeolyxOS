# Proposal: use RAM above 4 GB (memory layout for the beta, S9)

Status: **proposal, not implemented.** No kernel code changes with this
document. It asks for owner decision D7 and is part of the Beta ABI batch
(D2, 2026-11-06). Stream: S9 of
`/mnt/project-files/beta-plan/parallel-workstreams.md`.
Checked against: `main` at c1b2f63 (PR #49), plus PR #50 (PCI 64-bit BARs).

## 1. The problem

The kernel reaches all physical memory through an identity map
(virtual address = physical address). `paging_init()` builds that map for
0-4 GB only. The physical memory manager does not respect the same limit:

| Piece | Today on `main` |
|-------|-----------------|
| Kernel identity map | 0-4 GB, 2 MB pages, supervisor-only (`paging.c`, `paging_init`) |
| PMM | Tracks up to 16 GB (`MAX_PHYSICAL_MEMORY`, `pmm.h`) and marks every usable page in that range free, including pages above 4 GB |
| Allocation order | Lowest free page first (`pmm_alloc_page`) |
| Who touches a frame | The kernel, always through its identity address: page tables (`(pte_t *)phys`), the kernel heap, process frames written by `addrspace.c`, shm objects, DMA buffers |

So the first allocation after low memory runs out returns a page above
4 GB, and the kernel faults the first time it touches it. How much low
memory there is depends on the machine, not on how much RAM it has:

| Machine (8 GB) | RAM below 4 GB | RAM above 4 GB (counted free, not mapped) |
|----------------|----------------|-------------------------------------------|
| QEMU q35 | about 2.0 GB | 6 GB (`0x100000000-0x280000000`) |
| QEMU pc (i440fx) | about 3.0 GB | 5 GB (`0x100000000-0x240000000`) |
| Typical PC | 2-3.5 GB (the PCI hole takes the rest) | the rest |

Source: the PMM lines of `/mnt/project-files/beta-plan/S9/pci64-q35-8G.log`
and `pci64-pc-8G.log` (`[PMM] Ready: 7942 MB free`).

Reproduced on q35 with 8 GB (a temporary test patch, not committed, that
takes pages from the PMM after the kernel page tables are active): after
454,965 pages (about 1.7 GB) the PMM returned `0x100000000`, and the first
write to it was a kernel page fault (`CR2 0x0000000100000000`,
`KERNEL EXCEPTION`). Log: `/mnt/project-files/beta-plan/S9/repro-pmm-above-4G-q35.log`.

Today nothing allocates that much, so it has not shown up. With apps out of
process (S2), a C library with `mmap` (S3, S4) and larger windows, it will:
the symptom is a kernel page fault (`KERNEL EXCEPTION`) at an address
above `0x100000000`, at a time that depends on what ran before.

## 2. Options

### Option A (recommended): extend the identity map to all RAM

At `paging_init()`, identity-map every RAM range from the UEFI memory map,
not just 0-4 GB. Virtual still equals physical, so **no other kernel code
changes**: page tables, the heap, `addrspace.c`, shm and drivers keep
using physical addresses as pointers.

Rules:

1. **Only RAM is mapped above 4 GB.** Ranges of type conventional, loader,
   boot services, ACPI reclaim, ACPI NVS and runtime services are mapped
   write-back. Holes and MMIO are not mapped (a write-back mapping over
   device memory allows speculative reads of device registers). Device
   ranges above 4 GB keep using `paging_mmio_register()` from PR #50
   (uncached).
2. **Page size:** 1 GB pages where the CPU supports them (CPUID
   `0x80000001` EDX bit 26) and the range covers a whole aligned 1 GB;
   2 MB pages where it covers a whole aligned 2 MB; 4 KB pages at the
   edges. Cost of the tables for 64 GB of RAM: with 1 GB pages, one page
   (the PDPT); with 2 MB pages only, 32 PDs = 128 KB.
3. **Kernel-only, as today.** No `PAGE_USER` on these entries. The user
   window (16-256 MB) and its reserved physical alias are unchanged.
4. **Built before any process exists.** Process address spaces copy the
   kernel's top-level entries when they are created
   (`addrspace_create`), and every process is created after
   `paging_init()`, so all of them see the whole map. Memory that
   appears later (hot-add) is out of scope.
5. **The shm region stays free.** The per-process shared region is PML4
   slot 128 (64-64.5 TiB). RAM there would need a 64 TiB machine; if a
   memory map ever reaches it, RAM from that address up is left out and
   logged, and the PMM is told to ignore it.
6. **PMM limit raised to 64 GB for the beta.** `MAX_PHYSICAL_MEMORY`
   becomes 64 GB (the bitmap grows from 512 KB to 2 MB of `.bss`; the
   kernel still ends far below the user window at 16 MB). RAM above
   64 GB is ignored and logged once. The PMM and the identity map use the
   same limit, so the PMM never hands out a page the kernel cannot touch.
7. **DMA below 4 GB.** Some devices only reach 32-bit addresses (AHCI
   without `CAP.S64A`, any controller in 32-bit mode). New
   `pmm_alloc_pages_below(count, limit)` returns pages under `limit`.
   Drivers use it for DMA buffers when the device has no 64-bit support.
   Today every DMA allocation happens early, while low memory is free, so
   this only matters later; it is listed so S8 (USB) and S9 (NVMe, AHCI)
   use it from the start.

What stays the same: the kernel image at 1 MB, the user window,
`AS_SHARED_BASE`, `NeolyxBootInfo`, every syscall, every userspace
address. Userspace sees only more free memory.

### Option B: cap memory at 4 GB

Set the PMM limit to 4 GB (one line). Safe and simple, but a VM or PC with
8 GB gets 2-3.5 GB, and Task Manager would show less memory than the
machine has. Option A is the long-term layout, and B is work that would
be thrown away.

### Not proposed: a higher-half direct map

Moving the kernel to `0xFFFF8000...` with a separate direct map is the
usual design for large kernels, but it changes every place that uses a
physical address as a pointer (page tables, `addrspace.c`, shm, drivers).
That is too large and risky for the beta. Option A keeps the door open:
a later higher-half move only changes how a physical address becomes a
pointer.

## 3. Recommendation

**Option A**, implemented by S9 by 2026-11-14:

1. `pmm.h`: `MAX_PHYSICAL_MEMORY` 64 GB; log RAM ignored above it.
2. `paging.c`: map RAM ranges from the memory map at `paging_init()`
   (rules 1-5), log the ranges and the page sizes used.
3. `pmm.c`: `pmm_alloc_pages_below()`.
4. Tests (logs to `/mnt/project-files/beta-plan/S9/`):
   - q35 `-m 8G` and `-m 16G`: a kernel stress test allocates every free
     page, writes and reads a pattern on each, logs the highest address
     used (above `0x200000000`), frees them all; no fault, free count
     back to the start value.
   - A user process allocates more than 2 GB (shm objects until
     `S3`'s `mmap` lands), touches every page, exits; the frames return.
   - Boot to desktop on q35 and pc with `-m 2G`, `8G`, `16G`, and `64G`.
   - Page-table cost logged at boot.

If the owner prefers Option B, S9 ships it in one small PR instead, and
the stress test above checks that no page above 4 GB is ever returned.

## 4. Until D7 is decided

Nothing changes on `main`. The fault in section 1 is possible today but
needs more than about 2 GB of allocations on q35 (3 GB on pc), which no
current workload reaches.

## 5. Compatibility

- No syscall, struct or ABI flag changes; no `NX_ABI_MINOR` change.
- Memory layout change (needs sign-off under `CLAUDE.md`): the kernel
  identity map grows from 0-4 GB to all RAM up to 64 GB.
- S1's change to map the framebuffer without `PAGE_USER` (PR #48, A1) is
  independent. A GOP framebuffer above 4 GB is device memory, so it goes
  through `paging_mmio_register()`, not the RAM map.
