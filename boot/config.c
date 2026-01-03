/*
 * Neolyx OS Configuration System Implementation
 * 
 * Copyright (c) 2024 Ketivee Organization
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

#include "inc/config.h"

// Utility functions
UINTN StrLen(const CHAR16* str) {
    UINTN len = 0;
    while (str[len] != 0) len++;
    return len;
}

UINTN StrCpy(CHAR16* dest, const CHAR16* src) {
    UINTN i = 0;
    while (src[i] != 0) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

UINTN StrnCpy(CHAR16* dest, const CHAR16* src, UINTN max_len) {
    UINTN i = 0;
    while (i < max_len - 1 && src[i] != 0) {
        dest[i] = src[i];
        i++;
    }
    dest[i] = 0;
    return i;
}

UINTN StrCmp(const CHAR16* str1, const CHAR16* str2) {
    UINTN i = 0;
    while (str1[i] != 0 && str2[i] != 0) {
        if (str1[i] != str2[i]) {
            return str1[i] - str2[i];
        }
        i++;
    }
    return str1[i] - str2[i];
}

UINTN StrDecimalToUintn(const CHAR16* str) {
    UINTN result = 0;
    UINTN i = 0;
    while (str[i] >= L'0' && str[i] <= L'9') {
        result = result * 10 + (str[i] - L'0');
        i++;
    }
    return result;
}

UINT8 StrToBool(const CHAR16* str) {
    if (StrCmp(str, L"true") == 0 || StrCmp(str, L"1") == 0 || 
        StrCmp(str, L"yes") == 0 || StrCmp(str, L"on") == 0) {
        return 1;
    }
    return 0;
}

// NCF Parser Implementation
EFI_STATUS ParseNcfFile(const CHAR16* file_data, UINTN file_size, NCF_Config* config) {
    if (!file_data || !config) return 1; // EFI_INVALID_PARAMETER
    
    config->SectionCount = 0;
    UINTN pos = 0;
    NCF_Section* current_section = NULL;
    
    while (pos < file_size && config->SectionCount < MAX_SECTIONS) {
        // Skip whitespace
        while (pos < file_size && (file_data[pos] == L' ' || file_data[pos] == L'\t' || 
               file_data[pos] == L'\r' || file_data[pos] == L'\n')) {
            pos++;
        }
        
        if (pos >= file_size) break;
        
        // Skip comments
        if (file_data[pos] == L'#') {
            while (pos < file_size && file_data[pos] != L'\n') pos++;
            continue;
        }
        
        // Parse section header [SectionName]
        if (file_data[pos] == L'[') {
            pos++; // Skip '['
            UINTN section_start = pos;
            while (pos < file_size && file_data[pos] != L']' && file_data[pos] != L'\n') {
                pos++;
            }
            
            if (pos < file_size && file_data[pos] == L']') {
                current_section = &config->Sections[config->SectionCount];
                UINTN section_len = pos - section_start;
                if (section_len >= MAX_SECTION_LENGTH) section_len = MAX_SECTION_LENGTH - 1;
                
                StrnCpy(current_section->Name, &file_data[section_start], section_len);
                current_section->EntryCount = 0;
                config->SectionCount++;
                pos++; // Skip ']'
            }
            continue;
        }
        
        // Parse key=value pairs
        if (current_section && current_section->EntryCount < MAX_KEYS_PER_SECTION) {
            UINTN key_start = pos;
            while (pos < file_size && file_data[pos] != L'=' && file_data[pos] != L'\n') {
                pos++;
            }
            
            if (pos < file_size && file_data[pos] == L'=') {
                UINTN key_len = pos - key_start;
                if (key_len >= MAX_KEY_LENGTH) key_len = MAX_KEY_LENGTH - 1;
                
                NCF_Entry* entry = &current_section->Entries[current_section->EntryCount];
                StrnCpy(entry->Key, &file_data[key_start], key_len);
                
                pos++; // Skip '='
                UINTN value_start = pos;
                while (pos < file_size && file_data[pos] != L'\n') {
                    pos++;
                }
                
                UINTN value_len = pos - value_start;
                if (value_len >= MAX_VALUE_LENGTH) value_len = MAX_VALUE_LENGTH - 1;
                
                StrnCpy(entry->Value, &file_data[value_start], value_len);
                
                // Determine value type
                if (StrToBool(entry->Value)) {
                    entry->Type = NCF_BOOL;
                } else if (entry->Value[0] >= L'0' && entry->Value[0] <= L'9') {
                    entry->Type = NCF_NUMBER;
                } else {
                    entry->Type = NCF_STRING;
                }
                
                current_section->EntryCount++;
            }
        }
        
        // Skip to next line
        while (pos < file_size && file_data[pos] != L'\n') pos++;
        if (pos < file_size) pos++; // Skip '\n'
    }
    
    return 0; // EFI_SUCCESS
}

EFI_STATUS GetNcfValue(const NCF_Config* config, const CHAR16* section, const CHAR16* key, CHAR16* value, UINTN max_len) {
    if (!config || !section || !key || !value) return 1; // EFI_INVALID_PARAMETER
    
    for (UINTN i = 0; i < config->SectionCount; i++) {
        if (StrCmp(config->Sections[i].Name, section) == 0) {
            for (UINTN j = 0; j < config->Sections[i].EntryCount; j++) {
                if (StrCmp(config->Sections[i].Entries[j].Key, key) == 0) {
                    StrnCpy(value, config->Sections[i].Entries[j].Value, max_len);
                    return 0; // EFI_SUCCESS
                }
            }
        }
    }
    
    return 14; // EFI_NOT_FOUND
}

EFI_STATUS GetNcfNumber(const NCF_Config* config, const CHAR16* section, const CHAR16* key, UINTN* value) {
    CHAR16 str_value[MAX_VALUE_LENGTH];
    EFI_STATUS status = GetNcfValue(config, section, key, str_value, MAX_VALUE_LENGTH);
    if (status == 0) { // EFI_SUCCESS
        *value = StrDecimalToUintn(str_value);
    }
    return status;
}

EFI_STATUS GetNcfBool(const NCF_Config* config, const CHAR16* section, const CHAR16* key, UINT8* value) {
    CHAR16 str_value[MAX_VALUE_LENGTH];
    EFI_STATUS status = GetNcfValue(config, section, key, str_value, MAX_VALUE_LENGTH);
    if (status == 0) { // EFI_SUCCESS
        *value = StrToBool(str_value);
    }
    return status;
}

// TIE Parser Implementation (Simplified JSON-like parser)
EFI_STATUS ParseTieFile(const CHAR16* file_data, UINTN file_size, TIE_Config* config) {
    if (!file_data || !config) return 1; // EFI_INVALID_PARAMETER
    
    // Initialize config
    config->ComponentCount = 0;
    config->DependencyCount = 0;
    
    // Simple parsing for demonstration - in real implementation, use proper JSON parser
    // For now, we'll just look for key patterns
    
    // Look for "name": "value" patterns
    UINTN pos = 0;
    while (pos < file_size - 10) {
        // Look for "name":
        if (file_data[pos] == L'"' && file_data[pos+1] == L'n' && file_data[pos+2] == L'a' &&
            file_data[pos+3] == L'm' && file_data[pos+4] == L'e' && file_data[pos+5] == L'"' &&
            file_data[pos+6] == L':') {
            
            pos += 7; // Skip "name":
            while (pos < file_size && (file_data[pos] == L' ' || file_data[pos] == L'\t')) pos++;
            
            if (pos < file_size && file_data[pos] == L'"') {
                pos++; // Skip opening quote
                UINTN name_start = pos;
                while (pos < file_size && file_data[pos] != L'"') pos++;
                
                UINTN name_len = pos - name_start;
                if (name_len >= 64) name_len = 63;
                
                StrnCpy(config->Name, &file_data[name_start], name_len);
            }
        }
        pos++;
    }
    
    return 0; // EFI_SUCCESS
}

// Enhanced boot configuration reader
EFI_STATUS ReadBootConfig(const CHAR16* config_path, BootConfig* config) {
    if (!config) return 1; // EFI_INVALID_PARAMETER
    
    // Set comprehensive defaults
    StrCpy(config->KernelPath, L"\\NXFS\\System\\kernel.elf");
    StrCpy(config->InitrdPath, L"\\NXFS\\System\\initrd.img");
    config->Timeout = 5;
    StrCpy(config->DefaultMode, L"graphical");
    StrCpy(config->BootParameters, L"console=tty0 root=/dev/sda2");
    
    // Memory settings
    config->HeapSize = 64 * 1024 * 1024; // 64MB
    config->StackSize = 8 * 1024 * 1024;  // 8MB
    config->KernelLoadAddress = 0x100000; // 1MB
    config->EnableMemoryProtection = 1;
    
    // Graphics settings
    config->GraphicsMode = 0;
    StrCpy(config->GraphicsResolution, L"1024x768x32");
    config->EnableGraphicsAcceleration = 1;
    config->EnableFramebuffer = 1;
    
    // Security settings
    config->EnableSecureBoot = 0;
    config->RequireKernelSignature = 0;
    StrCpy(config->BootPassword, L"");
    StrCpy(config->CertificatePath, L"\\EFI\\BOOT\\cert.pem");
    
    // Debug settings
    config->EnableDebug = 0;
    config->DebugLevel = 1;
    StrCpy(config->DebugLogFile, L"\\EFI\\BOOT\\debug.log");
    config->EnableSerialDebug = 0;
    config->SerialPort = 0x3F8;
    config->SerialBaudRate = 115200;
    
    // Network settings
    config->EnableNetworkBoot = 0;
    config->DHCPTimeout = 30;
    StrCpy(config->TFTPServer, L"192.168.1.1");
    StrCpy(config->BootFile, L"boot.kernel");
    
    // Driver settings
    config->DriverCount = 0;
    
    // Boot entries
    config->BootEntryCount = 1;
    StrCpy(config->BootEntries[0].Name, L"Neolyx OS");
    StrCpy(config->BootEntries[0].KernelPath, L"\\NXFS\\System\\kernel.elf");
    StrCpy(config->BootEntries[0].InitrdPath, L"\\NXFS\\System\\initrd.img");
    StrCpy(config->BootEntries[0].Parameters, L"console=tty0 root=/dev/sda2");
    config->BootEntries[0].Default = 1;
    config->BootEntries[0].Timeout = 5;
    
    // System information
    StrCpy(config->SystemName, L"Neolyx OS");
    StrCpy(config->SystemVersion, L"2.1.0");
    StrCpy(config->Architecture, L"x86_64");
    StrCpy(config->Platform, L"UEFI");
    
    // TODO: Read from actual config file
    // For now, return success with defaults
    return 0; // EFI_SUCCESS
}

// Validate boot configuration
EFI_STATUS ValidateBootConfig(const BootConfig* config) {
    if (!config) return 1; // EFI_INVALID_PARAMETER
    
    // Check required fields
    if (StrLen(config->KernelPath) == 0) {
        return 2; // EFI_INVALID_PARAMETER
    }
    
    if (config->Timeout > 60) {
        return 2; // EFI_INVALID_PARAMETER
    }
    
    if (config->HeapSize < 1024 * 1024) { // Minimum 1MB
        return 2; // EFI_INVALID_PARAMETER
    }
    
    return 0; // EFI_SUCCESS
}

// Load driver configuration
EFI_STATUS LoadDriverConfig(const CHAR16* driver_path, DriverConfig* driver) {
    if (!driver_path || !driver) return 1; // EFI_INVALID_PARAMETER
    
    // TODO: Implement driver configuration loading
    // For now, set defaults
    StrCpy(driver->Name, L"Generic Driver");
    StrCpy(driver->Path, driver_path);
    StrCpy(driver->Type, L"generic");
    driver->LoadEarly = 0;
    driver->Required = 0;
    StrCpy(driver->Parameters, L"");
    
    return 0; // EFI_SUCCESS
} 