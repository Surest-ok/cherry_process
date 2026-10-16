.text
/*
 * CherryProcess - Architecture Execution Trampoline for AArch64
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * Provides decoupled ELF user-mode entry & host register restoration:
 *   int cherry_arch_enter_elf(uintptr_t entry_point, uintptr_t user_sp);
 *   void cherry_arch_exit_elf(void);
 */

.extern proc_arch_set_host_sp
.extern proc_arch_get_host_sp

.section .text
.align 4
.global cherry_arch_enter_elf
.type cherry_arch_enter_elf, %function

cherry_arch_enter_elf:
    /* Arguments:
     *   X0 = entry_point
     *   X1 = user_sp (System V ABI aligned stack pointer)
     */

    /* 1. Save host callee-saved registers on host stack */
    stp     x29, x30, [sp, #-16]!
    stp     x27, x28, [sp, #-16]!
    stp     x25, x26, [sp, #-16]!
    stp     x23, x24, [sp, #-16]!
    stp     x21, x22, [sp, #-16]!
    stp     x19, x20, [sp, #-16]!

    /* Save host SP in process descriptor via proc_arch_set_host_sp */
    mov     x19, x0             /* Save entry_point in x19 */
    mov     x20, x1             /* Save user_sp in x20 */
    mov     x0, sp
    bl      proc_arch_set_host_sp
    mov     x16, x19            /* Target entry point in x16 */
    mov     sp, x20             /* Switch to ELF user ABI stack */

    /* 3. Setup initial register state per Linux System V ABI */
    mov     x0, xzr             /* X0 = 0 (no rtld_fini exit handler) */
    mov     x1, xzr
    mov     x2, xzr
    mov     x3, xzr
    mov     x4, xzr
    mov     x5, xzr
    mov     x6, xzr
    mov     x7, xzr
    mov     x8, xzr
    mov     x9, xzr
    mov     x10, xzr
    mov     x11, xzr
    mov     x12, xzr
    mov     x13, xzr
    mov     x14, xzr
    mov     x15, xzr
    mov     x17, xzr
    mov     x18, xzr
    mov     x19, xzr
    mov     x20, xzr
    mov     x21, xzr
    mov     x22, xzr
    mov     x23, xzr
    mov     x24, xzr
    mov     x25, xzr
    mov     x26, xzr
    mov     x27, xzr
    mov     x28, xzr
    mov     x29, xzr            /* Frame pointer = 0 */
    adr     x30, cherry_arch_exit_elf /* Return to exit trampoline if main() returns */
    msr     tpidr_el0, xzr      /* Reset thread pointer for fresh ELF */

    /* 4. Branch to entry point */
    br      x16

.global cherry_arch_exit_elf
.type cherry_arch_exit_elf, %function
cherry_arch_exit_elf:
    /* Program returned via ret or terminated via exit() syscall.
     * X0 holds exit return code.
     * Restore host SP from process descriptor and callee-saved registers.
     */
    mov     x19, x0             /* Save return code */
    bl      proc_arch_get_host_sp
    mov     sp, x0              /* Restore host SP */
    mov     x0, x19             /* Restore return code */
    ldp     x19, x20, [sp], #16
    ldp     x21, x22, [sp], #16
    ldp     x23, x24, [sp], #16
    ldp     x25, x26, [sp], #16
    ldp     x27, x28, [sp], #16
    ldp     x29, x30, [sp], #16
    ret                         /* Return to host C caller with X0 */