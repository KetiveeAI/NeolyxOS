// Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

use serde::{Deserialize, Serialize};
use std::path::PathBuf;
use anyhow::Result;
use std::collections::HashMap;

/// ReoLab configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReoLabConfig {
    pub storage: StorageConfig,
    pub virtualization: VirtualizationConfig,
    pub network: NetworkConfig,
    pub gui: GUIConfig,
}

/// Storage configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct StorageConfig {
    pub base_path: PathBuf,
    pub default_disk_format: String,
    pub enable_compression: bool,
}

/// Virtualization configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct VirtualizationConfig {
    pub default_hypervisor: String,
    pub enable_kvm: bool,
    pub enable_gpu_passthrough: bool,
    pub enable_usb_passthrough: bool,
}

/// Network configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct NetworkConfig {
    pub default_bridge: String,
    pub enable_nat: bool,
    pub dhcp_range: String,
}

/// GUI configuration
#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct GUIConfig {
    pub theme: String,
    pub window_size: (u32, u32),
    pub enable_animations: bool,
}

impl Default for ReoLabConfig {
    fn default() -> Self {
        Self {
            storage: StorageConfig {
                base_path: PathBuf::from("/var/lib/reolab"),
                default_disk_format: "qcow2".to_string(),
                enable_compression: true,
            },
            virtualization: VirtualizationConfig {
                default_hypervisor: "qemu".to_string(),
                enable_kvm: true,
                enable_gpu_passthrough: false,
                enable_usb_passthrough: false,
            },
            network: NetworkConfig {
                default_bridge: "virbr0".to_string(),
                enable_nat: true,
                dhcp_range: "192.168.122.2,192.168.122.254".to_string(),
            },
            gui: GUIConfig {
                theme: "dark".to_string(),
                window_size: (1024, 768),
                enable_animations: true,
            },
        }
    }
}

/// Configuration manager
pub struct ConfigManager {
    config: ReoLabConfig,
    config_path: PathBuf,
}

impl ConfigManager {
    /// Create a new configuration manager
    pub fn new() -> Result<Self> {
        let config_path = PathBuf::from("/etc/reolab/config.toml");
        let config = Self::load_config(&config_path)?;
        
        Ok(Self { config, config_path })
    }

    /// Load configuration from file
    fn load_config(path: &PathBuf) -> Result<ReoLabConfig> {
        if path.exists() {
            let config_content = std::fs::read_to_string(path)?;
            let config: ReoLabConfig = toml::from_str(&config_content)?;
            Ok(config)
        } else {
            // Return default configuration
            Ok(ReoLabConfig::default())
        }
    }

    /// Save configuration to file
    pub fn save_config(&self) -> Result<()> {
        let config_content = toml::to_string_pretty(&self.config)?;
        
        // Create parent directory if it doesn't exist
        if let Some(parent) = self.config_path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        
        std::fs::write(&self.config_path, config_content)?;
        Ok(())
    }

    /// Get configuration reference
    pub fn get_config(&self) -> &ReoLabConfig {
        &self.config
    }

    /// Get mutable configuration reference
    pub fn get_config_mut(&mut self) -> &mut ReoLabConfig {
        &mut self.config
    }
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct Config {
    pub editor: EditorConfig,
    pub languages: LanguageConfig,
    pub compiler: CompilerConfig,
    pub theme: ThemeConfig,
    pub ai: AIConfig,
    pub extensions: ExtensionsConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct EditorConfig {
    pub font_size: f32,
    pub tab_size: usize,
    pub insert_spaces: bool,
    pub word_wrap: bool,
    pub line_numbers: bool,
    pub minimap: bool,
    pub auto_save: bool,
    pub auto_save_interval: u64, // seconds
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct LanguageConfig {
    pub c: CConfig,
    pub cpp: CppConfig,
    pub objc: ObjCConfig,
    pub reox: ReoxConfig,
    pub rust: RustConfig,
    pub perl: PerlConfig,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CConfig {
    pub compiler: String,
    pub flags: Vec<String>,
    pub include_paths: Vec<PathBuf>,
    pub library_paths: Vec<PathBuf>,
    pub libraries: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CppConfig {
    pub compiler: String,
    pub flags: Vec<String>,
    pub include_paths: Vec<PathBuf>,
    pub library_paths: Vec<PathBuf>,
    pub libraries: Vec<String>,
    pub cpp_standard: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ObjCConfig {
    pub compiler: String,
    pub flags: Vec<String>,
    pub include_paths: Vec<PathBuf>,
    pub library_paths: Vec<PathBuf>,
    pub libraries: Vec<String>,
    pub framework_paths: Vec<PathBuf>,
    pub frameworks: Vec<String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ReoxConfig {
    pub compiler: String,
    pub flags: Vec<String>,
    pub optimization_level: u8,
    pub debug_info: bool,
    pub target_arch: String,
    pub target_os: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct RustConfig {
    pub cargo_path: String,
    pub flags: Vec<String>,
    pub target: String,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct PerlConfig {
    pub interpreter: String,
    pub flags: Vec<String>,
    pub module_paths: Vec<PathBuf>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct CompilerConfig {
    pub output_directory: PathBuf,
    pub intermediate_directory: PathBuf,
    pub parallel_compilation: bool,
    pub max_jobs: usize,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ThemeConfig {
    pub name: String,
    pub dark_mode: bool,
    pub syntax_highlighting: bool,
    pub custom_colors: HashMap<String, String>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct AIConfig {
    pub enabled: bool,
    pub api_key: Option<String>,
    pub model: String,
    pub max_tokens: usize,
    pub temperature: f32,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
pub struct ExtensionsConfig {
    pub enabled_extensions: Vec<String>,
    pub extension_settings: HashMap<String, serde_json::Value>,
}

impl Default for Config {
    fn default() -> Self {
        Self {
            editor: EditorConfig::default(),
            languages: LanguageConfig::default(),
            compiler: CompilerConfig::default(),
            theme: ThemeConfig::default(),
            ai: AIConfig::default(),
            extensions: ExtensionsConfig::default(),
        }
    }
}

impl Default for EditorConfig {
    fn default() -> Self {
        Self {
            font_size: 14.0,
            tab_size: 4,
            insert_spaces: true,
            word_wrap: true,
            line_numbers: true,
            minimap: true,
            auto_save: true,
            auto_save_interval: 30,
        }
    }
}

impl Default for LanguageConfig {
    fn default() -> Self {
        Self {
            c: CConfig::default(),
            cpp: CppConfig::default(),
            objc: ObjCConfig::default(),
            reox: ReoxConfig::default(),
            rust: RustConfig::default(),
            perl: PerlConfig::default(),
        }
    }
}

impl Default for CConfig {
    fn default() -> Self {
        Self {
            compiler: "gcc".to_string(),
            flags: vec!["-Wall".to_string(), "-Wextra".to_string(), "-std=c11".to_string()],
            include_paths: vec![],
            library_paths: vec![],
            libraries: vec![],
        }
    }
}

impl Default for CppConfig {
    fn default() -> Self {
        Self {
            compiler: "g++".to_string(),
            flags: vec!["-Wall".to_string(), "-Wextra".to_string(), "-std=c++17".to_string()],
            include_paths: vec![],
            library_paths: vec![],
            libraries: vec![],
            cpp_standard: "c++17".to_string(),
        }
    }
}

impl Default for ObjCConfig {
    fn default() -> Self {
        Self {
            compiler: "clang".to_string(),
            flags: vec!["-Wall".to_string(), "-Wextra".to_string(), "-fobjc-arc".to_string()],
            include_paths: vec![],
            library_paths: vec![],
            libraries: vec![],
            framework_paths: vec![],
            frameworks: vec![],
        }
    }
}

impl Default for ReoxConfig {
    fn default() -> Self {
        Self {
            compiler: "reoxc".to_string(),
            flags: vec!["-O2".to_string()],
            optimization_level: 2,
            debug_info: true,
            target_arch: "x86_64".to_string(),
            target_os: "neolyx".to_string(),
        }
    }
}

impl Default for RustConfig {
    fn default() -> Self {
        Self {
            cargo_path: "cargo".to_string(),
            flags: vec![],
            target: "x86_64-unknown-linux-gnu".to_string(),
        }
    }
}

impl Default for PerlConfig {
    fn default() -> Self {
        Self {
            interpreter: "perl".to_string(),
            flags: vec!["-w".to_string()],
            module_paths: vec![],
        }
    }
}

impl Default for CompilerConfig {
    fn default() -> Self {
        Self {
            output_directory: PathBuf::from("build"),
            intermediate_directory: PathBuf::from("build/obj"),
            parallel_compilation: true,
            max_jobs: num_cpus::get(),
        }
    }
}

impl Default for ThemeConfig {
    fn default() -> Self {
        Self {
            name: "dark".to_string(),
            dark_mode: true,
            syntax_highlighting: true,
            custom_colors: HashMap::new(),
        }
    }
}

impl Default for AIConfig {
    fn default() -> Self {
        Self {
            enabled: false,
            api_key: None,
            model: "gpt-4".to_string(),
            max_tokens: 2048,
            temperature: 0.7,
        }
    }
}

impl Default for ExtensionsConfig {
    fn default() -> Self {
        Self {
            enabled_extensions: vec![],
            extension_settings: HashMap::new(),
        }
    }
}

impl Config {
    pub fn load() -> Result<Self> {
        let config_path = Self::get_config_path()?;
        
        if config_path.exists() {
            let content = std::fs::read_to_string(config_path)?;
            let config: Config = toml::from_str(&content)?;
            Ok(config)
        } else {
            let config = Config::default();
            config.save()?;
            Ok(config)
        }
    }
    
    pub fn save(&self) -> Result<()> {
        let config_path = Self::get_config_path()?;
        
        // Create parent directory if it doesn't exist
        if let Some(parent) = config_path.parent() {
            std::fs::create_dir_all(parent)?;
        }
        
        let content = toml::to_string_pretty(self)?;
        std::fs::write(config_path, content)?;
        Ok(())
    }
    
    fn get_config_path() -> Result<PathBuf> {
        let mut path = dirs::config_dir()
            .ok_or_else(|| anyhow::anyhow!("Could not find config directory"))?;
        path.push("reolab");
        path.push("config.toml");
        Ok(path)
    }
} 