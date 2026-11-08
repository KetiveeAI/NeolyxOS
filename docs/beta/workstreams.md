# NeolyxOS public beta: workstreams

**Target:** public beta before 2026-12-31. Testing is in VMs only until about
2026-12-01 (owner decision, 2026-11-01).
**Plan:** each stream owns its own folders and its own syscall block
(`docs/kernel/syscall-registry.md`). ABI changes go to the owner in batches.

Each stream edits only its own row, in the same PR as the work. `State` is
one of: Not started, In progress, Waiting on owner, Paused, Done.
`docs/tracker.html` is built from this file by `scripts/gen_tracker.py`.

## Streams

| Stream | Area | State | Merged PRs | Open PRs | Next step |
|--------|------|-------|------------|----------|-----------|
| S1 | Window server, input and display security | In progress | #44, #48 | #55 | Restrict fb.map, fb.flip and input.poll to system processes after the ABI batch |
| S2 | Built-in apps out of the desktop process | Not started | | | Wave 2 (around 2026-11-10): app bundle layout doc, then Files |
| S3 | Kernel process, file and terminal syscalls | In progress | #49 | #58 | PR 1 (mmap, fstat, dup, pipe) by 2026-11-10; proposal approved |
| S4 | nxlibc, the NeolyxOS C library | In progress | | | Core headers and link flags by 2026-11-12 |
| S5 | Shell, commands and Terminal | Not started | | | Wave 2: shell parser with host tests |
| S6 | Crypto, TLS 1.3 and HTTP | In progress | | #52, #53, #54, #56, #57 | X.509 and TLS 1.3 client |
| S7 | Signed packages and app store | Not started | | | Wave 2: NPA v2 format and host signing tools |
| S8 | USB input and storage | Paused | | | Owner decides around 2026-12-01 (VM-only testing until then) |
| S9 | Platform and boot | In progress | #50 | | RAM above 4 GB, from-scratch loader, shutdown and flush |
| S10 | Release, CI and integration | In progress | #51 | | CI on every PR, Beta ABI batch on 2026-11-06 |
| N | Networking | In progress | #43, #45, #46, #47 | | Socket timeouts, poll and DNS resolve after sign-off |

## What the beta still needs

| Gap | Stream | State |
|-----|--------|-------|
| C library (stdio, stdlib, string, time) | S4 | In progress |
| mmap, fstat, dup, pipe, spawn with argv/envp/fds | S3 | In progress |
| Userspace shell in Terminal (PTY) | S3, S5 | Not started |
| HTTPS (crypto, TLS 1.3, HTTP client) | S6 | In progress |
| Signed packages, real store client, Store app | S7 | Not started |
| Apps outside the desktop process (Files, Settings, Text Editor, Calculator, Task Manager) | S2 | Not started |
| Only the window server may touch the screen and global input | S1 | In progress |
| CI on every PR and a QEMU boot smoke test | S10 | In progress |
| Low-end VM performance runs (1 CPU, 1 GiB) | S10 | Not started |
| RAM above 4 GB | S9 | Not started |
| Bootloader without gnu-efi (from-scratch rule) | S9 | Not started |
| Shutdown powers off and flushes files | S9 | Not started |
| USB keyboard, mouse and boot stick | S8 | Paused |
| License text matches file headers | Owner | Waiting on owner |

## Milestones

| Date | Milestone | State |
|------|-----------|-------|
| 2026-11-06 | Registry merged, CI on, Beta ABI batch sent to the owner | In progress |
| 2026-11-15 | M1: libc core in use, crypto vectors pass, first apps out of process | Not started |
| 2026-11-29 | M2 (alpha): shell in Terminal, HTTPS fetch, signed install from a local file | Not started |
| 2026-12-06 | Feature freeze | Not started |
| 2026-12-20 | RC1 image | Not started |
| 2026-12-27 | RC2 image, owner go/no-go | Not started |
| before 2026-12-31 | Public beta | Not started |
