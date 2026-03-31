# Contributing to Neolyx OS

Thank you for your interest in contributing to Neolyx OS! We welcome contributions from developers of all skill levels.

## Getting Started

1. Fork the repository
2. Clone your fork: `git clone https://github.com/YOUR_USERNAME/NeolyxOS.git`
3. Create a new branch: `git checkout -b feature/your-feature-name`
4. Make your changes
5. Test your changes thoroughly
6. Commit with clear messages: `git commit -m "Add: description of your changes"`
7. Push to your fork: `git push origin feature/your-feature-name`
8. Open a Pull Request

## Development Setup

### Prerequisites
- GCC cross-compiler for x86_64-elf
- NASM assembler
- GNU Make
- QEMU (for testing)
- UEFI firmware (OVMF)

### Building
```bash
make clean
make all
```

### Testing
```bash
./boot_test.sh
```

See [PROJECT_STRUCTURE.md](PROJECT_STRUCTURE.md) for detailed architecture information.

## Contribution Guidelines

### Code Style
- Use consistent indentation (4 spaces)
- Follow existing code formatting
- Add comments for complex logic
- Keep functions focused and modular

### Commit Messages
- Use clear, descriptive commit messages
- Start with a verb (Add, Fix, Update, Remove, etc.)
- Reference issue numbers when applicable

### Pull Requests
- Provide a clear description of changes
- Link related issues
- Ensure all tests pass
- Update documentation if needed
- Keep PRs focused on a single feature or fix

## Areas for Contribution

### Beginner-Friendly
- Documentation improvements
- Icon design and UI enhancements
- Bug reports and testing
- Code comments and examples

### Intermediate
- Driver development
- File system improvements
- Desktop environment features
- Network stack enhancements

### Advanced
- Kernel optimization
- Memory management
- Scheduler improvements
- Hardware support expansion

## Reporting Issues

When reporting bugs, please include:
- OS version and build information
- Steps to reproduce
- Expected vs actual behavior
- Relevant logs or screenshots
- Hardware specifications (if relevant)

## Questions?

Feel free to open an issue for questions or join discussions. We're here to help!

Visit our website at [neolyx.ketivee.com](https://neolyx.ketivee.com/) for more information.

## Support the Project

If you'd like to support Neolyx OS development, consider [making a donation](https://neolyx.ketivee.com/donate).

## License

By contributing, you agree that your contributions will be licensed under the same license as the project (see [LICENSE](LICENSE)).
