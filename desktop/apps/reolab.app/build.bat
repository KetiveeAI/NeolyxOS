@echo off
echo Building ReoLab Virtualization Manager...

REM Check if Rust is installed
rustc --version >nul 2>&1
if errorlevel 1 (
    echo Error: Rust is not installed. Please install Rust from https://rustup.rs/
    exit /b 1
)

REM Build CLI version
echo Building CLI version...
cargo build --release --bin reolabctl
if errorlevel 1 (
    echo Error: Failed to build CLI version
    exit /b 1
)

REM Build GUI version (if Qt is available)
echo Building GUI version...
cargo build --release --bin reolab-gui --features gui-qt
if errorlevel 1 (
    echo Warning: Failed to build GUI version. Qt dependencies may not be available.
    echo CLI version was built successfully.
)

echo Build completed successfully!
echo.
echo Executables:
echo   reolabctl.exe - Command line interface
echo   reolab-gui.exe - Graphical user interface (if Qt is available)
echo.
echo Usage:
echo   reolabctl.exe list-vms
echo   reolabctl.exe create-vm --name "Windows 11" --os windows
echo   reolab-gui.exe 