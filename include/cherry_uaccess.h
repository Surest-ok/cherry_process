/*
 * CherryUAccess - User Space Pointer Validation & Safe Copy Gateway
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * Provides Linux/POSIX-compatible safe pointer access and copy primitives
 * to safeguard kernel space from invalid or malicious user-space memory access.
 */

#ifndef _CHERRY_UACCESS_H_
#define _CHERRY_UACCESS_H_

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <errno.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VERIFY_READ   0
#define VERIFY_WRITE  1

/**
 * Validate that a user-space memory buffer does not wrap around and avoids
 * protected low memory (e.g. NULL guard page).
 */
static inline bool access_ok(int type, const void *addr, size_t size)
{
    (void)type;
    if (size == 0) {
        return true;
    }
    if (!addr) {
        return false;
    }

    uintptr_t start = (uintptr_t)addr;
    uintptr_t end = start + size;

    /* Integer overflow protection */
    if (end < start) {
        return false;
    }

    /* Zero-page NULL pointer dereference guard (first 4KB reserved) */
    if (start < 0x1000) {
        return false;
    }

    return true;
}

/**
 * Copy data from user space to kernel space with bounds and safety check.
 * Returns number of uncopied bytes (0 on complete success).
 */
static inline unsigned long copy_from_user(void *to, const void *from, unsigned long n)
{
    if (n == 0) {
        return 0;
    }
    if (!to || !access_ok(VERIFY_READ, from, n)) {
        return n;
    }
    memcpy(to, from, n);
    return 0;
}

/**
 * Copy data from kernel space to user space with bounds and safety check.
 * Returns number of uncopied bytes (0 on complete success).
 */
static inline unsigned long copy_to_user(void *to, const void *from, unsigned long n)
{
    if (n == 0) {
        return 0;
    }
    if (!from || !access_ok(VERIFY_WRITE, to, n)) {
        return n;
    }
    memcpy(to, from, n);
    return 0;
}

/**
 * Find length of user-space string up to count bytes.
 * Returns length including null terminator, or 0 on fault.
 */
static inline long strnlen_user(const void *src, long count)
{
    if (!src || count <= 0) {
        return 0;
    }
    if (!access_ok(VERIFY_READ, src, 1)) {
        return 0;
    }

    const char *s = (const char *)src;
    long len = 0;
    while (len < count) {
        if (!access_ok(VERIFY_READ, s + len, 1)) {
            return 0;
        }
        if (s[len] == '\0') {
            return len + 1;
        }
        len++;
    }
    return count;
}

/**
 * Safely copy a null-terminated string from user space into a kernel buffer.
 * Returns string length (excluding null terminator) or negative error code.
 */
static inline long strncpy_from_user(char *dst, const void *src, long count)
{
    if (!dst || !src || count <= 0) {
        return -EINVAL;
    }
    if (!access_ok(VERIFY_READ, src, 1)) {
        return -EFAULT;
    }

    const char *s = (const char *)src;
    long i = 0;
    while (i < count) {
        if (!access_ok(VERIFY_READ, s + i, 1)) {
            return -EFAULT;
        }
        dst[i] = s[i];
        if (dst[i] == '\0') {
            return i;
        }
        i++;
    }
    return count;
}

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_UACCESS_H_ */
