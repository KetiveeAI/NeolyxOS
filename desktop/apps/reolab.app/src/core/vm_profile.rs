// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use anyhow::Result;
use std::path::PathBuf;
use uuid::Uuid;
use crate::core::vm_manager::VMConfig;

/// VM Profile Generator - creates optimal VM configurations
pub struct VMProfileGenerator {
    templates: Vec<VMProfileTemplate>,
}

/// VM Profile Template for different OS types
#[derive(Debug, Clone)]
pub struct VMProfileTemplate {
    pub os_type: String,
    pub name: String,
    pub min_cores: u32,
    pub recommended_cores: u32,
    pub min_memory_mb: u32,
    pub recommended_memory_mb: u32,
    pub min_disk_gb: u32,
    pub recommended_disk_gb: u32,
    pub network_enabled: bool,
    pub gpu_passthrough: bool,
    pub usb_passthrough: bool,
}

impl VMProfileGenerator {
    /// Create a new VM profile generator
    pub fn new() -> Result<Self> {
        let templates = Self::load_default_templates();
        
        Ok(Self { templates })
    }

    /// Create a VM configuration based on OS type and requirements
    pub fn create_config(
        &self,
        name: &str,
        os_type: &str,
        cores: u32,
        memory_mb: u32,
        disk_size_gb: u32,
    ) -> Result<VMConfig> {
        let template = self.find_template(os_type)?;
        
        // Validate requirements against template
        let validated_cores = cores.max(template.min_cores);
        let validated_memory = memory_mb.max(template.min_memory_mb);
        let validated_disk = disk_size_gb.max(template.min_disk_gb);
        
        let disk_path = PathBuf::from(format!("/var/lib/reolab/vms/{}.qcow2", name));
        
        Ok(VMConfig {
            name: name.to_string(),
            uuid: Uuid::new_v4(),
            os_type: os_type.to_string(),
            cpu_cores: validated_cores,
            memory_mb: validated_memory,
            disk_size_gb: validated_disk,
            network_enabled: template.network_enabled,
            gpu_passthrough: template.gpu_passthrough,
            usb_passthrough: template.usb_passthrough,
            iso_path: None,
            disk_path,
            snapshot_path: None,
        })
    }

    /// Create a recommended configuration based on OS type
    pub fn create_recommended_config(&self, name: &str, os_type: &str) -> Result<VMConfig> {
        let template = self.find_template(os_type)?;
        
        self.create_config(
            name,
            os_type,
            template.recommended_cores,
            template.recommended_memory_mb,
            template.recommended_disk_gb,
        )
    }

    /// Find template by OS type
    fn find_template(&self, os_type: &str) -> Result<&VMProfileTemplate> {
        self.templates
            .iter()
            .find(|t| t.os_type == os_type)
            .ok_or_else(|| anyhow::anyhow!("No template found for OS type: {}", os_type))
    }

    /// Load default templates
    fn load_default_templates() -> Vec<VMProfileTemplate> {
        vec![
            // Windows templates
            VMProfileTemplate {
                os_type: "windows".to_string(),
                name: "Windows 11".to_string(),
                min_cores: 2,
                recommended_cores: 4,
                min_memory_mb: 4096,
                recommended_memory_mb: 8192,
                min_disk_gb: 64,
                recommended_disk_gb: 128,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            VMProfileTemplate {
                os_type: "windows10".to_string(),
                name: "Windows 10".to_string(),
                min_cores: 2,
                recommended_cores: 4,
                min_memory_mb: 2048,
                recommended_memory_mb: 4096,
                min_disk_gb: 32,
                recommended_disk_gb: 64,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            
            // Linux templates
            VMProfileTemplate {
                os_type: "ubuntu".to_string(),
                name: "Ubuntu".to_string(),
                min_cores: 1,
                recommended_cores: 2,
                min_memory_mb: 1024,
                recommended_memory_mb: 2048,
                min_disk_gb: 10,
                recommended_disk_gb: 20,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            VMProfileTemplate {
                os_type: "debian".to_string(),
                name: "Debian".to_string(),
                min_cores: 1,
                recommended_cores: 2,
                min_memory_mb: 1024,
                recommended_memory_mb: 2048,
                min_disk_gb: 10,
                recommended_disk_gb: 20,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            VMProfileTemplate {
                os_type: "centos".to_string(),
                name: "CentOS".to_string(),
                min_cores: 1,
                recommended_cores: 2,
                min_memory_mb: 1024,
                recommended_memory_mb: 2048,
                min_disk_gb: 10,
                recommended_disk_gb: 20,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            
            // macOS templates (for development/testing)
            VMProfileTemplate {
                os_type: "macos".to_string(),
                name: "macOS".to_string(),
                min_cores: 2,
                recommended_cores: 4,
                min_memory_mb: 4096,
                recommended_memory_mb: 8192,
                min_disk_gb: 64,
                recommended_disk_gb: 128,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            
            // Gaming templates
            VMProfileTemplate {
                os_type: "gaming".to_string(),
                name: "Gaming VM".to_string(),
                min_cores: 4,
                recommended_cores: 8,
                min_memory_mb: 8192,
                recommended_memory_mb: 16384,
                min_disk_gb: 100,
                recommended_disk_gb: 256,
                network_enabled: true,
                gpu_passthrough: true,
                usb_passthrough: true,
            },
            
            // Development templates
            VMProfileTemplate {
                os_type: "development".to_string(),
                name: "Development VM".to_string(),
                min_cores: 2,
                recommended_cores: 4,
                min_memory_mb: 2048,
                recommended_memory_mb: 4096,
                min_disk_gb: 20,
                recommended_disk_gb: 50,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
            
            // Minimal templates
            VMProfileTemplate {
                os_type: "minimal".to_string(),
                name: "Minimal VM".to_string(),
                min_cores: 1,
                recommended_cores: 1,
                min_memory_mb: 512,
                recommended_memory_mb: 1024,
                min_disk_gb: 5,
                recommended_disk_gb: 10,
                network_enabled: true,
                gpu_passthrough: false,
                usb_passthrough: false,
            },
        ]
    }

    /// Get available OS types
    pub fn get_available_os_types(&self) -> Vec<String> {
        self.templates.iter().map(|t| t.os_type.clone()).collect()
    }

    /// Get template information
    pub fn get_template_info(&self, os_type: &str) -> Result<&VMProfileTemplate> {
        self.find_template(os_type)
    }
} 