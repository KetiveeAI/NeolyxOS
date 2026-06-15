use std::fs;
use std::path::{Path, PathBuf};
use std::process::{Command, Stdio};
use std::io;

/// The core abstraction trait for OS-level operations.
/// This trait ensures that ReoLab can be seamlessly ported to NeolyxOS
/// simply by providing a NeolyxOS implementation of this trait.
pub trait Platform: Send + Sync {
    /// Reads a file to a string
    fn read_file(&self, path: &Path) -> io::Result<String>;
    
    /// Writes a string to a file
    fn write_file(&self, path: &Path, content: &str) -> io::Result<()>;
    
    /// Lists contents of a directory
    fn list_dir(&self, path: &Path) -> io::Result<Vec<PathBuf>>;
    
    /// Spawns a background process and returns its ID
    fn spawn_process(&self, command: &str, args: &[&str]) -> io::Result<u32>;
    
    /// Resolves an absolute path based on the OS path semantics
    fn resolve_path(&self, base: &Path, relative: &str) -> PathBuf;
}

/// Linux (Tauri) implementation of the Platform trait.
pub struct LinuxPlatform;

impl LinuxPlatform {
    pub fn new() -> Self {
        Self
    }
}

impl Platform for LinuxPlatform {
    fn read_file(&self, path: &Path) -> io::Result<String> {
        fs::read_to_string(path)
    }

    fn write_file(&self, path: &Path, content: &str) -> io::Result<()> {
        fs::write(path, content)
    }

    fn list_dir(&self, path: &Path) -> io::Result<Vec<PathBuf>> {
        let mut entries = Vec::new();
        if path.is_dir() {
            for entry in fs::read_dir(path)? {
                let entry = entry?;
                entries.push(entry.path());
            }
        }
        Ok(entries)
    }

    fn spawn_process(&self, command: &str, args: &[&str]) -> io::Result<u32> {
        let child = Command::new(command)
            .args(args)
            .stdout(Stdio::piped())
            .stderr(Stdio::piped())
            .spawn()?;
        
        Ok(child.id())
    }

    fn resolve_path(&self, base: &Path, relative: &str) -> PathBuf {
        base.join(relative)
    }
}
