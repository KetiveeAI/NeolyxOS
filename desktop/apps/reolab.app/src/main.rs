#![cfg_attr(
    all(not(debug_assertions), target_os = "windows"),
    windows_subsystem = "windows"
)]

use std::sync::Arc;
use tokio::sync::Mutex;

mod core;
mod languages;
mod extensions;
mod config;
mod platform;

use config::Config;
use platform::{Platform, LinuxPlatform};

fn main() {
    // Initialize logging
    env_logger::init();
    
    // Load configuration
    let config = Config::load().unwrap_or_default();
    
    log::info!("Starting ReoLab IDE with Tauri backend");

    let platform: Arc<dyn Platform> = Arc::new(LinuxPlatform::new());

    tauri::Builder::default()
        .manage(Arc::new(Mutex::new(config)))
        .manage(platform)
        .run(tauri::generate_context!())
        .expect("error while running tauri application");
}