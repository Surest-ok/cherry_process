/*
 * CherrySignal - POSIX Real-time Signal Framework for CherryOS
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * Implements POSIX-compliant real-time signal masks, sigaction dispositions,
 * and cross-process asynchronous event notifications.
 */

#ifndef _CHERRY_SIGNAL_H_
#define _CHERRY_SIGNAL_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cherry_vfs.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Standard POSIX Signals */
#ifndef SIGHUP
#define SIGHUP           1
#define SIGINT           2
#define SIGQUIT          3
#define SIGILL           4
#define SIGTRAP          5
#define SIGABRT          6
#define SIGBUS           7
#define SIGFPE           8
#define SIGKILL          9
#define SIGUSR1         10
#define SIGSEGV         11
#define SIGUSR2         12
#define SIGPIPE         13
#define SIGALRM         14
#define SIGTERM         15
#define SIGCHLD         17
#define SIGCONT         18
#define SIGSTOP         19
#define SIGTSTP         20
#define SIGTTIN         21
#define SIGTTOU         22
#define SIGURG          23
#define SIGXCPU         24
#define SIGXFSZ         25
#define SIGVTALRM       26
#define SIGPROF         27
#define SIGWINCH        28
#define SIGIO           29
#define SIGPOLL         SIGIO
#define SIGPWR          30
#define SIGSYS          31
#endif

#define CHERRY_NSIG     32

#ifndef _SIGSET_T_DECLARED
#define _SIGSET_T_DECLARED
typedef uint32_t sigset_t;
#endif

typedef void (*sighandler_t)(int);

#ifndef SIG_DFL
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)
#endif

/* sigprocmask actions */
#ifndef SIG_BLOCK
#define SIG_BLOCK   0
#define SIG_UNBLOCK 1
#define SIG_SETMASK 2
#endif

/* struct sigaction flags */
#ifndef SA_NOCLDSTOP
#define SA_NOCLDSTOP 0x00000001
#define SA_NOCLDWAIT 0x00000002
#define SA_SIGINFO   0x00000004
#define SA_RESTART   0x10000000
#endif

struct sigaction {
    sighandler_t sa_handler;
    sigset_t     sa_mask;
    int          sa_flags;
    void       (*sa_restorer)(void);
};

struct process;

/**
 * Initialize signal disposition table and masks for a newly created process.
 */
void proc_signal_init(struct process *p);

/**
 * Send a signal to a process by PID.
 */
int proc_signal_send(pid_t pid, int sig);

/**
 * POSIX rt_sigaction system call implementation.
 */
int sys_rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact, size_t sigsetsize);

/**
 * POSIX rt_sigprocmask system call implementation.
 */
int sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset, size_t sigsetsize);

/**
 * POSIX rt_sigpending system call implementation.
 */
int sys_rt_sigpending(sigset_t *set, size_t sigsetsize);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_SIGNAL_H_ */
