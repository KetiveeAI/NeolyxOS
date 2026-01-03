/*
 * Neolyx OS Configuration System
 * 
 * Copyright (c) 2025 Ketivee Organization
 * All rights reserved.
 * 
 * This file is part of the Neolyx OS project.
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 * 
 * 1. Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 * 
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 * 
 * 3. Neither the name of the Ketivee Organization nor the names of its
 *    contributors may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 * 
 * Project: https://opengit.ketivee.com/projects/neolyx-os
 * License: MIT License
 * Version: 2.1.0
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include <stddef.h>

// Basic types
typedef uint16_t CHAR16;
typedef uint64_t EFI_STATUS;
typedef uint64_t UINTN;
typedef uint32_t UINT32;
typedef uint64_t UINT64;
typedef uint8_t UINT8;
typedef int64_t INTN;

// Configuration constants
#define MAX_PATH_LENGTH 256
#define MAX_SECTION_LENGTH 64
#define MAX_KEY_LENGTH 64
#define MAX_VALUE_LENGTH 512
#define MAX_SECTIONS 16
#define MAX_KEYS_PER_SECTION 32
#define MAX_COMPONENTS 16
#define MAX_DRIVERS 8
#define MAX_BOOT_ENTRIES 4

// NCF (NXFS Config) structures
typedef enum {
    NCF_STRING,
    NCF_NUMBER,
    NCF_BOOL
} NcfValueType;

typedef struct {
    CHAR16 Key[MAX_KEY_LENGTH];
    CHAR16 Value[MAX_VALUE_LENGTH];
    NcfValueType Type;
} NCF_Entry;

typedef struct {
    CHAR16 Name[MAX_SECTION_LENGTH];
    NCF_Entry Entries[MAX_KEYS_PER_SECTION];
    UINTN EntryCount;
} NCF_Section;

typedef struct {
    NCF_Section Sections[MAX_SECTIONS];
    UINTN SectionCount;
} NCF_Config;

// TIE (Targeted Installation Entry) structures
typedef struct {
    CHAR16 Source[MAX_PATH_LENGTH];
    CHAR16 Destination[MAX_PATH_LENGTH];
    CHAR16 Permissions[16];
    UINT8 IsDirectory;
} TIE_Component;

typedef struct {
    CHAR16 Type[32];
    CHAR16 Name[64];
    CHAR16 Version[32];
    CHAR16 Description[128];
    CHAR16 Author[64];
    CHAR16 License[32];
    CHAR16 TargetPath[MAX_PATH_LENGTH];
    TIE_Component Components[MAX_COMPONENTS];
    UINTN ComponentCount;
    CHAR16 Dependencies[MAX_COMPONENTS][64];
    UINTN DependencyCount;
} TIE_Config;

// Driver configuration
typedef struct {
    CHAR16 Name[64];
    CHAR16 Path[MAX_PATH_LENGTH];
    CHAR16 Type[32];
    UINT8 LoadEarly;
    UINT8 Required;
    CHAR16 Parameters[256];
} DriverConfig;

// Boot entry configuration
typedef struct {
    CHAR16 Name[64];
    CHAR16 KernelPath[MAX_PATH_LENGTH];
    CHAR16 InitrdPath[MAX_PATH_LENGTH];
    CHAR16 Parameters[512];
    UINT8 Default;
    UINT8 Timeout;
} BootEntry;

// Enhanced boot configuration structure
typedef struct {
    // Basic boot settings
    CHAR16 KernelPath[MAX_PATH_LENGTH];
    CHAR16 InitrdPath[MAX_PATH_LENGTH];
    UINTN Timeout;
    CHAR16 DefaultMode[32];
    CHAR16 BootParameters[512];
    
    // Memory settings
    UINTN HeapSize;
    UINTN StackSize;
    UINTN KernelLoadAddress;
    UINT8 EnableMemoryProtection;
    
    // Graphics settings
    UINT32 GraphicsMode;
    CHAR16 GraphicsResolution[32];
    UINT8 EnableGraphicsAcceleration;
    UINT8 EnableFramebuffer;
    
    // Security settings
    UINT8 EnableSecureBoot;
    UINT8 RequireKernelSignature;
    CHAR16 BootPassword[64];
    CHAR16 CertificatePath[MAX_PATH_LENGTH];
    
    // Debug settings
    UINT8 EnableDebug;
    UINTN DebugLevel;
    CHAR16 DebugLogFile[MAX_PATH_LENGTH];
    UINT8 EnableSerialDebug;
    UINTN SerialPort;
    UINTN SerialBaudRate;
    
    // Network settings
    UINT8 EnableNetworkBoot;
    UINTN DHCPTimeout;
    CHAR16 TFTPServer[64];
    CHAR16 BootFile[MAX_PATH_LENGTH];
    
    // Driver settings
    DriverConfig Drivers[MAX_DRIVERS];
    UINTN DriverCount;
    
    // Boot entries
    BootEntry BootEntries[MAX_BOOT_ENTRIES];
    UINTN BootEntryCount;
    
    // System information
    CHAR16 SystemName[64];
    CHAR16 SystemVersion[32];
    CHAR16 Architecture[16];
    CHAR16 Platform[32];
} BootConfig;

// Function prototypes
EFI_STATUS ParseNcfFile(const CHAR16* file_data, UINTN file_size, NCF_Config* config);
EFI_STATUS GetNcfValue(const NCF_Config* config, const CHAR16* section, const CHAR16* key, CHAR16* value, UINTN max_len);
EFI_STATUS GetNcfNumber(const NCF_Config* config, const CHAR16* section, const CHAR16* key, UINTN* value);
EFI_STATUS GetNcfBool(const NCF_Config* config, const CHAR16* section, const CHAR16* key, UINT8* value);

EFI_STATUS ParseTieFile(const CHAR16* file_data, UINTN file_size, TIE_Config* config);
EFI_STATUS ReadBootConfig(const CHAR16* config_path, BootConfig* config);
EFI_STATUS ValidateBootConfig(const BootConfig* config);
EFI_STATUS LoadDriverConfig(const CHAR16* driver_path, DriverConfig* driver);

// Utility functions
UINTN StrLen(const CHAR16* str);
UINTN StrCpy(CHAR16* dest, const CHAR16* src);
UINTN StrnCpy(CHAR16* dest, const CHAR16* src, UINTN max_len);
UINTN StrCmp(const CHAR16* str1, const CHAR16* str2);
UINTN StrDecimalToUintn(const CHAR16* str);
UINT8 StrToBool(const CHAR16* str);

// EFI helper macros
#define EFI_ERROR(Status) (((INTN)(Status)) < 0)

#endif // CONFIG_H 