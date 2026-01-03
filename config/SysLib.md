# NeolyxOS Configuration System

This directory contains the configuration files for NeolyxOS.

## Configuration Files

| File | Purpose | Location After Install | Permission |
|------|---------|------------------------|------------|
| `boot.nlc` | Installation metadata, boot disk, boot flags | `/System/Config/boot.nlc` | SIP Protected |
| `host.nlc` | Hostname, hardware IDs, locale, users | `/System/Config/host.nlc` | Admin Writable |
| `system.nlc` | Kernel, drivers, filesystem, security | `/System/Config/system.nlc` | SIP Protected |
| `user_defaults.nlc` | Template for user preferences | `/System/Config/user_defaults.nlc` | Template (Read) |
| `permissions.nlc` | File system access control, SIP paths | `/System/Config/permissions.nlc` | SIP Protected |

## File Format (.nlc)

NeolyxOS uses the `.nlc` (NeoLyx Config) format - an INI-style syntax:

```ini
[section_name]
# Comment
key = "value"
another_key = "another_value"
```

## macOS-like Principles

- **System Integrity Protection (SIP)**: `/System/` paths are read-only
- **User Isolation**: Users can only write to their own `/Users/<name>/` directory
- **Admin Paths**: `/Library/` and `/Applications/` require admin privileges
- **Recovery Mode**: Required to modify SIP-protected files

## Path Structure After Installation

```
/System/                          # SIP Protected
├── Config/
│   ├── boot.nlc
│   ├── host.nlc
│   ├── system.nlc
│   ├── user_defaults.nlc
│   └── permissions.nlc
├── Kernel/
├── Drivers/
└── Library/

/Library/                         # Admin writable
├── Preferences/
└── Application Support/

/Users/<username>/                # User writable
├── Library/
│   └── Preferences/user.nlc     # Copy of user_defaults
├── Desktop/
├── Documents/
└── Downloads/
```

## Usage in Kernel

The installer (`installer.c`) writes to `boot.nlc` after installation.
The SIP module (`sip.c`) reads `permissions.nlc` to enforce access control.

---
Copyright (c) 2025 KetiveeAI
