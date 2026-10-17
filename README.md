# CherryProcess - 轻量化进程模型与 POSIX 兼容 Syscall 子系统

`cherry_process` 是专为 CherryOS（基于 FreeRTOS SMP AArch64 平台）设计的轻量化进程抽象模型与 POSIX 系统调用层，同时提供完整的 **BusyBox 多呼叫二进制实用工具集** 与 **ash/sh 兼容命令行 Shell**。

---

## 核心特性

1. **轻量化进程控制块 (PCB)**：
   - 独立的进程标识符 (`pid`、`ppid`) 与生命周期状态机 (`READY`、`RUNNING`、`SLEEPING`、`ZOMBIE`、`DEAD`)。
   - 每个进程私有的文件描述符表 (`fd_table[32]`)，映射全局 CherryVFS 句柄，支持标准的 `stdin` (0)、`stdout` (1)、`stderr` (2) 继承与重定向。
   - 进程级工作目录 (`cwd`) 与命令行参数管理 (`argc`、`argv`、`envp`)。
   - `waitpid()` 进程回收与同步原语，支持 `WNOHANG` 非阻塞轮询。

2. **POSIX 兼容 System Call 体系**：
   - 遵循 Linux ARM64 体系结构标准系统调用编号 (`__NR_*`) 与 `cherry_syscall()` 分发器。
   - 支持核心系统调用：`sys_open`, `sys_openat`, `sys_close`, `sys_read`, `sys_write`, `sys_lseek`, `sys_stat`, `sys_fstat`, `sys_ioctl`, `sys_pipe2`, `sys_dup`, `sys_dup2`, `sys_mkdir`, `sys_unlink`, `sys_getcwd`, `sys_chdir`, `sys_getpid`, `sys_getppid`, `sys_exit`, `sys_wait4`, `sys_kill`, `sys_uname`, `sys_nanosleep`, `sys_clock_gettime`, `sys_brk`。

3. **内核级管道 IPC (`cherry_pipe`)**：
   - 4KB 环形缓冲区单向数据流通道，无缝接入 CherryVFS `vfs_file_operations`。
   - 支持多读者/多写者计数、空管道自动阻塞唤醒、写者全关返回 EOF、读者全关报错 EPIPE 等标准语义。

4. **BusyBox 进程包装与 Applet 引擎**：
   - 完整的多呼叫二进制架构，每个 Applet 均可在独立的进程上下文中并发执行。
   - 内置 Applet 矩阵：
     - **Shell & 进程管理**：`sh` (ash), `ps`, `kill`, `sleep`, `true`, `false`。
     - **文件与目录操作**：`ls`, `cat`, `echo`, `cp`, `mv`, `rm`, `mkdir`, `rmdir`, `pwd`, `touch`, `hexdump`。
     - **文本分析工具**：`head`, `tail`, `wc`, `grep`。
     - **系统诊断工具**：`uname`, `df`, `free`, `dmesg`, `date`。
   - **管道与重定向 Shell (`sh`)**：
     - 支持管道串联执行（如 `echo hello | cat | wc -c`）。
     - 支持标准输出/输入重定向 (`> file`, `>> file`, `< file`)。
     - 支持内置命令 `cd`, `pwd`, `exit` 以及后台执行。

5. **ELF64 加载器与 Linux ABI 兼容环境 (`cherry_elf`)**：
   - 支持解析标准 64 位 ELF 目标文件 (`Elf64_Ehdr`, `Elf64_Phdr`, `PT_LOAD`)。
   - 支持 Position-Independent Executable (PIE) 与动态内存重定位加载。
   - 构造标准的 Linux ARM64 System V ABI 初始用户栈与辅助向量表 (`auxv`)：
     - `argc`, `argv[]`, `envp[]`
     - `AT_PHDR`, `AT_PHENT`, `AT_PHNUM`, `AT_PAGESZ=4096`, `AT_BASE`, `AT_ENTRY`
     - `AT_RANDOM` (16 字节随机数，用于 glibc `__stack_chk_guard` 栈保护)
   - 补全 glibc 启动阶段必需的底层系统调用：
     - `set_tid_address`, `getrandom`, `writev`, `readv`, `newfstatat`, `mmap`, `munmap`, `mprotect`, `prlimit64`, `rseq`, `brk`。

6. **用户空间指针安全网关 (`cherry_uaccess`)**：
   - 全面引入 Linux 标准零信任用户空间边界校验与安全拷贝原语：`access_ok()`, `copy_from_user()`, `copy_to_user()`, `strnlen_user()`, `strncpy_from_user()`。
   - 所有接收用户指针的系统调用（`sys_read`, `sys_write`, `sys_stat`, `sys_fstat`, `sys_getcwd` 等）均严密防范非法空指针、内核空间越界指针与未映射地址注入。

7. **进程虚拟内存区结构化管理 (`cherry_vma`)**：
   - 引入 `struct vm_area_struct` 树链体系，结构化跟踪各进程用户空间映射段（代码段、数据段、堆、栈、共享库与动态分配段）。
   - 完备支持 `do_mmap()`, `do_munmap()`, `do_mprotect()` 语义。
   - 进程退出时由 `vma_destroy_all()` 统一进行全量无泄漏内存析构与垃圾回收。

8. **POSIX 实时信号框架 (`cherry_signal`)**：
   - 完整支持 POSIX 1..31 号信号、`sigset_t` 信号集位图与 `struct sigaction` 信号处置动作注册。
   - 实现 `proc_signal_send()`, `sys_rt_sigaction()`, `sys_rt_sigprocmask()`, `sys_rt_sigpending()`, `sys_kill()`。
   - 子进程终止时自动向父进程投递 `SIGCHLD`，完善父子进程异步事件通知机制。

9. **内核完全解耦架构 (`cherry_kal` 抽象与硬件解耦)**：
   - `cherry_process` 与内核完全解耦，源码中零引用 `FreeRTOS.h`、`task.h` 等具体实时操作系统内部头文件与私有类型。
   - 所有线程生命周期统一由 `cherry_kal` (`kthread_run`, `kthread_exit`, `kthread_delete`, `kthread_current`) 承载，实现可移植的进程运行环境。
   - 进程与内核陷入点解耦，提供通用陷入调度分发器 `cherry_syscall_dispatch_trap()` 与 AArch64 架构蹦床 `cherry_arch_enter_elf()`。
   - 彻底解决用户空间 ELF 执行返回、用户栈切换与内核现场保存恢复的解耦。


---

## 命令体系区分 (fstool vs busybox)

为避免内核态文件系统管理命令与用户态应用命令冲突，系统进行了明确的职责分工与命令空间划分：

| 维度 | `cherry_fstools` (`fstool`) | `cherry_process` (`busybox`) |
| :--- | :--- | :--- |
| **定位** | **内核态存储与文件系统底层管理维护** | **进程级用户态多合一实用工具箱** |
| **执行上下文** | 内核当前线程，直接调用 VFS 驱动与 Ext4 物理层 | 独立的轻量级进程（PCB），拥有私有 FD 表与工作目录 |
| **标准调用入口** | `fstool <subcommand> [args...]`<br>如 `fstool fdisk /dev/mmcblk0`<br>`fstool mount /dev/mmcblk0p1 /mnt ext4`<br>`fstool mkfs /dev/mmcblk0p1`<br>`fstool fsck /dev/mmcblk0p1` | `busybox <applet> [args...]` 或 `sh`<br>如 `busybox ls -l`<br>`busybox cat /etc/passwd`<br>`busybox df`<br>`busybox ps` |
| **符号与脚本** | 提供 `fstools_*` C 符号供 VxWorks 风格直接调用 | 在 `sh` 中支持完整的管道 `\|` 与重定向 `>` / `>>` / `<` |

---

## 15 点商业级自动化验收测试套件

| 验证项 | 验证内容 | 成功标志 |
| :--- | :--- | :--- |
| **`spawn`** | 进程创建、PID 分配、FreeRTOS Task 关联 | `spawn=pass` |
| **`wait`** | 进程退出码传递、父进程 waitpid 阻塞回收与僵尸状态清理 | `wait=pass` |
| **`fd`** | 进程私有 FD 表隔离、标准输入输出继承与重定向 | `fd=pass` |
| **`pipe`** | 管道 `sys_pipe2` 创建、跨读写端环形缓冲流式传输 | `pipe=pass` |
| **`syscall`** | POSIX 系统调用分发器 (`getpid`, `uname`, `getcwd`) 正确性 | `syscall=pass` |
| **`uaccess`** | 用户空间零信任安全校验 (`access_ok`, `copy_from_user`, EFAULT 注入防护) | `uaccess=pass` |
| **`vma`** | 进程虚拟内存区结构化管理 (`do_mmap`, `do_mprotect`, `do_munmap`, 退出自动回收) | `vma=pass` |
| **`sig`** | POSIX 实时信号框架 (`sigaction`, `sigprocmask`, `sigpending`, `SIGCHLD`) | `sig=pass` |
| **`applets`** | BusyBox 基础工具 (`echo`, `cat`, `ls`, `mkdir`) 进程化运行 | `applets=pass` |
| **`sh`** | BusyBox Shell 管道与多进程串联 (`cmd1 \| cmd2`) 验证 | `sh=pass` |
| **`smp_ps`** | SMP 多核并发进程调度与 `ps` 进程快照输出 | `smp_ps=pass` |
| **`elf_glibc`**| ELF64 校验与加载、System V ABI 用户栈 (`auxv`、`AT_RANDOM`) 与 glibc 系统调用 | `elf_glibc=pass` |
| **`nopie`** | Linux 标准非 PIE 绝对地址二进制程序执行 | `nopie=pass` |
| **`dyn`** | 完整 Linux 动态解释器 (`ld-linux-aarch64.so.1`) 动态链接应用运行 | `dyn=pass` |
| **`all`** | 综合验收套件 100% 通过标志 | `all=pass` |

测试输出格式：
```text
cherry_process: [process-verify] spawn=pass wait=pass fd=pass pipe=pass syscall=pass uaccess=pass vma=pass sig=pass applets=pass sh=pass smp_ps=pass elf_glibc=pass nopie=pass dyn=pass all=pass
```
