#!/bin/bash

echo "Building ReoLab Virtualization Manager..."

# Check if Rust is installed
if ! command -v rustc &> /dev/null; then
    echo "Error: Rust is not installed. Please install Rust from https://rustup.rs/"
    exit 1
fi

# Build CLI version
echo "Building CLI version..."
cargo build --release --bin reolabctl
if [ $? -ne 0 ]; then
    echo "Error: Failed to build CLI version"
    exit 1
fi

# Build GUI version (if Qt is available)
echo "Building GUI version..."
cargo build --release --bin reolab-gui --features gui-qt
if [ $? -ne 0 ]; then
    echo "Warning: Failed to build GUI version. Qt dependencies may not be available."
    echo "CLI version was built successfully."
fi

echo "Build completed successfully!"
echo
echo "Executables:"
echo "  reolabctl - Command line interface"
echo "  reolab-gui - Graphical user interface (if Qt is available)"
echo
echo "Usage:"
echo "  ./target/release/reolabctl list-vms"
echo "  ./target/release/reolabctl create-vm --name 'Windows 11' --os windows"
echo "  ./target/release/reolab-gui" 