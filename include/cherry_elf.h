/*
 * CherryProcess - ELF64 Executable Loader & Linux System V ABI Environment
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#ifndef _CHERRY_ELF_H_
#define _CHERRY_ELF_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Standard ELF Types */
typedef uint64_t Elf64_Addr;
typedef uint64_t Elf64_Off;
typedef uint16_t Elf64_Half;
typedef uint32_t Elf64_Word;
typedef int32_t  Elf64_Sword;
typedef uint64_t Elf64_Xword;
typedef int64_t  Elf64_Sxword;

#define EI_NIDENT       16

/* ELF Header */
typedef struct {
    unsigned char e_ident[EI_NIDENT];
    Elf64_Half    e_type;
    Elf64_Half    e_machine;
    Elf64_Word    e_version;
    Elf64_Addr    e_entry;
    Elf64_Off     e_phoff;
    Elf64_Off     e_shoff;
    Elf64_Word    e_flags;
    Elf64_Half    e_ehsize;
    Elf64_Half    e_phentsize;
    Elf64_Half    e_phnum;
    Elf64_Half    e_shentsize;
    Elf64_Half    e_shnum;
    Elf64_Half    e_shstrndx;
} Elf64_Ehdr;

/* Program Header */
typedef struct {
    Elf64_Word    p_type;
    Elf64_Word    p_flags;
    Elf64_Off     p_offset;
    Elf64_Addr    p_vaddr;
    Elf64_Addr    p_paddr;
    Elf64_Xword   p_filesz;
    Elf64_Xword   p_memsz;
    Elf64_Xword   p_align;
} Elf64_Phdr;

/* Section Header */
typedef struct {
    Elf64_Word    sh_name;
    Elf64_Word    sh_type;
    Elf64_Xword   sh_flags;
    Elf64_Addr    sh_addr;
    Elf64_Off     sh_offset;
    Elf64_Xword   sh_size;
    Elf64_Word    sh_link;
    Elf64_Word    sh_info;
    Elf64_Xword   sh_addralign;
    Elf64_Xword   sh_entsize;
} Elf64_Shdr;

/* Section Types */
#define SHT_NULL            0
#define SHT_PROGBITS        1
#define SHT_SYMTAB          2
#define SHT_STRTAB          3
#define SHT_RELA            4
#define SHT_HASH            5
#define SHT_DYNAMIC         6
#define SHT_NOTE            7
#define SHT_NOBITS          8
#define SHT_REL             9
#define SHT_SHLIB           10
#define SHT_DYNSYM          11
#define SHT_INIT_ARRAY      14
#define SHT_FINI_ARRAY      15
#define SHT_PREINIT_ARRAY   16


/* Dynamic Structure */
typedef struct {
    Elf64_Sxword d_tag;
    union {
        Elf64_Xword d_val;
        Elf64_Addr  d_ptr;
    } d_un;
} Elf64_Dyn;

/* Dynamic tags */
#define DT_NULL         0
#define DT_NEEDED       1
#define DT_PLTRELSZ     2
#define DT_PLTGOT       3
#define DT_HASH         4
#define DT_STRTAB       5
#define DT_SYMTAB       6
#define DT_RELA         7
#define DT_RELASZ       8
#define DT_RELAENT      9
#define DT_INIT_ARRAY   25
#define DT_FINI_ARRAY   26
#define DT_INIT_ARRAYSZ 27
#define DT_FINI_ARRAYSZ 28

/* Relocation Structure */
typedef struct {
    Elf64_Addr   r_offset;
    Elf64_Xword  r_info;
    Elf64_Sxword r_addend;
} Elf64_Rela;

#define R_AARCH64_NONE      0
#define R_AARCH64_RELATIVE  1027
#define R_AARCH64_IRELATIVE 1032

/* e_ident indices */
#define EI_MAG0         0
#define EI_MAG1         1
#define EI_MAG2         2
#define EI_MAG3         3
#define EI_CLASS        4
#define EI_DATA         5
#define EI_VERSION      6
#define EI_OSABI        7

#define ELFMAG0         0x7f
#define ELFMAG1         'E'
#define ELFMAG2         'L'
#define ELFMAG3         'F'

#define ELFCLASS64      2
#define ELFDATA2LSB     1
#define EV_CURRENT      1
#define ELFOSABI_NONE   0
#define ELFOSABI_LINUX  3

/* e_type */
#define ET_NONE         0
#define ET_REL          1
#define ET_EXEC         2
#define ET_DYN          3       /* Position Independent Executable (PIE) */

/* e_machine */
#define EM_AARCH64      183     /* 0xB7 ARM 64-bit Architecture */

/* p_type */
#define PT_NULL         0
#define PT_LOAD         1
#define PT_DYNAMIC      2
#define PT_INTERP       3
#define PT_NOTE         4
#define PT_SHLIB        5
#define PT_PHDR         6
#define PT_TLS          7
#define PT_GNU_STACK    0x6474e551
#define PT_GNU_RELRO    0x6474e552

/* p_flags */
#define PF_X            0x1
#define PF_W            0x2
#define PF_R            0x4

/* Auxiliary Vector (auxv) Types according to Linux System V ABI */
#define AT_NULL         0       /* End of vector */
#define AT_IGNORE       1       /* Entry should be ignored */
#define AT_EXECFD       2       /* File descriptor of program */
#define AT_PHDR         3       /* Program headers for program */
#define AT_PHENT        4       /* Size of program header entry */
#define AT_PHNUM        5       /* Number of program headers */
#define AT_PAGESZ       6       /* System page size (4096) */
#define AT_BASE         7       /* Base address of interpreter */
#define AT_FLAGS        8       /* Flags */
#define AT_ENTRY        9       /* Entry point of program */
#define AT_NOTELF       10      /* Program is not ELF */
#define AT_UID          11      /* Real uid */
#define AT_EUID         12      /* Effective uid */
#define AT_GID          13      /* Real gid */
#define AT_EGID         14      /* Effective gid */
#define AT_PLATFORM     15      /* String identifying platform */
#define AT_HWCAP        16      /* CPU capabilities */
#define AT_CLKTCK       17      /* Frequency of times() */
#define AT_SECURE       23      /* Boolean, secure mode */
#define AT_BASE_PLATFORM 24     /* String identifying real platform */
#define AT_RANDOM       25      /* Address of 16 random bytes (stack canary seed) */
#define AT_HWCAP2       26      /* Extension of AT_HWCAP */
#define AT_EXECFN       31      /* Filename of executed program */
#define AT_SYSINFO_EHDR 33      /* vDSO address */

/* Loaded ELF Binary Descriptor */
struct elf_image {
    void     *raw_mem;          /* Pointer returned by kmalloc to free */
    uintptr_t entry_point;      /* Virtual address of entry point (CPU start PC) */
    uintptr_t main_entry_point; /* Main executable entry point (for AT_ENTRY) */
    uintptr_t load_base;        /* Base address in physical RAM */
    size_t    total_size;       /* Memory footprint size */
    uintptr_t phdr_vaddr;       /* Address of loaded program headers */
    uint16_t  phnum;            /* Number of program headers */
    uint16_t  phentsize;        /* Size of each program header */
    bool      is_pie;           /* True if ET_DYN position-independent */
    bool      has_interp;       /* True if dynamic linker PT_INTERP present */
    char      interp_path[128]; /* Path to requested interpreter (e.g. /lib/ld-linux-aarch64.so.1) */
    uintptr_t interp_base;      /* Base address where interpreter is loaded */
    uintptr_t interp_entry;     /* Entry point of interpreter */
    void     *interp_mem;       /* Memory allocated for interpreter */
};

/* =========================================================================
 * ELF Loader Operations
 * ========================================================================= */

/**
 * Validates ELF64 header for AArch64 compatibility.
 */
int proc_elf_validate(const void *buf, size_t size);

/**
 * Loads an ELF executable from VFS path into allocated memory.
 */
int proc_elf_load(const char *path, struct elf_image *img);

/**
 * Unloads and frees an ELF executable memory.
 */
void proc_elf_unload(struct elf_image *img);

/**
 * Formats Linux AArch64 System V ABI stack frame (argc, argv, envp, auxv, random seed).
 * Returns the aligned stack pointer ready for process execution.
 */
uintptr_t proc_elf_setup_stack(uintptr_t stack_top, int argc, char **argv, char **envp,
                               const struct elf_image *img);

/**
 * Spawns an ELF executable as a new CherryOS process.
 */
/* Complete ELF Process Context */
#define PROC_ELF_STACK_SIZE     (64 * 1024) /* 64KB user stack */

struct elf_process_ctx {
    struct elf_image img;
    void            *stack_mem;
    uintptr_t        user_sp;
    int              argc;
    char           **argv;
    char           **envp;
    int              exit_code;
    bool             running;
};

/* Architecture execution trampoline (implemented in proc_arch_aarch64.S) */
int cherry_arch_enter_elf(uintptr_t entry_point, uintptr_t user_sp);

/* Synchronous execution of an ELF binary in current process context */
int proc_exec_elf(const char *path, int argc, char **argv, char **envp);

int proc_spawn_elf(const char *path, int argc, char **argv, char **envp,
                   int stdin_vfs_fd, int stdout_vfs_fd, int stderr_vfs_fd);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_ELF_H_ */
