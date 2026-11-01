# CI, boot image contents and regression runs

**Owner:** stream S10 (release, CI and integration).

Every check below is a script in the repository, so a CI failure can be
reproduced locally with the same command.

## What CI runs on every pull request

Workflow: `.github/workflows/ci.yml`, on every pull request to `main`, every
push to `main`, and by hand (`workflow_dispatch`).

| Job | Command | Fails when |
|-----|---------|------------|
| Syscall registry | `scripts/check_syscall_registry.sh` | a number is registered twice, in an unallocated range or above `SYS_MAX`; a number the registry lists as in use is missing; `SYS_MAX != NX_SYSCALL_MAX` |
| Build | `./build_all.sh --werror`, `./rebuild_image.sh`, `make -C boot` | any C file fails with the GCC 15 error flags, the loader does not build from source, or an image program is missing |
| Host tests | `scripts/run_host_tests.sh --werror` | a test in `tests/host/` fails, or a new test file has no entry in the script |
| QEMU boot smoke | `scripts/qemu_run.sh --run fdtest --run wintest:300` | the desktop does not reach `[DESKTOP] Initial render complete`, the kernel halts, the desktop exits, or a test does not print `RESULT: PASS` |

The GCC 15 error flags are `-Werror=implicit-function-declaration
-Werror=implicit-int -Werror=int-conversion -Werror=incompatible-pointer-types`.
GCC 15 treats these as errors by default and the owner's machine runs it;
CI runs GCC 13 with the flags. `./build_all.sh --werror` (or `NX_WERROR=1`)
passes them to every Makefile through `CC`, so no Makefile changes.

The smoke job uses KVM when the runner offers it and TCG otherwise. Its
serial log, summary and screenshot are kept as the `qemu-smoke` artifact.

## Programs on the boot image

`config/image-apps.list` lists every program installed on the image, one
per line: build folder, built file, path on the image. `build_all.sh`
builds each one (`make -B -C <folder>`) and `rebuild_image.sh` copies it.
Streams add their own programs by appending a line in their own PR; the
scripts never need editing for a new program. Image paths must start with
`/apps/` or `/System/`; a bad line stops the build.

## Booting and testing headless

```
./build_all.sh --werror && ./rebuild_image.sh
scripts/qemu_run.sh --cpus 2 --mem 4G --run fdtest --run shmtest --screenshot
```

`qemu_run.sh` boots a private copy of the image with the Setup marker
(`/System/Config/setup_complete.nlc`) added, waits for the desktop, then
starts each `--run` program from the app drawer with the QEMU monitor's
`sendkey` and waits for its `RESULT:` line. Results go to
`build/qemu-run/summary.txt` (key=value lines) and `serial.log`. Faults in
ordinary processes are counted, not failed, because several tests crash a
child on purpose. `--help` lists every option.

## Regression on main (twice a week)

```
scripts/regression.sh [--skip "nettest"]
```

Runs the registry check, host tests and the `--werror` build, then boots
two VM profiles (owner decision 2026-11-01: VMs first, low resources
matter):

| Profile | vCPU | RAM |
|---------|------|-----|
| `low` | 1 | 1 GiB |
| `normal` | 2 | 4 GiB |

Each boot measures time to the desktop and the host CPU QEMU uses while the
desktop is idle, takes memory snapshots with `tests/userland/memprobe`
(`mem.stats`) before and after the tests and after memory pressure, runs
every in-QEMU test, reads the desktop's `[DESKTOP] Frames:` lines for frame
draw time while idle, with `nxwtest`'s windows open and under `cpuhog`, and
runs `memstress` to check the desktop survives memory pressure. It writes
`build/regression/<date>/report.md`. S10 posts the report and keeps the
logs in `/mnt/project-files/beta-plan/S10/`.

`nettest` needs outside DNS from the VM; skip it with `--skip nettest` on a
host that has none and say so in the report. `nxuitest`'s last part needs a
person, so the regression only waits for its automatic checks.

A red `main` stops merges until it is fixed (beta plan, section 3.4).
