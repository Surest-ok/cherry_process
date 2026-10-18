/*
 * CherryProcess Acceptance Test Suite
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "process_test.h"
#include "cherry_process.h"
#include "cherry_syscall.h"
#include "cherry_busybox.h"
#include "cherry_elf.h"
#include "cherry_printk.h"
#include "native_elf_bin.h"
#include "native_nopie_bin.h"
#include <string.h>

static int dummy_worker_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    return 42;
}

static int fd_worker_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    int fd = sys_open("/cherry_proc_test/proc_fd_test.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return 1;

    const char *msg = "hello from child process fd";
    ssize_t w = sys_write(fd, msg, strlen(msg));
    sys_close(fd);

    return (w == (ssize_t)strlen(msg)) ? 0 : 2;
}

static int smp_worker_fn(int argc, char **argv)
{
    (void)argc;
    (void)argv;
    kal_thread_sleep_ms(50);
    return 0;
}

int cherry_process_run_tests(void)
{
    pr_info("cherry_process: ==> starting cherryprocess acceptance test suite...\n");

    bool pass_spawn    = false;
    bool pass_wait     = false;
    bool pass_fd       = false;
    bool pass_pipe     = false;
    bool pass_syscall  = false;
    bool pass_uaccess  = false;
    bool pass_vma      = false;
    bool pass_signal   = false;
    bool pass_applets  = false;
    bool pass_sh       = false;
    bool pass_smp_ps   = false;
    bool pass_elf      = false;
    bool pass_native_nopie = false;
    bool pass_dynamic_app  = false;

    sys_mkdir("/cherry_proc_test", 0755);

    /* 1. Test Process Spawn */
    char *args1[] = { (char *)"dummy", NULL };
    pid_t pid1 = proc_spawn("dummy_worker", dummy_worker_fn, 1, args1, NULL, -1, -1, -1);
    if (pid1 > 1) {
        pass_spawn = true;
    }

    /* 2. Test Process Exit & Waitpid */
    if (pass_spawn) {
        int status = 0;
        pid_t waited = proc_waitpid(pid1, &status, 0);
        if (waited == pid1 && status == 42) {
            pass_wait = true;
        }
    }

    /* 3. Test Per-process File Descriptors */
    char *args_fd[] = { (char *)"fd_worker", NULL };
    pid_t pid_fd = proc_spawn("fd_worker", fd_worker_fn, 1, args_fd, NULL, -1, -1, -1);
    if (pid_fd > 0) {
        int st_fd = 0;
        proc_waitpid(pid_fd, &st_fd, 0);
        if (st_fd == 0) {
            int verify_fd = sys_open("/cherry_proc_test/proc_fd_test.tmp", O_RDONLY, 0);
            if (verify_fd >= 0) {
                char buf[64];
                ssize_t r = sys_read(verify_fd, buf, sizeof(buf) - 1);
                sys_close(verify_fd);
                if (r > 0) {
                    buf[r] = '\0';
                    if (strstr(buf, "hello from child process fd")) {
                        pass_fd = true;
                    }
                }
            }
        }
    }

    /* 4. Test In-Memory Pipe & Redirection */
    int pipefd[2];
    if (sys_pipe2(pipefd, 0) == 0) {
        const char *pipe_msg = "cherry_pipe_message_stream_test";
        ssize_t w = sys_write(pipefd[1], pipe_msg, strlen(pipe_msg));
        char read_buf[64];
        memset(read_buf, 0, sizeof(read_buf));
        ssize_t r = sys_read(pipefd[0], read_buf, sizeof(read_buf) - 1);

        sys_close(pipefd[0]);
        sys_close(pipefd[1]);

        if (w == (ssize_t)strlen(pipe_msg) && r == w && strcmp(read_buf, pipe_msg) == 0) {
            pass_pipe = true;
        }
    }

    /* 5. Test POSIX System Call Dispatcher (cherry_syscall) */
    long cur_pid = cherry_syscall(__NR_getpid, 0, 0, 0, 0, 0, 0);
    struct utsname un;
    long un_ret = cherry_syscall(__NR_uname, (long)&un, 0, 0, 0, 0, 0);
    char cwd_buf[128];
    long cwd_ret = cherry_syscall(__NR_getcwd, (long)cwd_buf, (long)sizeof(cwd_buf), 0, 0, 0, 0);

    unsigned long aff_mask = 0;
    long getaff_ret = cherry_syscall(__NR_sched_getaffinity, 0, sizeof(aff_mask), (long)&aff_mask, 0, 0, 0);
    long setaff_ret = cherry_syscall(__NR_sched_setaffinity, 0, sizeof(aff_mask), (long)&aff_mask, 0, 0, 0);

    if (cur_pid > 0 && un_ret == 0 && (strcmp(un.sysname, "CherryOS") == 0 || strcmp(un.sysname, "Linux") == 0) &&
        cwd_ret == 0 && strlen(cwd_buf) > 0 &&
        getaff_ret == 0 && setaff_ret == 0 && aff_mask != 0) {
        pass_syscall = true;
    }

    /* 5.1 Test UAccess Security Gateway */
    char u_kbuf[32] = "kernel_data";
    if (!access_ok(VERIFY_READ, (void *)0x10, 32) &&
        !access_ok(VERIFY_WRITE, (void *)0x0, 1) &&
        access_ok(VERIFY_READ, u_kbuf, sizeof(u_kbuf))) {
        char dst_buf[32];
        unsigned long uncopied = copy_from_user(dst_buf, (void *)0x10, sizeof(dst_buf));
        if (uncopied == sizeof(dst_buf)) {
            ssize_t bad_r = sys_read(0, (void *)0x20, 16);
            ssize_t bad_w = sys_write(1, (void *)0x20, 16);
            if (bad_r == -EFAULT && bad_w == -EFAULT) {
                pass_uaccess = true;
            }
        }
    }

    /* 5.2 Test VMA Subsystem */
    struct process *cur_p = proc_current();
    void *vma_m = do_mmap(cur_p, NULL, 4096, PROT_READ | PROT_WRITE, MAP_ANONYMOUS, -1, 0);
    if (vma_m != MAP_FAILED && vma_m != NULL) {
        struct vm_area_struct *found_vma = vma_find(cur_p, (uintptr_t)vma_m);
        int v_cnt = vma_count(cur_p);
        if (found_vma && found_vma->vm_start == (uintptr_t)vma_m && v_cnt >= 1) {
            do_mprotect(cur_p, vma_m, 4096, PROT_READ);
            if (found_vma->vm_prot == PROT_READ) {
                do_munmap(cur_p, vma_m, 4096);
                pass_vma = true;
            }
        }
    }

    /* 5.3 Test POSIX Signal Architecture */
    struct sigaction sa, old_sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = SIG_IGN;
    if (sys_rt_sigaction(SIGUSR1, &sa, &old_sa, 8) == 0) {
        sigset_t bmask = (1U << SIGUSR2);
        sigset_t old_bmask = 0;
        if (sys_rt_sigprocmask(SIG_BLOCK, &bmask, &old_bmask, 8) == 0) {
            proc_signal_send(sys_getpid(), SIGUSR2);
            sigset_t pset = 0;
            if (sys_rt_sigpending(&pset, 8) == 0 && (pset & (1U << SIGUSR2))) {
                sys_rt_sigprocmask(SIG_UNBLOCK, &bmask, NULL, 8);
                pass_signal = true;
            }
        }
        sys_rt_sigaction(SIGUSR1, &old_sa, NULL, 8);
    }

    /* 6. Test ALL 27 BusyBox Applets via Process Execution & Multi-call Architecture */
    int bb_pass_count = 0;

    /* (1) busybox (main dispatcher / help) */
    int r_bb = busybox_run_cmd("busybox");
    int r_bb_help = busybox_run_cmd("busybox --help");
    bool ok_bb = (r_bb == 0 && r_bb_help == 0);
    if (ok_bb) bb_pass_count++;

    /* (2) echo & redirection (>) */
    int r_echo = busybox_run_cmd("echo 'busybox test line' > /cherry_proc_test/bb_test.txt");
    bool ok_echo = false;
    if (r_echo == 0) {
        int vfd = sys_open("/cherry_proc_test/bb_test.txt", O_RDONLY, 0);
        if (vfd >= 0) {
            char tbuf[64];
            ssize_t tr = sys_read(vfd, tbuf, sizeof(tbuf) - 1);
            sys_close(vfd);
            if (tr > 0) {
                tbuf[tr] = '\0';
                if (strstr(tbuf, "busybox test line")) ok_echo = true;
            }
        }
    }
    if (ok_echo) bb_pass_count++;

    /* (3) pwd */
    bool ok_pwd = (busybox_run_cmd("pwd") == 0);
    if (ok_pwd) bb_pass_count++;

    /* (4) mkdir */
    bool ok_mkdir = (busybox_run_cmd("mkdir /cherry_proc_test/bb_dir") == 0);
    if (ok_mkdir) bb_pass_count++;

    /* (5) touch */
    bool ok_touch = (busybox_run_cmd("touch /cherry_proc_test/bb_dir/bb_file.txt") == 0);
    if (ok_touch) bb_pass_count++;

    /* (6) ls */
    bool ok_ls = (busybox_run_cmd("ls -l /cherry_proc_test/bb_dir") == 0);
    if (ok_ls) bb_pass_count++;

    /* (7) cp */
    bool ok_cp = (busybox_run_cmd("cp /cherry_proc_test/bb_test.txt /cherry_proc_test/bb_dir/bb_copy.txt") == 0);
    if (ok_cp) bb_pass_count++;

    /* (8) mv */
    bool ok_mv = (busybox_run_cmd("mv /cherry_proc_test/bb_dir/bb_copy.txt /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_mv) bb_pass_count++;

    /* (9) cat */
    bool ok_cat = (busybox_run_cmd("cat /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_cat) bb_pass_count++;

    /* (10) head */
    bool ok_head = (busybox_run_cmd("head -n 2 /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_head) bb_pass_count++;

    /* (11) tail */
    bool ok_tail = (busybox_run_cmd("tail -n 2 /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_tail) bb_pass_count++;

    /* (12) wc */
    bool ok_wc = (busybox_run_cmd("wc /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_wc) bb_pass_count++;

    /* (13) grep */
    bool ok_grep = (busybox_run_cmd("grep busybox /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_grep) bb_pass_count++;

    /* (14) hexdump */
    bool ok_hexdump = (busybox_run_cmd("hexdump /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_hexdump) bb_pass_count++;

    /* (15) rm */
    bool ok_rm = (busybox_run_cmd("rm /cherry_proc_test/bb_dir/bb_file.txt /cherry_proc_test/bb_dir/bb_moved.txt") == 0);
    if (ok_rm) bb_pass_count++;

    /* (16) rmdir */
    bool ok_rmdir = (busybox_run_cmd("rmdir /cherry_proc_test/bb_dir") == 0);
    if (ok_rmdir) bb_pass_count++;

    /* (17) uname */
    bool ok_uname = (busybox_run_cmd("uname -a") == 0);
    if (ok_uname) bb_pass_count++;

    /* (18) ps */
    bool ok_ps = (busybox_run_cmd("ps") == 0);
    if (ok_ps) bb_pass_count++;

    /* (19) kill */
    bool ok_kill = (busybox_run_cmd("kill -0 1") == 0);
    if (ok_kill) bb_pass_count++;

    /* (20) sleep */
    bool ok_sleep = (busybox_run_cmd("sleep 0") == 0);
    if (ok_sleep) bb_pass_count++;

    /* (21) df */
    bool ok_df = (busybox_run_cmd("df") == 0);
    if (ok_df) bb_pass_count++;

    /* (22) free */
    bool ok_free = (busybox_run_cmd("free") == 0);
    if (ok_free) bb_pass_count++;

    /* (23) dmesg */
    bool ok_dmesg = (busybox_run_cmd("dmesg") == 0);
    if (ok_dmesg) bb_pass_count++;

    /* (24) date */
    bool ok_date = (busybox_run_cmd("date") == 0);
    if (ok_date) bb_pass_count++;

    /* (25) true */
    bool ok_true = (busybox_run_cmd("true") == 0);
    if (ok_true) bb_pass_count++;

    /* (26) false (exit status 1 is standard Unix specification for false) */
    bool ok_false = (busybox_run_cmd("false") == 1);
    if (ok_false) bb_pass_count++;

    /* (27) sh (scripted subshell execution) */
    bool ok_sh = (busybox_run_cmd("sh -c 'echo shell_direct_ok'") == 0);
    if (ok_sh) bb_pass_count++;

    /* (28) wget */
    bool ok_wget = (busybox_run_cmd("wget") != 0);
    if (ok_wget) bb_pass_count++;

    /* (29) iperf3 */
    bool ok_iperf3 = (busybox_run_cmd("iperf3") != 0);
    if (ok_iperf3) bb_pass_count++;

    /* Multi-call binary invocation verification: busybox <applet> [args] */
    bool ok_multicall = (busybox_run_cmd("busybox echo multi_ok") == 0 &&
                         busybox_run_cmd("busybox uname -a") == 0 &&
                         busybox_run_cmd("busybox pwd") == 0);

    if (bb_pass_count == 29 && ok_multicall) {
        pass_applets = true;
    }

    pr_info("cherry_process: [busybox-verify] %d/29 applets passed (multicall=%s)\n",
            bb_pass_count, ok_multicall ? "pass" : "fail");

    /* 7. Test BusyBox Shell Pipelines */
    int ret_sh1 = busybox_run_cmd("echo pipeline_ok | cat");
    int ret_sh2 = busybox_run_cmd("echo pipeline_wc | wc");
    if (ret_sh1 == 0 && ret_sh2 == 0) {
        pass_sh = true;
    }

    /* 8. Test SMP Concurrent Processes & ps Report */
    char *smp_args[] = { (char *)"smp_worker", NULL };
    pid_t smp1 = proc_spawn("smp_worker1", smp_worker_fn, 1, smp_args, NULL, -1, -1, -1);
    pid_t smp2 = proc_spawn("smp_worker2", smp_worker_fn, 1, smp_args, NULL, -1, -1, -1);

    if (smp1 > 0 && smp2 > 0) {
        char *ps_argv[] = { (char *)"ps", NULL };
        int ps_ret = applet_ps(1, ps_argv);

        int st1 = 0, st2 = 0;
        proc_waitpid(smp1, &st1, 0);
        proc_waitpid(smp2, &st2, 0);

        if (ps_ret == 0) {
            pass_smp_ps = true;
        }
    }

    /* 9. Test Complete ELF64 Loader, glibc Syscalls & Trampoline Execution */
    uint8_t dummy_elf[sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr) + 64];
    memset(dummy_elf, 0, sizeof(dummy_elf));

    Elf64_Ehdr *eh = (Elf64_Ehdr *)dummy_elf;
    eh->e_ident[EI_MAG0] = ELFMAG0;
    eh->e_ident[EI_MAG1] = ELFMAG1;
    eh->e_ident[EI_MAG2] = ELFMAG2;
    eh->e_ident[EI_MAG3] = ELFMAG3;
    eh->e_ident[EI_CLASS] = ELFCLASS64;
    eh->e_ident[EI_DATA] = ELFDATA2LSB;
    eh->e_ident[EI_VERSION] = EV_CURRENT;
    eh->e_type = ET_DYN; /* PIE */
    eh->e_machine = EM_AARCH64;
    eh->e_version = EV_CURRENT;
    eh->e_entry = 0x1000; /* Virtual entry address matching ph->p_vaddr */
    eh->e_phoff = sizeof(Elf64_Ehdr);
    eh->e_ehsize = sizeof(Elf64_Ehdr);
    eh->e_phentsize = sizeof(Elf64_Phdr);
    eh->e_phnum = 1;

    Elf64_Phdr *ph = (Elf64_Phdr *)(dummy_elf + sizeof(Elf64_Ehdr));
    ph->p_type = PT_LOAD;
    ph->p_flags = PF_R | PF_X;
    ph->p_offset = sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr);
    ph->p_vaddr = 0x1000;
    ph->p_paddr = 0x1000;
    ph->p_filesz = 64;
    ph->p_memsz = 128;
    ph->p_align = 4096;

    /* Real AArch64 executable instructions emitting Linux syscalls via svc #0:
     *   mov x8, #172  (0xd2801588) -> __NR_getpid
     *   svc #0        (0xd4000001) -> trap into FreeRTOS_SWI_Handler -> cherry_syscall_dispatch_trap
     *   mov x0, #42   (0xd2800540) -> exit status 42
     *   mov x8, #93   (0xd2800ba8) -> __NR_exit
     *   svc #0        (0xd4000001) -> trap into FreeRTOS_SWI_Handler -> cherry_arch_exit_elf
     *   ret           (0xd65f03c0)
     */
    uint32_t *elf_code = (uint32_t *)(dummy_elf + sizeof(Elf64_Ehdr) + sizeof(Elf64_Phdr));
    elf_code[0] = 0xd2801588; /* mov x8, #172 (__NR_getpid) */
    elf_code[1] = 0xd4000001; /* svc #0 */
    elf_code[2] = 0xd2800540; /* mov x0, #42 */
    elf_code[3] = 0xd2800ba8; /* mov x8, #93 (__NR_exit) */
    elf_code[4] = 0xd4000001; /* svc #0 */
    elf_code[5] = 0xd65f03c0; /* ret */

    int elf_fd = sys_open("/cherry_proc_test/sample_app.elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
    if (elf_fd >= 0) {
        sys_write(elf_fd, dummy_elf, sizeof(dummy_elf));
        sys_close(elf_fd);

        char *test_argv[] = { (char *)"sample_app", (char *)"--version", NULL };
        char *test_envp[] = { (char *)"PATH=/bin", NULL };

        /* (a) Test Synchronous ELF Execution via proc_exec_elf */
        int exec_ret = proc_exec_elf("/cherry_proc_test/sample_app.elf", 2, test_argv, test_envp);

        /* (b) Test Asynchronous Process Spawn via proc_spawn_elf */
        pid_t elf_pid = proc_spawn_elf("/cherry_proc_test/sample_app.elf", 2, test_argv, test_envp, -1, -1, -1);
        int elf_status = 0;
        if (elf_pid > 0) {
            proc_waitpid(elf_pid, &elf_status, 0);
        }

        /* (c) Test glibc bootstrap syscalls */
        long tid = cherry_syscall(__NR_set_tid_address, 0, 0, 0, 0, 0, 0);
        char rand_buf[16];
        long rnd = cherry_syscall(__NR_getrandom, (long)rand_buf, sizeof(rand_buf), 0, 0, 0, 0);

        struct iovec iov;
        const char *hello_v = "hello writev";
        iov.iov_base = (void *)hello_v;
        iov.iov_len = strlen(hello_v);
        int tmp_wfd = sys_open("/cherry_proc_test/writev.tmp", O_WRONLY | O_CREAT | O_TRUNC, 0644);
        long wv = (tmp_wfd >= 0) ? cherry_syscall(__NR_writev, tmp_wfd, (long)&iov, 1, 0, 0, 0) : -1;
        if (tmp_wfd >= 0) sys_close(tmp_wfd);

        /* (d) Test Native Linux ELF compiled with aarch64-linux-gnu-gcc */
        bool pass_native_elf = false;
        int nfd = sys_open("/cherry_proc_test/native_app.elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (nfd >= 0) {
            sys_write(nfd, _tmp_freestanding_elf, _tmp_freestanding_elf_len);
            sys_close(nfd);

            pid_t npid = proc_spawn_elf("/cherry_proc_test/native_app.elf", 1, test_argv, test_envp, -1, -1, -1);
            int nstatus = 0;
            if (npid > 0) {
                proc_waitpid(npid, &nstatus, 0);
            }
            if (nstatus == 42) {
                pass_native_elf = true;
                pr_info("cherry_process: native Linux static-pie ELF execution verified (status=42)\n");
            }
        }

        /* (e) Test I/O Multiplexing System Calls (poll, epoll) on pipe */
        int pfd[2] = {-1, -1};
        bool pass_poll_sys = false;
        if (sys_pipe2(pfd, 0) == 0) {
            struct pollfd p_fds[1];
            p_fds[0].fd = pfd[0];
            p_fds[0].events = POLLIN;
            p_fds[0].revents = 0;
            int r_poll1 = sys_poll(p_fds, 1, 0);

            sys_write(pfd[1], "A", 1);
            p_fds[0].revents = 0;
            int r_poll2 = sys_poll(p_fds, 1, 0);

            int ep = sys_epoll_create1(0);
            struct epoll_event ee;
            ee.events = EPOLLIN;
            ee.data.fd = pfd[0];
            sys_epoll_ctl(ep, EPOLL_CTL_ADD, pfd[0], &ee);

            struct epoll_event ee_out[2];
            int r_ep = sys_epoll_pwait(ep, ee_out, 2, 0, NULL);

            sys_close(ep);
            char c;
            sys_read(pfd[0], &c, 1);
            sys_close(pfd[0]);
            sys_close(pfd[1]);

            if (r_poll1 == 0 && r_poll2 == 1 && (p_fds[0].revents & POLLIN) && r_ep == 1 && (ee_out[0].events & EPOLLIN)) {
                pass_poll_sys = true;
                pr_info("cherry_process: sys_poll and sys_epoll on pipe verified\n");
            }
        }

        /* (f) Test Original BusyBox static-pie ELF Dynamic Loading & Execution */
        bool pass_busybox_elf = false;
        char *bb_test_argv[] = { (char *)"busybox", (char *)"--help", NULL };
        pid_t bb_pid = proc_spawn_elf("/bin/busybox", 2, bb_test_argv, test_envp, -1, -1, -1);
        int bb_status = -1;
        if (bb_pid > 0) {
            proc_waitpid(bb_pid, &bb_status, 0);
        }
        if (bb_status == 0) {
            pass_busybox_elf = true;
            pr_info("cherry_process: original BusyBox ELF execution verified (status=0)\n");
        }

        /* (g) Test Native Linux Non-PIE (ET_EXEC) ELF Loading & Execution */
        pass_native_nopie = false;
        int nopie_fd = sys_open("/cherry_proc_test/native_nopie.elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (nopie_fd >= 0) {
            sys_write(nopie_fd, _tmp_native_nopie_elf, _tmp_native_nopie_elf_len);
            sys_close(nopie_fd);

            pid_t nopie_pid = proc_spawn_elf("/cherry_proc_test/native_nopie.elf", 1, test_argv, test_envp, -1, -1, -1);
            int nopie_status = 0;
            if (nopie_pid > 0) {
                proc_waitpid(nopie_pid, &nopie_status, 0);
            }
            if (nopie_status == 42) {
                pass_native_nopie = true;
                pr_info("cherry_process: native Linux non-PIE (ET_EXEC) ELF execution verified (status=42)\n");
            } else {
                pr_err("cherry_process: native non-PIE failed with status=%d\n", nopie_status);
            }
        }

        /* (h) Test Dynamically Linked Application with Interpreter (PT_INTERP / ld.so) */
        pass_dynamic_app = false;
        struct stat ld_st;
        if (sys_stat("/lib/ld-linux-aarch64.so.1", &ld_st) != 0 || ld_st.st_size < 10000) {
            sys_mkdir("/lib", 0755);
            int ld_fd = sys_open("/lib/ld-linux-aarch64.so.1", O_WRONLY | O_CREAT | O_TRUNC, 0755);
            if (ld_fd >= 0) {
                sys_write(ld_fd, _tmp_ld_so_elf, _tmp_ld_so_elf_len);
                sys_close(ld_fd);
            }
        }

        int dyn_fd = sys_open("/cherry_proc_test/dynamic_app.elf", O_WRONLY | O_CREAT | O_TRUNC, 0755);
        if (dyn_fd >= 0) {
            sys_write(dyn_fd, _tmp_dynamic_app_elf, _tmp_dynamic_app_elf_len);
            sys_close(dyn_fd);

            pid_t dyn_pid = proc_spawn_elf("/cherry_proc_test/dynamic_app.elf", 1, test_argv, test_envp, -1, -1, -1);
            int dyn_status = 0;
            if (dyn_pid > 0) {
                proc_waitpid(dyn_pid, &dyn_status, 0);
            }
            if (dyn_status == 42) {
                pass_dynamic_app = true;
                pr_info("cherry_process: dynamic linked ELF with PT_INTERP (ld.so) verified (status=42)\n");
            } else {
                pr_err("cherry_process: dynamic linked ELF failed with status=%d\n", dyn_status);
            }
        }

        /* (i) Test Ubuntu Native GNU Bash & Script Execution if rootfs mounted */
        bool pass_ubuntu_bash = true;
        struct stat bash_st;
        if (sys_stat("/bin/bash", &bash_st) == 0 && bash_st.st_size > 0) {
            pass_ubuntu_bash = false;
            char *bash_argv[] = { (char *)"bash", (char *)"-c", (char *)"echo [UBUNTU-BASH] command_str_ok; exit 42", NULL };
            pid_t bpid = proc_spawn_elf("/bin/bash", 3, bash_argv, NULL, -1, -1, -1);
            int bstatus = 0;
            if (bpid > 0) {
                proc_waitpid(bpid, &bstatus, 0);
            }
            if (bstatus == 42 || bstatus == 0) {
                pass_ubuntu_bash = true;
                pr_info("cherry_process: Ubuntu native GNU bash execution verified (status=%d)\n", bstatus);
            }

            /* Test Script file execution */
            if (sys_stat("/bin/test_bash.sh", &bash_st) == 0) {
                char *sh_argv[] = { (char *)"bash", (char *)"/bin/test_bash.sh", NULL };
                pid_t sh_pid = proc_spawn_elf("/bin/bash", 2, sh_argv, NULL, -1, -1, -1);
                int sh_status = 0;
                if (sh_pid > 0) {
                    proc_waitpid(sh_pid, &sh_status, 0);
                }
                pr_info("cherry_process: Ubuntu bash script execution completed (status=%d)\n", sh_status);
            }
        }

        if (exec_ret == 42 && elf_status == 42 && pass_native_elf && pass_busybox_elf &&
            pass_native_nopie && pass_dynamic_app && pass_ubuntu_bash &&
            tid > 0 && rnd == 16 && wv == (long)strlen(hello_v) && pass_poll_sys) {
            pass_elf = true;
        }
    }

    bool pass_ipc = proc_ipc_run_tests();

    bool all_pass = pass_spawn && pass_wait && pass_fd && pass_pipe &&
                    pass_ipc &&
                    pass_syscall && pass_uaccess && pass_vma && pass_signal &&
                    pass_applets && pass_sh && pass_smp_ps && pass_elf;

    pr_info("cherry_process: [process-verify] spawn=%s wait=%s fd=%s pipe=%s syscall=%s uaccess=%s vma=%s sig=%s ipc=%s applets=%s sh=%s smp_ps=%s elf_glibc=%s nopie=%s dyn=%s all=%s\n",
            pass_spawn ? "pass" : "fail",
            pass_wait ? "pass" : "fail",
            pass_fd ? "pass" : "fail",
            pass_pipe ? "pass" : "fail",
            pass_syscall ? "pass" : "fail",
            pass_uaccess ? "pass" : "fail",
            pass_vma ? "pass" : "fail",
            pass_signal ? "pass" : "fail",
            pass_ipc ? "pass" : "fail",
            pass_applets ? "pass" : "fail",
            pass_sh ? "pass" : "fail",
            pass_smp_ps ? "pass" : "fail",
            pass_elf ? "pass" : "fail",
            pass_native_nopie ? "pass" : "fail",
            pass_dynamic_app ? "pass" : "fail",
            all_pass ? "pass" : "fail");

    return all_pass ? 0 : -1;
}

