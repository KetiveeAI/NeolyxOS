# Changelog

All notable changes to Neolyx OS will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Initial project structure
- UEFI bootloader implementation
- 64-bit C kernel with multitasking
- NXFS file system
- Graphical desktop environment
- Network manager and TCP/IP stack
- nxpkg package manager
- Hardware drivers (GPU, storage, input, network)
- Custom icon library
- File explorer
- Community files (CODE_OF_CONDUCT, CONTRIBUTING, SECURITY, SUPPORT)
- GitHub issue and PR templates

### Changed
- Enhanced README with better documentation structure

### Fixed
- Various bug fixes and stability improvements

## [0.1.0] - Initial Development

### Added
- Core operating system foundation
- Basic kernel functionality
- Initial hardware support

---

## How to Update This Changelog

When making changes, add entries under the `[Unreleased]` section in the appropriate category:

- **Added** for new features
- **Changed** for changes in existing functionality
- **Deprecated** for soon-to-be removed features
- **Removed** for now removed features
- **Fixed** for any bug fixes
- **Security** for vulnerability fixes

When releasing a new version, move items from `[Unreleased]` to a new version section with the date.
