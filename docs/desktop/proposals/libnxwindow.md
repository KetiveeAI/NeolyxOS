# Proposal: libnxwindow, the client window library (week 4)

Status: **approved by the project owner on 2026-11-01 ("approve as
written").** Changing it needs the owner's sign-off again. Implemented so
far: step 1, libnxwindow in C (`sdk/nxwindow/nxwindow.{h,c}`, host test
`tests/host/nxwindow_test.c`, QEMU test `tests/userland/nxwtest`).
Plan: week 4 of the 1.0 Preview plan ("libnxwindow, C and REOX"). Builds on
the approved window protocol 1.0 (`nxserver-window-protocol.md`, merged in
PRs #24, #27 and #28) and the kernel IPC interface (`kernel-shm-ipc.md`,
ABI 1.2).
Audited against `main` at 9d7aafe (2026-11-01).

## Why

Since week 3 a separate process can open a window, but only by speaking the
raw protocol: `tests/userland/wintest` is 800 lines of hand-built messages,
shm bookkeeping and event parsing. No app should have to do that. Week 4
puts that work in one library, `libnxwindow`, so that:

- a **C app** opens a window, draws into its pixels and reads events with
  about ten calls, and
- a **REOX app** written with NxUI (`sdk/nxui`, for example
  `desktop/apps/Calculator.app`) runs in its **own window**, with the
  server's decorations, **without changing its source**.

Today NxUI's standalone path maps the whole framebuffer (`fb.map`), draws
its own window chrome on top of whatever the desktop shows and polls raw
input (`input.poll`). That path cannot coexist with the desktop. Week 5
(apps out of process) and the planned restriction of `fb.map`, `fb.flip`,
`cursor.set` and `input.poll` to system processes both need it replaced.

This document fixes what becomes public and hard to change later:

1. **The C API** of `libnxwindow` (`sdk/nxwindow/nxwindow.h`), which every
   NeolyxOS GUI app will link against.
2. **The buffering model** the library gives apps.
3. **How NxUI and REOX apps run on it**, including two changes to SDK
   interfaces (`reox_ffi_host_t` and one new NxUI call).

There are **no kernel changes, no ABI changes and no protocol changes**: the
library speaks protocol 1.0 exactly as approved.

## What exists today (checked in the code)

| Piece | Today |
| --- | --- |
| Protocol | `sdk/nxwindow/nxwin_proto.h`, protocol 1.0, frozen layouts with `_Static_assert` |
| Server | `desktop/shell/nxserver/nxserver.c`, port `nx.display`, server-side decorations, zero-copy shm buffers, damage-driven frames, `FRAME_DONE` once a commit is presented (also while minimized) |
| Raw client | `tests/userland/wintest/wintest.c`: its own syscall stubs, message building, reply matching, shm handling. Protocol conformance test, not reusable |
| Syscall wrappers | `desktop/include/nxsyscall.h` has `nx_handle_close`, `nx_shm_map/unmap`, `nx_chan_send/recv`, `nx_obj_wait`, `nx_port_listen/accept`. Missing for clients: `shm.create`, `port.connect`, `handle.dup`, `abi.query` |
| NxUI | `sdk/nxui/nxui.c`: REOX-visible handles over `gui/nxrender_c/src/reox_ffi.c`. Its NeolyxOS host maps the framebuffer, polls `input.poll` with relative mouse motion, flips the whole screen and sleeps 16 ms per frame, forever, even when idle |
| `reox_ffi` standalone loop | Creates an nxrender window manager on the whole screen, centres the window, draws nxrender's own title bar and close control, redraws everything every frame |
| Calculator.app | REOX, builds with `sdk/reox.mk` on the owner's machine (needs `reoxc` and `librx_neolyx.a` from the REOX repository); not on the boot image |
| Launching | The app drawer starts any `/apps/<name>` binary with `nx_spawn` (`desktop/shell/app_drawer.c`) |

## Decision 1: one static library, one connection per process

- **Where:** `sdk/nxwindow/nxwindow.h` (public) and `sdk/nxwindow/nxwindow.c`,
  next to the protocol header. `sdk/nxwindow/Makefile` builds
  `libnxwindow.a` with the standard freestanding userspace flags; `reox.mk`
  builds it into every REOX app like the other SDK support objects. NeolyxOS
  has no shared libraries, so apps link it statically.
- **One connection per process**, as the protocol already requires. The
  library keeps it in static state, so the API has no connection object:
  `nxw_connect()` once, then windows.
- **No heap.** Window state lives in a static table of
  `NXW_WINDOWS_PER_CLIENT` (8) entries; the event queue is a fixed ring.
  The library works in any freestanding app, with or without `nx_malloc`.
- **Single-threaded.** Every call must come from the same thread. NeolyxOS
  userspace has no threads today; this is stated so nobody assumes locking.
- **Dependencies:** only `nxwin_proto.h`, the kernel's `nx_ipc.h` and
  `desktop/include/nxsyscall.h`. The four missing wrappers
  (`nx_shm_create`, `nx_port_connect`, `nx_handle_dup`, `nx_abi_query`) are
  added to `nxsyscall.h`; they are thin calls to existing syscalls.

**Alternative:** a connection object (`nxw_conn_t *`) passed to every call.
More general, but the protocol allows one connection per process anyway, and
it makes the REOX binding harder. Not recommended for 1.x.

## Decision 2: single buffer, drawn only between FRAME_DONE and COMMIT

Each window has **one** shm buffer that the library allocates, maps
read-write, attaches and owns. The protocol's zero-copy rule (do not draw
after `COMMIT` until `FRAME_DONE`) is **enforced by the library**, not left
to each app:

- `nxw_frame_begin()` gives the app the pixels when the buffer is free and
  returns `-EBUSY` while a commit is still waiting for its `FRAME_DONE`.
- `nxw_frame_commit()` sends the damage and marks the buffer busy.
- The `NXW_EV_FRAME` event says the buffer is free again.

Timing: the server presents on its 16 ms tick and sends `FRAME_DONE` in the
same tick, so an app whose drawing takes less than a tick still reaches
60 frames per second with one buffer. The buffer keeps its contents between
frames, so an app redraws only what changed.

Resizes: on `CONFIGURE` the library records the new size. The next
`nxw_frame_begin()` creates a buffer of that size, attaches it, unmaps the
old one and sets `frame.full = 1` (the new buffer is zeroed; the app must draw
all of it). While the app has not drawn the new size, the server keeps
showing the old buffer, as the protocol says.

Known limit: the server can redraw a client window for its own reasons (the
cursor moving over it, a menu closing) while the app is drawing, so a slow
app can show a half-drawn frame of **its own** window for one tick. The
spec already allows this; it never affects other windows or the shell.

**Alternatives:**
- **Two buffers alternated with `BUF_ATTACH` every frame.** Works with
  protocol 1.0 and removes the half-drawn frames, but every frame costs the
  server a map and an unmap of the whole window and the client a handle
  duplicate. Not recommended.
- **Buffer slots in a protocol 1.1** (attach two buffers once, `COMMIT`
  names the slot). The right long-term fix; it is a protocol change, so it
  would come as its own proposal after the Preview.

## The C API (`sdk/nxwindow/nxwindow.h`), needs sign-off

Every call returns 0 (or a positive count) on success and a **negative
errno** on failure, using the kernel's values (`kernel/include/core/syscall.h`).
The kernel has no code for three library cases, so `nxwindow.h` defines them
with the customary numbers, which the kernel does not use: `NXW_EPROTO` (71,
the server refused the protocol version), `NXW_EISCONN` (106, already
connected) and `NXW_ENOTCONN` (107, not connected). The kernel's list is not
changed. Nothing fails silently: every
failure is also logged (see Logging). Pointers are checked for NULL, sizes
against the protocol limits, titles for length and NUL termination.

```c
#define NXWL_API_MAJOR 1          /* this header; additive changes only in 1.x */
#define NXWL_API_MINOR 0

typedef struct nxw_window nxw_window_t;   /* opaque, owned by the library */

typedef struct {
    uint16_t proto_major, proto_minor;    /* from WELCOME */
    uint32_t screen_w, screen_h;
    uint32_t max_windows;                 /* per client */
    uint32_t max_size;                    /* largest width or height */
} nxw_display_info_t;

typedef struct {
    const char *title;        /* UTF-8, at most 63 bytes plus the NUL */
    uint32_t    width, height;/* content size, 1..max_size */
    int32_t     x, y;         /* frame top-left, or NXW_POS_AUTO */
    uint32_t    flags;        /* NXW_WIN_RESIZABLE or 0 */
} nxw_window_desc_t;

typedef struct {
    uint32_t *pixels;         /* XRGB8888, row r starts at pixels + r * stride / 4 */
    uint32_t  width, height;  /* the window's current content size */
    uint32_t  stride;         /* bytes per row */
    int       full;           /* 1: new buffer, draw everything */
} nxw_frame_t;
```

### Connection

| Call | Does | Errors |
| --- | --- | --- |
| `int nxw_connect(nxw_display_info_t *info)` | `abi.query` (needs `NX_ABI_FLAG_SHM` and `NX_ABI_FLAG_IPC`), `port.connect("nx.display")`, `HELLO`, waits up to 2 s for `WELCOME`. `info` may be NULL | `-ENOSYS` kernel without IPC; `-ENOENT` no window server; `-NXW_EPROTO` server refused the version; `-ETIMEDOUT`; `-NXW_EISCONN` already connected |
| `void nxw_disconnect(void)` | Destroys every window, unmaps every buffer, closes the channel. Safe to call when not connected | none |
| `nx_handle_t nxw_connection_handle(void)` | The channel, for apps that wait on other objects too with `obj.wait` (`READABLE` means call `nxw_poll_event`) | `NX_HANDLE_INVALID` when not connected |

### Windows

| Call | Does | Errors |
| --- | --- | --- |
| `int nxw_window_create(const nxw_window_desc_t *d, nxw_window_t **out)` | `WIN_CREATE`, waits up to 2 s for `WIN_CREATED` or `ERROR`. The window appears at its first commit | `-EINVAL` bad size, flags or title; `-EMFILE` too many windows; `-NXW_ENOTCONN`; `-ETIMEDOUT`; `-EPIPE` server gone |
| `int nxw_window_destroy(nxw_window_t *w)` | `WIN_DESTROY`, unmaps its buffer. `w` is invalid afterwards | `-EBADF` not a live window |
| `int nxw_window_set_title(nxw_window_t *w, const char *title)` | `WIN_SET_TITLE` | `-EINVAL`, `-EBADF` |
| `int nxw_window_get_size(nxw_window_t *w, uint32_t *width, uint32_t *height, uint32_t *state)` | Current size and `NXW_STATE_*` from the last `CONFIGURE` (or the granted size). Any out pointer may be NULL | `-EBADF` |

Window pointers are checked against the library's table and a generation
count, so a stale or foreign pointer is rejected with `-EBADF` and logged,
never dereferenced.

### Frames

| Call | Does | Errors |
| --- | --- | --- |
| `int nxw_frame_begin(nxw_window_t *w, nxw_frame_t *f)` | Fills `f` with the buffer to draw into. On first use or after a size change, creates the shm buffer, maps it and sends `BUF_ATTACH` | `-EBUSY` waiting for `FRAME_DONE`; `-ENOMEM` shm quota or mapping limit; `-EBADF` |
| `int nxw_frame_commit(nxw_window_t *w, const nxw_rect_t *rects, uint32_t count)` | `COMMIT`. `count` 0 means the whole buffer. Rectangles are clipped to the buffer; empty ones are dropped; more than 16 are merged into their bounding box (logged once per window) | `-EINVAL` no `nxw_frame_begin` before it, or `rects` NULL with `count` > 0; `-EBADF`; `-EPIPE` |

### Events

```c
enum {
    NXW_EV_CONFIGURE = 1,     /* size or state changed: redraw at the new size */
    NXW_EV_CLOSE,             /* the user clicked close: save, then destroy */
    NXW_EV_KEY,
    NXW_EV_POINTER,
    NXW_EV_FRAME,             /* the last commit is on screen; the buffer is free */
    NXW_EV_ERROR,             /* the server refused an asynchronous request */
    NXW_EV_DISCONNECTED       /* the server went away; every window is gone */
};

typedef struct {
    uint32_t      type;       /* NXW_EV_* */
    nxw_window_t *window;     /* NULL for DISCONNECTED and connection-wide ERROR */
    union {
        struct { uint32_t width, height, state; } configure;
        struct { uint32_t commit_seq; uint64_t time_ms; } frame;
        struct { uint8_t scancode, ascii, pressed, modifiers; uint32_t time_ms; } key;
        struct { uint32_t kind; int32_t x, y; uint32_t buttons; int32_t wheel; uint32_t time_ms; } pointer;
        struct { uint16_t op; int32_t code; } error;
    };
} nxw_event_t;
```

| Call | Does |
| --- | --- |
| `int nxw_poll_event(nxw_event_t *ev)` | Never blocks. 1 an event, 0 none, negative errno on failure |
| `int nxw_wait_event(nxw_event_t *ev, uint64_t timeout_ms)` | Waits on the channel with `obj.wait`. 1 an event, 0 timeout, negative errno. `NX_WAIT_FOREVER` waits forever |

- `WELCOME`, `WIN_CREATED` and replies are consumed by the calls that wait for
  them; events that arrive meanwhile are queued, never lost.
- The queue holds 64 events. Pointer motion for the same window is coalesced
  (the newest wins). When the queue is full, the oldest motion event is
  dropped first; drops are counted and logged at most once per second, like
  the server.
- `PEER_CLOSED` on the channel produces one `NXW_EV_DISCONNECTED`; after it
  every window call returns `-NXW_ENOTCONN` and every buffer is unmapped.
- Sends that get `-EAGAIN` (the server's queue is full) wait for `WRITABLE`
  for up to 1 s, reading and queuing events while waiting, then fail with
  `-ETIMEDOUT`. The library never spins.

### Example

```c
nxw_display_info_t info;
if (nxw_connect(&info) != 0) return 1;
nxw_window_desc_t d = { "Hello", 320, 200, NXW_POS_AUTO, NXW_POS_AUTO, NXW_WIN_RESIZABLE };
nxw_window_t *w;
if (nxw_window_create(&d, &w) != 0) return 1;
int dirty = 1;
for (;;) {
    nxw_frame_t f;
    if (dirty && nxw_frame_begin(w, &f) == 0) {
        draw(&f);                              /* the app's own drawing */
        nxw_frame_commit(w, NULL, 0);
        dirty = 0;
    }
    nxw_event_t ev;
    if (nxw_wait_event(&ev, NX_WAIT_FOREVER) <= 0) break;
    if (ev.type == NXW_EV_CONFIGURE) dirty = 1;
    if (ev.type == NXW_EV_CLOSE || ev.type == NXW_EV_DISCONNECTED) break;
}
nxw_disconnect();
```

The library does not draw. C apps draw into `f.pixels` themselves or with
NXCanvas (`desktop/include/nxcanvas.h`), which already renders into any
XRGB8888 buffer.

### Logging

To serial, prefixed `[NXWIN]`: connect and disconnect (with the protocol
version), every window create and destroy (id, size), every buffer
allocation (size), every error returned to the app and every `ERROR` from
the server (op, code, window), dropped-event bursts. Frames and commits are
logged only in an `NXWIN_TRACE` build.

## Decision 3: NxUI and REOX apps run in a window

REOX apps use NxUI, so the REOX side of week 4 is **NxUI running on
libnxwindow**. REOX code keeps calling `nxui_window_create` and `nxui_run`;
nothing in `Calculator.app/src` changes.

- **`sdk/nxui/nxui.c` (NeolyxOS host):** connects with `nxw_connect`, creates
  the window from `nxui_window_create`'s title and size, and hands the
  window's buffer to the bridge instead of the framebuffer. It no longer
  calls `fb.map`, `fb.flip`, `cursor.set`, `input.register` or `input.poll`.
- **`reox_ffi` windowed mode** (`gui/nxrender_c/src/reox_ffi.c`): when the host
  says it is windowed, the nxrender window is borderless at (0, 0) and fills
  the buffer (the server draws the title bar and close button), pointer
  positions are absolute and content-local, and the loop is **event-driven**:
  it renders only after input, a resize or `nxui_*` changes, waits for the
  buffer to be free, and otherwise sleeps in `nxw_wait_event`. An idle NxUI
  app costs nothing. `CONFIGURE` re-creates the drawing context on the new
  buffer and lays the window out again. `CLOSE_REQUEST` ends `nxui_run` the
  same way the old close control did, and the app's window is destroyed.
  Commits send the whole buffer for now (`count` 0); nxrender has no damage
  tracking yet.
- **Change to `reox_ffi_host_t`** (SDK platform interface, needs sign-off):
  one new flag field `windowed`, one new optional callback
  `int (*wait_event)(reox_ffi_input_event_t *ev, uint32_t timeout_ms)`, and
  new event types `REOX_INPUT_POINTER_ABS` (absolute `x`, `y`),
  `REOX_INPUT_RESIZE` (new `width`, `height` and buffer) and
  `REOX_INPUT_CLOSE`. Existing fields and event types keep their meaning, so
  the embedded mode (`reox_ffi_init`, used by the desktop shell) is
  unchanged.
- **New NxUI call** (REOX-visible API, needs sign-off):
  `int64_t nxui_window_set_resizable(nxui_handle_t window, bool resizable)`.
  NxUI windows are **not resizable by default**, matching how
  Calculator.app is laid out today.
- **No fallback to the framebuffer.** When no window server is reachable,
  `nxui_run` logs why and returns an error. Drawing straight to the
  framebuffer would paint over the desktop, so a silent fallback would only
  hide a broken setup. The kernel calls stay available until week 5
  restricts them.

**Alternative:** keep the framebuffer path as a fallback when `nx.display`
is missing (useful for running an app with no desktop). Not recommended:
nothing in the Preview boots without the desktop, and the path would block
the week 5 restriction.

## Work plan (one PR each, tested in QEMU, merged before the next)

1. **This proposal.**
2. **libnxwindow in C:** `sdk/nxwindow/nxwindow.{h,c}`, `Makefile`, the four
   `nxsyscall.h` wrappers. `tests/host/nxwindow_test.c` runs the library
   against a scripted in-process server over a fake channel (reply matching,
   queued events during waits, coalescing and overflow, `-EBUSY` rule,
   resize, every error path, stale window pointers).
   `tests/userland/nxwtest` uses only the library in QEMU: two windows,
   drawing, damage, resize, close, errors (bad title, ninth window,
   destroyed window), and an interactive mode that stays open until closed.
   It goes on the boot image as `/apps/nxwtest`.
3. **NxUI on libnxwindow:** the `reox_ffi` windowed mode, the NxUI host, the
   new call, `reox.mk` linking the library. `tests/userland/nxuitest`, a C
   program on the NxUI C API (labels, a grid of buttons, keys), runs as a
   window in QEMU without needing the REOX compiler. Calculator.app (REOX)
   is built and tested on the owner's machine, which has `reoxc` and
   `librx_neolyx.a`; once it passes, `rebuild_image.sh` puts it on the image
   as `/apps/Calculator`.

### Week 4 exit test

- `nxwtest` passes in QEMU and its windows can be dragged, tiled, minimized,
  restored and closed like built-in windows.
- `nxuitest` and Calculator.app each run as their own process in their own
  window, with server decorations; clicks and keys work; the close button
  ends the app; killing the app removes its window and returns its memory.
- An idle NxUI app renders no frames (the frame log shows skipped ticks).
- wintest, ipc_test, shmtest, fdtest and fbtest still pass; the host tests
  pass.

## Not in this proposal

- Moving the built-in desktop apps (Settings, Path, Terminal, ...) out of
  process: week 5.
- Restricting `fb.map`, `fb.flip`, `cursor.set` and `input.poll` to system
  processes: week 5.
- Damage tracking inside nxrender, buffer slots (protocol 1.1), popups and
  menus as separate surfaces, clipboard, cursor shapes, HiDPI.
- A raw-pixel REOX API. REOX can only pass `int`, `float`, `bool` and
  `string` to `extern fn` today (`sdk/REOXC_REQUIREMENTS.md`, 3.2 and 3.3),
  so REOX apps draw through NxUI.
