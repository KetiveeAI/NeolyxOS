# ReoLab IDE — Build Plan

## Phase 1 — Core Shell (Linux + Tauri) · Week 1–2

Get a window open, nothing more.

- **Tauri project scaffold** — Rust backend + frontend webview, clean IPC boundary defined from day one
- **Basic layout** — File tree panel, editor area, terminal pane — no logic yet, just structure
- **Platform abstraction layer (thin)** — FS ops, process spawn, path resolution — all behind a trait so NeolyxOS impl slots in later

**Stack:** Rust, Tauri, TypeScript, Linux

---

## Phase 2 — Reox Language Tooling · Week 3–6

The core reason ReoLab exists.

- **Reox LSP server** — Syntax highlighting, diagnostics, go-to-def, autocomplete — written in Rust, runs as sidecar process
- **Build integration** — Invoke Reox compiler from IDE, stream stdout/stderr to terminal pane, parse errors into inline markers
- **DAP debugger adapter** — Debug Adapter Protocol bridge so standard debug UI works with Reox programs

**Stack:** Rust, LSP, DAP, Reox

> This phase is the NLnet / EF demo story — "we built a full language toolchain in Rust"

---

## Phase 3 — Multi-language Support · Week 7–9

C, C++, Go for full Ketivee stack coverage.

- **clangd integration** — C/C++ LSP via clangd — covers NeolyxOS kernel work and Zepra Browser directly
- **gopls integration** — Go LSP for KetiveeHealth backend and SSO
- **Project config system** — Per-project language, compiler, build command — stored in `.reolab/config.toml`

**Stack:** clangd, gopls, rust-analyzer

---

## Phase 4 — Polish + Public Release · Week 10–12

Ship it, document it, grow Reox adoption.

- **.deb / .AppImage packaging** — One-command install on Ubuntu/Debian, GitHub Releases
- **Docs + getting started** — "Install ReoLab, write your first Reox program" — 5 minute flow
- **Demo Short (@swanayagupta)** — ReoLab writing + compiling Reox on Linux — Shorts outperform long-form, use that

**Stack:** cargo-bundle, AppImage, GitHub Actions

---

## Phase 5 — NeolyxOS Port · NeolyxOS beta+

Swap Tauri → ZepraEngine when OS is ready.

- **Swap renderer only** — Replace Tauri's webview with ZepraEngine — same HTML/CSS/JS frontend, zero UI rewrite
- **NeolyxOS platform impl** — Write the NeolyxOS backend for the platform abstraction layer defined in Phase 1
- **Rust core untouched** — LSP, DAP, compiler integration, project config — all carry over with zero changes

> This is additive, not a rewrite. The platform abstraction layer in Phase 1 exists specifically for this moment.

---

## Architecture Summary

```
ReoLab
├── Core (pure Rust)         ← LSP, DAP, compiler bridge — fully portable
├── Frontend (HTML/CSS/JS)   ← same UI code on Linux and NeolyxOS
└── Platform layer (thin)
    ├── Linux impl            ← Tauri (Phase 1–4)
    └── NeolyxOS impl         ← ZepraEngine (Phase 5)
```

## Key Decisions

- **Full Rust** — no C mixing, no FFI overhead, Reox alignment
- **Tauri now, ZepraEngine later** — same frontend, swap only the renderer
- **Platform layer defined in Phase 1** — makes Phase 5 additive not a rewrite
- **Linux first** — real dogfooding immediately, NLnet/EF demo ready, port comes naturally