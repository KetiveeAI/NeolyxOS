#ifndef ELF_H
#define ELF_H

#if __has_include(<efi.h>)
#include <efi.h>
#else
    // Minimal fallback typedefs for UEFI types if efi.h is missing
    #include <stdint.h>
    typedef uint8_t  UINT8;
    typedef uint16_t UINT16;
    typedef uint32_t UINT32;
    typedef uint64_t UINT64;
#endif

#define EI_NIDENT 16

typedef struct {
    UINT8 e_ident[EI_NIDENT];
    UINT16 e_type;
    UINT16 e_machine;
    UINT32 e_version;
    UINT64 e_entry;
    UINT64 e_phoff;
    UINT64 e_shoff;
    UINT32 e_flags;
    UINT16 e_ehsize;
    UINT16 e_phentsize;
    UINT16 e_phnum;
    UINT16 e_shentsize;
    UINT16 e_shnum;
    UINT16 e_shstrndx;
} Elf64_Ehdr;

typedef struct {
    UINT32 p_type;
    UINT32 p_flags;
    UINT64 p_offset;
    UINT64 p_vaddr;
    UINT64 p_paddr;
    UINT64 p_filesz;
    UINT64 p_memsz;
    UINT64 p_align;
} Elf64_Phdr;

#define PT_LOAD 1
#define ELF_MAGIC 0x464C457F  // "\x7FELF"

#endif