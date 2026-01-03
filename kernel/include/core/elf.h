/*
 * NeolyxOS ELF Loader Header
 * 
 * Support for loading ELF64 executables.
 * 
 * Copyright (c) 2025 KetiveeAI
 * SPDX-License-Identifier: MIT
 */

#ifndef NEOLYX_ELF_H
#define NEOLYX_ELF_H

#include <stdint.h>

/* ============ ELF Constants ============ */

/* ELF Magic Number (0x7F 'E' 'L' 'F') */
#define ELF_MAGIC       0x464C457F

/* ELF Class */
#define ELFCLASS32      1
#define ELFCLASS64      2

/* ELF Data Encoding */
#define ELFDATA2LSB     1   /* Little endian */
#define ELFDATA2MSB     2   /* Big endian */

/* ELF OS/ABI */
#define ELFOSABI_NONE   0
#define ELFOSABI_LINUX  3
#define ELFOSABI_NEOLYX 0x4E  /* NeolyxOS custom */

/* ELF Type */
#define ET_NONE         0   /* No file type */
#define ET_REL          1   /* Relocatable file */
#define ET_EXEC         2   /* Executable file */
#define ET_DYN          3   /* Shared object file */
#define ET_CORE         4   /* Core file */

/* ELF Machine */
#define EM_X86_64       62  /* AMD x86-64 */
#define EM_AARCH64      183 /* ARM AARCH64 */

/* Program Header Types */
#define PT_NULL         0   /* Unused */
#define PT_LOAD         1   /* Loadable segment */
#define PT_DYNAMIC      2   /* Dynamic linking info */
#define PT_INTERP       3   /* Interpreter path */
#define PT_NOTE         4   /* Auxiliary info */
#define PT_SHLIB        5   /* Reserved */
#define PT_PHDR         6   /* Program header table */
#define PT_TLS          7   /* Thread-local storage */

/* Program Header Flags */
#define PF_X            0x1 /* Execute */
#define PF_W            0x2 /* Write */
#define PF_R            0x4 /* Read */

/* Section Header Types */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_NOBITS      8
#define SHT_REL         9

/* ============ ELF64 Structures ============ */

/* ELF64 Header */
typedef struct {
    uint8_t  e_ident[16];   /* ELF identification */
    uint16_t e_type;        /* Object file type */
    uint16_t e_machine;     /* Machine type */
    uint32_t e_version;     /* Object file version */
    uint64_t e_entry;       /* Entry point address */
    uint64_t e_phoff;       /* Program header offset */
    uint64_t e_shoff;       /* Section header offset */
    uint32_t e_flags;       /* Processor-specific flags */
    uint16_t e_ehsize;      /* ELF header size */
    uint16_t e_phentsize;   /* Program header entry size */
    uint16_t e_phnum;       /* Number of program headers */
    uint16_t e_shentsize;   /* Section header entry size */
    uint16_t e_shnum;       /* Number of section headers */
    uint16_t e_shstrndx;    /* Section name string table index */
} __attribute__((packed)) elf64_ehdr_t;

/* ELF64 Program Header */
typedef struct {
    uint32_t p_type;        /* Segment type */
    uint32_t p_flags;       /* Segment flags */
    uint64_t p_offset;      /* Segment file offset */
    uint64_t p_vaddr;       /* Segment virtual address */
    uint64_t p_paddr;       /* Segment physical address */
    uint64_t p_filesz;      /* Segment size in file */
    uint64_t p_memsz;       /* Segment size in memory */
    uint64_t p_align;       /* Segment alignment */
} __attribute__((packed)) elf64_phdr_t;

/* ELF64 Section Header */
typedef struct {
    uint32_t sh_name;       /* Section name (string table index) */
    uint32_t sh_type;       /* Section type */
    uint64_t sh_flags;      /* Section flags */
    uint64_t sh_addr;       /* Virtual address */
    uint64_t sh_offset;     /* File offset */
    uint64_t sh_size;       /* Section size */
    uint32_t sh_link;       /* Link to another section */
    uint32_t sh_info;       /* Additional section info */
    uint64_t sh_addralign;  /* Section alignment */
    uint64_t sh_entsize;    /* Entry size if table */
} __attribute__((packed)) elf64_shdr_t;

/* ELF64 Symbol Table Entry */
typedef struct {
    uint32_t st_name;       /* Symbol name (string table index) */
    uint8_t  st_info;       /* Symbol type and binding */
    uint8_t  st_other;      /* Symbol visibility */
    uint16_t st_shndx;      /* Section index */
    uint64_t st_value;      /* Symbol value */
    uint64_t st_size;       /* Symbol size */
} __attribute__((packed)) elf64_sym_t;

/* ============ ELF Loader Info ============ */

typedef struct {
    uint64_t entry_point;   /* Entry point address */
    uint64_t base_addr;     /* Base load address */
    uint64_t top_addr;      /* Highest loaded address */
    uint64_t brk;           /* Program break (heap start) */
    int      is_valid;      /* Successfully loaded */
    int      is_dynamic;    /* Needs dynamic linker */
    char     interp[256];   /* Interpreter path if needed */
} elf_info_t;

/* ============ ELF Loader API ============ */

/**
 * Validate ELF header.
 * 
 * @param data Pointer to ELF file data
 * @param size Size of data
 * @return 0 if valid, -1 if invalid
 */
int elf_validate(const void *data, uint64_t size);

/**
 * Get ELF info without loading.
 * 
 * @param data Pointer to ELF file data
 * @param info Output info structure
 * @return 0 on success, -1 on error
 */
int elf_get_info(const void *data, elf_info_t *info);

/**
 * Load ELF into memory.
 * 
 * @param data Pointer to ELF file data
 * @param info Output info structure
 * @return 0 on success, -1 on error
 */
int elf_load(const void *data, elf_info_t *info);

/**
 * Load ELF from file path.
 * 
 * @param path Path to ELF file
 * @param info Output info structure
 * @return 0 on success, -1 on error
 */
int elf_load_file(const char *path, elf_info_t *info);

#endif /* NEOLYX_ELF_H */
