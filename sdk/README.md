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
  crt/rx_crt0_neolyx.c   _start for REOX apps (calls int main(void))
  crt/rx_mem_neolyx.c    memset/memcpy/memmove/strdup/... for freestanding apps
  nxui/nxui.h, nxui.c    NxUI: the REOX-visible UI API over gui/nxrender_c
```

## The pipeline

```
app.rx --reoxc--> app.c --gcc (freestanding)--> app.o --ld user.ld--> bin/app
                   ^                                   |
         rx_core.h (REOX repo)             libnxrender_neolyx.a  (gui/nxrender_c)
                                           librx_neolyx.a         (REOX repo, runtime/)
                                           crt0 + mem shims + NxUI (this sdk)
                                           libc_stubs.c + nx_malloc.c (desktop/lib)
```

1. `reoxc compile --emit c --target neolyx -o out.c in.rx` turns REOX into
   freestanding C11 whose only includes are `<stdint.h>`, `<stdbool.h>`,
   `<stddef.h>` and `rx_core.h`.  The SDK reaches reoxc through
   `tools/reoxc-neolyx`, which finds it (toolchain.env), checks its version,
   and calls it with that command line.  A reoxc without `--target` (1.1.x
   before the NeolyxOS target) is driven with the legacy `reoxc in.rx -o
   out.c` spelling instead.  The wrapper never edits the generated C.
2. The C is compiled with the same flags as every NeolyxOS userspace binary
   (`desktop/Makefile`): `-ffreestanding -fno-stack-protector -mno-red-zone`,
   no libc, and linked with `ld -nostdlib -T desktop/user.ld`.
3. The link pulls in `gui/nxrender_c/libnxrender_neolyx.a` (nxrender built
   freestanding, `make -C gui/nxrender_c neolyx`) and `librx_neolyx.a` from
   the REOX repository (`make reox-runtime`, below).

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
| `REOX_LDLIBS` | `librx_neolyx.a` + `libnxrender_neolyx.a` |
| `REOX_SUPPORT_OBJS` | crt0, memory shims, NxUI, `libc_stubs.o`, `nx_malloc.o`, built under `build/` |
| `REOX_RUNTIME_LIB` | expected path of `librx_neolyx.a`; override on the command line |
| `REOX_RUNTIME_AVAILABLE` | `1` when that archive exists, else `0` |
| `NEOLYX_USER_LDFLAGS` | `-nostdlib -T desktop/user.ld` |

`make reox-info` prints the resolved paths; `make reox-runtime` builds
`librx_neolyx.a` in the REOX checkout (`make -C $REOX_ROOT/reox-lang/runtime
neolyx NEOLYX_ROOT=<this tree>`).

### When `librx_neolyx.a` is missing

Every REOX program needs the runtime: `string` values are reference-counted
`rx_string`s, layers and structs with reference fields use ARC, and
`print`, interpolation, arrays and the math builtins live there.  The SDK
never builds it implicitly (the REOX checkout is read-only to NeolyxOS);
the link stops with

```
reox.mk: missing <path>/librx_neolyx.a
reox.mk: build it with 'make reox-runtime' (runs: make -C ... neolyx NEOLYX_ROOT=...)
```

In the REOX repo, `make -C reox-lang/runtime symcheck` verifies that the
archive needs nothing outside the link contract of
`REOXC_REQUIREMENTS.md` section 2.

Point the SDK at an archive built elsewhere with
`make REOX_RUNTIME_LIB=/path/to/librx_neolyx.a`.

## Writing the app

### Entry point and hooks

* `fn main() -> int` (and `fn main()`, which returns 0) becomes
  `int main(void)`; `crt/rx_crt0_neolyx.c` provides `_start`, calls it and
  exits with its status.
* `pub fn rx_ui_on_action(act: int)` is called when a button whose action
  id was set with `nxui_button_set_action` is clicked.
* `pub fn rx_ui_on_key(key: int)` receives ASCII key presses.

Both hooks are weak symbols: an app that does not export them gets one
logged diagnostic instead of a crash.

### NxUI (`nxui/nxui.h`)

`extern fn` parameters can be `int`, `float`, `bool` and `string` (passed
to C as `const char*`; a returned `const char*` is copied into an
`rx_string`), but not function types yet, so NxUI uses validated 64-bit integer handles (0 is never
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
`nxui_concat`) predate REOX string operations; new code can use string
interpolation and `+` instead.  Their results live in a ring of 8 static
buffers (see the header) and are copied into REOX strings on return.

### What compiles for `--target neolyx` today

Checked on 2026-10-30 against the REOX main branch with `--target neolyx`
and `gcc -std=c11 -ffreestanding -nostdinc` (every `tests/spec` example of
the REOX repo passes the same check in its test suite):

* functions, `pub fn`, `const`, globals, `struct`, `variant`, `protocol`,
  generics, `import`;
* `layer` with fields and methods; a method without `self` is a
  `static fn` and is called as `Keypad.key(...)` (C name `Keypad_key`);
* `if`, `while`, `match` (lowered to `switch`/`if`), `break`, `return`;
* string interpolation `"v=\(v)"` and `+`, `len(s)`, arrays with
  `push(a, x)` / `len(a)`, `print` / `println` of any scalar,
  `sqrt` / `floor` and the other math builtins.

Not yet (see `REOXC_REQUIREMENTS.md` section 3): `as` casts, `fn(...)`
types in parameters (so no closures into NxUI), `.len()` / `.push()` method
syntax, and `panel` (its generated C uses an `rx_app_t` API that the runtime
does not declare, so the C build fails).  `panel`, `layer`, `action`,
`signal` and `bind` are keywords.

### Diagnostics

reoxc exits 1 on any error diagnostic and writes no output file, so `make`
never links stale C.  `tools/reoxc-neolyx` still deletes the output on a
failure, which covers older reoxc builds that exited 0.

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
| `librx_neolyx.a` | built by `make reox-runtime` (REOX `reox-lang/runtime`, `make neolyx`); linked by every app |
| `reoxc --target neolyx` | done in REOX; the wrapper detects it and calls reoxc natively |
| Calculator host engine test | `make -C desktop/apps/Calculator.app test` (needs `librx_linux.a`: `make -C $REOX_ROOT/reox-lang/runtime linux`) |
| Launch from the desktop in QEMU | blocked: the desktop shell has no process launcher yet (`desktop/shell/app_drawer.c`, `app_drawer_launch_app` is a TODO) and the boot image carries only the kernel with the desktop ELF embedded |
