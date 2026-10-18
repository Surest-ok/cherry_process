# -*- python -*-
from SCons.Script import *

Import('env')

proc_env = env.Clone()

proc_env.Append(CPPPATH=[
    '#components/cherry_process/include',
    '#components/cherry_process/tests',
    '#components/cherry_vfs/include',
    '#components/cherry_kal/include',
    '#components/cherry_printk/include',
    '#components/cherry_shell/include',
    '#include',
    '#kernel/include',
    '#kernel/portable/GCC/ARM_AARCH64_SRE',
    '#bsp/include',
    '#arch/include',
])

sources = [
    'src/proc_core.c',
    'src/proc_vma.c',
    'src/proc_signal.c',
    'src/proc_pipe.c',
    'src/proc_syscall.c',
    'src/proc_ipc.c',
    'src/busybox_core.c',
    'src/busybox_applets.c',
    'src/busybox_sh.c',
    'src/proc_shell.c',
    'src/proc_elf.c',
    'src/proc_arch_aarch64.S',
    'src/proc_busybox_bin.S',
    'tests/process_test.c',
]

proc_objs = proc_env.Object(sources)

cherry_process_lib = proc_env.Library(
    target='libcherry_process.a',
    source=proc_objs
)

Return('cherry_process_lib')
