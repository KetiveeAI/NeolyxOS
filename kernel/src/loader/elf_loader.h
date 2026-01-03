/*
 * NeolyxOS ELF Loader
 * 
 * Production-ready ELF binary loader with:
 * - ELF64 parsing and validation
 * - Program segment loading into RAM
 * - Dynamic linking support
 * - Relocation handling
 * 
 * Copyright (c) 2025 KetiveeAI
 */

#ifndef ELF_LOADER_H
#define ELF_LOADER_H

#include <stdint.h>

/* ============ ELF Magic ============ */
#define ELF_MAGIC       0x464C457F
#define ELF_CLASS_64    2
#define ELF_DATA_LE     1
#define ELF_OSABI_SYSV  0
#define ELF_MACHINE_X86_64 0x3E

/* ============ ELF Types ============ */
#define ET_NONE         0
#define ET_REL          1  /* Relocatable */
#define ET_EXEC         2  /* Executable */
#define ET_DYN          3  /* Shared object */
#define ET_CORE         4  /* Core dump */

/* ============ Program Header Types ============ */
#define PT_NULL         0
#define PT_LOAD         1  /* Loadable segment */
#define PT_DYNAMIC      2  /* Dynamic linking info */
#define PT_INTERP       3  /* Interpreter path */
#define PT_NOTE         4  /* Auxiliary info */
#define PT_SHLIB        5  /* Reserved */
#define PT_PHDR         6  /* Program header table */
#define PT_TLS          7  /* Thread-local storage */

/* ============ Section Header Types ============ */
#define SHT_NULL        0
#define SHT_PROGBITS    1
#define SHT_SYMTAB      2
#define SHT_STRTAB      3
#define SHT_RELA        4
#define SHT_HASH        5
#define SHT_DYNAMIC     6
#define SHT_NOTE        7
#define SHT_NOBITS      8
#define SHT_REL         9
#define SHT_DYNSYM      11

/* ============ Segment Flags ============ */
#define PF_X            0x1  /* Executable */
#define PF_W            0x2  /* Writable */
#define PF_R            0x4  /* Readable */

/* ============ ELF Header (64-bit) ============ */
typedef struct {
    uint8_t  e_ident[16];    /* Magic and identification */
    uint16_t e_type;         /* Object file type */
    uint16_t e_machine;      /* Machine type */
    uint32_t e_version;      /* Object file version */
    uint64_t e_entry;        /* Entry point address */
    uint64_t e_phoff;        /* Program header offset */
    uint64_t e_shoff;        /* Section header offset */
    uint32_t e_flags;        /* Processor flags */
    uint16_t e_ehsize;       /* ELF header size */
    uint16_t e_phentsize;    /* Program header entry size */
    uint16_t e_phnum;        /* Number of program headers */
    uint16_t e_shentsize;    /* Section header entry size */
    uint16_t e_shnum;        /* Number of section headers */
    uint16_t e_shstrndx;     /* Section name string table index */
} __attribute__((packed)) Elf64_Ehdr;

/* ============ Program Header (64-bit) ============ */
typedef struct {
    uint32_t p_type;         /* Segment type */
    uint32_t p_flags;        /* Segment flags */
    uint64_t p_offset;       /* Segment offset in file */
    uint64_t p_vaddr;        /* Virtual address */
    uint64_t p_paddr;        /* Physical address */
    uint64_t p_filesz;       /* Size in file */
    uint64_t p_memsz;        /* Size in memory */
    uint64_t p_align;        /* Alignment */
} __attribute__((packed)) Elf64_Phdr;

/* ============ Section Header (64-bit) ============ */
typedef struct {
    uint32_t sh_name;        /* Section name index */
    uint32_t sh_type;        /* Section type */
    uint64_t sh_flags;       /* Section flags */
    uint64_t sh_addr;        /* Virtual address */
    uint64_t sh_offset;      /* File offset */
    uint64_t sh_size;        /* Section size */
    uint32_t sh_link;        /* Link to other section */
    uint32_t sh_info;        /* Additional info */
    uint64_t sh_addralign;   /* Alignment */
    uint64_t sh_entsize;     /* Entry size */
} __attribute__((packed)) Elf64_Shdr;

/* ============ Symbol Table Entry ============ */
typedef struct {
    uint32_t st_name;        /* Symbol name index */
    uint8_t  st_info;        /* Type and binding */
    uint8_t  st_other;       /* Visibility */
    uint16_t st_shndx;       /* Section index */
    uint64_t st_value;       /* Symbol value */
    uint64_t st_size;        /* Symbol size */
} __attribute__((packed)) Elf64_Sym;

/* ============ Loaded Segment Info ============ */
typedef struct {
    uint64_t vaddr;          /* Virtual address */
    uint64_t paddr;          /* Physical address (in RAM) */
    uint64_t size;           /* Size in memory */
    uint32_t flags;          /* Protection flags */
} loaded_segment_t;

/* ============ Loaded Image ============ */
typedef struct {
    uint64_t entry_point;
    uint64_t base_address;
    uint64_t image_size;
    
    loaded_segment_t *segments;
    int segment_count;
    
    char     *interp;        /* Dynamic linker path */
    uint64_t dynamic_section;
    
    void     *symbol_table;
    void     *string_table;
} loaded_image_t;

/* ============ Load Result ============ */
#define ELF_SUCCESS           0
#define ELF_ERR_NOT_ELF      -1
#define ELF_ERR_NOT_64BIT    -2
#define ELF_ERR_NOT_EXEC     -3
#define ELF_ERR_WRONG_ARCH   -4
#define ELF_ERR_NO_MEMORY    -5
#define ELF_ERR_CORRUPT      -6
#define ELF_ERR_SEGMENT      -7

/* ============ Public API ============ */

/**
 * elf_validate - Validate ELF header
 * 
 * @data: Pointer to ELF file data
 * @size: Size of file data
 * 
 * Returns: ELF_SUCCESS or error code
 */
int elf_validate(const void *data, uint64_t size);

/**
 * elf_load - Load ELF file into memory
 * 
 * @data: Pointer to ELF file data
 * @size: Size of file data
 * @image: Output loaded image info
 * 
 * Returns: ELF_SUCCESS or error code
 */
int elf_load(const void *data, uint64_t size, loaded_image_t *image);

/**
 * elf_get_entry - Get entry point from ELF
 * 
 * @data: Pointer to ELF file data
 * 
 * Returns: Entry point address, or 0 on error
 */
uint64_t elf_get_entry(const void *data);

/**
 * elf_get_segment - Get segment info
 * 
 * @data: Pointer to ELF file data
 * @index: Segment index
 * @out_phdr: Output program header
 * 
 * Returns: ELF_SUCCESS or error code
 */
int elf_get_segment(const void *data, int index, Elf64_Phdr *out_phdr);

/**
 * elf_load_segment - Load single segment into RAM
 * 
 * @data: Pointer to ELF file data
 * @phdr: Program header
 * @dest: Destination address (or 0 for vaddr)
 * 
 * Returns: Loaded address, or 0 on error
 */
uint64_t elf_load_segment(const void *data, Elf64_Phdr *phdr, uint64_t dest);

/**
 * elf_unload - Unload ELF image from memory
 * 
 * @image: Loaded image to unload
 */
void elf_unload(loaded_image_t *image);

#endif /* ELF_LOADER_H */
