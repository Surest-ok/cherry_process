/*
 * CherryProcess - ELF64 Loader Implementation for Linux AArch64 Executables
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_elf.h"
#include "cherry_process.h"
#include "cherry_syscall.h"
#include "cherry_kal.h"
#include "cherry_printk.h"
#include "cherry_env.h"
#include <string.h>

extern int snprintf(char *str, size_t size, const char *format, ...);


#define ELF_PAGE_SIZE       4096
#define ELF_ALIGN_UP(x, a)  (((x) + ((a) - 1)) & ~((a) - 1))
#define ELF_ALIGN_DN(x, a)  ((x) & ~((a) - 1))

int proc_elf_validate(const void *buf, size_t size)
{
    if (!buf || size < sizeof(Elf64_Ehdr)) {
        return -EINVAL;
    }

    const Elf64_Ehdr *ehdr = (const Elf64_Ehdr *)buf;

    if (ehdr->e_ident[EI_MAG0] != ELFMAG0 ||
        ehdr->e_ident[EI_MAG1] != ELFMAG1 ||
        ehdr->e_ident[EI_MAG2] != ELFMAG2 ||
        ehdr->e_ident[EI_MAG3] != ELFMAG3) {
        return -ENOEXEC;
    }

    if (ehdr->e_ident[EI_CLASS] != ELFCLASS64) {
        return -ENOEXEC;
    }

    if (ehdr->e_ident[EI_DATA] != ELFDATA2LSB) {
        return -ENOEXEC;
    }

    if (ehdr->e_machine != EM_AARCH64) {
        return -ENOEXEC;
    }

    if (ehdr->e_type != ET_EXEC && ehdr->e_type != ET_DYN) {
        return -ENOEXEC;
    }

    return 0;
}

static int proc_elf_load_binary(const char *path, void **out_mem, uintptr_t *out_base,
                                size_t *out_size, uintptr_t *out_entry,
                                uintptr_t *out_min_vaddr, uintptr_t *out_phdr_vaddr,
                                uint16_t *out_phnum, uint16_t *out_phentsize,
                                bool *out_is_pie, bool *out_has_interp,
                                char *out_interp_path, size_t interp_path_size,
                                size_t extra_heap_reserve)
{
    if (!path || !out_mem || !out_base || !out_size || !out_entry) {
        return -EINVAL;
    }

    int fd = sys_open(path, O_RDONLY, 0);
    if (fd < 0) {
        pr_err("proc_elf: failed to open '%s' (err=%d)\n", path, fd);
        return fd;
    }

    Elf64_Ehdr ehdr;
    ssize_t rd = sys_read(fd, &ehdr, sizeof(Elf64_Ehdr));
    if (rd != sizeof(Elf64_Ehdr)) {
        sys_close(fd);
        return -EIO;
    }

    int v_ret = proc_elf_validate(&ehdr, sizeof(Elf64_Ehdr));
    if (v_ret != 0) {
        pr_err("proc_elf: invalid ELF header in '%s' (err=%d)\n", path, v_ret);
        sys_close(fd);
        return v_ret;
    }

    if (ehdr.e_phnum == 0 || ehdr.e_phentsize != sizeof(Elf64_Phdr)) {
        sys_close(fd);
        return -ENOEXEC;
    }

    size_t phdr_bytes = ehdr.e_phnum * sizeof(Elf64_Phdr);
    Elf64_Phdr *ph_table = (Elf64_Phdr *)kmalloc(phdr_bytes, GFP_KERNEL);
    if (!ph_table) {
        sys_close(fd);
        return -ENOMEM;
    }

    sys_lseek(fd, (off_t)ehdr.e_phoff, 0);
    rd = sys_read(fd, ph_table, phdr_bytes);
    if (rd != (ssize_t)phdr_bytes) {
        kfree(ph_table);
        sys_close(fd);
        return -EIO;
    }

    /* Compute bounding span of all PT_LOAD segments */
    uintptr_t min_vaddr = (uintptr_t)-1;
    uintptr_t max_vaddr = 0;
    int load_count = 0;
    uintptr_t max_align = 65536;
    bool has_interp = false;
    uintptr_t phdr_vaddr_found = 0;

    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (ph_table[i].p_type == PT_LOAD) {
            load_count++;
            uintptr_t seg_start = ph_table[i].p_vaddr;
            uintptr_t seg_end   = ph_table[i].p_vaddr + ph_table[i].p_memsz;
            if (seg_start < min_vaddr) min_vaddr = seg_start;
            if (seg_end > max_vaddr)   max_vaddr = seg_end;
            if (ph_table[i].p_align > max_align) {
                max_align = ph_table[i].p_align;
            }
        } else if (ph_table[i].p_type == PT_INTERP) {
            has_interp = true;
            if (out_interp_path && interp_path_size > 0) {
                size_t path_len = ph_table[i].p_filesz;
                if (path_len >= interp_path_size) {
                    path_len = interp_path_size - 1;
                }
                sys_lseek(fd, (off_t)ph_table[i].p_offset, 0);
                ssize_t ird = sys_read(fd, out_interp_path, path_len);
                if (ird > 0) {
                    out_interp_path[ird] = '\0';
                } else {
                    out_interp_path[0] = '\0';
                }
            }
        }
    }

    if (load_count == 0 || max_vaddr <= min_vaddr) {
        kfree(ph_table);
        sys_close(fd);
        return -ENOEXEC;
    }

    bool is_pie = (ehdr.e_type == ET_DYN);
    size_t span = ELF_ALIGN_UP(max_vaddr - min_vaddr, max_align);

    /* Allocate memory for program image (plus heap reserve if requested) */
    void *load_mem = kmalloc(span + max_align + extra_heap_reserve, GFP_KERNEL);
    if (!load_mem) {
        kfree(ph_table);
        sys_close(fd);
        return -ENOMEM;
    }
    uintptr_t base = ELF_ALIGN_UP((uintptr_t)load_mem, max_align);
    memset((void *)base, 0, span + extra_heap_reserve);

    /* Load each PT_LOAD segment */
    for (int i = 0; i < ehdr.e_phnum; i++) {
        if (ph_table[i].p_type == PT_LOAD) {
            uintptr_t dest_addr = base + (ph_table[i].p_vaddr - min_vaddr);

            if (ph_table[i].p_filesz > 0) {
                sys_lseek(fd, (off_t)ph_table[i].p_offset, 0);
                ssize_t seg_rd = sys_read(fd, (void *)dest_addr, ph_table[i].p_filesz);
                if (seg_rd != (ssize_t)ph_table[i].p_filesz) {
                    kfree(load_mem);
                    kfree(ph_table);
                    sys_close(fd);
                    return -EIO;
                }
            }

            /* Zero BSS portion */
            if (ph_table[i].p_memsz > ph_table[i].p_filesz) {
                size_t bss_sz = ph_table[i].p_memsz - ph_table[i].p_filesz;
                memset((void *)(dest_addr + ph_table[i].p_filesz), 0, bss_sz);
            }
        } else if (ph_table[i].p_type == PT_PHDR) {
            phdr_vaddr_found = base + (ph_table[i].p_vaddr - min_vaddr);
        }
    }

    intptr_t delta = (intptr_t)base - (intptr_t)min_vaddr;

    /* Perform ELF relocations if loaded at offset delta != 0 (both PIE in flat memory and Non-PIE) */
    if (delta != 0) {
        /* 1. Dynamic relocations (if PT_DYNAMIC is present) */
        for (int i = 0; i < ehdr.e_phnum; i++) {
            if (ph_table[i].p_type == PT_DYNAMIC) {
                Elf64_Dyn *dyn = (Elf64_Dyn *)(base + (ph_table[i].p_vaddr - min_vaddr));
                uintptr_t rela_vaddr = 0;
                size_t rela_sz = 0;
                size_t rela_ent = sizeof(Elf64_Rela);
                uintptr_t init_arr_vaddr = 0;
                size_t init_arr_sz = 0;
                uintptr_t fini_arr_vaddr = 0;
                size_t fini_arr_sz = 0;

                while (dyn->d_tag != DT_NULL) {
                    if (dyn->d_tag == DT_RELA) rela_vaddr = dyn->d_un.d_ptr;
                    else if (dyn->d_tag == DT_RELASZ) rela_sz = dyn->d_un.d_val;
                    else if (dyn->d_tag == DT_RELAENT) rela_ent = dyn->d_un.d_val;
                    else if (dyn->d_tag == DT_INIT_ARRAY) init_arr_vaddr = dyn->d_un.d_ptr;
                    else if (dyn->d_tag == DT_INIT_ARRAYSZ) init_arr_sz = dyn->d_un.d_val;
                    else if (dyn->d_tag == DT_FINI_ARRAY) fini_arr_vaddr = dyn->d_un.d_ptr;
                    else if (dyn->d_tag == DT_FINI_ARRAYSZ) fini_arr_sz = dyn->d_un.d_val;
                    dyn++;
                }

                if (rela_vaddr && rela_sz && rela_ent >= sizeof(Elf64_Rela)) {
                    size_t count = rela_sz / rela_ent;
                    Elf64_Rela *r = (Elf64_Rela *)(base + (rela_vaddr - min_vaddr));
                    for (size_t k = 0; k < count; k++) {
                        uint32_t type = (uint32_t)(r[k].r_info & 0xffffffff);
                        if (type == R_AARCH64_RELATIVE || type == R_AARCH64_IRELATIVE) {
                            if (r[k].r_offset >= min_vaddr && r[k].r_offset < max_vaddr) {
                                uintptr_t *loc = (uintptr_t *)(base + (r[k].r_offset - min_vaddr));
                                *loc = (uintptr_t)(r[k].r_addend + delta);
                            }
                        }
                    }
                }

                if (init_arr_vaddr && init_arr_sz) {
                    size_t count = init_arr_sz / sizeof(uintptr_t);
                    uintptr_t *arr = (uintptr_t *)(base + (init_arr_vaddr - min_vaddr));
                    for (size_t k = 0; k < count; k++) {
                        if (arr[k] >= min_vaddr && arr[k] < max_vaddr) {
                            arr[k] += delta;
                        }
                    }
                }

                if (fini_arr_vaddr && fini_arr_sz) {
                    size_t count = fini_arr_sz / sizeof(uintptr_t);
                    uintptr_t *arr = (uintptr_t *)(base + (fini_arr_vaddr - min_vaddr));
                    for (size_t k = 0; k < count; k++) {
                        if (arr[k] >= min_vaddr && arr[k] < max_vaddr) {
                            arr[k] += delta;
                        }
                    }
                }
                break;
            }
        }

        /* 2. Check Section Headers if present in file (for static non-PIE binaries) */
        if (ehdr.e_shoff != 0 && ehdr.e_shnum > 0 && ehdr.e_shentsize == sizeof(Elf64_Shdr)) {
            size_t shdr_bytes = ehdr.e_shnum * sizeof(Elf64_Shdr);
            Elf64_Shdr *sh_table = (Elf64_Shdr *)kmalloc(shdr_bytes, GFP_KERNEL);
            if (sh_table) {
                sys_lseek(fd, (off_t)ehdr.e_shoff, 0);
                if (sys_read(fd, sh_table, shdr_bytes) == (ssize_t)shdr_bytes) {
                    for (int i = 0; i < ehdr.e_shnum; i++) {
                        if (sh_table[i].sh_type == SHT_INIT_ARRAY || sh_table[i].sh_type == SHT_FINI_ARRAY) {
                            if (sh_table[i].sh_addr >= min_vaddr && sh_table[i].sh_addr < max_vaddr) {
                                size_t count = sh_table[i].sh_size / sizeof(uintptr_t);
                                uintptr_t *arr = (uintptr_t *)(base + (sh_table[i].sh_addr - min_vaddr));
                                for (size_t k = 0; k < count; k++) {
                                    if (arr[k] >= min_vaddr && arr[k] < max_vaddr) {
                                        arr[k] += delta;
                                    }
                                }
                            }
                        } else if (sh_table[i].sh_type == SHT_RELA) {
                            size_t count = sh_table[i].sh_size / sizeof(Elf64_Rela);
                            Elf64_Rela *relas = (Elf64_Rela *)kmalloc(sh_table[i].sh_size, GFP_KERNEL);
                            if (relas) {
                                sys_lseek(fd, (off_t)sh_table[i].sh_offset, 0);
                                if (sys_read(fd, relas, sh_table[i].sh_size) == (ssize_t)sh_table[i].sh_size) {
                                    for (size_t k = 0; k < count; k++) {
                                        uint32_t type = (uint32_t)(relas[k].r_info & 0xffffffff);
                                        if (type == R_AARCH64_RELATIVE || type == R_AARCH64_IRELATIVE) {
                                            if (relas[k].r_offset >= min_vaddr && relas[k].r_offset < max_vaddr) {
                                                uintptr_t *loc = (uintptr_t *)(base + (relas[k].r_offset - min_vaddr));
                                                *loc = (uintptr_t)(relas[k].r_addend + delta);
                                            }
                                        }
                                    }
                                }
                                kfree(relas);
                            }
                        }
                    }
                }
                kfree(sh_table);
            }
        }
    }

    sys_close(fd);

    *out_mem       = load_mem;
    *out_base      = base;
    *out_size      = span;
    *out_entry     = base + (ehdr.e_entry - min_vaddr);
    if (out_min_vaddr)   *out_min_vaddr = min_vaddr;
    if (out_phdr_vaddr)  *out_phdr_vaddr = phdr_vaddr_found ? phdr_vaddr_found : (base + ehdr.e_phoff);
    if (out_phnum)       *out_phnum = ehdr.e_phnum;
    if (out_phentsize)   *out_phentsize = ehdr.e_phentsize;
    if (out_is_pie)      *out_is_pie = is_pie;
    if (out_has_interp)  *out_has_interp = has_interp;

    kfree(ph_table);
    return 0;
}

int proc_elf_load(const char *path, struct elf_image *img)
{
    if (!path || !img) {
        return -EINVAL;
    }
    memset(img, 0, sizeof(struct elf_image));

    uintptr_t min_vaddr = 0;
    int ret = proc_elf_load_binary(path, &img->raw_mem, &img->load_base,
                                   &img->total_size, &img->entry_point,
                                   &min_vaddr, &img->phdr_vaddr,
                                   &img->phnum, &img->phentsize,
                                   &img->is_pie, &img->has_interp,
                                   img->interp_path, sizeof(img->interp_path),
                                   4 * 1024 * 1024 /* 4MB preallocated heap buffer for brk */);
    if (ret != 0) {
        return ret;
    }

    img->main_entry_point = img->entry_point;

    /* If binary requests dynamic linker (PT_INTERP), load the interpreter */
    if (img->has_interp) {
        const char *candidates[4];
        int num_cand = 0;
        candidates[num_cand++] = img->interp_path;
        if (strcmp(img->interp_path, "/lib/ld-linux-aarch64.so.1") != 0) {
            candidates[num_cand++] = "/lib/ld-linux-aarch64.so.1";
        }
        if (strcmp(img->interp_path, "/usr/lib/ld-linux-aarch64.so.1") != 0) {
            candidates[num_cand++] = "/usr/lib/ld-linux-aarch64.so.1";
        }

        int interp_ret = -ENOENT;
        for (int i = 0; i < num_cand; i++) {
            bool interp_pie = false;
            bool dummy_interp = false;
            char dummy_path[32];
            uintptr_t dummy_min = 0, dummy_phdr = 0;
            uint16_t dummy_phnum = 0, dummy_phent = 0;
            size_t interp_sz = 0;

            interp_ret = proc_elf_load_binary(candidates[i], &img->interp_mem,
                                               &img->interp_base, &interp_sz,
                                               &img->interp_entry, &dummy_min,
                                               &dummy_phdr, &dummy_phnum, &dummy_phent,
                                               &interp_pie, &dummy_interp,
                                               dummy_path, sizeof(dummy_path), 0);
            if (interp_ret == 0) {
                pr_info("proc_elf: dynamic interpreter loaded from '%s' (base=0x%lx, entry=0x%lx)\n",
                        candidates[i], (unsigned long)img->interp_base, (unsigned long)img->interp_entry);
                break;
            }
        }

        if (interp_ret != 0) {
            pr_err("proc_elf: failed to load interpreter '%s' (err=%d)\n", img->interp_path, interp_ret);
            proc_elf_unload(img);
            return interp_ret;
        }

        /* Initial execution jump point is interpreter entry point */
        img->entry_point = img->interp_entry;
    }

    pr_info("proc_elf: loaded '%s' (base=0x%lx, entry=0x%lx, main_entry=0x%lx, size=%lu bytes, pie=%d, interp=%s)\n",
            path, (unsigned long)img->load_base, (unsigned long)img->entry_point,
            (unsigned long)img->main_entry_point, (unsigned long)img->total_size,
            img->is_pie, img->has_interp ? img->interp_path : "none");

    return 0;
}

void proc_elf_unload(struct elf_image *img)
{
    if (!img) return;
    if (img->raw_mem) {
        kfree(img->raw_mem);
        img->raw_mem = NULL;
    }
    if (img->interp_mem) {
        kfree(img->interp_mem);
        img->interp_mem = NULL;
    }
    img->phdr_vaddr = 0;
}


uintptr_t proc_elf_setup_stack(uintptr_t stack_top, int argc, char **argv, char **envp,
                               const struct elf_image *img)
{
    if (!stack_top || !img) return 0;

    uintptr_t sp = stack_top;

    /* 1. Allocate space on stack for 16-byte random seed (AT_RANDOM) */
    sp -= 16;
    uint8_t *rand_seed = (uint8_t *)sp;
    uint64_t t = ktime_get_real_ns();
    for (int i = 0; i < 16; i++) {
        rand_seed[i] = (uint8_t)((t >> (i * 3)) ^ (i * 0x5a + 0x33));
    }
    uintptr_t at_random_ptr = sp;

    /* 2. Determine environment variables (inject default POSIX/Ubuntu environment if none provided) */
    static const char *s_default_envp[] = {
        "PATH=/bin:/usr/bin:/sbin:/usr/sbin",
        "LD_LIBRARY_PATH=/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu:/lib:/usr/lib",
        "HOME=/root",
        "USER=root",
        "TERM=linux",
        "SHELL=/bin/bash",
        "PWD=/",
        NULL
    };
    char **effective_envp = envp;
    char **built_envp = NULL;
    if (!effective_envp || !effective_envp[0]) {
        built_envp = proc_env_build_envp();
        effective_envp = built_envp ? built_envp : (char **)s_default_envp;
    }

    int envc = 0;
    while (effective_envp[envc]) envc++;

    /* 3. Push string data for envp onto stack */
    uintptr_t envp_ptrs[PROC_MAX_ENVP];
    for (int i = envc - 1; i >= 0; i--) {
        size_t len = strlen(effective_envp[i]) + 1;
        sp -= len;
        memcpy((void *)sp, effective_envp[i], len);
        envp_ptrs[i] = sp;
    }

    if (built_envp) {
        proc_env_free_envp(built_envp);
    }

    /* 4. Push string data for argv onto stack */
    uintptr_t argv_ptrs[PROC_MAX_ARGS];
    for (int i = argc - 1; i >= 0; i--) {
        if (argv && argv[i]) {
            size_t len = strlen(argv[i]) + 1;
            sp -= len;
            memcpy((void *)sp, argv[i], len);
            argv_ptrs[i] = sp;
        } else {
            argv_ptrs[i] = 0;
        }
    }

    /* 16-byte alignment */
    sp = ELF_ALIGN_DN(sp, 16);

    /* 5. Construct Auxiliary Vector (auxv) */
    struct {
        uint64_t a_type;
        uint64_t a_val;
    } auxv[] = {
        { AT_PHDR,          img->phdr_vaddr },
        { AT_PHENT,         img->phentsize },
        { AT_PHNUM,         img->phnum },
        { AT_PAGESZ,        ELF_PAGE_SIZE },
        { AT_BASE,          img->has_interp ? img->interp_base : 0 },
        { AT_FLAGS,         0 },
        { AT_ENTRY,         img->main_entry_point },
        { AT_UID,           0 },
        { AT_EUID,          0 },
        { AT_GID,           0 },
        { AT_EGID,          0 },
        { AT_SECURE,        0 },
        { AT_RANDOM,        at_random_ptr },
        { AT_HWCAP,         0xff },
        { AT_CLKTCK,        100 },
        { AT_EXECFN,        (argc > 0 && argv_ptrs[0]) ? argv_ptrs[0] : 0 },
        { AT_NULL,          0 }
    };

    int auxv_count = sizeof(auxv) / sizeof(auxv[0]);

    /* Allocate space for auxv, envp[], argv[], argc */
    size_t table_size = (auxv_count * sizeof(auxv[0])) +
                        ((envc + 1) * sizeof(uint64_t)) +
                        ((argc + 1) * sizeof(uint64_t)) +
                        sizeof(uint64_t); /* argc */

    table_size = ELF_ALIGN_UP(table_size, 16);
    sp -= table_size;

    uint64_t *stack_words = (uint64_t *)sp;
    int idx = 0;

    /* argc */
    stack_words[idx++] = (uint64_t)argc;

    /* argv pointers */
    for (int i = 0; i < argc; i++) {
        stack_words[idx++] = (uint64_t)argv_ptrs[i];
    }
    stack_words[idx++] = 0; /* NULL terminator */

    /* envp pointers */
    for (int i = 0; i < envc; i++) {
        stack_words[idx++] = (uint64_t)envp_ptrs[i];
    }
    stack_words[idx++] = 0; /* NULL terminator */

    /* auxv table */
    for (int i = 0; i < auxv_count; i++) {
        stack_words[idx++] = auxv[i].a_type;
        stack_words[idx++] = auxv[i].a_val;
    }

    return sp;
}

static int elf_process_worker(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    struct process *p = proc_current();
    if (!p || !p->priv) return -EINVAL;

    struct elf_process_ctx *ctx = (struct elf_process_ctx *)p->priv;
    p->priv = NULL; /* Transfer ownership to worker */

    /* Initialize process heap bounds */
    p->heap_start = ctx->img.load_base + ctx->img.total_size;
    p->brk        = p->heap_start;
    p->heap_end   = p->heap_start + (4 * 1024 * 1024);

    /* Allocate dedicated user stack (256KB) for dynamic ELF execution */
    size_t stack_size = 256 * 1024;
    void *stack_buf = kmalloc(stack_size, GFP_KERNEL);
    if (!stack_buf) {
        proc_elf_unload(&ctx->img);
        kfree(ctx);
        return -ENOMEM;
    }
    uintptr_t stack_top = ELF_ALIGN_DN((uintptr_t)stack_buf + stack_size, 16);
    uintptr_t user_sp = proc_elf_setup_stack(stack_top, p->argc, p->argv, p->envp, &ctx->img);
    if (!user_sp) {
        kfree(stack_buf);
        proc_elf_unload(&ctx->img);
        kfree(ctx);
        return -EINVAL;
    }

    /* Execute ELF entry point via architecture trampoline with full System V ABI stack */
    int ret = cherry_arch_enter_elf(ctx->img.entry_point, user_sp);
    ctx->exit_code = ret;
    ctx->running = false;

    /* Resource reclamation */
    kfree(stack_buf);
    proc_elf_unload(&ctx->img);
    kfree(ctx);

    return ret;
}

int proc_spawn_elf(const char *path, int argc, char **argv, char **envp,
                   int stdin_vfs_fd, int stdout_vfs_fd, int stderr_vfs_fd)
{
    if (!path) return -EINVAL;

    struct elf_process_ctx *ctx = (struct elf_process_ctx *)kzalloc(sizeof(*ctx), GFP_KERNEL);
    if (!ctx) return -ENOMEM;

    /* 1. Load ELF binary into memory */
    int ret = proc_elf_load(path, &ctx->img);
    if (ret != 0) {
        kfree(ctx);
        return ret;
    }

    ctx->argc = argc;
    ctx->argv = argv;
    ctx->envp = envp;
    ctx->running = true;

    /* Extract process name (basename) */
    const char *proc_name = strrchr(path, '/');
    proc_name = proc_name ? proc_name + 1 : path;

    /* 2. Spawn child process with ctx passed via priv */
    pid_t pid = proc_spawn_ex(proc_name, elf_process_worker, argc, argv, envp,
                              stdin_vfs_fd, stdout_vfs_fd, stderr_vfs_fd, ctx);

    if (pid <= 0) {
        proc_elf_unload(&ctx->img);
        kfree(ctx);
        return pid;
    }

    struct process *child = proc_get_by_pid(pid);
    if (child) {
        if (path[0] == '/') {
            strncpy(child->exec_path, path, VFS_PATH_MAX - 1);
        } else {
            snprintf(child->exec_path, VFS_PATH_MAX, "/%s", path);
        }
    }

    return pid;
}

int proc_exec_elf(const char *path, int argc, char **argv, char **envp)
{
    if (!path) return -EINVAL;

    struct elf_image img;
    int ret = proc_elf_load(path, &img);
    if (ret != 0) return ret;

    struct process *p = proc_current();
    if (p) {
        p->heap_start = img.load_base + img.total_size;
        p->brk        = p->heap_start;
        p->heap_end   = p->heap_start + (4 * 1024 * 1024);
        if (path[0] == '/') {
            strncpy(p->exec_path, path, VFS_PATH_MAX - 1);
        } else {
            snprintf(p->exec_path, VFS_PATH_MAX, "/%s", path);
        }
    }

    size_t stack_size = 256 * 1024;
    void *stack_buf = kmalloc(stack_size, GFP_KERNEL);
    if (!stack_buf) {
        proc_elf_unload(&img);
        return -ENOMEM;
    }
    uintptr_t stack_top = ELF_ALIGN_DN((uintptr_t)stack_buf + stack_size, 16);
    uintptr_t sp = proc_elf_setup_stack(stack_top, argc, argv, envp, &img);
    if (!sp) {
        kfree(stack_buf);
        proc_elf_unload(&img);
        return -EINVAL;
    }

    /* Synchronously enter ELF */
    int exit_code = cherry_arch_enter_elf(img.entry_point, sp);

    kfree(stack_buf);
    proc_elf_unload(&img);

    return exit_code;
}
