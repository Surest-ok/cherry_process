/*
 * CherryVMA - Virtual Memory Area (VMA) Subsystem for CherryOS
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * Provides structured tracking and lifecycle management for process memory
 * mappings (mmap / munmap / mprotect), preventing memory leaks and supporting
 * dynamic linkers and userspace libraries.
 */

#ifndef _CHERRY_VMA_H_
#define _CHERRY_VMA_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cherry_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Memory Protection Bits */
#ifndef PROT_NONE
#define PROT_NONE       0x0     /* Page cannot be accessed */
#define PROT_READ       0x1     /* Page can be read */
#define PROT_WRITE      0x2     /* Page can be written */
#define PROT_EXEC       0x4     /* Page can be executed */
#endif

/* Memory Mapping Flags */
#ifndef MAP_SHARED
#define MAP_SHARED      0x01    /* Share changes */
#define MAP_PRIVATE     0x02    /* Changes are private */
#define MAP_FIXED       0x10    /* Interpret addr exactly */
#define MAP_ANONYMOUS   0x20    /* Don't use a file */
#define MAP_ANON        MAP_ANONYMOUS
#endif

#ifndef MAP_FAILED
#define MAP_FAILED      ((void *)-1)
#endif

struct process;

/**
 * Virtual Memory Area Descriptor.
 * Describes a contiguous range of virtually mapped addresses belonging to a process.
 */
struct vm_area_struct {
    uintptr_t               vm_start;           /* Start address (inclusive) */
    uintptr_t               vm_end;             /* End address (exclusive) */
    int                     vm_prot;            /* Access permissions (PROT_*) */
    int                     vm_flags;           /* Mapping flags (MAP_*) */
    off_t                   vm_pgoff;           /* File offset (if file-backed) */
    int                     vm_fd;              /* Underlying file descriptor or -1 */
    void                   *vm_allocated_base;  /* Actual heap memory buffer */
    struct vm_area_struct  *vm_next;           /* Singly-linked list next pointer */
};

/**
 * Initialize VMA list for a process.
 */
void vma_init(struct process *p);

/**
 * Find the VMA containing the specified address.
 */
struct vm_area_struct *vma_find(struct process *p, uintptr_t addr);

/**
 * Core kernel mmap implementation: allocates memory backing, populates data
 * if file-backed, and registers the VMA in the process address space.
 */
void *do_mmap(struct process *p, void *addr, size_t length, int prot, int flags, int fd, off_t offset);

/**
 * Core kernel munmap implementation: locates and frees the VMA and its backing memory.
 */
int do_munmap(struct process *p, void *addr, size_t length);

/**
 * Core kernel mprotect implementation: updates protection flags on matching VMAs.
 */
int do_mprotect(struct process *p, void *addr, size_t length, int prot);

/**
 * Destroy all VMAs and reclaim all associated memory when a process terminates.
 */
void vma_destroy_all(struct process *p);

/**
 * Return total number of active VMAs currently mapped in the process.
 */
int vma_count(struct process *p);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_VMA_H_ */
