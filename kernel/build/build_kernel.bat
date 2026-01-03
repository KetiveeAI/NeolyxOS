@echo off
echo Building Neolyx OS Kernel...
echo.

REM Set MSYS2 path
set MSYS2_PATH=C:\msys64\usr\bin\bash.exe

REM Check if MSYS2 exists
if not exist "%MSYS2_PATH%" (
    echo ERROR: MSYS2 not found at: %MSYS2_PATH%
    echo Please install MSYS2 from https://www.msys2.org/
    pause
    exit /b 1
)

echo Found MSYS2 at: %MSYS2_PATH%
echo.

REM Get current directory
set CURRENT_DIR=%CD%

echo Building kernel in MSYS2 bash environment...
echo Project directory: %CURRENT_DIR%
echo.

REM Run the build in MSYS2
"%MSYS2_PATH%" --login -c "cd '%CURRENT_DIR%' && make clean && make"

if errorlevel 1 (
    echo.
    echo Build failed!
    pause
    exit /b 1
)

echo.
echo Kernel build completed successfully!
echo Generated files:
dir *.elf *.o 2>nul
pause 