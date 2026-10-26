# Calculator.app

NeolyxOS Calculator, written in REOX.  First application built through the
NeolyxOS REOX SDK (`sdk/`); see `sdk/README.md` for the pipeline.

```
Calculator.app/
  src/calculator.rx     the app: engine + UI (REOX)
  Makefile              includes ../../../sdk/reox.mk
  manifest.npa          app manifest
  test/engine_test.c    host-side engine test (make test)
  legacy/               previous C implementation, reference only (make legacy)
  bin/calculator        output: freestanding NeolyxOS ELF (ignored by git)
```

## Build

```sh
make                 # src/calculator.rx -> src/calculator.c -> bin/calculator
make test            # host build of the generated C + NxUI, 37 engine checks
make info            # resolved toolchain paths
make legacy          # old C sources; does not compile against today's nxrender
```

`make` needs `reoxc` (path in `sdk/toolchain.env`) and builds
`gui/nxrender_c/libnxrender_neolyx.a` on demand.  The generated C
references no REOX runtime symbol, so the ELF links whether or not
`librx_neolyx.a` exists; the final line of the build says which.

## Design

* **Entry** is exact: a typed number is `mantissa / 10^frac_digits` with
  up to 15 digits, so the display never shows float noise while typing.
  Results are doubles formatted to 10 decimals with trailing zeros
  trimmed; magnitudes of 1e18 and above switch to `d.ddddde+NN`.
* **Engine** is a classic accumulator / pending-operator machine
  (`engine_operator`, `engine_equals`), with memory (MC, MR, M+, M-),
  percent, negate, backspace, square root (Newton iteration: REOX has no
  math builtins yet), square and reciprocal.  Division by zero, the
  square root of a negative and 1/0 enter an `Error` state cleared by the
  next digit or AC.
* **UI** is two `layer`s: `Display` (expression line, value, memory
  indicator) and `Keypad` (4 x 7 grid of `nxui_button`s).  Buttons carry
  an action id; NxUI calls `rx_ui_on_action(act)` on click and
  `rx_ui_on_key(key)` on keyboard input, and both feed `engine_dispatch`.
* **No strings in REOX yet**: formatting goes through `nxui_format_input`,
  `nxui_format_number` and `nxui_concat`.

## Running on NeolyxOS

The ELF is a standalone userspace process: `nxui_run` maps the
framebuffer, polls input and presents frames through `nxsyscall.h`.
It has not yet been launched inside the booted desktop because the shell
has no process launcher (`desktop/shell/app_drawer.c`,
`app_drawer_launch_app` is a TODO) and the boot image carries only the
kernel with the desktop ELF embedded.  Linking the Calculator objects
into the desktop and calling `reox_ffi_init()` is the embedded path the
bridge already supports.

## Keyboard

`0`-`9`, `.`, `+ - * /`, Enter or `=`, Esc (AC), Backspace, `%`.
