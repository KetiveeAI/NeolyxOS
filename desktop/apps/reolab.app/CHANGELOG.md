# Changelog

All notable changes to ReoLab IDE will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.1.0] - 2024-12-25

### Added
- **Initial Release** of ReoLab IDE for Neolyx OS
- **Multi-language support** for C, C++, Objective-C, Rust, Perl, and ReoX
- **Custom ReoX programming language** with `.reox` extension
- **Modern GUI** using egui framework
- **Project Explorer** for file management
- **Integrated Terminal** for command execution
- **Syntax highlighting** for all supported languages
- **Language-specific compilation** with proper error handling
- **Configuration system** with TOML support
- **Extensible plugin architecture** for future enhancements

### ReoX Language Features
- **Native Neolyx OS integration** with kernel API access
- **Memory safety** with optional ownership model
- **Binary formats**: `.reoxobj` and `.reoxexe`
- **OS-specific keywords**: `kernel::`, `syscall::`, `hardware::`
- **Complete language specification** with syntax, types, and features
- **Compiler architecture** with lexer, parser, and AST

### Supported Languages
- **C**: GCC/Clang compilation, clang-format, linting
- **C++**: G++/Clang++ compilation, C++17 support, advanced syntax highlighting
- **Objective-C**: Clang compilation, ARC support, framework integration
- **Rust**: Cargo build system, rustfmt, clippy linting
- **Perl**: Syntax checking, perltidy formatting, warning detection
- **ReoX**: Custom compiler (reoxc), native OS integration

### Documentation
- **Comprehensive README** with usage examples
- **Language specification** for ReoX
- **Installation instructions** for multiple platforms
- **Configuration examples** and best practices
- **Build status documentation** with component overview

### Architecture
- **Modular design** with proper Rust module structure
- **Cross-platform compatibility** (Windows, macOS, Linux)
- **Professional-grade IDE** with modern features
- **Production-ready** for Neolyx OS development

---

## [Unreleased]

### Planned Features
- Advanced debugging support
- AI code completion and suggestions
- Extension marketplace
- Performance profiling tools
- Real-time system monitoring
- Custom ReoX runtime
- Hardware abstraction layer
- Direct kernel API access 