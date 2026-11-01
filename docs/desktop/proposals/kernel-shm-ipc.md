# Proposal: kernel shared memory and IPC (ABI v1.1)

Status: **proposal, not implemented. Needs the project owner's sign-off.**
No kernel code changes until it is approved.
Plan: weeks 1 and 2 of the 1.0 Preview plan (nxserver window server).
Audited against `main` at f6bfc76 (2026-11-01).

## Why

`docs/STATUS.md` lists shared memory and IPC as **Missing**. Without them a
second process cannot get a window: `gui/nxrender_c/src/nxserver/nxserver_compositor.c`
expects each window to carry an `shm_buffer`, but nothing in the kernel can
create or share one. This proposal defines the smallest kernel interface that
lets apps draw into their own memory and hand frames to one compositor
(nxserver), with each app's memory and handles dying with its process.

## What exists today (checked in the code)

| Piece | Today |
| --- | --- |
| Dispatcher | `nx_syscall.c`, table in `syscall_table.def`, `NX_SYSCALL_MAX` = 256, handlers take 5 arguments (RDI, RSI, RDX, R10, R8; `arch.S` passes exactly these) |
| Free numbers | 140 to 149 are unused; `SYS_MAX` in `syscall.h` is 150 (it only sizes the legacy table in `syscall.c`) |
| User memory | One private window per process, `0x0100_0000`–`0x1000_0000` (16 MB to 256 MB), 4 KB pages. Physical RAM at the same addresses is reserved in the PMM so the kernel identity map never aliases a user page (`mm/addrspace.c`) |
| Blocking | `process_block`/`process_unblock`, `process_sleep` with `wake_tick` (PIT, 1 tick = 1 ms). One CPU, kernel not preemptible |
| Threads, fork | No threads; `fork` returns `-ENOSYS`. So a process never runs two syscalls at once |
| Privilege | `PROC_FLAG_SYSTEM` marks the desktop; SIP uses it. `current_priv` is still one global |
| NX bit | `EFER.NXE` is not enabled, so every readable user page is executable |

## Design in one paragraph

Kernel objects (shared memory, channel endpoints, ports) are reached only
through **handles**: small per-process integers that carry **rights**. A
handle cannot be guessed or forged, and it is moved to another process only
by sending it in a channel message. Shared memory is a fixed-size set of
zeroed pages that each holder maps into a new per-process **shared region**.
Channels carry small messages plus up to 4 handles; sends never block, so a
slow or hostile client can never stall the compositor. One call, `obj.wait`,
blocks on many handles with a timeout. Named **ports** let any app find
nxserver (`nx.display`); only system processes may register `nx.*` names.
Every object is freed when its last handle, mapping or queued reference is
gone, including when the owner dies.

## Syscall table

All new. Numbers 140 to 147 are the ones the release plan reserved. Three
port calls do not fit below 150, so this proposal takes 149 to 152 and raises
`SYS_MAX` to 160. 148 stays reserved for `fb.flip_rects` (week 5, separate
proposal).

| # | Name | Signature | Returns |
| --- | --- | --- | --- |
| 140 | `handle.close` | `int sys_handle_close(nx_handle_t h)` | 0 |
| 141 | `shm.create` | `nx_handle_t sys_shm_create(uint64_t size, uint32_t flags)` | handle |
| 142 | `shm.map` | `void *sys_shm_map(nx_handle_t h, uint32_t prot, uint64_t *out_size)` | address |
| 143 | `shm.unmap` | `int sys_shm_unmap(void *addr)` | 0 |
| 144 | `chan.create` | `int sys_chan_create(nx_handle_t out[2], uint32_t flags)` | 0 |
| 145 | `chan.send` | `int sys_chan_send(nx_handle_t h, const nx_chan_msg_t *msg)` | 0 |
| 146 | `chan.recv` | `int sys_chan_recv(nx_handle_t h, nx_chan_msg_t *msg)` | 0 |
| 147 | `obj.wait` | `int sys_obj_wait(nx_wait_item_t *items, uint32_t count, uint64_t timeout_ms)` | ready count |
| 148 | *(reserved: `fb.flip_rects`)* | not part of this proposal | |
| 149 | `handle.dup` | `nx_handle_t sys_handle_dup(nx_handle_t h, uint32_t rights)` | handle |
| 150 | `port.listen` | `nx_handle_t sys_port_listen(const char *name, uint32_t flags)` | port handle |
| 151 | `port.connect` | `nx_handle_t sys_port_connect(const char *name, uint32_t flags)` | channel handle |
| 152 | `port.accept` | `nx_handle_t sys_port_accept(nx_handle_t port, uint32_t *out_pid)` | channel handle |

All calls are `PRIV_USER` in the dispatcher; the checks below are done by
the handlers. Every `flags` argument must be 0 in v1.1 (`-EINVAL` otherwise),
so later versions can add flags without breaking old callers. Errors are
negative errno values as today. Two new codes are added to `syscall.h`, with
their POSIX numbers like the existing ones: `EMSGSIZE` (90) and `ETIMEDOUT` (110).

## Shared types (new header `kernel/include/core/nx_ipc.h`)

Fixed-width types only and no kernel includes, so the same header serves the
kernel, `desktop/include`, libnxwindow and the REOX runtime. Pointers inside
structs are `uint64_t` so the layout is the same from every language.
Each struct gets a `_Static_assert` on its size.

```c
typedef int32_t nx_handle_t;          /* > 0 valid. 0 is never a handle. */
#define NX_HANDLE_INVALID      0

/* Rights carried by a handle */
#define NX_RIGHT_READ          0x01   /* shm: map readable. chan: recv. port: accept. All: wait */
#define NX_RIGHT_WRITE         0x02   /* shm: map writable. chan: send */
#define NX_RIGHT_MAP           0x04   /* shm: may be mapped */
#define NX_RIGHT_TRANSFER      0x08   /* may be sent in a channel message */
#define NX_RIGHT_DUPLICATE     0x10   /* may be passed to handle.dup */
#define NX_RIGHTS_SAME         0x80000000u /* handle.dup: keep the same rights */

/* shm.map prot */
#define NX_SHM_PROT_READ       0x1
#define NX_SHM_PROT_WRITE      0x2    /* on x86 a writable page is also readable */

/* Signals (obj.wait) */
#define NX_SIGNAL_READABLE     0x1    /* chan: a message is queued. port: a connection is pending */
#define NX_SIGNAL_WRITABLE     0x2    /* chan: the peer's queue has room */
#define NX_SIGNAL_PEER_CLOSED  0x4    /* chan: the other endpoint is gone */

#define NX_WAIT_FOREVER        UINT64_MAX

/* Limits (v1.1). Exceeding any of them is an error, never a silent clamp. */
#define NX_HANDLE_MAX          256          /* handles per process */
#define NX_SHM_SIZE_MAX        (64ULL << 20) /* 64 MiB per object (5K at 32 bpp fits) */
#define NX_SHM_PROC_QUOTA      (256ULL << 20) /* shm bytes created and alive, per process */
#define NX_SHM_MAP_MAX         128          /* live mappings per process */
#define NX_CHAN_DATA_MAX       1024         /* bytes per message */
#define NX_CHAN_HANDLES_MAX    4            /* handles per message */
#define NX_CHAN_QUEUE_MAX      64           /* queued messages per endpoint */
#define NX_CHAN_PROC_QUEUED    (1ULL << 20) /* bytes a process has sent that are not yet received */
#define NX_WAIT_ITEMS_MAX      64
#define NX_PORT_NAME_MAX       31           /* bytes, excluding the NUL */
#define NX_PORT_BACKLOG        16           /* pending connections per port */
#define NX_PORT_MAX            32           /* ports system-wide */

typedef struct {
    uint64_t data;          /* user pointer to the message bytes */
    uint64_t handles;       /* user pointer to an nx_handle_t array */
    uint32_t data_len;      /* send: bytes to send. recv: in = capacity, out = bytes received (or needed) */
    uint32_t handle_count;  /* send: handles to move. recv: in = capacity, out = handles received (or needed) */
    uint32_t sender_pid;    /* recv out: PID that sent the message, filled by the kernel. send: must be 0 */
    uint32_t flags;         /* must be 0 */
} nx_chan_msg_t;            /* 32 bytes */

typedef struct {
    nx_handle_t handle;
    uint32_t    signals;    /* in: signals that end the wait */
    uint32_t    observed;   /* out: all signals asserted on return */
    uint32_t    reserved;   /* must be 0 */
} nx_wait_item_t;           /* 16 bytes */
```

## Semantics

### Handles

- A handle value is an index into the caller's handle table plus a
  generation count, so a closed and reused slot never matches an old value
  (stale handle gives `-EBADF`, not someone else's object). Values are always
  positive; they are not file descriptors and the two never mix.
- **140 `handle.close(h)`**: drops the handle. The object lives on if other
  handles, mappings or queued messages still reference it.
  Errors: `-EBADF`.
- **149 `handle.dup(h, rights)`**: new handle to the same object. `rights`
  must be a subset of the original's, or `NX_RIGHTS_SAME`. Needs
  `NX_RIGHT_DUPLICATE`. Used to keep a buffer while sending a copy, or to
  hand out a read-only copy. Errors: `-EBADF`, `-EACCES` (no DUPLICATE right,
  or rights not a subset), `-EINVAL` (unknown bits), `-EMFILE` (table full).
- `spawn` and `exec` do not pass handles on. A new program starts with an
  empty table; `exec` closes all handles and mappings first. A program finds
  services through ports.

Default rights: `shm.create` gives READ, WRITE, MAP, TRANSFER, DUPLICATE.
Channel endpoints get READ, WRITE, TRANSFER (no DUPLICATE, so each endpoint
has exactly one holder and "peer closed" is exact). Port handles get READ
only: a port cannot be sent or duplicated.

### Shared memory

- **141 `shm.create(size, flags)`**: allocates `size` rounded up to 4 KB, all
  pages zeroed, physically scattered, below 4 GB so the kernel can reach them.
  The size is fixed for the object's life: it cannot shrink, so a process that
  has mapped someone else's buffer never faults on it. Pages are charged to
  the creating process until the object is freed. Refused if the request
  would leave less than 64 MiB of free physical memory.
  Errors: `-EINVAL` (size 0, above `NX_SHM_SIZE_MAX`, flags not 0),
  `-ENOMEM` (out of memory, quota or system floor; the log says which),
  `-EMFILE`.
- **142 `shm.map(h, prot, out_size)`**: maps the whole object into the
  caller's shared region and returns the address (page-aligned). The kernel
  picks the address. `out_size` (may be NULL) receives the object size, so
  nxserver checks a client buffer is at least `stride * height` before
  reading it. Needs MAP and READ; `NX_SHM_PROT_WRITE` also needs WRITE. The
  same object may be mapped more than once. Errors: `-EBADF`, `-EACCES`,
  `-EINVAL` (prot 0 or unknown bits, handle is not shm), `-ENOMEM` (region
  full, `NX_SHM_MAP_MAX` reached, no page-table memory), `-EFAULT` (bad
  `out_size`; the mapping is undone).
- **143 `shm.unmap(addr)`**: `addr` must be a base returned by `shm.map`;
  the whole mapping goes. The TLB entries are flushed before return. A
  mapping keeps its object alive after the handle is closed, so a client can
  close its handle right after mapping. Errors: `-EINVAL`.

### Channels

- **144 `chan.create(out, flags)`**: makes two connected endpoints and writes
  their handles to `out[0]` and `out[1]`. Errors: `-EFAULT` (nothing is
  created), `-EMFILE`, `-ENOMEM`.
- **145 `chan.send(h, msg)`**: copies `data_len` bytes and moves
  `handle_count` handles into the peer's queue. It is atomic: if anything is
  wrong, nothing is queued and no handle moves. Moved handles disappear from
  the sender and keep their rights. **Send never blocks**; on a full queue it
  returns `-EAGAIN` and the sender waits for `NX_SIGNAL_WRITABLE`.
  Errors: `-EBADF`, `-EACCES` (no WRITE right, or a carried handle lacks
  TRANSFER), `-EINVAL` (too much data, too many handles, a handle listed
  twice, `h` itself in the list, nonzero `sender_pid` or `flags`), `-EFAULT`,
  `-EAGAIN` (peer queue full, or the sender's `NX_CHAN_PROC_QUEUED` budget is
  used), `-EPIPE` (peer closed).
- **146 `chan.recv(h, msg)`**: takes the oldest message. Data is copied into
  `msg->data`, and handles are installed in the caller's table and written to
  `msg->handles`. `sender_pid` is filled by the kernel and cannot be forged.
  Never blocks. Errors: `-EBADF`, `-EACCES`, `-EAGAIN` (empty), `-EPIPE`
  (empty and peer closed; messages sent before the close are still
  delivered), `-EMSGSIZE` (a buffer is too small: the needed sizes are written
  to `data_len`/`handle_count` and the message stays queued), `-EMFILE`
  (the handles do not fit; the message stays queued), `-EFAULT`.
- Messages are delivered in order, one at a time; there is no partial read.
  Handles in messages that are never received are closed when the receiving
  endpoint is destroyed.

### Waiting

- **147 `obj.wait(items, count, timeout_ms)`**: level-triggered. Returns at
  once if any item already has one of its `signals` asserted; otherwise
  blocks until one does or the timeout passes. On return every item's
  `observed` holds all its currently asserted signals. Returns the number of
  items whose `observed & signals` is nonzero. `timeout_ms` = 0 polls,
  `NX_WAIT_FOREVER` waits without limit; resolution is 1 ms (PIT).
  Needs READ on each handle. Shared-memory handles have no signals and give
  `-EINVAL`. Errors: `-EINVAL` (count 0 or above 64, unknown signal bits,
  reserved not 0), `-EBADF` (nothing waited), `-EACCES`, `-EFAULT`,
  `-ETIMEDOUT`.
- nxserver's loop is: `obj.wait` on its port and all client channels with a
  timeout of the time left to the next frame, then `input.poll`, composite,
  flip. Input becoming a handle is not part of v1.1.

### Ports (service discovery)

- **150 `port.listen(name, flags)`**: registers a name and returns a port
  handle. Names are 1 to 31 bytes of `a-z 0-9 . _ -`, matched exactly.
  Names starting with `nx.` may only be registered by `PROC_FLAG_SYSTEM`
  processes, so no app can pose as `nx.display`. The name is released when
  the port handle is closed or its owner exits. Audited.
  Errors: `-EFAULT`, `-EINVAL` (bad name), `-EPERM` (`nx.` from a non-system
  process), `-EEXIST` (name in use), `-ENFILE` (32 ports in use), `-EMFILE`.
- **151 `port.connect(name, flags)`**: creates a channel, returns one end to
  the caller and queues the other on the port. Does not wait for the server
  to accept; the client may send right away (up to the queue limit). If the
  port closes before accepting, the client sees `NX_SIGNAL_PEER_CLOSED`.
  Audited. Errors: `-EFAULT`, `-EINVAL`, `-ENOENT` (no such port),
  `-EAGAIN` (backlog full), `-EMFILE`, `-ENOMEM`.
- **152 `port.accept(port, out_pid)`**: takes the oldest pending connection
  and returns the server-side endpoint; `out_pid` (may be NULL) receives the
  PID that connected. Never blocks; wait for `NX_SIGNAL_READABLE` on the
  port. Errors: `-EBADF`, `-EACCES`, `-EINVAL` (not a port), `-EAGAIN`,
  `-EMFILE`, `-EFAULT`.

### Lifetime and process exit

Each object counts its references: handles, mappings, messages carrying it,
and port backlog entries. It is freed when the count reaches zero, and shm
pages go back to the PMM at that moment. On exit or crash, `terminate()`
closes every handle and removes every mapping before `addrspace_destroy`. So
a crashed app loses its window and its memory returns, while a buffer that
nxserver still has mapped stays valid until nxserver unmaps it.

## Memory layout change (needs sign-off)

Shared mappings do **not** go in the existing user window. That window is
240 MB, of which 64 MB is the heap reserve, and RAM at the same physical
addresses is reserved, so growing it costs real memory. One double-buffered
4K window is 66 MB, so nxserver would run out after two.

Proposal: a per-process **shared region** of 512 GiB at
`0x0000_4000_0000_0000` to `0x0000_4080_0000_0000` (PML4 slot 128).

- It is outside the identity map, so it needs no PMM reservation. The PDPT
  and page tables for it are private to the process and allocated on the
  first `shm.map`.
- Slot 128 sits above any physical address a CPU with 46 or fewer physical
  address bits can produce, so an identity-mapped MMIO range (such as a GOP
  framebuffer placed above 4 GB) cannot land there. `addrspace_init` checks
  that the slot is empty in the kernel tables; if it is not, shared memory
  is disabled with a logged error instead of mapping over it.
- Each mapping is 2 MiB-aligned with an unmapped gap after it, and addresses
  are reused after `shm.unmap`. Leaf entries carry a software bit (bit 9,
  "shared") so teardown never returns a shared frame to the PMM as if it
  were private.
- `addrspace_check_range` (used by `syscall_validate_ptr`) accepts the
  region, so pointers into shared buffers can be passed to other syscalls.
- `addrspace_destroy` also tears down the region (dropping references, not
  freeing shared frames directly).

## Logging and audit

Logged to serial: every shm create and free (PID, size), every port listen,
connect, accept and close, every limit or quota hit, and every `-EPERM` or
`-EACCES` with PID and call. `port.listen` and `port.connect` carry
`NX_FLAG_AUDIT`. Successful `chan.send`/`chan.recv`/`obj.wait`/`shm.map` are
not logged in normal builds: nxserver makes hundreds of them per second and
serial output would stall the single CPU. An `IPC_TRACE` build flag logs
them for debugging.

## Versioning

- `NX_ABI_MINOR` goes from 0 to 1 (only new calls; nothing existing changes).
- `nx_abi_info_t.flags` gets `NX_ABI_FLAG_SHM` (0x0001) and `NX_ABI_FLAG_IPC`
  (0x0002), so libnxwindow can check with `abi.query` and fail cleanly on an
  older kernel. The flags are cleared if the shared region could not be set
  up.
- Once 1.0 ships these numbers, signatures and struct layouts are frozen.

## What this changes for existing code

- No existing syscall changes number, signature or behaviour.
- `SYS_MAX` 150 to 160 (sizes the legacy table in `syscall.c`).
- `process_t` gets new fields appended at the end (handle table pointer,
  mapping list, shm and queue byte counters), as the multi-process work did,
  so earlier offsets stay the same.
- Memory layout gains the shared region above.

## Kernel test (week 2 exit)

A userland program `tests/userland/ipc_test`, run in QEMU through the normal
image, checks:

1. A parent registers `test.ipc` and spawns a child. The child connects,
   creates a 4 MiB shm, fills it with a pattern, and sends the handle. The
   parent maps it read-only, checks the pattern, and replies.
2. The parent kills the child, unmaps and closes, and the PMM free page count
   is back to its value before the test.
3. Hostile cases each give the documented error and leave the kernel
   running: stale handle after close, wrong handle type, oversized message,
   full queue, too-small receive buffer (then a retry succeeds), sending a
   port handle, `nx.` name from a non-system process, `obj.wait` with 65
   items, `shm.create` above the quota.

## Not in this proposal

- `fb.flip_rects` (148), display-server privilege on `fb.map`/`fb.flip`/
  `cursor.set`/`input.poll`, the per-process fd table, anonymous
  `mmap`/`munmap` (11, 12): separate week 1 and week 5 items.
- The nxserver window protocol (create window, attach buffer, commit with
  damage, frame done, input events). It runs in userspace over these
  channels and gets its own document in week 3.
- Enabling `EFER.NXE`. Without it shared pages are executable like every
  other user page. Recommended as a separate week 1 change.

## Alternatives considered

- **Named shm keys** (`shm_key` in `nxserver_compositor.h`): any process
  that guesses a key could map another app's window. Handles are
  unforgeable, so the key field is dropped.
- **Blocking send**: lets one client freeze the compositor. Sends fail with
  `-EAGAIN` instead, and only `obj.wait` blocks.
- **Passing a channel at spawn instead of ports**: only works for apps the
  desktop launches itself, not for programs started from Terminal. Ports
  cover both.
- **Growing the user window for shm**: costs reserved RAM for every byte and
  still caps nxserver far below its needs (see the memory layout section).
