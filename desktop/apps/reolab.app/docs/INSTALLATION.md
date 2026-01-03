# ReoLab IDE Installation Guide

## 🚀 **Quick Start**

### Prerequisites
- **Rust toolchain** (1.70+)
- **System compilers** (GCC, Clang, etc.)
- **Git** for cloning the repository

### Installation Steps

1. **Clone the repository**
   ```bash
   git clone https://github.com/ketivee/neolyx-os.git
   cd neolyx-os/apps/reolab
   ```

2. **Install Rust** (if not already installed)
   ```bash
   curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
   source ~/.cargo/env
   ```

3. **Install system compilers**
   ```bash
   # Ubuntu/Debian
   sudo apt update
   sudo apt install gcc g++ clang clang-format make

   # macOS
   brew install gcc clang rust

   # Windows (MSYS2)
   pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-clang rust
   ```

4. **Build ReoLab IDE**
   ```bash
   cargo build --release
   ```

5. **Run the IDE**
   ```bash
   ./target/release/reolab-ide
   ```

## 📋 **Detailed Installation by Platform**

### Ubuntu/Debian Linux

```bash
# Update package list
sudo apt update

# Install essential build tools
sudo apt install build-essential

# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Install compilers and tools
sudo apt install gcc g++ clang clang-format make cmake

# Install additional tools
sudo apt install git curl wget

# Clone and build
git clone https://github.com/ketivee/neolyx-os.git
cd neolyx-os/apps/reolab
cargo build --release
```

### macOS

```bash
# Install Homebrew (if not installed)
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Install compilers and tools
brew install gcc clang rust cmake

# Install additional tools
brew install git curl wget

# Clone and build
git clone https://github.com/ketivee/neolyx-os.git
cd neolyx-os/apps/reolab
cargo build --release
```

### Windows (MSYS2)

```bash
# Install MSYS2 from https://www.msys2.org/

# Open MSYS2 MinGW64 terminal

# Update package database
pacman -Syu

# Install Rust
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh
source ~/.cargo/env

# Install compilers and tools
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-clang rust cmake

# Install additional tools
pacman -S git curl wget

# Clone and build
git clone https://github.com/ketivee/neolyx-os.git
cd neolyx-os/apps/reolab
cargo build --release
```

### Windows (WSL)

```bash
# Install WSL2 and Ubuntu from Microsoft Store

# Follow Ubuntu installation steps above
```

## 🔧 **Language-Specific Setup**

### C/C++ Development
```bash
# Verify GCC installation
gcc --version
g++ --version

# Verify Clang installation
clang --version
clang++ --version

# Install clang-format for code formatting
# Ubuntu/Debian
sudo apt install clang-format

# macOS
brew install clang-format

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-clang-tools-extra
```

### Rust Development
```bash
# Verify Rust installation
rustc --version
cargo --version

# Install additional Rust tools
cargo install rustfmt
cargo install clippy
```

### Perl Development
```bash
# Verify Perl installation
perl --version

# Install perltidy for code formatting
# Ubuntu/Debian
sudo apt install perltidy

# macOS
brew install perltidy

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-perl-cpanplus
cpan App::perltidy
```

### ReoX Development
```bash
# ReoX compiler is included with ReoLab IDE
# No additional setup required
```

## ⚙️ **Configuration**

### First Run Setup
1. **Launch ReoLab IDE**
   ```bash
   ./target/release/reolab-ide
   ```

2. **Configure language settings**
   - Go to `Settings` → `Languages`
   - Verify compiler paths
   - Set default flags and options

3. **Configure editor preferences**
   - Go to `Settings` → `Editor`
   - Set font size, tab size, theme
   - Enable/disable features

### Configuration Files
```bash
# Global configuration
~/.config/reolab/config.toml

# Project-specific configuration
./reolab.toml
```

## 🐛 **Troubleshooting**

### Common Issues

#### Rust not found
```bash
# Add Rust to PATH
export PATH="$HOME/.cargo/bin:$PATH"
# Add to ~/.bashrc or ~/.zshrc for persistence
```

#### Compiler not found
```bash
# Verify installation
which gcc
which g++
which clang

# Add to PATH if needed
export PATH="/usr/bin:$PATH"
```

#### Build errors
```bash
# Clean and rebuild
cargo clean
cargo build --release

# Update dependencies
cargo update
```

#### Permission errors
```bash
# Fix permissions
chmod +x target/release/reolab-ide
```

### Getting Help
- **Documentation**: See `/docs` directory
- **Issues**: Report on GitHub
- **Discussions**: Join community discussions
- **Email**: dev@ketivee.ai

## 🎯 **Next Steps**

After installation:
1. **Read the README** for usage instructions
2. **Try the examples** in `/examples` directory
3. **Configure your preferences** in settings
4. **Join the community** for support and updates

---

**ReoLab IDE** - Ready for Neolyx OS development! 🚀 