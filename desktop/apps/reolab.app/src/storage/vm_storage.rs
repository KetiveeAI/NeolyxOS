// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use std::path::{Path, PathBuf};
use std::fs;
use anyhow::Result;
use uuid::Uuid;
use crate::core::vm_manager::{VMConfig, VirtualMachine, VMStatus};

/// VM Storage Manager - handles VM disk images, snapshots, and configurations
pub struct VMStorage {
    base_path: PathBuf,
    vms_path: PathBuf,
    isos_path: PathBuf,
    snapshots_path: PathBuf,
}

impl VMStorage {
    /// Create a new VM storage manager
    pub fn new() -> Result<Self> {
        let base_path = PathBuf::from("/var/lib/reolab");
        let vms_path = base_path.join("vms");
        let isos_path = base_path.join("isos");
        let snapshots_path = base_path.join("snapshots");

        // Create directories if they don't exist
        fs::create_dir_all(&vms_path)?;
        fs::create_dir_all(&isos_path)?;
        fs::create_dir_all(&snapshots_path)?;

        Ok(Self {
            base_path,
            vms_path,
            isos_path,
            snapshots_path,
        })
    }

    /// Create storage for a new VM
    pub async fn create_vm_storage(&self, config: &VMConfig) -> Result<()> {
        log::info!("Creating storage for VM: {}", config.name);

        // Create VM directory
        let vm_dir = self.vms_path.join(&config.uuid.to_string());
        fs::create_dir_all(&vm_dir)?;

        // Create disk image if it doesn't exist
        if !config.disk_path.exists() {
            self.create_disk_image(&config.disk_path, config.disk_size_gb).await?;
        }

        log::info!("VM storage created successfully");
        Ok(())
    }

    /// Save VM configuration
    pub async fn save_vm_config(&self, config: &VMConfig) -> Result<()> {
        let config_path = self.vms_path
            .join(&config.uuid.to_string())
            .join("config.json");

        let config_json = serde_json::to_string_pretty(config)?;
        fs::write(config_path, config_json)?;

        Ok(())
    }

    /// Load VM configuration
    pub async fn load_vm_config(&self, uuid: Uuid) -> Result<VMConfig> {
        let config_path = self.vms_path
            .join(uuid.to_string())
            .join("config.json");

        let config_json = fs::read_to_string(config_path)?;
        let config: VMConfig = serde_json::from_str(&config_json)?;

        Ok(config)
    }

    /// Load all VMs from storage
    pub async fn load_vms(&self) -> Result<Vec<VirtualMachine>> {
        let mut vms = Vec::new();

        if !self.vms_path.exists() {
            return Ok(vms);
        }

        for entry in fs::read_dir(&self.vms_path)? {
            let entry = entry?;
            let path = entry.path();

            if path.is_dir() {
                if let Some(dir_name) = path.file_name() {
                    if let Ok(uuid) = Uuid::parse_str(dir_name.to_string_lossy().as_ref()) {
                        if let Ok(config) = self.load_vm_config(uuid).await {
                            let vm = VirtualMachine {
                                config,
                                status: VMStatus::Stopped,
                                pid: None,
                                start_time: None,
                            };
                            vms.push(vm);
                        }
                    }
                }
            }
        }

        Ok(vms)
    }

    /// Delete VM storage
    pub async fn delete_vm_storage(&self, uuid: Uuid) -> Result<()> {
        log::info!("Deleting VM storage: {}", uuid);

        let vm_dir = self.vms_path.join(uuid.to_string());
        if vm_dir.exists() {
            fs::remove_dir_all(vm_dir)?;
        }

        log::info!("VM storage deleted successfully");
        Ok(())
    }

    /// Create a disk image
    async fn create_disk_image(&self, path: &Path, size_gb: u32) -> Result<()> {
        log::info!("Creating disk image: {} ({} GB)", path.display(), size_gb);

        // Create parent directory if it doesn't exist
        if let Some(parent) = path.parent() {
            fs::create_dir_all(parent)?;
        }

        // Use qemu-img to create the disk image
        let output = std::process::Command::new("qemu-img")
            .args(&[
                "create",
                "-f", "qcow2",
                &path.to_string_lossy(),
                &format!("{}G", size_gb),
            ])
            .output()?;

        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Failed to create disk image: {}", error));
        }

        log::info!("Disk image created successfully");
        Ok(())
    }

    /// Create a snapshot
    pub async fn create_snapshot(&self, config: &VMConfig, name: &str) -> Result<()> {
        log::info!("Creating snapshot '{}' for VM: {}", name, config.name);

        let snapshot_dir = self.snapshots_path.join(&config.uuid.to_string());
        fs::create_dir_all(&snapshot_dir)?;

        let snapshot_path = snapshot_dir.join(format!("{}.qcow2", name));

        // Use qemu-img to create snapshot
        let output = std::process::Command::new("qemu-img")
            .args(&[
                "snapshot",
                "-c", name,
                &config.disk_path.to_string_lossy(),
            ])
            .output()?;

        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Failed to create snapshot: {}", error));
        }

        log::info!("Snapshot created successfully: {}", name);
        Ok(())
    }

    /// Restore from snapshot
    pub async fn restore_snapshot(&self, uuid: Uuid, snapshot_name: &str) -> Result<()> {
        log::info!("Restoring snapshot '{}' for VM: {}", snapshot_name, uuid);

        let config = self.load_vm_config(uuid).await?;

        // Use qemu-img to restore snapshot
        let output = std::process::Command::new("qemu-img")
            .args(&[
                "snapshot",
                "-a", snapshot_name,
                &config.disk_path.to_string_lossy(),
            ])
            .output()?;

        if !output.status.success() {
            let error = String::from_utf8_lossy(&output.stderr);
            return Err(anyhow::anyhow!("Failed to restore snapshot: {}", error));
        }

        log::info!("Snapshot restored successfully: {}", snapshot_name);
        Ok(())
    }

    /// List snapshots for a VM
    pub async fn list_snapshots(&self, uuid: Uuid) -> Result<Vec<String>> {
        let config = self.load_vm_config(uuid).await?;
        let mut snapshots = Vec::new();

        // Use qemu-img to list snapshots
        let output = std::process::Command::new("qemu-img")
            .args(&[
                "snapshot",
                "-l",
                &config.disk_path.to_string_lossy(),
            ])
            .output()?;

        if output.status.success() {
            let output_str = String::from_utf8_lossy(&output.stdout);
            for line in output_str.lines() {
                if line.contains("Snapshot list:") {
                    continue;
                }
                if let Some(name) = line.split_whitespace().nth(1) {
                    snapshots.push(name.to_string());
                }
            }
        }

        Ok(snapshots)
    }

    /// Get storage usage information
    pub async fn get_storage_info(&self) -> Result<StorageInfo> {
        let mut total_size = 0;
        let mut vm_count = 0;

        if self.vms_path.exists() {
            for entry in fs::read_dir(&self.vms_path)? {
                let entry = entry?;
                let path = entry.path();

                if path.is_dir() {
                    vm_count += 1;
                    total_size += self.get_directory_size(&path)?;
                }
            }
        }

        Ok(StorageInfo {
            total_size_mb: total_size / (1024 * 1024),
            vm_count,
            base_path: self.base_path.clone(),
        })
    }

    /// Get directory size recursively
    fn get_directory_size(&self, path: &Path) -> Result<u64> {
        let mut size = 0;

        if path.is_file() {
            size += fs::metadata(path)?.len();
        } else if path.is_dir() {
            for entry in fs::read_dir(path)? {
                let entry = entry?;
                size += self.get_directory_size(&entry.path())?;
            }
        }

        Ok(size)
    }

    /// Mount ISO file
    pub async fn mount_iso(&self, iso_path: &Path) -> Result<PathBuf> {
        let mount_point = self.isos_path.join(iso_path.file_name().unwrap());
        
        if !mount_point.exists() {
            fs::create_dir_all(&mount_point)?;
        }

        // Mount ISO (platform specific)
        #[cfg(target_os = "linux")]
        {
            let output = std::process::Command::new("mount")
                .args(&[
                    "-o", "loop",
                    &iso_path.to_string_lossy(),
                    &mount_point.to_string_lossy(),
                ])
                .output()?;

            if !output.status.success() {
                let error = String::from_utf8_lossy(&output.stderr);
                return Err(anyhow::anyhow!("Failed to mount ISO: {}", error));
            }
        }

        Ok(mount_point)
    }

    /// Unmount ISO file
    pub async fn unmount_iso(&self, mount_point: &Path) -> Result<()> {
        #[cfg(target_os = "linux")]
        {
            let output = std::process::Command::new("umount")
                .arg(&mount_point.to_string_lossy())
                .output()?;

            if !output.status.success() {
                let error = String::from_utf8_lossy(&output.stderr);
                return Err(anyhow::anyhow!("Failed to unmount ISO: {}", error));
            }
        }

        Ok(())
    }
}

/// Storage information
#[derive(Debug)]
pub struct StorageInfo {
    pub total_size_mb: u64,
    pub vm_count: u32,
    pub base_path: PathBuf,
} 