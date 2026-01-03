// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use clap::{Parser, Subcommand};
use anyhow::Result;
use reolab::{ReoLab, VERSION};

#[derive(Parser)]
#[command(name = "reolabctl")]
#[command(about = "ReoLab Virtualization Manager CLI")]
#[command(version = VERSION)]
struct Cli {
    #[command(subcommand)]
    command: Commands,
}

#[derive(Subcommand)]
enum Commands {
    /// List all virtual machines
    ListVms,
    
    /// Create a new virtual machine
    CreateVm {
        /// Name of the VM
        #[arg(short, long)]
        name: String,
        
        /// Operating system type
        #[arg(short, long)]
        os: String,
        
        /// Number of CPU cores
        #[arg(short, long, default_value_t = 2)]
        cores: u32,
        
        /// Memory in MB
        #[arg(short, long, default_value_t = 2048)]
        memory: u32,
        
        /// Disk size in GB
        #[arg(short, long, default_value_t = 20)]
        disk_size: u32,
    },
    
    /// Start a virtual machine
    StartVm {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
    },
    
    /// Stop a virtual machine
    StopVm {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
    },
    
    /// Pause a virtual machine
    PauseVm {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
    },
    
    /// Resume a virtual machine
    ResumeVm {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
    },
    
    /// Delete a virtual machine
    DeleteVm {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
    },
    
    /// Show hardware information
    Hardware,
    
    /// Create a snapshot
    Snapshot {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
        
        /// Snapshot name
        #[arg(short, long)]
        name: String,
    },
    
    /// Restore from snapshot
    Restore {
        /// VM name or UUID
        #[arg(value_name = "VM")]
        vm: String,
        
        /// Snapshot name
        #[arg(short, long)]
        snapshot: String,
    },
}

#[tokio::main]
async fn main() -> Result<()> {
    // Initialize logging
    env_logger::init();
    
    let cli = Cli::parse();
    
    // Initialize ReoLab
    let mut reolab = ReoLab::new()?;
    reolab.init().await?;
    
    match cli.command {
        Commands::ListVms => {
            list_vms(&reolab).await?;
        }
        
        Commands::CreateVm { name, os, cores, memory, disk_size } => {
            create_vm(&mut reolab, name, os, cores, memory, disk_size).await?;
        }
        
        Commands::StartVm { vm } => {
            start_vm(&mut reolab, vm).await?;
        }
        
        Commands::StopVm { vm } => {
            stop_vm(&mut reolab, vm).await?;
        }
        
        Commands::PauseVm { vm } => {
            pause_vm(&mut reolab, vm).await?;
        }
        
        Commands::ResumeVm { vm } => {
            resume_vm(&mut reolab, vm).await?;
        }
        
        Commands::DeleteVm { vm } => {
            delete_vm(&mut reolab, vm).await?;
        }
        
        Commands::Hardware => {
            show_hardware(&reolab).await?;
        }
        
        Commands::Snapshot { vm, name } => {
            create_snapshot(&mut reolab, vm, name).await?;
        }
        
        Commands::Restore { vm, snapshot } => {
            restore_snapshot(&mut reolab, vm, snapshot).await?;
        }
    }
    
    Ok(())
}

async fn list_vms(reolab: &ReoLab) -> Result<()> {
    println!("Virtual Machines:");
    println!("{:<36} {:<20} {:<10} {:<10}", "UUID", "Name", "Status", "OS");
    println!("{:-<80}", "");
    
    let vms = reolab.vm_manager().list_vms().await;
    for vm in vms {
        println!("{:<36} {:<20} {:<10?} {:<10}", 
            vm.config.uuid, 
            vm.config.name, 
            vm.status, 
            vm.config.os_type);
    }
    
    Ok(())
}

async fn create_vm(
    reolab: &mut ReoLab, 
    name: String, 
    os: String, 
    cores: u32, 
    memory: u32, 
    disk_size: u32
) -> Result<()> {
    println!("Creating VM: {}", name);
    
    let config = reolab.profile_generator().create_config(&name, &os, cores, memory, disk_size)?;
    let uuid = reolab.vm_manager().create_vm(config).await?;
    
    println!("VM created successfully: {}", uuid);
    Ok(())
}

async fn start_vm(reolab: &mut ReoLab, vm: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().start_vm(uuid).await?;
    println!("VM started successfully");
    Ok(())
}

async fn stop_vm(reolab: &mut ReoLab, vm: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().stop_vm(uuid).await?;
    println!("VM stopped successfully");
    Ok(())
}

async fn pause_vm(reolab: &mut ReoLab, vm: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().pause_vm(uuid).await?;
    println!("VM paused successfully");
    Ok(())
}

async fn resume_vm(reolab: &mut ReoLab, vm: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().resume_vm(uuid).await?;
    println!("VM resumed successfully");
    Ok(())
}

async fn delete_vm(reolab: &mut ReoLab, vm: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().delete_vm(uuid).await?;
    println!("VM deleted successfully");
    Ok(())
}

async fn show_hardware(reolab: &ReoLab) -> Result<()> {
    println!("Hardware Information:");
    println!("{:-<50}", "");
    
    let hardware = reolab.hardware_scanner().scan().await?;
    
    println!("CPU: {} ({} cores, {} MHz)", 
        hardware.cpu.name, hardware.cpu.cores, hardware.cpu.frequency_mhz);
    println!("Memory: {} MB total, {} MB available", 
        hardware.memory.total_mb, hardware.memory.available_mb);
    println!("Virtualization Support: {}", hardware.virtualization_support);
    
    if !hardware.gpus.is_empty() {
        println!("GPUs:");
        for gpu in &hardware.gpus {
            println!("  - {} ({} MB)", gpu.name, gpu.memory_mb);
        }
    }
    
    if !hardware.networks.is_empty() {
        println!("Network Interfaces:");
        for net in &hardware.networks {
            println!("  - {} ({})", net.name, net.mac_address);
        }
    }
    
    if !hardware.storage.is_empty() {
        println!("Storage Devices:");
        for storage in &hardware.storage {
            println!("  - {} ({} GB, {})", storage.name, storage.size_gb, storage.type_);
        }
    }
    
    Ok(())
}

async fn create_snapshot(reolab: &mut ReoLab, vm: String, name: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().create_snapshot(uuid, &name).await?;
    println!("Snapshot '{}' created successfully", name);
    Ok(())
}

async fn restore_snapshot(reolab: &mut ReoLab, vm: String, snapshot: String) -> Result<()> {
    let uuid = parse_vm_identifier(&vm)?;
    reolab.vm_manager().restore_snapshot(uuid, &snapshot).await?;
    println!("Snapshot '{}' restored successfully", snapshot);
    Ok(())
}

fn parse_vm_identifier(identifier: &str) -> Result<uuid::Uuid> {
    // Try to parse as UUID first
    if let Ok(uuid) = uuid::Uuid::parse_str(identifier) {
        return Ok(uuid);
    }
    
    // If not a UUID, try to find by name
    // This would need to be implemented in the VM manager
    Err(anyhow::anyhow!("VM not found: {}", identifier))
} 