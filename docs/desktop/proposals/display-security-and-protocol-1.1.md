# Proposal: display and input security, window protocol 1.1 (S1, Beta ABI batch)

Status: **proposal, needs the project owner's sign-off** (Beta ABI batch,
decision D2 in the beta plan). Nothing here is implemented yet.
Stream: S1, window server, input and display security.
Audited against `main` at 18512d2 (2026-11-01).

This document asks for two things:

1. **Part A, display and input security.** Only system processes (today: the
   desktop) can touch the screen, the cursor or the global input queue. Apps
   draw and get input only through the window server.
2. **Part B, window protocol 1.1.** Three additions for the beta: cursor
   shapes, a text clipboard, and translucent content with an opaque region.

**Syscall numbers: none.** Part A changes the behaviour of existing calls.
Part B is carried by the window protocol, not by syscalls. S1 keeps its
registry blocks (125-126, 129, 158-167) unused for now.
**ABI flag: 0x0010**, `NX_ABI_FLAG_DISPLAY_SECURE`, defined in
`kernel/include/core/nx_fb.h`. It is set only after `net.if_list` has moved
off 0x0010 (S10 and N are moving it to 0x0020); until then S1 does not set it.
**ABI minor:** goes to 1.5 with the batch (S10 bumps it).

## Why

Today any process can draw on the whole screen and read every key the user
types. That rules out a public beta: a downloaded app could fake the login or
password dialog, or log keystrokes typed into another window.

| What an app can do today | How (checked in the code) |
| --- | --- |
| Write any pixel on screen, no syscall needed | `paging_map_framebuffer` (`kernel/src/arch/x86_64/paging.c:570`) maps the framebuffer with `PAGE_USER` in the kernel's shared page tables, so it is mapped in every process. `fb.info` (121) hands any process its address in `base_addr`. |
| Get that address directly | `fb.map` (120) returns `g_framebuffer_addr` to any caller (`kernel/src/core/syscall.c:1083`) |
| Replace the whole screen | `fb.flip` (122) copies the caller's buffer over the screen (`syscall.c:1124`) |
| Read every key and mouse event, before the desktop sees it | `input.poll` (123) drains the global keyboard and mouse queues (`syscall.c:1211`). Whoever polls first gets the event; the desktop never sees it |
| Move or hide the cursor | `cursor.set` (128), no check (`syscall.c:1045`) |
| Take over the GPU and the screen | `nxgame.gpu_acquire` (114) asks the session manager for exclusive display, then `nxgame.get_buffer` (119) copies a struct with the framebuffer's **kernel virtual and physical addresses** to the caller. `nxgame.*` handles are global slots with no owner, so any process can destroy or stop another's session |

What already does it right: `fb.flip_rects` (148, `kernel/src/core/nx_fb.c`)
checks `PROC_FLAG_SYSTEM` first and logs refusals at a capped rate. Part A uses
the same rule everywhere.

Who uses these calls today (searched the whole repository):

- The desktop: `nxui.c` (`fb.info`, `fb.map` as a sanity check, `fb.flip`,
  `fb.flip_rects`, `cursor.set` to hide the kernel cursor), `desktop_shell.c`
  (`input.poll`; also a dead direct-framebuffer path, `init_framebuffer`, that
  nothing calls), `main.c` and `views/settings_sysinfo.c` (`fb.info`). The
  desktop is spawned with `PROC_FLAG_SYSTEM` (`kernel/src/core/main.c:450`), so
  it keeps working.
- Old test programs not built into the image: `desktop/test/test_userspace.c`,
  `desktop/test/input_test.c`, `desktop/userspace_desktop.c`.
- No app in `desktop/apps/`, `sdk/` or `tests/userland/` calls them. Apps on
  libnxwindow (`wintest`, `nxwtest`, `nxuitest`, Calculator) only talk to
  `nx.display`.
- REOX apps that use the old NxUI direct-framebuffer path (in the REOX
  repository, outside this one) stop drawing and must move to libnxwindow,
  which week 4 already did for Calculator.

`PROC_FLAG_SYSTEM` cannot be gained by an app: `spawn` (13) never passes it
(`syscall.c:274`), and `exec` keeps the caller's flags. One latent hole:
`nxtask_kdrv_suspend` (`kernel/src/drivers/system/nxtask_kdrv.c:327`) sets
`PROC_FLAG_SYSTEM` on the process it suspends, as a "suspended" marker, which
would make that process a system process. Nothing calls it today. S3 owns
process state; S1 asks S3 to replace the marker with a process state before
Part A ships (a one-line fix, no ABI change).

## Part A: display and input are for system processes only

### A1. The framebuffer is no longer mapped for user mode

`paging_map_framebuffer` maps the framebuffer **without** `PAGE_USER`. Only the
kernel writes to the screen, through `fb.flip` and `fb.flip_rects`, which copy
from the caller's own memory. The desktop already presents this way (`nxui.c`
renders into its own back buffer and calls `fb.flip_rects`); the unused
direct-framebuffer path in `desktop_shell.c` is removed first, in a non-ABI PR.

This is a **memory-layout change** (one page-table flag) and needs the owner's
sign-off on its own. It touches `paging.c`, which S9 also works in for the
direct map (D7); S1 sends S9 the one-line change to review and lands it after
S9's mapping work, or before it if S9 has not started.

Cost: none measurable. The desktop copied through the kernel before this, and
`fb.flip_rects` copies only damaged rectangles.

### A2. Which calls become system-only

| # | Call | System process | Any other process |
| --- | --- | --- | --- |
| 120 | `fb.map` | Fills `fb_info_t` if asked, returns 1 (non-zero, so old callers that test for NULL keep working). It no longer returns an address, because user mode cannot reach the framebuffer after A1 | `-EPERM`, logged |
| 121 | `fb.info` | Unchanged | Allowed, but `base_addr` is 0. Width, height, pitch, bpp and format stay readable (Settings shows the resolution). Apps should take the screen size from the protocol's `WELCOME` |
| 122 | `fb.flip` | Unchanged | `-EPERM`, logged |
| 123 | `input.poll` | Unchanged | `-EPERM`, logged. The queue is not touched, so a refused call cannot steal an event |
| 124 | `input.register` | Unchanged (a stub that returns 0) | `-EPERM`, logged |
| 128 | `cursor.set` | Unchanged | `-EPERM`, logged |
| 110-119 | `nxgame.*` | Unchanged | `-EPERM`, logged. Games draw through a window until a full-screen path exists after the beta. `nxgame.get_buffer` also stops copying kernel addresses: it zeroes `virt_addr` and `phys_addr` for every caller |
| 148 | `fb.flip_rects` | Unchanged | Already `-EPERM` |

The check comes **first** in each handler, before any argument is read, so a
refused app learns nothing from argument errors (the rule `nx_fb.c` already
follows).

Why in the handlers and not the table's `PRIV_SYSTEM` column: the dispatcher's
`syscall_has_priv` compares against one global `current_priv` that nothing
raises, so `PRIV_SYSTEM` entries (`reboot`, `shutdown`, `rtc.set`) refuse
every process today, including the desktop. Fixing that is a dispatcher change
with its own behaviour change (the desktop could then reboot and set the
clock), which belongs to S3 and S10. S1 does not depend on it. Table entries
for these calls stay `PRIV_USER` so the dispatcher does not refuse silently
before the handler can log.

### A3. Logging and audit

- One helper, `nx_display_require_system(num)` in `nx_fb.c`, does the check
  and the log for every call in A2.
- Each refusal logs `[DISPLAY] denied <call> to pid <pid> (<name>): not a
  system process`. The first 16 refusals after boot are logged in full; after
  that, one line at most every 5 s with the count since the last line, so a
  hostile app cannot flood the serial log or slow the kernel.
- The first refusal for each syscall also goes to the audit log
  (`nx_audit_log(pid, num, NX_AUDIT_BLOCKED)`).
- `fb.info` stops logging every call (it logs today on each call, which floods
  the log when Settings is open).
- Boot logs whether the framebuffer is mapped supervisor-only.

### A4. Code placement

`fb.map`, `fb.info`, `fb.flip`, `cursor.set` and the kernel cursor move from
`syscall.c` into `kernel/src/core/nx_fb.c`. `input.poll` and `input.register`
move into a new `kernel/src/core/nx_input.c`. The move is a separate non-ABI
PR first (same behaviour, same numbers), so the ABI PR is a small diff.

### A5. What apps see

- `abi.query` sets `NX_ABI_FLAG_DISPLAY_SECURE` (0x0010) once A1 and A2 are
  in. An app that finds it set knows the framebuffer and input calls are
  closed to it.
- A program that used `fb.map` and wrote to the address: refused, and if it
  kept an address from before, it faults on the first write and is ended by
  the fault router, which logs it.
- Nothing changes for apps on libnxwindow.

### A6. Test (QEMU), `tests/userland/sectest`

A normal (non-system) process:

- calls `fb.map`, `fb.flip`, `input.poll`, `input.register`, `cursor.set`,
  `nxgame.create` and `fb.flip_rects` and expects `-EPERM` for each;
- calls `fb.info` and expects success with `base_addr == 0`;
- checks that `abi.query` reports `NX_ABI_FLAG_DISPLAY_SECURE`;
- spawns a child that writes to the framebuffer address given on its command
  line (the test script takes it from the boot log) and expects the child to
  be ended by a page fault.

The serial log must show each refusal once and the fault. The desktop must
still draw, take keyboard and mouse input, and run `wintest`, `nxwtest` and
`nxuitest` unchanged.

## Part B: window protocol 1.1

### Version negotiation

- `NXW_PROTO_MINOR` becomes 1. Every 1.0 struct, number and rule stays as it
  is (the header is frozen for 1.x).
- The client sends its minor in `HELLO`; `WELCOME` answers with
  `min(client minor, server minor)`. That is the **negotiated minor**.
- A request added in 1.1, sent on a connection that negotiated 1.0, is an
  unknown op: a protocol violation, as in 1.0.
- The server sends 1.1 events only to clients that negotiated 1.1, so a 1.0
  libnxwindow never receives an op it does not know.
- New requests that carry a handle carry exactly one shared-memory handle,
  like `BUF_ATTACH`. The server still never sends channel or port handles.

### B1. Cursor shapes

**Request `WIN_SET_CURSOR` (0x0107)**, 16 bytes: `hdr, u32 win, u32 shape`.

| Shape | Value | Shape | Value |
| --- | --- | --- | --- |
| `DEFAULT` (arrow) | 0 | `RESIZE_EW` | 6 |
| `TEXT` (I-beam) | 1 | `RESIZE_NS` | 7 |
| `POINTER` (hand) | 2 | `RESIZE_NWSE` | 8 |
| `CROSSHAIR` | 3 | `RESIZE_NESW` | 9 |
| `MOVE` | 4 | `NOT_ALLOWED` | 10 |
| `WAIT` | 5 | `HIDDEN` | 11 |

- The shape applies only while the pointer is over that window's content. Over
  the title bar, edges, other windows and the desktop, the server picks the
  shape, as it does today.
- `HIDDEN` hides the cursor over the content (games, video), and only while
  that window has focus. The cursor comes back as soon as the pointer leaves
  the content or the window loses focus, so an app cannot hide the cursor
  from the user anywhere else.
- Unknown shape: `ERROR -EINVAL`, the connection stays open.
- The server draws every shape itself with NXCanvas paths (from scratch, no
  image files), with the same shadow as today's arrow. Custom cursor images
  are not in 1.1.
- Cost: none when the shape does not change. A change damages the cursor's
  old and new bounds, as a move does.

### B2. Text clipboard

One clipboard, held by the server (the desktop), shared by client windows and
the shell's own windows. Text only, UTF-8.

**Request `CLIP_SET` (0x010A)**, 24 bytes: `hdr, u32 win, u32 bytes, u32
flags (0), u32 reserved (0)`. Carries one shm handle with at least the
`READ` and `MAP` rights; the text is the first `bytes` bytes of the object.
The server maps it read-only, copies the text into its own memory, unmaps and
closes the handle. The client can reuse or close its object right away.

**Request `CLIP_GET` (0x010B)**, 12 bytes: `hdr, u32 win`.
Reply **`CLIP_DATA` (0x0208)**, 16 bytes: `hdr (seq echoes the request), u32
win, u32 bytes`. When `bytes` is above 0 it carries one new shm object holding
the text, with `READ` and `MAP` rights only. When the clipboard is empty,
`bytes` is 0 and there is no handle.

**Event `CLIP_CHANGED` (0x0209)**, 16 bytes: `hdr, u32 win, u32 bytes`. Sent
to the client whose window is active, when the clipboard changes and when its
window becomes active, so the app can enable or disable Paste. It carries the
length only, never the text.

Rules:

- **Only the active window, and only right after user input.** `CLIP_SET` and
  `CLIP_GET` are accepted only for the window that has keyboard focus, and only
  within 1000 ms after the server delivered a `KEY` or a pointer `BUTTON`
  event to that window. Otherwise: `ERROR -EPERM`, logged. A background app
  cannot read what the user copied or replace it behind their back. (macOS and
  Windows let any app read the clipboard; recent versions added prompts and
  warnings because apps abused it. This rule needs no prompt.)
- Text is 1 byte to 1 MiB (`NXW_CLIP_MAX`), valid UTF-8, no NUL byte.
  Anything else: `ERROR -EINVAL`. A handle that is not shm, or a shm object
  smaller than `bytes`: protocol violation, as for `BUF_ATTACH`.
- The server keeps one copy. A new `CLIP_SET` replaces it. The clipboard
  outlives the app that set it, and is lost when the desktop restarts.
- Logged: every set and get, with PID, window and length. Never the text.
- In-process shell windows (Notes, Shell, Settings until S2 moves them) use
  the same store through `desktop_clipboard_set_text` and
  `desktop_clipboard_get_text` in `desktop_shell.h`, with the same rules.

### B3. Translucent content and the opaque region

In 1.0 a client buffer is `XRGB8888`: the server ignores alpha, and the whole
content is opaque. An opaque-region hint only means something once content can
be translucent, so 1.1 adds both together.

**Request `BUF_ATTACH_EX` (0x0108)**, 40 bytes: the `BUF_ATTACH` fields plus
`u32 format, u32 flags (0)`. Same handle rules. Formats:

- `NXW_FORMAT_XRGB8888` (1): the same as `BUF_ATTACH`.
- `NXW_FORMAT_ARGB8888_PRE` (2): premultiplied alpha (each colour channel at
  most alpha). The server blends it over what is behind the window. Over a
  translucent part, the server does not fill the window background, and the
  drop shadow is drawn in full.

**Request `WIN_SET_OPAQUE` (0x0109)**, 272 bytes: `hdr, u32 win, u32 count,
nxw_rect_t rects[16]`, in buffer coordinates, the same shape as `COMMIT`. It
says which parts of an `ARGB8888_PRE` buffer are fully opaque. It takes effect
at the next `COMMIT`, like a new buffer. `count` 0 means nothing is opaque,
which is the default for an `ARGB8888_PRE` buffer. For `XRGB8888` the whole
buffer is opaque whatever the region says.

What the server does with it:

- R4 part 1's pass culling treats a window as opaque only inside its opaque
  region (each rectangle intersected with the window's interior), so whatever
  is below an opaque part is still skipped.
- The server trusts the hint only for culling: a pixel in an opaque rectangle
  whose alpha is below 255 is drawn as if it were opaque (alpha forced to 255
  there), so a wrong hint can only spoil the app's own window. This is what
  makes the hint safe: the result never depends on what is below.
- Premultiplied input is not checked per pixel (that would cost a full pass
  over every commit). Out-of-range channels are clamped by the blend, so a
  bad buffer can only look wrong inside its own window.

Cost: an `XRGB8888` window costs the same as today. An `ARGB8888_PRE` window
costs one blend per translucent pixel per frame where it is damaged, about 2x
an opaque copy, and it cannot cull what is below its translucent parts.
Background blur ("vibrancy") is not in 1.1.

### B4. Header and library

- `sdk/nxwindow/nxwin_proto.h`: the new ops, structs (with `_Static_assert`
  sizes), shape and format values, `NXW_CLIP_MAX`.
- `libnxwindow` (`sdk/nxwindow/nxwindow.h`), new calls:
  - `nxw_window_set_cursor(w, shape)`
  - `nxw_clipboard_set_text(w, text, bytes)`
  - `nxw_clipboard_get_text(w)`, answered by an `NXW_EV_CLIPBOARD` event that
    gives the text in a library-owned buffer valid until the next event
  - `nxw_window_set_format(w, format)` and `nxw_window_set_opaque(w, rects,
    count)`
  - each returns `-ENOTSUP` when the server negotiated 1.0.
- The REOX SDK binds the same calls (owner's REOX repository).

### B5. Tests (QEMU)

- `wintest`: a `proto11` mode that sends every new op with good and bad
  arguments (unknown shape, oversized text, invalid UTF-8, a set from an
  inactive window, a 1.1 op on a 1.0 connection) and checks each answer; the
  desktop must stay up and log each case.
- **Copy text between two windows:** two `nxwtest` windows. Ctrl+C in the
  first sets its sample text; Ctrl+V in the second gets it and draws it; the
  test checks the bytes match. The script sends the keys through the QEMU
  monitor. The same check from Notes to `nxwtest` and back covers the shell's
  own windows.
- Cursor: `nxwtest` sets `TEXT` over its content; a screenshot shows the
  I-beam over it and the arrow over the title bar.
- Opaque region: an `ARGB8888_PRE` test window with an opaque centre and a
  translucent border, checked by screenshot, and by the
  `DESKTOP_DAMAGE_VERIFY` build reporting 0 wrong pixels.

## Work plan (one PR each, tested in QEMU)

Non-ABI work starts now, while the batch waits:

1. Desktop: remove the dead direct-framebuffer path in `desktop_shell.c`;
   `nxui.c` stops calling `fb.map`.
2. Kernel: move the fb, cursor and input syscalls into `nx_fb.c` and
   `nx_input.c` (no behaviour change); `fb.info` stops logging every call.
3. Frame time with 9 windows open, before and after R4, logged to the beta
   plan's S1 folder.

After sign-off:

4. `[ABI]` Part A: the checks, logging, `nxgame.get_buffer` address zeroing,
   `sectest`. Then the supervisor-only framebuffer mapping (after S3's
   `nxtask` fix and with S9's review), and `NX_ABI_FLAG_DISPLAY_SECURE` (after
   `net.if_list` moves off 0x0010).
5. `[ABI]` Protocol 1.1 header and negotiation, cursor shapes.
6. `[ABI]` Clipboard: server store, `CLIP_*`, libnxwindow calls, the
   two-window copy test.
7. `[ABI]` `BUF_ATTACH_EX`, `WIN_SET_OPAQUE`, culling by region.

## Decisions for the owner

1. **Part A as written**, including the supervisor-only framebuffer mapping
   (memory layout). Recommended: yes. Without A1, A2 is only a speed bump,
   because any process can still write to the screen at the address.
2. **`fb.map` returns 1 instead of an address for the desktop.** Recommended:
   yes; an address user mode cannot reach is worse than none.
3. **Clipboard rule: active window and recent input only.** Recommended: yes.
   The alternative, any connected client can read, matches older desktops and
   lets any background app read passwords the user copies.
4. **Translucent content (B3) in the beta.** Recommended: yes, it is what the
   opaque region needs, and polish such as translucent sidebars depends on
   it. If the owner wants a smaller batch, B3 can move to the second batch
   (around 2026-11-24) without changing B1 and B2.

## Not in this proposal

- Custom cursor images, rich clipboard formats (images, files), drag and
  drop, popup and menu surfaces, HiDPI scale, background blur.
- Making `PRIV_SYSTEM` table entries work per process (dispatcher, S3/S10).
- Full-screen exclusive mode for games.
- A separate nxserver process (still possible after 1.0 without changing
  clients).
