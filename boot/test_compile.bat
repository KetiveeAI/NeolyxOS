@echo off
echo Building Neolyx OS Bootloader...

REM Set up environment
set CC=C:\msys64\mingw64\bin\gcc.exe
set LD=C:\msys64\mingw64\bin\ld.exe
set OBJCOPY=C:\msys64\mingw64\bin\objcopy.exe

REM Clean previous build
if exist *.o del *.o
if exist *.EFI del *.EFI

REM Build main bootloader
echo Building main bootloader...
%CC% -m64 -march=x86-64 -mtune=generic -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -O2 -I./inc -I../include -I./gnu-efi-3.0.18/inc main.c -o main.o
%CC% -m64 -march=x86-64 -mtune=generic -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -O2 -I./inc -I../include -I./gnu-efi-3.0.18/inc efilib.c -o efilib.o
%CC% -m64 -march=x86-64 -mtune=generic -nostdlib -nostdinc -fno-builtin -fno-stack-protector -nostartfiles -nodefaultlibs -Wall -Wextra -c -O2 -I./inc -I../include -I./gnu-efi-3.0.18/inc graphics.c -o graphics.o

REM Link bootloader
%LD% -melf_x86_64 -T gnu-efi-3.0.18/gnuefi/elf_x86_64_efi.lds -static -o BOOTX64.EFI main.o efilib.o graphics.o

if exist BOOTX64.EFI (
    echo Build successful! BOOTX64.EFI created.
    echo File size:
    dir BOOTX64.EFI
) else (
    echo Build failed!
    exit /b 1
)

echo.
echo Ready for testing in QEMU or UEFI environment.
pause 