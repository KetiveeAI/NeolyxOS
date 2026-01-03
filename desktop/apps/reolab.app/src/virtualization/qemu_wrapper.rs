// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use std::process::{Command, Stdio};
use std::path::PathBuf;
use tokio::process::Child;
use anyhow::Result;
use crate::core::vm_manager::VMConfig;

/// QEMU wrapper for managing virtual machines
pub struct QEMUWrapper {
    qemu_path: PathBuf,
}

impl QEMUWrapper {
    /// Create a new QEMU wrapper
    pub fn new() -> Result<Self> {
        // Try to find QEMU binary
        let qemu_path = Self::find_qemu_binary()?;
        
        Ok(Self { qemu_path })
    }

    /// Start a VM using QEMU
    pub async fn start_vm(&self, config: &VMConfig) -> Result<u32> {
        log::info!("Starting VM with QEMU: {}", config.name);

        let mut args = self.build_qemu_args(config)?;
        
        // Start QEMU process
        let mut child = Command::new(&self.qemu_path)
            .args(&args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()?;

        let pid = child.id();
        log::info!("QEMU process started with PID: {}", pid);

        // Store child process for later management
        // Note: In a real implementation, you'd want to store this somewhere
        // to manage the process lifecycle

        Ok(pid)
    }

    /// Stop a VM by PID
    pub async fn stop_vm(&self, pid: u32) -> Result<()> {
        log::info!("Stopping QEMU process with PID: {}", pid);

        // Send SIGTERM to the process
        #[cfg(unix)]
        {
            use nix::sys::signal::{kill, Signal};
            use nix::unistd::Pid;
            
            kill(Pid::from_raw(pid as i32), Signal::SIGTERM)?;
        }

        #[cfg(windows)]
        {
            // Windows implementation would use different approach
            log::warn!("Process termination not implemented for Windows");
        }

        log::info!("QEMU process stopped: {}", pid);
        Ok(())
    }

    /// Pause a VM
    pub async fn pause_vm(&self, pid: u32) -> Result<()> {
        log::info!("Pausing QEMU process with PID: {}", pid);

        // Send SIGSTOP to the process
        #[cfg(unix)]
        {
            use nix::sys::signal::{kill, Signal};
            use nix::unistd::Pid;
            
            kill(Pid::from_raw(pid as i32), Signal::SIGSTOP)?;
        }

        log::info!("QEMU process paused: {}", pid);
        Ok(())
    }

    /// Resume a VM
    pub async fn resume_vm(&self, pid: u32) -> Result<()> {
        log::info!("Resuming QEMU process with PID: {}", pid);

        // Send SIGCONT to the process
        #[cfg(unix)]
        {
            use nix::sys::signal::{kill, Signal};
            use nix::unistd::Pid;
            
            kill(Pid::from_raw(pid as i32), Signal::SIGCONT)?;
        }

        log::info!("QEMU process resumed: {}", pid);
        Ok(())
    }

    /// Build QEMU command line arguments
    fn build_qemu_args(&self, config: &VMConfig) -> Result<Vec<String>> {
        let mut args = Vec::new();

        // Basic QEMU options
        args.push("-enable-kvm".to_string());
        args.push("-m".to_string());
        args.push(format!("{}", config.memory_mb));
        args.push("-smp".to_string());
        args.push(format!("{}", config.cpu_cores));

        // Disk configuration
        args.push("-drive".to_string());
        args.push(format!("file={},format=qcow2,if=virtio", 
            config.disk_path.display()));

        // ISO file if provided
        if let Some(iso_path) = &config.iso_path {
            args.push("-cdrom".to_string());
            args.push(iso_path.to_string_lossy().to_string());
        }

        // Network configuration
        if config.network_enabled {
            args.push("-net".to_string());
            args.push("nic,model=virtio".to_string());
            args.push("-net".to_string());
            args.push("user".to_string());
        }

        // GPU passthrough
        if config.gpu_passthrough {
            args.push("-device".to_string());
            args.push("vfio-pci,host=00:01.0".to_string());
            args.push("-device".to_string());
            args.push("vfio-pci,host=00:01.1".to_string());
        }

        // USB passthrough
        if config.usb_passthrough {
            args.push("-usb".to_string());
            args.push("-device".to_string());
            args.push("usb-host,vendorid=0x1234,productid=0x5678".to_string());
        }

        // Display options
        args.push("-display".to_string());
        args.push("gtk".to_string());

        // Additional options for better performance
        args.push("-cpu".to_string());
        args.push("host".to_string());
        args.push("-machine".to_string());
        args.push("type=q35,accel=kvm".to_string());

        Ok(args)
    }

    /// Find QEMU binary in system
    fn find_qemu_binary() -> Result<PathBuf> {
        let possible_paths = [
            "/usr/bin/qemu-system-x86_64",
            "/usr/local/bin/qemu-system-x86_64",
            "qemu-system-x86_64", // Try PATH
        ];

        for path in &possible_paths {
            if std::path::Path::new(path).exists() {
                return Ok(PathBuf::from(path));
            }
        }

        // Try to find in PATH
        if let Ok(output) = Command::new("which").arg("qemu-system-x86_64").output() {
            if output.status.success() {
                let path = String::from_utf8(output.stdout)?;
                return Ok(PathBuf::from(path.trim()));
            }
        }

        Err(anyhow::anyhow!("QEMU binary not found"))
    }

    /// Check if QEMU is available
    pub fn is_available(&self) -> bool {
        self.qemu_path.exists()
    }

    /// Get QEMU version
    pub fn get_version(&self) -> Result<String> {
        let output = Command::new(&self.qemu_path)
            .arg("--version")
            .output()?;

        if output.status.success() {
            let version = String::from_utf8(output.stdout)?;
            Ok(version.lines().next().unwrap_or("Unknown").to_string())
        } else {
            Err(anyhow::anyhow!("Failed to get QEMU version"))
        }
    }
} 