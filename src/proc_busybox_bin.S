/*
 * CherryProcess - Embedded BusyBox Multi-Call Binary
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

.section .rodata
.align 8
.global cherry_busybox_elf_start
.global cherry_busybox_elf_end

cherry_busybox_elf_start:
    .incbin "components/cherry_process/data/busybox.elf"
cherry_busybox_elf_end:
