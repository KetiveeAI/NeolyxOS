# NeolyxOS REOX SDK

REOX is the platform language of NeolyxOS, the way Swift is the platform
language of macOS.  This directory is how a NeolyxOS application consumes it.
REOX itself (compiler, runtime, standard library) lives in its own repository
and is **not vendored here**; `toolchain.env` records where it is.

```
sdk/
  README.md              this file
  REOXC_REQUIREMENTS.md  what reoxc must grow for NeolyxOS (for the REOX team)
  toolchain.env          REOX_ROOT, reoxc host/system paths (shell + make)
  reox.mk                GNU make include: %.c: %.rx rule, flags, support objects
  tools/reoxc-neolyx     front-end wrapper until reoxc has --target neolyx
  crt/rx_crt0_neolyx.c   _start for REOX apps (calls int64_t main(void))
  crt/rx_mem_neolyx.c    memset/memcpy/memmove/strdup/... for freestanding apps
  nxui/nxui.h, nxui.c    NxUI: the REOX-visible UI API over gui/nxrender_c
```

## The pipeline

```
app.rx --reoxc--> app.c --gcc (freestanding)--> app.o --ld user.ld--> bin/app
                   ^                                   |
         reox_runtime.h (REOX repo)        libnxrender_neolyx.a  (gui/nxrender_c)
                                           librx_neolyx.a         (REOX repo, Workstream D)
                                           crt0 + mem shims + NxUI (this sdk)
                                           libc_stubs.c + nx_malloc.c (desktop/lib)
```

1. `reoxc` turns REOX into C11.  Today the SDK calls it through
   `tools/reoxc-neolyx`, which presents the target command line
   (`reoxc compile --emit c --target neolyx -o out.c in.rx`) and maps it onto
   what reoxc 1.1.x accepts.  It never edits the generated C.
2. The C is compiled with the same flags as every NeolyxOS userspace binary
   (`desktop/Makefile`): `-ffreestanding -fno-stack-protector -mno-red-zone`,
   no libc, and linked with `ld -nostdlib -T desktop/user.ld`.
3. The link pulls in `gui/nxrender_c/libnxrender_neolyx.a` (nxrender built
   freestanding, `make -C gui/nxrender_c neolyx`) and, when it exists,
   `librx_neolyx.a` from the REOX repository.

## Using the SDK in an app

`desktop/apps/Calculator.app` is the reference.  Minimal Makefile:

```make
include ../../../sdk/reox.mk          # from desktop/apps/<Name>.app/

REOX_SRCS  := src/main.rx
REOX_GEN_C := $(REOX_SRCS:.rx=.c)
APP_OBJS   := $(REOX_GEN_C:.c=.o)
.PRECIOUS: $(REOX_GEN_C)

all: bin/app
bin/app: $(APP_OBJS) $(REOX_SUPPORT_OBJS) $(REOX_LDLIBS)
	@mkdir -p bin
	$(LD) $(NEOLYX_USER_LDFLAGS) -o $@ $(APP_OBJS) $(REOX_SUPPORT_OBJS) $(REOX_LDLIBS)
%.o: %.c
	$(CC) $(REOX_CFLAGS) -c $< -o $@
```

`reox.mk` provides:

| Variable | Meaning |
|---|---|
| `REOXC` | compiler front end (default: `tools/reoxc-neolyx`) |
| `REOX_CFLAGS` | freestanding flags + nxrender, NxUI, desktop and REOX runtime include paths |
| `REOX_LDLIBS` | `librx_neolyx.a` (if present) + `libnxrender_neolyx.a` |
| `REOX_SUPPORT_OBJS` | crt0, memory shims, NxUI, `libc_stubs.o`, `nx_malloc.o`, built under `build/` |
| `REOX_RUNTIME_LIB` | expected path of `librx_neolyx.a`; override on the command line |
| `REOX_RUNTIME_AVAILABLE` | `1` when that archive exists, else `0` |
| `NEOLYX_USER_LDFLAGS` | `-nostdlib -T desktop/user.ld` |

`make reox-info` prints the resolved paths.

### When `librx_neolyx.a` is missing

Workstream D (REOX repo) produces it.  Until then `REOX_LDLIBS` omits it and
the link succeeds only if the generated C references no runtime symbol
(the Calculator is written that way).  Any program that uses `print`,
string interpolation, arrays, `panel`, `try` or the math builtins fails at
link time with the exact missing symbol.  Nothing is stubbed.

Point the SDK at an archive built elsewhere with
`make REOX_RUNTIME_LIB=/path/to/librx_neolyx.a`.

## Writing the app

### Entry point and hooks

* `fn main() -> int` becomes `int64_t main(void)`; `crt/rx_crt0_neolyx.c`
  provides `_start`, calls it and exits with its status.
* `pub fn rx_ui_on_action(act: int)` is called when a button whose action
  id was set with `nxui_button_set_action` is clicked.
* `pub fn rx_ui_on_key(key: int)` receives ASCII key presses.

Both hooks are weak symbols: an app that does not export them gets one
logged diagnostic instead of a crash.

### NxUI (`nxui/nxui.h`)

reoxc 1.1.x can declare `extern fn` with `int`, `float`, `bool` and
`string` only, so NxUI uses validated 64-bit integer handles (0 is never
valid; stale or wrong-kind handles are rejected and logged).  Declare what
you use:

```reox
extern fn nxui_window_create(title: string, width: int, height: int) -> int;
extern fn nxui_grid(cols: int, gap: int) -> int;
extern fn nxui_button_create(label: string) -> int;
extern fn nxui_button_set_action(button: int, act: int) -> int;
extern fn nxui_run(window: int) -> int;
```

`nxui_run` installs the NeolyxOS platform host (framebuffer via
`SYS_FB_MAP`, input via `SYS_INPUT_POLL`, flip, sleep) into the FFI bridge
and runs the window until `nxui_quit()` or the window close control.  When
the same binary is linked into the desktop shell and the shell has called
`reox_ffi_init()`, `nxui_run` only registers the window and returns.

Formatting helpers (`nxui_format_number`, `nxui_format_input`,
`nxui_concat`) exist because REOX has no string operations yet; results
live in a ring of 8 static buffers (see the header).

### REOX subset that compiles today

Verified against reoxc 1.1.1 on 2026-10-25 (see `REOXC_REQUIREMENTS.md`
for the gaps): functions, `pub fn`, `const`, global `let mut`, `struct`,
`layer` (fields incl. `@Bind let`, methods become `Layer_method`),
`if / else if / else`, `while`, `break`, `return`, int/float/bool/string,
arithmetic with implicit int-to-float, `extern fn`.

Avoid: `match`, `as` casts, closures and `fn` types, string `+` and
interpolation, arrays, `print`, `panel` (emits calls to a runtime API that
does not exist yet), `try/catch`.  The identifiers `panel`, `layer`,
`action`, `signal`, `bind`, `state` are keywords.

### Reserved identifier reminder

`reoxc` does not fail on parse or type errors (it exits 0 and writes a
truncated C file).  `tools/reoxc-neolyx` turns its diagnostics into a build
failure and deletes the output, so `make` never links stale C.

## Logging

Every validation failure in `reox_ffi.c` and `nxui.c` is written to the
serial console through `SYS_DEBUG_PRINT` with a `[REOX_FFI]` or `[NXUI]`
prefix, recorded in `reox_ffi_last_error()`, and counted.  `nxui_run`
returns the number of bridge errors raised during the run.

## Status

| Item | State |
|---|---|
| `%.c: %.rx` rule, flags, support objects | done |
| `reox_ffi.c` complete (no stubs) | done |
| Calculator.app in REOX, freestanding ELF | builds (`make -C desktop/apps/Calculator.app`) |
| `librx_neolyx.a` | not yet produced by Workstream D; not required by the Calculator |
| `reoxc --target neolyx` | not yet; wrapper in place, spec in `REOXC_REQUIREMENTS.md` |
| Launch from the desktop in QEMU | blocked: the desktop shell has no process launcher yet (`desktop/shell/app_drawer.c`, `app_drawer_launch_app` is a TODO) and the boot image carries only the kernel with the desktop ELF embedded |
