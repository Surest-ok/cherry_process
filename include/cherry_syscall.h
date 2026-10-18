/*
 * CherrySyscall - POSIX-compatible System Call Subsystem for CherryOS
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * Implements Linux AArch64 standard system call numbers and dispatcher.
 */

#ifndef _CHERRY_SYSCALL_H_
#define _CHERRY_SYSCALL_H_

#include <stdint.h>
#include <stddef.h>
#include "cherry_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standard Linux AArch64 System Call Numbers (__NR_*) */
#define __NR_getcwd             17
#define __NR_epoll_create1      20
#define __NR_epoll_ctl          21
#define __NR_epoll_pwait        22
#define __NR_dup                23
#define __NR_dup3               24
#define __NR_fcntl              25
#define __NR_ioctl              29
#define __NR_mkdirat            34
#define __NR_unlinkat           35
#define __NR_renameat           38
#define __NR_stat               40
#define __NR_ftruncate          46
#define __NR_chdir              49
#define __NR_openat             56
#define __NR_close              57
#define __NR_pipe2              59
#define __NR_getdents64         61
#define __NR_lseek              62
#define __NR_read               63
#define __NR_write              64
#define __NR_readv              65
#define __NR_writev             66
#define __NR_pselect6           72
#define __NR_ppoll              73
#define __NR_readlinkat         78
#define __NR_newfstatat         79
#define __NR_fstat              80
#define __NR_exit               93
#define __NR_exit_group         94
#define __NR_set_tid_address    96
#define __NR_futex              98
#define __NR_set_robust_list    99
#define __NR_nanosleep          101
#define __NR_setitimer          103
#define __NR_clock_gettime      113
#define __NR_sched_yield        124
#define __NR_kill               129
#define __NR_rt_sigaction       134
#define __NR_rt_sigprocmask     135
#define __NR_rt_sigpending      136
#define __NR_uname              160
#define __NR_gettimeofday       169
#define __NR_getpid             172
#define __NR_getppid            173
#define __NR_getuid             174
#define __NR_geteuid            175
#define __NR_getgid             176
#define __NR_getegid            177
#define __NR_mq_open            180
#define __NR_mq_unlink          181
#define __NR_mq_timedsend       182
#define __NR_mq_timedreceive    183
#define __NR_mq_notify          184
#define __NR_mq_getsetattr      185
#define __NR_socket             198
#define __NR_socketpair         199
#define __NR_bind               200
#define __NR_listen             201
#define __NR_accept             202
#define __NR_connect            203
#define __NR_getsockname        204
#define __NR_getpeername        205
#define __NR_sendto             206
#define __NR_recvfrom           207
#define __NR_setsockopt         208
#define __NR_getsockopt         209
#define __NR_shutdown           210
#define __NR_sendmsg            211
#define __NR_recvmsg            212
#define __NR_brk                214
#define __NR_munmap             215
#define __NR_mmap               222
#define __NR_mprotect           226
#define __NR_accept4            242
#define __NR_wait4              260
#define __NR_faccessat          48
#define __NR_setpgid            154
#define __NR_getpgid            155
#define __NR_umask              166
#define __NR_sysinfo            179
#define __NR_clone              220
#define __NR_execve             221
#define __NR_prlimit64          261
#define __NR_getrandom          278
#define __NR_statx              291
#define __NR_rseq               293
#define __NR_faccessat2         439

/* Linux sysinfo structure */
struct linux_sysinfo {
    long uptime;             /* Seconds since boot */
    unsigned long loads[3];  /* 1, 5, and 15 minute load averages */
    unsigned long totalram;  /* Total usable main memory size */
    unsigned long freeram;   /* Available memory size */
    unsigned long sharedram; /* Amount of shared memory */
    unsigned long bufferram; /* Memory used by buffers */
    unsigned long totalswap; /* Total swap space size */
    unsigned long freeswap;  /* Swap space still available */
    unsigned short procs;    /* Number of current processes */
    unsigned short pad;
    unsigned long totalhigh;
    unsigned long freehigh;
    unsigned int mem_unit;   /* Memory unit size in bytes */
    char _f[20-2*sizeof(long)-sizeof(int)];
};

/* POSIX struct iovec */
struct iovec {
    void  *iov_base;
    size_t iov_len;
};

/* POSIX struct utsname */
struct utsname {
    char sysname[65];
    char nodename[65];
    char release[65];
    char version[65];
    char machine[65];
    char domainname[65];
};

/* POSIX struct timespec */
#if !defined(_STRUCT_TIMESPEC) && !defined(_TIMESPEC_DECLARED)
#define _STRUCT_TIMESPEC
#define _TIMESPEC_DECLARED
struct timespec {
    int64_t tv_sec;
    int64_t tv_nsec;
};
#endif

#define CLOCK_REALTIME          0
#define CLOCK_MONOTONIC         1

#define WNOHANG                 1
#define WUNTRACED               2

/* =========================================================================
 * Central Syscall Dispatcher
 * ========================================================================= */

/**
 * Dispatch system call by number with up to 6 arguments (Linux ARM64 ABI).
 */
long cherry_syscall(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

/* =========================================================================
 * Process-aware POSIX C System Call Wrappers
 * ========================================================================= */

int     sys_open(const char *path, int flags, mode_t mode);
int     sys_openat(int dirfd, const char *path, int flags, mode_t mode);
int     sys_close(int fd);
ssize_t sys_read(int fd, void *buf, size_t count);
ssize_t sys_write(int fd, const void *buf, size_t count);
off_t   sys_lseek(int fd, off_t offset, int whence);
int     sys_stat(const char *path, struct stat *buf);
int     sys_fstat(int fd, struct stat *buf);
int     sys_ftruncate(int fd, off_t length);
int     sys_fcntl(int fd, int cmd, long arg);
ssize_t sys_readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz);
int     sys_gettimeofday(struct timeval *tv, void *tz);
int     sys_ioctl(int fd, unsigned long req, void *arg);
int     sys_pipe2(int pipefd[2], int flags);
int     sys_dup(int oldfd);
int     sys_dup2(int oldfd, int newfd);
int     sys_mkdir(const char *path, mode_t mode);
int     sys_unlink(const char *path);
int     sys_getcwd(char *buf, size_t size);
int     sys_chdir(const char *path);
pid_t   sys_getpid(void);
pid_t   sys_getppid(void);
void    sys_exit(int status);
pid_t   sys_wait4(pid_t pid, int *wstatus, int options, void *rusage);
int     sys_kill(pid_t pid, int sig);
int     sys_uname(struct utsname *buf);
int     sys_nanosleep(const struct timespec *req, struct timespec *rem);
int     sys_clock_gettime(int clk_id, struct timespec *tp);
void   *sys_brk(void *addr);

/* I/O Multiplexing System Calls */
int     sys_poll(struct pollfd *fds, nfds_t nfds, int timeout);
int     sys_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const void *sigmask);
int     sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout);
int     sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, const struct timespec *timeout, const void *sigmask);
int     sys_epoll_create1(int flags);
int     sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event);
int     sys_epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const void *sigmask);

/* Socket System Calls */
int     sys_socket(int domain, int type, int protocol);
int     sys_bind(int sockfd, const void *addr, socklen_t addrlen);
int     sys_connect(int sockfd, const void *addr, socklen_t addrlen);
int     sys_listen(int sockfd, int backlog);
int     sys_accept(int sockfd, void *addr, socklen_t *addrlen);
ssize_t sys_sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, socklen_t addrlen);
ssize_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen);
int     sys_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen);
int     sys_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen);
int     sys_shutdown(int sockfd, int how);

/* Decoupled hardware / architecture trap frame for system calls */
struct syscall_frame {
    uint64_t x0, x1, x2, x3, x4, x5, x6, x7;
    uint64_t x8; /* Linux AArch64 syscall number */
    uint64_t x9, x10, x11, x12, x13, x14, x15, x16;
    uint64_t x17, x18, x19, x20, x21, x22, x23, x24;
    uint64_t x25, x26, x27, x28, x29, x30;
    uint64_t sp;
    uint64_t pc;
    uint64_t pstate;
};

long cherry_syscall_dispatch_trap(struct syscall_frame *frame);


#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_SYSCALL_H_ */
