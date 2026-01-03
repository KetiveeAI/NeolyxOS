// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use serde::{Deserialize, Serialize};
use std::collections::HashMap;
use std::path::PathBuf;
use tokio::sync::RwLock;
use uuid::Uuid;
use anyhow::Result;

use crate::virtualization::qemu_wrapper::QEMUWrapper;
use crate::storage::vm_storage::VMStorage;

/// VM status enumeration
#[derive(Debug, Clone, Serialize, Deserialize, PartialEq)]
pub enum VMStatus {
    Stopped,
    Starting,
    Running,
    Paused,
    Stopping,
    Error(String),
}

/// VM configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VMConfig {
    pub name: String,
    pub uuid: Uuid,
    pub os_type: String,
    pub cpu_cores: u32,
    pub memory_mb: u32,
    pub disk_size_gb: u32,
    pub network_enabled: bool,
    pub gpu_passthrough: bool,
    pub usb_passthrough: bool,
    pub iso_path: Option<PathBuf>,
    pub disk_path: PathBuf,
    pub snapshot_path: Option<PathBuf>,
}

/// Virtual Machine instance
#[derive(Debug)]
pub struct VirtualMachine {
    pub config: VMConfig,
    pub status: VMStatus,
    pub pid: Option<u32>,
    pub start_time: Option<chrono::DateTime<chrono::Utc>>,
}

/// VM Manager - handles VM lifecycle operations
pub struct VMManager {
    vms: RwLock<HashMap<Uuid, VirtualMachine>>,
    qemu_wrapper: QEMUWrapper,
    storage: VMStorage,
}

impl VMManager {
    /// Create a new VM manager
    pub fn new() -> Result<Self> {
        let qemu_wrapper = QEMUWrapper::new()?;
        let storage = VMStorage::new()?;

        Ok(Self {
            vms: RwLock::new(HashMap::new()),
            qemu_wrapper,
            storage,
        })
    }

    /// Initialize the VM manager
    pub async fn init(&mut self) -> Result<()> {
        log::info!("Initializing VM Manager");
        
        // Load existing VMs from storage
        let existing_vms = self.storage.load_vms().await?;
        for vm in existing_vms {
            self.vms.write().await.insert(vm.config.uuid, vm);
        }

        log::info!("VM Manager initialized with {} VMs", self.vms.read().await.len());
        Ok(())
    }

    /// Create a new VM
    pub async fn create_vm(&mut self, config: VMConfig) -> Result<Uuid> {
        log::info!("Creating VM: {}", config.name);

        // Create storage for the VM
        self.storage.create_vm_storage(&config).await?;

        // Create VM instance
        let vm = VirtualMachine {
            config: config.clone(),
            status: VMStatus::Stopped,
            pid: None,
            start_time: None,
        };

        let uuid = config.uuid;
        self.vms.write().await.insert(uuid, vm);

        // Save VM configuration
        self.storage.save_vm_config(&config).await?;

        log::info!("VM created successfully: {}", uuid);
        Ok(uuid)
    }

    /// Start a VM
    pub async fn start_vm(&mut self, uuid: Uuid) -> Result<()> {
        log::info!("Starting VM: {}", uuid);

        let mut vms = self.vms.write().await;
        let vm = vms.get_mut(&uuid)
            .ok_or_else(|| anyhow::anyhow!("VM not found: {}", uuid))?;

        if vm.status != VMStatus::Stopped {
            return Err(anyhow::anyhow!("VM is not in stopped state"));
        }

        vm.status = VMStatus::Starting;

        // Start VM using QEMU
        let pid = self.qemu_wrapper.start_vm(&vm.config).await?;
        vm.pid = Some(pid);
        vm.status = VMStatus::Running;
        vm.start_time = Some(chrono::Utc::now());

        log::info!("VM started successfully: {} (PID: {})", uuid, pid);
        Ok(())
    }

    /// Stop a VM
    pub async fn stop_vm(&mut self, uuid: Uuid) -> Result<()> {
        log::info!("Stopping VM: {}", uuid);

        let mut vms = self.vms.write().await;
        let vm = vms.get_mut(&uuid)
            .ok_or_else(|| anyhow::anyhow!("VM not found: {}", uuid))?;

        if vm.status != VMStatus::Running {
            return Err(anyhow::anyhow!("VM is not running"));
        }

        vm.status = VMStatus::Stopping;

        // Stop VM using QEMU
        if let Some(pid) = vm.pid {
            self.qemu_wrapper.stop_vm(pid).await?;
        }

        vm.status = VMStatus::Stopped;
        vm.pid = None;
        vm.start_time = None;

        log::info!("VM stopped successfully: {}", uuid);
        Ok(())
    }

    /// Pause a VM
    pub async fn pause_vm(&mut self, uuid: Uuid) -> Result<()> {
        log::info!("Pausing VM: {}", uuid);

        let mut vms = self.vms.write().await;
        let vm = vms.get_mut(&uuid)
            .ok_or_else(|| anyhow::anyhow!("VM not found: {}", uuid))?;

        if vm.status != VMStatus::Running {
            return Err(anyhow::anyhow!("VM is not running"));
        }

        // Pause VM using QEMU
        if let Some(pid) = vm.pid {
            self.qemu_wrapper.pause_vm(pid).await?;
        }

        vm.status = VMStatus::Paused;
        log::info!("VM paused successfully: {}", uuid);
        Ok(())
    }

    /// Resume a VM
    pub async fn resume_vm(&mut self, uuid: Uuid) -> Result<()> {
        log::info!("Resuming VM: {}", uuid);

        let mut vms = self.vms.write().await;
        let vm = vms.get_mut(&uuid)
            .ok_or_else(|| anyhow::anyhow!("VM not found: {}", uuid))?;

        if vm.status != VMStatus::Paused {
            return Err(anyhow::anyhow!("VM is not paused"));
        }

        // Resume VM using QEMU
        if let Some(pid) = vm.pid {
            self.qemu_wrapper.resume_vm(pid).await?;
        }

        vm.status = VMStatus::Running;
        log::info!("VM resumed successfully: {}", uuid);
        Ok(())
    }

    /// Delete a VM
    pub async fn delete_vm(&mut self, uuid: Uuid) -> Result<()> {
        log::info!("Deleting VM: {}", uuid);

        // Stop VM if running
        if let Ok(()) = self.stop_vm(uuid).await {
            // VM was running and stopped
        }

        // Remove from memory
        self.vms.write().await.remove(&uuid);

        // Remove from storage
        self.storage.delete_vm_storage(uuid).await?;

        log::info!("VM deleted successfully: {}", uuid);
        Ok(())
    }

    /// List all VMs
    pub async fn list_vms(&self) -> Vec<&VirtualMachine> {
        self.vms.read().await.values().collect()
    }

    /// Get VM by UUID
    pub async fn get_vm(&self, uuid: Uuid) -> Option<&VirtualMachine> {
        self.vms.read().await.get(&uuid)
    }

    /// Create snapshot of a VM
    pub async fn create_snapshot(&mut self, uuid: Uuid, name: &str) -> Result<()> {
        log::info!("Creating snapshot '{}' for VM: {}", name, uuid);

        let vms = self.vms.read().await;
        let vm = vms.get(&uuid)
            .ok_or_else(|| anyhow::anyhow!("VM not found: {}", uuid))?;

        // Create snapshot using storage
        self.storage.create_snapshot(&vm.config, name).await?;

        log::info!("Snapshot created successfully: {}", name);
        Ok(())
    }

    /// Restore VM from snapshot
    pub async fn restore_snapshot(&mut self, uuid: Uuid, snapshot_name: &str) -> Result<()> {
        log::info!("Restoring snapshot '{}' for VM: {}", snapshot_name, uuid);

        // Stop VM if running
        if let Ok(()) = self.stop_vm(uuid).await {
            // VM was running and stopped
        }

        // Restore snapshot using storage
        self.storage.restore_snapshot(uuid, snapshot_name).await?;

        log::info!("Snapshot restored successfully: {}", snapshot_name);
        Ok(())
    }
} 