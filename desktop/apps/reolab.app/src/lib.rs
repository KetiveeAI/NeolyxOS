// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

pub mod core;
pub mod virtualization;
pub mod drivers;
pub mod cli;
pub mod config;
pub mod storage;
pub mod gui;

pub use core::vm_manager::VMManager;
pub use core::hardware_scan::HardwareScanner;
pub use core::vm_profile::VMProfileGenerator;

/// ReoLab version information
pub const VERSION: &str = env!("CARGO_PKG_VERSION");
pub const AUTHORS: &str = env!("CARGO_PKG_AUTHORS");

/// Main ReoLab application struct
pub struct ReoLab {
    vm_manager: VMManager,
    hardware_scanner: HardwareScanner,
    profile_generator: VMProfileGenerator,
}

impl ReoLab {
    /// Create a new ReoLab instance
    pub fn new() -> Result<Self, Box<dyn std::error::Error>> {
        let vm_manager = VMManager::new()?;
        let hardware_scanner = HardwareScanner::new()?;
        let profile_generator = VMProfileGenerator::new()?;

        Ok(Self {
            vm_manager,
            hardware_scanner,
            profile_generator,
        })
    }

    /// Initialize ReoLab
    pub async fn init(&mut self) -> Result<(), Box<dyn std::error::Error>> {
        log::info!("Initializing ReoLab v{}", VERSION);
        
        // Scan hardware
        let hardware_info = self.hardware_scanner.scan().await?;
        log::info!("Hardware scan completed: {:?}", hardware_info);

        // Initialize VM manager
        self.vm_manager.init().await?;
        log::info!("VM manager initialized");

        Ok(())
    }

    /// Get VM manager reference
    pub fn vm_manager(&self) -> &VMManager {
        &self.vm_manager
    }

    /// Get hardware scanner reference
    pub fn hardware_scanner(&self) -> &HardwareScanner {
        &self.hardware_scanner
    }

    /// Get profile generator reference
    pub fn profile_generator(&self) -> &VMProfileGenerator {
        &self.profile_generator
    }
}

impl Default for ReoLab {
    fn default() -> Self {
        Self::new().expect("Failed to create ReoLab instance")
    }
} 