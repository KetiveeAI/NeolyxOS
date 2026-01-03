# ReoLab IDE - Neolyx OS Multi-Language Development Environment

## 🎯 **Overview**
ReoLab IDE is a comprehensive development environment designed specifically for Neolyx OS, supporting multiple programming languages with deep OS integration.

## 🚀 **Supported Languages**

### **System Languages**
- **C** - Traditional system programming with GCC/Clang
- **C++** - Object-oriented system programming with G++/Clang++
- **Objective-C** - Apple ecosystem compatibility with Clang

### **Modern Languages**
- **Rust** - Memory-safe systems programming with Cargo
- **Perl** - Scripting and automation

### **Custom Language**
- **ReoX** - Native Neolyx OS programming language with `.reox` extension

## 📁 **Project Structure**
```
/reolab/
├── src/
│   ├── main.rs                 # Main IDE application
│   ├── config/                 # Configuration management
│   │   └── mod.rs
│   ├── languages/              # Language support modules
│   │   ├── mod.rs             # Language manager
│   │   ├── c.rs               # C language support
│   │   ├── cpp.rs             # C++ language support
│   │   ├── objc.rs            # Objective-C support
│   │   ├── reox.rs            # ReoX language support
│   │   ├── rust.rs            # Rust support
│   │   └── perl.rs            # Perl support
│   ├── core/                  # Core compiler engine
│   │   ├── mod.rs
│   │   ├── compiler/          # ReoX compiler
│   │   ├── parser/            # Language parsers
│   │   └── ai_integration/    # AI assistance
│   ├── gui/                   # IDE interface
│   │   ├── mod.rs
│   │   ├── ide.rs             # Main IDE window
│   │   ├── editor/            # Code editor
│   │   ├── terminal/          # Integrated terminal
│   │   └── themes/            # UI themes
│   └── extensions/            # Plugin system
│       ├── mod.rs
│       ├── marketplace/       # Extension marketplace
│       ├── debugger/          # Debugging tools
│       └── formatter/         # Code formatting
├── examples/                  # Sample projects
│   ├── hello_c.c
│   └── hello_reox.reox
├── icons/                     # Language icons
├── docs/                      # Documentation
│   └── reox_language.md
└── Cargo.toml                 # Rust dependencies
```

## 🛠 **Features**

### **IDE Features**
- **Multi-language Editor** with syntax highlighting
- **Project Explorer** for file management
- **Integrated Terminal** for command execution
- **Build System** with language-specific compilation
- **Debugging Support** for all languages
- **AI Integration** for code completion and suggestions

### **Language-Specific Features**
- **C/C++**: GCC/Clang compilation, clang-format, linting
- **Objective-C**: Clang compilation, ARC support, framework integration
- **Rust**: Cargo build system, rustfmt, clippy linting
- **Perl**: Syntax checking, perltidy formatting, warning detection
- **ReoX**: Custom compiler (reoxc), native OS integration

### **ReoX Language Features**
- **Native Neolyx OS Integration**
- **Direct Kernel API Access**
- **Memory Safety** with optional ownership model
- **Binary Formats**: `.reoxobj` and `.reoxexe`
- **OS-Specific Keywords**: `kernel::`, `syscall::`, `hardware::`

## 🔧 **Installation & Setup**

### **Prerequisites**
```bash
# Install Rust toolchain
curl --proto '=https' --tlsv1.2 -sSf https://sh.rustup.rs | sh

# Install system compilers
# Ubuntu/Debian
sudo apt install gcc g++ clang clang-format

# macOS
brew install gcc clang rust

# Windows (MSYS2)
pacman -S mingw-w64-x86_64-gcc mingw-w64-x86_64-clang rust
```

### **Build ReoLab IDE**
```bash
cd neolyx-os/apps/reolab
cargo build --release
```

### **Run the IDE**
```bash
# GUI mode
./target/release/reolab-ide

# CLI mode
./target/release/reolabctl --help
```

## 📖 **Usage Examples**

### **C Programming**
```c
// hello.c
#include <stdio.h>

int main() {
    printf("Hello, Neolyx OS!\n");
    return 0;
}
```
```bash
# Compile and run
gcc -o hello hello.c
./hello
```

### **ReoX Programming**
```reox
// hello.reox
fn main() -> int {
    print("Hello, Neolyx OS from ReoX!");
    return 0;
}
```
```bash
# Compile and run
reoxc -o hello.reoxexe hello.reox
./hello.reoxexe
```

### **Rust Programming**
```rust
// main.rs
fn main() {
    println!("Hello, Neolyx OS from Rust!");
}
```
```bash
# Build and run
cargo build --release
./target/release/hello_rust
```

## ⚙️ **Configuration**

### **Language Settings**
```toml
# config.toml
[languages.c]
compiler = "gcc"
flags = ["-Wall", "-Wextra", "-std=c11"]

[languages.cpp]
compiler = "g++"
flags = ["-Wall", "-Wextra", "-std=c++17"]

[languages.reox]
compiler = "reoxc"
flags = ["-O2"]
optimization_level = 2
```

### **Editor Settings**
```toml
[editor]
font_size = 14.0
tab_size = 4
insert_spaces = true
word_wrap = true
line_numbers = true
```

## 🎨 **Themes & Customization**
- **Dark Theme** (default)
- **Light Theme**
- **Syntax Highlighting** for all languages
- **Custom Color Schemes**

## 🔌 **Extensions**
- **Marketplace** for language extensions
- **Debugger** integration
- **Code Formatter** support
- **AI Assistant** integration

## 🚀 **Development Roadmap**

### **Phase 1: Core IDE** ✅
- [x] Multi-language support
- [x] Basic editor functionality
- [x] Project management
- [x] Build system integration

### **Phase 2: Advanced Features** 🚧
- [ ] Advanced debugging
- [ ] AI code completion
- [ ] Extension marketplace
- [ ] Performance profiling

### **Phase 3: OS Integration** 📋
- [ ] Direct kernel API access
- [ ] Hardware abstraction layer
- [ ] Real-time system monitoring
- [ ] Custom ReoX runtime

## 🤝 **Contributing**
1. Fork the repository
2. Create a feature branch
3. Make your changes
4. Add tests
5. Submit a pull request

## 📄 **License**
Copyright (c) 2024 KetiveeAI and its branches. Licensed under the KetiveeAI License.

## 🆘 **Support**
- **Documentation**: See `/docs` directory
- **Issues**: Report bugs on GitHub
- **Discussions**: Join community discussions
- **Email**: dev@ketivee.ai

---

**ReoLab IDE** - Empowering Neolyx OS development with modern tools and native integration. 