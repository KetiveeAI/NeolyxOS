# Proposal: nxserver window server and protocol (week 3)

Status: **approved by the project owner on 2026-11-01 ("approve as
written").** Changing it needs the owner's sign-off again. Implemented so
far: step 1, `fb.flip_rects` (148, ABI 1.2); step 2, nxserver in the
desktop (`desktop/shell/nxserver`), the protocol header and
`tests/userland/wintest`; step 3, damage-driven frames
(`desktop/shell/nxserver/nxs_damage.c`, the frame loop in
`desktop/shell/desktop_shell.c`).
Plan: week 3 of the 1.0 Preview plan ("desktop becomes the nxserver
compositor, with damage tracking"). Builds on the approved kernel interface in
`kernel-shm-ipc.md` (ABI 1.1, merged in PRs #22 and #23).
Audited against `main` at 8a25c9a (2026-11-01).

## Why

Today only code compiled into the desktop can show a window. Every app
(Settings, Calculator, Notes, ...) is a render callback inside the desktop
process (`desktop_set_window_render`), so one bad app crashes the whole
shell. The kernel can now share memory and pass messages between processes.
Week 3 uses that to let a **separate process** open a window: it draws into a
shared-memory buffer, tells the window server which parts changed, and the
window server puts them on screen. Week 4 wraps the client side in
`libnxwindow` (C and REOX); week 5 moves the existing apps out of the desktop.

This document fixes three things that become public and hard to change later:

1. **Where the window server runs** (a process-architecture choice).
2. **The window protocol** that every app will speak, through `libnxwindow`.
3. **`fb.flip_rects` (syscall 148)**, a kernel ABI addition for presenting
   only the changed parts of the screen.

## What exists today (checked in the code)

| Piece | Today |
| --- | --- |
| Desktop process | `desktop/shell/desktop_shell.c`, PID 1, `PROC_FLAG_SYSTEM`. Owns `fb.map`, `fb.flip`, `input.poll`, `cursor.set` |
| Frame loop | `desktop_run()`: poll input, `render_desktop()`, sleep to a ~16 ms budget. Every frame redraws everything from the wallpaper up, then `fb.flip` copies the whole 1280x800 back buffer. In QEMU (TCG) a frame takes ~30 ms, ~14 ms of it rendering, even when nothing moves |
| Windows | `g_windows[MAX_WINDOWS]` (16) in the shell, z-order in `g_zorder`, server-drawn title bar, traffic lights, shadow and rounded corners (`draw_window_body`). Content comes from an in-process callback |
| Window manager | `window_manager.c`: tiling, snap, PiP, sidebar, spaces, minimize/restore animations, snapshots for Overview and Stage |
| Drawing | NXCanvas (`desktop/include/nxcanvas.h`), with `nxc_clip_rect` |
| `gui/nxrender_c` nxserver | `nxserver_compositor.c` is a sketch (region list, window list, `shm_key`); not built, not used. Named shm keys were rejected in the IPC spec in favour of handles |
| Kernel IPC | Handles, shm (141-143), channels (144-146), `obj.wait` (147), ports (150-152); `nx.*` port names only for system processes |

## Decision 1: the window server runs inside the desktop process

**Recommended:** nxserver is a module of the desktop process
(`desktop/shell/nxserver/`). The desktop registers port `nx.display`, accepts
client connections, and composites client windows in the same pass that draws
its own shell UI. A client window is an ordinary shell window (same
decorations, tiling, snap, minimize, Overview, Stage) whose content comes from
the client's buffer instead of a callback.

Why: the desktop already owns the framebuffer, input, cursor, z-order and all
window animations. Splitting them into a second process now would need a
second protocol for the shell itself (panels, global shortcuts, overlays) and
one more full-frame copy per frame, which doubles week 3. The protocol below
does not depend on this choice: clients cannot tell, so nxserver can become its
own process after 1.0 without changing any app.

**Alternative:** a separate `nxserver` process with the shell as a privileged
client. Better isolation (a shell crash would not close app windows) at the
cost described above. Not recommended for the Preview.

## Decision 2: server-side decorations, zero-copy buffers

- **Decorations are drawn by the server**, exactly as for today's windows, so
  every app looks native and a hung app can still be moved, minimized and
  closed. Clients draw only their content area.
- **Zero copy.** The server maps each client buffer read-only and composites
  straight from it. After a `COMMIT`, the client must not draw into that
  buffer again until the server sends `FRAME_DONE` for it (or it attaches a
  second buffer and alternates). A client that breaks this rule can only tear
  its own window: the server never trusts buffer contents, only reads pixels.

## Decision 3: present only what changed

- The server keeps a **damage region** per frame: client commit rectangles
  (moved to screen coordinates), old and new rectangles of windows that moved,
  resized, opened, closed or changed z-order, the cursor's old and new
  rectangles, and shell damage.
- **Shell damage is conservative.** Any shell activity (an input event the
  shell consumes, any running shell animation, any open overlay such as the
  app drawer, menus or Control Center, a clock change) damages the whole
  screen, as today. Components can later report smaller rectangles one by
  one; a component that does not is treated as "whole screen". Correctness
  first: when in doubt, redraw everything.
- **Empty damage means no frame:** nothing is rendered and nothing is flipped.
  An idle desktop stops costing ~14 ms of rendering per frame.
- **Partial frames** render with the canvas clipped to the damaged rectangles
  (merged to at most 8; if they cover more than half the screen, the whole
  screen is redrawn) and present with `fb.flip_rects`.
- The loop waits with `obj.wait` on the port and every client channel, with
  a timeout of the time left to the next 16 ms frame tick, instead of
  `nx_sleep`. A client message wakes the server at once.

## Kernel: `fb.flip_rects` (148), needs sign-off

The IPC spec kept 148 reserved for this call "in week 5". Damage tracking needs
it now, so this proposal brings it forward.

| # | Name | Signature | Returns |
| --- | --- | --- | --- |
| 148 | `fb.flip_rects` | `int sys_fb_flip_rects(const void *src, const nx_fb_rect_t *rects, uint32_t count)` | 0 |

```c
/* kernel/include/core/nx_fb.h (new, public) */
typedef struct {
    int32_t  x, y;          /* top-left, screen pixels */
    uint32_t w, h;
} nx_fb_rect_t;             /* 16 bytes, frozen */

#define NX_FB_RECTS_MAX     64
#define NX_ABI_FLAG_FB_RECTS 0x0004u   /* in nx_abi_info_t.flags */
```

- `src` is the caller's full linear back buffer (`width * 4` bytes per row,
  `height` rows), the same layout `fb.flip` takes, validated the same way.
- Each rectangle is clipped to the screen (64-bit arithmetic, no overflow);
  empty results are skipped. Only the clipped rectangles are copied, row by
  row, converting to the hardware pitch.
- Unlike `fb.flip`, it never draws the kernel's fallback cursor. The desktop
  already hides it and draws its own cursor into the frame.
- **Only `PROC_FLAG_SYSTEM` processes may call it** (`-EPERM`, logged). It is
  new, so this breaks nothing.
- Errors: `-EINVAL` (count 0 or above 64), `-EFAULT` (bad `src` or `rects`),
  `-EPERM`, `-ENODEV` (no framebuffer).
- ABI: `NX_ABI_MINOR` 1 to 2; `abi.query` sets `NX_ABI_FLAG_FB_RECTS`.

Not in this proposal: restricting the **existing** `fb.map`, `fb.flip`,
`cursor.set` and `input.poll` to system processes. That is still the open
week 1 question; it becomes safe to enforce in week 5, once apps have
`libnxwindow` instead of drawing to the framebuffer (this also replaces the
REOX NxUI direct-framebuffer path).

## The window protocol (version 1.0)

### Transport

- A client calls `port.connect("nx.display")` and gets a channel. One channel
  per client process; the server learns the client PID from `port.accept`.
- Every message is one channel message: a fixed little-endian struct of at
  most 1024 bytes that starts with a header. Only `BUF_ATTACH` carries a
  handle (exactly one shm handle). The protocol never carries channel or port
  handles, so the endpoint-cycle limitation noted in PR #23 cannot be reached
  through it.
- The public header is `sdk/nxwindow/nxwin_proto.h` (C, `_Static_assert` on
  every size). `libnxwindow` (week 4) is the supported way to speak it.

```c
typedef struct {
    uint16_t op;            /* NXW_OP_* */
    uint16_t len;           /* whole message in bytes, header included */
    uint32_t seq;           /* client: request number; server: echoes it in
                               replies and errors, 0 in events */
} nxw_hdr_t;                /* 8 bytes */

typedef struct { int32_t x, y; uint32_t w, h; } nxw_rect_t;   /* 16 bytes */

#define NXW_PROTO_MAJOR  1
#define NXW_PROTO_MINOR  0
#define NXW_TITLE_MAX    64        /* bytes of UTF-8 including the NUL */
#define NXW_DAMAGE_MAX   16        /* rectangles per COMMIT */
```

### Requests (client to server)

| Op | Name | Body (after the header) | Reply |
| --- | --- | --- | --- |
| 0x0101 | `HELLO` | `u16 major, u16 minor, u32 flags (0)` | `WELCOME` |
| 0x0102 | `WIN_CREATE` | `u32 width, u32 height, i32 x, i32 y, u32 flags, char title[64]` | `WIN_CREATED` |
| 0x0103 | `WIN_DESTROY` | `u32 win` | none |
| 0x0104 | `WIN_SET_TITLE` | `u32 win, char title[64]` | none |
| 0x0105 | `BUF_ATTACH` | `u32 win, u32 width, u32 height, u32 stride, u64 offset` + 1 shm handle | none |
| 0x0106 | `COMMIT` | `u32 win, u32 count, nxw_rect_t rects[16]` | `FRAME_DONE` later |

- `HELLO` must be the first message. A major version the server does not
  speak is refused with `ERROR` and the channel is closed.
- `WIN_CREATE`: content size in pixels (decorations are added outside it).
  `x`/`y` = `NXW_POS_AUTO` (`INT32_MIN`) lets the server place it (cascade,
  inside the work area). `flags`: `NXW_WIN_RESIZABLE` (0x1); other bits must be
  0. The title must be NUL-terminated valid UTF-8; invalid bytes are replaced
  when drawn. The window is not shown until its first `COMMIT`.
- `BUF_ATTACH`: format is XRGB8888 (alpha ignored; windows are opaque, the
  server masks the rounded corners). The server maps the shm read-only, checks
  `stride` (multiple of 4, at least `width * 4`, at most 16384) and that
  `offset + stride * height` fits in the object (64-bit math), then closes the
  handle; its mapping keeps the memory alive. The new buffer replaces the old
  one at the next `COMMIT`, and the old mapping is removed then.
- `COMMIT`: `count` 0 means the whole buffer changed; otherwise up to 16
  rectangles in buffer coordinates, clipped to the buffer. The server copies
  nothing; it composites from the buffer and replies `FRAME_DONE` once that
  commit has been presented.

### Events and replies (server to client)

| Op | Name | Body |
| --- | --- | --- |
| 0x0201 | `WELCOME` | `u16 major, u16 minor, u32 screen_w, u32 screen_h, u32 max_windows, u32 max_size` |
| 0x0202 | `WIN_CREATED` | `u32 win, u32 width, u32 height` |
| 0x0203 | `CONFIGURE` | `u32 win, u32 width, u32 height, u32 state` (`MAXIMIZED` 0x1, `TILED` 0x2, `ACTIVE` 0x4) |
| 0x0204 | `FRAME_DONE` | `u32 win, u32 commit_seq, u64 time_ms` |
| 0x0205 | `CLOSE_REQUEST` | `u32 win` |
| 0x0206 | `KEY` | `u32 win, u8 scancode, u8 ascii, u8 pressed, u8 modifiers, u32 time_ms` |
| 0x0207 | `POINTER` | `u32 win, u32 kind, i32 x, i32 y, u32 buttons, i32 wheel, u32 time_ms` |
| 0x02FF | `ERROR` | `u16 op, u16 reserved, i32 code (negative errno), u32 win` |

- `CONFIGURE` is sent when the user resizes, maximizes, tiles or restores the
  window, or when it gains or loses focus. The client should attach and commit
  a buffer of the new size; until it does, the server shows the old buffer
  anchored top-left on the window background.
- `CLOSE_REQUEST`: the user clicked the close button. The client decides
  (save prompts, etc.) and sends `WIN_DESTROY`. The server never closes a
  client window on its own while the client is alive, except on a protocol
  error.
- `POINTER` `kind`: `MOTION` 1, `BUTTON` 2, `ENTER` 3, `LEAVE` 4, `WHEEL` 5.
  Coordinates are content-local. Keys go to the focused window, pointer events
  to the window under the cursor. Shell shortcuts (NX key, NX+Tab, drawer,
  Control Center) are handled by the shell first and never reach clients.

### Errors and limits

- **Recoverable** mistakes get an `ERROR` and the connection stays open:
  unknown or foreign window id (`-EPERM` for another client's window, `-EBADF`
  for none), size out of range (`-EINVAL`), too many windows (`-EMFILE`), a
  buffer too small for its stride and height (`-EINVAL`), `COMMIT` before any
  buffer (`-EINVAL`).
- **Protocol violations close the connection** and are logged with the PID:
  `len` that does not match the op, an unknown op, anything before `HELLO`, a
  wrong handle count, a handle that is not shared memory, a message larger
  than its struct. All of that client's windows go away.
- A client that exits or crashes: its channel reports `PEER_CLOSED`, and the
  server destroys its windows and unmaps its buffers in the same frame.
- Limits: 8 windows per client; 16 windows in total (the shell's
  `MAX_WINDOWS`, shared with in-process apps until week 5); 16 clients; width
  and height 1 to 4096 (a 4096x4096 buffer is the 64 MiB shm maximum).
- **The server never blocks on a client.** Sends that fail with `-EAGAIN` go
  to a per-client queue of 64 messages; pointer motion and `FRAME_DONE` are
  coalesced; on overflow the oldest motion events are dropped first, and every
  drop is counted and logged at most once per second per client.

### Logging and audit

Logged to serial: every connect and disconnect (PID), every window create and
destroy (PID, window id, size), every protocol violation (PID, op, reason),
every dropped-event burst. Commits and frames are not logged in normal builds
(hundreds per second); an `NXSERVER_TRACE` build flag logs them.

## Work plan (one PR each, tested in QEMU, merged before the next)

1. **Kernel:** `fb.flip_rects` (148), `nx_fb.h`, ABI 1.2, and a userland test
   (rejected for non-system processes, rectangle clipping, bad pointers).
2. **nxserver in the desktop:** port `nx.display`, the protocol, client
   windows as shell windows, input routing, cleanup on disconnect.
   `tests/userland/wintest` speaks the raw protocol: opens a window, animates
   a square with small damage rectangles, answers `CONFIGURE` and
   `CLOSE_REQUEST`; a `hostile` mode sends every malformed case above.
3. **Damage-driven frames:** damage region, idle frames skipped, clipped
   partial rendering, `fb.flip_rects`, `obj.wait`-paced loop. The frame log
   shows skipped frames and per-frame damaged area.

### Week 3 exit test

- `wintest` opens a window from a separate process; it can be dragged, tiled,
  minimized, restored and closed like a built-in window, and keys and clicks
  reach it.
- An idle desktop renders no frames (the log shows them skipped); a client
  animating a 64x64 square costs about that square, not the whole screen.
- Killing or crashing `wintest` removes its window and returns all its memory.
- Every hostile case leaves the desktop running and is logged.
- shmtest, ipc_test and fdtest still pass.

## Not in this proposal

- `libnxwindow` (week 4), moving existing apps out of process (week 5).
- Transparent windows, other pixel formats, HiDPI scaling, per-client cursor
  shapes, clipboard, drag and drop, popup/menu surfaces. Each is a later
  protocol minor version; unknown ops are refused, so old servers fail
  cleanly.
- Restricting `fb.map`/`fb.flip`/`cursor.set`/`input.poll` (week 5, see above).

## Alternatives considered

- **Separate nxserver process** (Decision 1): more isolation, roughly double
  the week 3 work and one more full-frame copy. Possible after 1.0 without
  changing clients.
- **Client-side decorations:** apps would draw their own title bars; a hung
  app could not be moved or closed, and looks would drift between apps.
- **Copy on commit** (server copies damaged pixels into its own surface):
  protects against a client drawing after `COMMIT`, but costs a server-side
  copy of every window (up to 4 MiB each) and an extra copy per frame. Zero
  copy only lets a misbehaving client tear its own window.
- **Keeping full redraws and only adding the protocol:** simpler, but every
  client frame would cost a full-screen render (~14 ms in QEMU) and the idle
  desktop would keep burning CPU, which the Preview cannot afford on real
  hardware.
