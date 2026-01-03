// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use serde::{Deserialize, Serialize};
use sysinfo::{System, SystemExt, CpuExt, DiskExt, NetworkExt};
use anyhow::Result;

/// CPU information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CPUInfo {
    pub name: String,
    pub cores: u32,
    pub threads: u32,
    pub frequency_mhz: u64,
    pub architecture: String,
    pub vendor: String,
}

/// Memory information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct MemoryInfo {
    pub total_mb: u64,
    pub available_mb: u64,
    pub used_mb: u64,
}

/// GPU information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GPUInfo {
    pub name: String,
    pub memory_mb: u64,
    pub driver: String,
    pub pci_id: String,
}

/// Network interface information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NetworkInfo {
    pub name: String,
    pub mac_address: String,
    pub ip_address: String,
    pub speed_mbps: u64,
}

/// Storage device information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StorageInfo {
    pub name: String,
    pub size_gb: u64,
    pub type_: String, // SSD, HDD, NVMe
    pub mount_point: String,
}

/// Complete hardware information
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct HardwareInfo {
    pub cpu: CPUInfo,
    pub memory: MemoryInfo,
    pub gpus: Vec<GPUInfo>,
    pub networks: Vec<NetworkInfo>,
    pub storage: Vec<StorageInfo>,
    pub virtualization_support: bool,
}

/// Hardware scanner for detecting system resources
pub struct HardwareScanner {
    sys: System,
}

impl HardwareScanner {
    /// Create a new hardware scanner
    pub fn new() -> Result<Self> {
        let mut sys = System::new_all();
        sys.refresh_all();

        Ok(Self { sys })
    }

    /// Scan all hardware components
    pub async fn scan(&mut self) -> Result<HardwareInfo> {
        log::info!("Starting hardware scan");

        // Refresh system information
        self.sys.refresh_all();

        let cpu_info = self.scan_cpu()?;
        let memory_info = self.scan_memory()?;
        let gpu_info = self.scan_gpus()?;
        let network_info = self.scan_networks()?;
        let storage_info = self.scan_storage()?;
        let virtualization_support = self.check_virtualization_support()?;

        let hardware_info = HardwareInfo {
            cpu: cpu_info,
            memory: memory_info,
            gpus: gpu_info,
            networks: network_info,
            storage: storage_info,
            virtualization_support,
        };

        log::info!("Hardware scan completed");
        Ok(hardware_info)
    }

    /// Scan CPU information
    fn scan_cpu(&self) -> Result<CPUInfo> {
        let cpus = self.sys.cpus();
        if cpus.is_empty() {
            return Err(anyhow::anyhow!("No CPU detected"));
        }

        let cpu = &cpus[0]; // Use first CPU for general info
        let total_cores = cpus.len() as u32;

        Ok(CPUInfo {
            name: cpu.brand().to_string(),
            cores: total_cores,
            threads: total_cores, // Assuming 1 thread per core for simplicity
            frequency_mhz: cpu.frequency(),
            architecture: self.detect_architecture()?,
            vendor: self.detect_cpu_vendor()?,
        })
    }

    /// Scan memory information
    fn scan_memory(&self) -> Result<MemoryInfo> {
        let total = self.sys.total_memory();
        let used = self.sys.used_memory();
        let available = total - used;

        Ok(MemoryInfo {
            total_mb: total,
            available_mb: available,
            used_mb: used,
        })
    }

    /// Scan GPU information
    fn scan_gpus(&self) -> Result<Vec<GPUInfo>> {
        // Note: sysinfo doesn't provide GPU info directly
        // This would need to be implemented using other methods
        // For now, return empty vector
        log::warn!("GPU scanning not implemented yet");
        Ok(Vec::new())
    }

    /// Scan network interfaces
    fn scan_networks(&self) -> Result<Vec<NetworkInfo>> {
        let mut networks = Vec::new();

        for (name, network) in self.sys.networks() {
            let network_info = NetworkInfo {
                name: name.clone(),
                mac_address: network.mac_address().to_string(),
                ip_address: "0.0.0.0".to_string(), // Would need additional scanning
                speed_mbps: 1000, // Default assumption
            };
            networks.push(network_info);
        }

        Ok(networks)
    }

    /// Scan storage devices
    fn scan_storage(&self) -> Result<Vec<StorageInfo>> {
        let mut storage = Vec::new();

        for disk in self.sys.disks() {
            let storage_info = StorageInfo {
                name: disk.name().to_string_lossy().to_string(),
                size_gb: disk.total_space() / (1024 * 1024 * 1024),
                type_: self.detect_storage_type(disk)?,
                mount_point: disk.mount_point().to_string_lossy().to_string(),
            };
            storage.push(storage_info);
        }

        Ok(storage)
    }

    /// Check virtualization support
    fn check_virtualization_support(&self) -> Result<bool> {
        // Check for virtualization support in CPU
        // This would need to be implemented using CPUID or similar
        log::warn!("Virtualization support check not implemented yet");
        Ok(true) // Assume supported for now
    }

    /// Detect CPU architecture
    fn detect_architecture(&self) -> Result<String> {
        #[cfg(target_arch = "x86_64")]
        return Ok("x86_64".to_string());
        
        #[cfg(target_arch = "aarch64")]
        return Ok("aarch64".to_string());
        
        #[cfg(target_arch = "x86")]
        return Ok("x86".to_string());
        
        Ok("unknown".to_string())
    }

    /// Detect CPU vendor
    fn detect_cpu_vendor(&self) -> Result<String> {
        // This would need to be implemented using CPUID
        // For now, return a placeholder
        Ok("Unknown".to_string())
    }

    /// Detect storage type
    fn detect_storage_type(&self, disk: &sysinfo::Disk) -> Result<String> {
        // This would need to be implemented by checking device properties
        // For now, return a placeholder
        Ok("Unknown".to_string())
    }

    /// Get recommended VM configuration based on hardware
    pub fn get_recommended_vm_config(&self, os_type: &str) -> Result<VMConfig> {
        let hardware = self.scan()?;
        
        // Calculate recommended resources based on hardware
        let recommended_cores = (hardware.cpu.cores / 2).max(1);
        let recommended_memory = (hardware.memory.total_mb / 4).max(1024);
        let recommended_disk = 20; // 20GB default

        Ok(VMConfig {
            name: format!("{} VM", os_type),
            uuid: uuid::Uuid::new_v4(),
            os_type: os_type.to_string(),
            cpu_cores: recommended_cores,
            memory_mb: recommended_memory,
            disk_size_gb: recommended_disk,
            network_enabled: true,
            gpu_passthrough: false, // Disabled by default
            usb_passthrough: false, // Disabled by default
            iso_path: None,
            disk_path: PathBuf::from("/tmp/vm_disk.qcow2"),
            snapshot_path: None,
        })
    }
}

// Import VMConfig from vm_manager module
use crate::core::vm_manager::VMConfig;
use std::path::PathBuf; 