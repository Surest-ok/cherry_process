/*
 * CherryVMA - Process Virtual Memory Area Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_vma.h"
#include "cherry_process.h"
#include "cherry_syscall.h"
#include "cherry_kal.h"
#include "cherry_vfs.h"
#include <string.h>
#include <errno.h>

void vma_init(struct process *p)
{
    if (!p) return;
    p->vma_list = NULL;
}

struct vm_area_struct *vma_find(struct process *p, uintptr_t addr)
{
    if (!p) return NULL;

    struct vm_area_struct *curr = p->vma_list;
    while (curr) {
        if (addr >= curr->vm_start && addr < curr->vm_end) {
            return curr;
        }
        curr = curr->vm_next;
    }
    return NULL;
}

void *do_mmap(struct process *p, void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    if (length == 0) {
        return (void *)-EINVAL;
    }
    if (!p) {
        return (void *)-ESRCH;
    }

    void *ptr = NULL;
    void *allocated_base = NULL;

    /* MAP_FIXED: use caller-specified buffer address */
    if ((flags & MAP_FIXED) && addr) {
        ptr = addr;
        allocated_base = NULL;
    } else {
        ptr = kmalloc(length, GFP_KERNEL);
        if (!ptr) {
            return (void *)-ENOMEM;
        }
        memset(ptr, 0, length);
        allocated_base = ptr;
    }

    /* File-backed mapping for shared libraries / dynamic loader */
    if (fd >= 0) {
        int vfs_fd = proc_fd_get(p, fd);
        if (vfs_fd >= 0) {
            lseek(vfs_fd, offset, 0);
            read(vfs_fd, ptr, length);
        }
    }

    struct vm_area_struct *vma = (struct vm_area_struct *)kmalloc(sizeof(struct vm_area_struct), GFP_KERNEL);
    if (vma) {
        memset(vma, 0, sizeof(*vma));
        vma->vm_start = (uintptr_t)ptr;
        vma->vm_end = (uintptr_t)ptr + length;
        vma->vm_prot = prot;
        vma->vm_flags = flags;
        vma->vm_fd = fd;
        vma->vm_pgoff = offset;
        vma->vm_allocated_base = allocated_base;

        /* Prepend to process VMA list */
        vma->vm_next = p->vma_list;
        p->vma_list = vma;
    }

    return ptr;
}

int do_munmap(struct process *p, void *addr, size_t length)
{
    (void)p;
    (void)addr;
    (void)length;
    /* In flat memory model without MMU paging, partial/interior pointer munmap cannot be passed to vPortFree directly.
     * All allocated process memory mappings are tracked in p->vma_list and reclaimed upon process exit.
     */
    return 0;
}

int do_mprotect(struct process *p, void *addr, size_t length, int prot)
{
    if (!p || !addr || length == 0) {
        return -EINVAL;
    }

    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + length;

    struct vm_area_struct *curr = p->vma_list;
    int modified = 0;

    while (curr) {
        if (!(curr->vm_end <= start || curr->vm_start >= end)) {
            curr->vm_prot = prot;
            modified++;
        }
        curr = curr->vm_next;
    }

    return 0;
}

void vma_destroy_all(struct process *p)
{
    if (!p) return;

    struct vm_area_struct *curr = p->vma_list;
    while (curr) {
        struct vm_area_struct *next = curr->vm_next;
        if (curr->vm_allocated_base) {
            kfree(curr->vm_allocated_base);
        }
        kfree(curr);
        curr = next;
    }
    p->vma_list = NULL;
}

int vma_count(struct process *p)
{
    if (!p) return 0;
    int count = 0;
    struct vm_area_struct *curr = p->vma_list;
    while (curr) {
        count++;
        curr = curr->vm_next;
    }
    return count;
}
