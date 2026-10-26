# What `reoxc` must grow for NeolyxOS

Audience: the REOX compiler team (Workstreams A, B, C, D).
Source of truth for the NeolyxOS side: `sdk/reox.mk`, `sdk/tools/reoxc-neolyx`,
`sdk/nxui/nxui.h`, `desktop/apps/Calculator.app/src/calculator.rx`.
Measured against `reoxc 1.1.1 (linux x86_64)` on 2026-10-25.

Each item states the observed behaviour, the required behaviour, and the
acceptance check NeolyxOS will run.  Items are ordered by how hard they
block the SDK.

---

## 1. Command line

### 1.1 `compile` subcommand

* Observed: `reoxc compile file.rx` fails with `multiple input files not
  supported`; only `reoxc file.rx -o out.c` works.  `init`, `new`, `pkg`
  are the only subcommands (`src/cli.rs`, `parse_cli`).
* Required: `reoxc compile [OPTIONS] <INPUT>` with the existing options.
  The bare form may stay as an alias.
* Check: `reoxc compile src/calculator.rx -o /tmp/c.c` exits 0 and writes C.

### 1.2 `--target <triple>`

* Observed: no target notion; the C always `#include`s `<stdio.h>`,
  `<stdlib.h>`, `<string.h>`, `<setjmp.h>` and `"reox_nxrender_bridge.h"`
  (`src/codegen/mod.rs`, lines 67-74).
* Required: `--target neolyx` (default stays `host`).  Under `neolyx` the
  prologue is exactly:

  ```c
  #include <stdint.h>
  #include <stdbool.h>
  #include <stddef.h>
  #include "rx_core.h"            /* runtime/core  : language runtime, no OS deps */
  #include "rx_platform.h"        /* runtime/platform: selected by the target     */
  ```

  No hosted libc header, no `setjmp.h`, no `reox_nxrender_bridge.h`
  (that header is the SDL host bridge: `static inline` functions calling
  `calloc`/`strlen`/`nx_gpu_*`).  `rx_platform.h` for `neolyx` is
  `runtime/platform/neolyx/rx_platform_neolyx.h` and may include only
  `desktop/include/nxsyscall.h` and `gui/nxrender_c/include/reox_ffi.h`
  from the NeolyxOS tree.
* Check: `gcc -std=c11 -ffreestanding -nostdinc -I<gcc-include> -Iruntime/core
  -Iruntime/platform/neolyx -fsyntax-only out.c` succeeds for every example.

### 1.3 `--emit c` honoured and the only emit for `neolyx`

* Observed: `--emit obj|exe` shell out to the host `gcc` with host flags
  and `libreox_runtime.a` (`compile_c_to_exe`, `src/cli.rs`).
* Required: for `--target neolyx`, `--emit obj` and `--emit exe` either
  use the NeolyxOS flags from `sdk/reox.mk` (`NEOLYX_USER_CFLAGS`,
  `NEOLYX_USER_LDFLAGS`, `REOX_LDLIBS`) or exit 2 with
  `error: --emit exe requires the NeolyxOS SDK; use --emit c`.  Silently
  producing a Linux binary is not acceptable.

### 1.4 Exit status on diagnostics

* Observed: parse and type errors are printed, the exit status is 0, and a
  truncated C file is still written (e.g. `error[1:55]: expected ';'` then
  `compiled: p.rx -> p.c`).  `tools/reoxc-neolyx` currently greps the
  diagnostics to recover.
* Required: any `error` diagnostic gives exit status 1 and **no output
  file** (or the previous output untouched).  Warnings keep exit 0.
* Check: `reoxc compile bad.rx -o out.c; echo $?` prints 1 and `out.c`
  does not exist.

### 1.5 `--version` format

* Observed: `reoxc 1.1.1 (linux x86_64)`.  Keep it; the wrapper parses
  `^reoxc <semver>`.

---

## 2. Runtime layout and link contract (Workstream D)

* Required directories in the REOX repo:

  ```
  reox-lang/runtime/core/      rx_core.h, rx_core_*.c      zero OS deps, zero libc
  reox-lang/runtime/platform/
      linux/   rx_platform_linux.c   (SDL allowed)
      neolyx/  rx_platform_neolyx.c  rx_platform_neolyx.h
  ```

* Required artefact: `reox-lang/runtime/librx_neolyx.a`, produced by
  `make -C reox-lang/runtime neolyx`, built with exactly
  `-Wall -Wextra -std=c11 -O2 -ffreestanding -fno-stack-protector
  -mno-red-zone -U_FORTIFY_SOURCE -D__NEOLYXOS__`.  `sdk/reox.mk` looks for
  it at `$(REOX_RUNTIME_DIR)/librx_neolyx.a` and adds
  `-I$(REOX_RUNTIME_DIR)/core -I$(REOX_RUNTIME_DIR)/platform`.
* Allowed external symbols of `librx_neolyx.a` (everything else must be
  inside it): `malloc free calloc realloc strlen memcpy memset memmove
  strcmp strncmp strdup` (from `desktop/lib/libc_stubs.c` and
  `sdk/crt/rx_mem_neolyx.c`), `nx_malloc nx_mfree` (`desktop/lib/nx_malloc.c`),
  the `nx_*`/`reox_*` API of `gui/nxrender_c`, and the inline syscall
  wrappers of `nxsyscall.h`.
  Check: `nm -u librx_neolyx.a | sort -u` is a subset of that list.
* Rename or move `runtime/reox_ffi.h`: it is the SDL host bridge, and its
  name collides with `gui/nxrender_c/include/reox_ffi.h`.  `sdk/reox.mk`
  works around this with include-path order today.
* `rx_core.h` must declare everything codegen references.  Observed
  gaps (compiled a `panel`): `rx_app_t`, `rx_app_new`, `rx_app_set_title`,
  `rx_app_set_size`, `rx_app_run` are emitted but declared nowhere.
  On `neolyx` they must map to the FFI bridge:

  | emitted | NeolyxOS implementation |
  |---|---|
  | `rx_app_t* rx_app_new(void)` | wraps `reox_window_create("", 640, 480)` |
  | `rx_app_set_title(app, const char*)` | `reox_window_set_title` |
  | `rx_app_set_size(app, int64_t w, int64_t h)` | `reox_window_set_size` |
  | `rx_app_run(app)` | `reox_app_run` (blocks until quit) |

* `print(x)`/`println(x)`: codegen emits `print(width)` for an `int`
  while `reox_runtime.h` declares `print(rx_string)`.  Emit
  `print_int`/`print_float`/`print_bool`/`print` by static type, and on
  `neolyx` implement them over `nx_debug_print` (`SYS_DEBUG_PRINT`, 127).

---

## 3. Language features the Calculator had to work around

Observed with probes under `reox-lang/tests`-style files; each failure is
reproducible with the one-liner shown.

| # | Feature | Observed | Required | Repro |
|---|---|---|---|---|
| 3.1 | `match` | emits a GCC statement-expression using `typeof`, rejected under `-std=c11`; `match` as the last statement before `return` is a parse error | lower to `if`/`switch`; no `typeof` | `fn k(a:int)->int{ match a { 1 => { return 10; } _ => { return 0; } } }` |
| 3.2 | `as` casts | `expected ';' ... found Ident("as")` | `expr as int/float/bool`, C cast with range check in debug | `let i: int = x as int;` |
| 3.3 | function types / closures as `extern fn` params | `expected type, found Fn` | `fn(int) -> void` type usable in params and lets; C function pointer | `extern fn set(h: fn(int) -> void) -> void;` |
| 3.4 | string ops | `string_concat`, `str(v)` undefined; `.len()` not a field | `+` on strings, interpolation, `.len()`, `to_string()`; emitted calls must exist in `rx_core` | `let s: string = "v=\(v)";` |
| 3.5 | arrays | `[int]` lowers to `int64_t*` with a brace initialiser; `.push`/`.len` rejected | `rx_array` from `rx_core.h` | `let mut d: [int] = [1,2]; d.push(3);` |
| 3.6 | math builtins | `sqrt`, `pow`, `floor` undefined although `reox_runtime.h` declares `sqrt_float` etc. | expose them (`sqrt(x: float) -> float`, ...) | `let q: float = sqrt(2.0);` |
| 3.7 | `panel` | emitted `_setup` references the undeclared `rx_app_*` API; tuple property `size: (280, 480)` is a parse error | see section 2; accept `size: (w, h)` | `panel W { size: (280, 480), fn root() {} }` |
| 3.8 | keywords | `panel`, `action`, `layer`, `signal`, `bind`, `state` cannot be identifiers (`expected identifier, found Panel`) | document; consider contextual keywords for `action`, `state` | `let panel: int = 0;` |
| 3.9 | `layer` methods | no `self`; `@Bind let` fields compile to plain struct fields with no change tracking | Workstream B (`self`, methods) and `@Bind` invalidation calling `reox_*_set_*` through the bridge | `layer D { @Bind let t: string = "0"; fn f() { self.t = "1"; } }` |

Because of 3.3 the SDK routes button clicks through two exported hook
functions (`rx_ui_on_action`, `rx_ui_on_key`, see `sdk/nxui/nxui.h`).  Once
3.3 lands, NxUI gains `nxui_button_set_on_click(h, fn(int) -> void)` and the
hooks become optional.

Because of 3.4 the SDK provides `nxui_format_number`, `nxui_format_input`
and `nxui_concat`.  Once 3.4 lands these move into `rx_core`.

---

## 4. Codegen details to keep

These are relied on by `sdk/crt` and `sdk/nxui`; changing them breaks the
link.

* `fn main() -> int` emits `int64_t main(void)`; `_start` in
  `crt/rx_crt0_neolyx.c` calls it.  A `fn main()` without `-> int` emits
  `void main(void)`; require `-> int` for `neolyx` or emit a wrapper.
* `pub fn` and plain `fn` both emit external linkage.  `pub fn` must stay
  external (hooks depend on it); plain `fn` may become `static`.
* Type mapping: `int` -> `int64_t`, `float` -> `double`, `bool` -> `bool`,
  `string` -> `const char*`, struct by value.  NxUI's prototypes are
  written against exactly this mapping.
* `const X: int = v;` emits `static const int64_t X = v;`.  Fine.
* `extern fn` emits `extern` prototypes verbatim; no name mangling.

---

## 5. Acceptance run (NeolyxOS side)

```sh
cd neolyx-os
make -C gui/nxrender_c neolyx
make -C desktop/apps/Calculator.app clean all     # REOXC_NATIVE=1 once 1.x lands
nm -u desktop/apps/Calculator.app/bin/calculator   # must print nothing
```

With 1.1 to 1.4 implemented, `sdk/tools/reoxc-neolyx` is deleted and
`REOXC` in `sdk/reox.mk` points at `reoxc` directly with the same command
line.
