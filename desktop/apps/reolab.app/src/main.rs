use eframe::egui;
use std::path::PathBuf;
use std::sync::Arc;
use tokio::sync::Mutex;

mod gui;
mod core;
mod languages;
mod extensions;
mod config;

use gui::ide::ReoLabIDE;
use config::Config;

#[tokio::main]
async fn main() -> Result<(), eframe::Error> {
    // Initialize logging
    env_logger::init();
    
    // Load configuration
    let config = Config::load().unwrap_or_default();
    
    // Create the IDE
    let ide = ReoLabIDE::new(config);
    
    // Run the GUI
    let options = eframe::NativeOptions {
        initial_window_size: Some(egui::vec2(1200.0, 800.0)),
        min_window_size: Some(egui::vec2(800.0, 600.0)),
        ..Default::default()
    };
    
    eframe::run_native(
        "ReoLab IDE - Neolyx OS Development Environment",
        options,
        Box::new(|_cc| Box::new(ide)),
    )
} 