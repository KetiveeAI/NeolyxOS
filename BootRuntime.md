POWER ON
│
├── UEFI FIRMWARE
│   ├── Hardware Init (CPU, RAM, PCI)
│   ├── UEFI Boot Manager
│   └── Load EFI/BOOT/BOOTX64.EFI
│
├── NEOLYXOS BOOTLOADER (EFI APP)
│   ├── GOP Framebuffer Init
│   ├── Keyboard Init
│   ├── Mount ESP (FAT32)
│   ├── Locate kernel.bin
│   ├── Load Kernel to RAM
│   ├── Build BootInfo
│   ├── ExitBootServices()
│   └── Jump to Kernel Entry
│
├── NEOLYXOS KERNEL (Early Boot)
│   ├── GDT / IDT Setup
│   ├── Physical Memory Manager
│   ├── Framebuffer Ownership
│   ├── Disk Driver Init
│   ├── Syscall Table (31 syscalls)
│   └── Mount ESP
│
├── INSTALLATION CHECK
│   ├── neolyx.installed EXISTS?
│   │
│   ├── NO  ──► INSTALL MODE
│   │            │
│   │            ├── Installer Core
│   │            ├── Disk Detection
│   │            ├── User Selects Edition
│   │            │     ├── Desktop
│   │            │     └── Server
│   │            │
│   │            ├── Disk Selection
│   │            ├── Disk Formatting
│   │            │     ├── GPT + Protective MBR
│   │            │     ├── ESP (FAT32)
│   │            │     └── Root FS (NXFS)
│   │            │
│   │            ├── Copy Bootloader
│   │            ├── Copy kernel.bin
│   │            ├── Install Base System
│   │            ├── Write neolyx.installed
│   │            ├── Write install.log
│   │            └── Reboot System
│   │
│   └── YES ──► NORMAL BOOT
│                │
│                ├── Init Process
│                ├── Driver Stack
│                ├── Filesystem Mount
│                ├── Scheduler Start
│                │
│                ├── Edition Branch
│                │     ├── Desktop Edition
│                │     │     ├── NXRender
│                │     │     ├── Compositor
│                │     │     ├── Desktop Shell
│                │     │     └── Terminal / Apps
│                │     │
│                │     └── Server Edition
│                │           ├── No GUI
│                │           ├── System Services
│                │           └── Console Login
│                │
│                └── FULL NEOLYXOS RUNNING
│
└── USER SPACE
    ├── User Programs (NXFS)
    ├── Syscalls Interface
    ├── NXGame Bridge
    └── Applications
