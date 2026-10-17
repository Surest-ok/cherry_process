/*
 * CherrySignal - POSIX Real-time Signal Framework Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_signal.h"
#include "cherry_process.h"
#include "cherry_syscall.h"
#include "cherry_uaccess.h"
#include <string.h>
#include <errno.h>

void proc_signal_init(struct process *p)
{
    if (!p) return;

    p->sig_pending = 0;
    p->sig_blocked = 0;

    for (int i = 0; i < CHERRY_NSIG; i++) {
        p->sig_actions[i].sa_handler = SIG_DFL;
        p->sig_actions[i].sa_mask = 0;
        p->sig_actions[i].sa_flags = 0;
        p->sig_actions[i].sa_restorer = NULL;
    }
}

int proc_signal_send(pid_t pid, int sig)
{
    if (sig < 0 || sig >= CHERRY_NSIG) {
        return -EINVAL;
    }

    struct process *target = proc_get_by_pid(pid);
    if (!target) {
        return -ESRCH;
    }

    /* Signal 0 is used for existence probing */
    if (sig == 0) {
        return 0;
    }

    /* SIGKILL cannot be ignored or caught */
    if (sig == SIGKILL) {
        return proc_kill(pid, 9);
    }

    /* Check signal disposition */
    struct sigaction *act = &target->sig_actions[sig];

    if (act->sa_handler == SIG_IGN) {
        return 0;
    }

    /* Custom handler */
    if (act->sa_handler != SIG_DFL && act->sa_handler != SIG_IGN) {
        if (!(target->sig_blocked & (1U << sig))) {
            act->sa_handler(sig);
        } else {
            target->sig_pending |= (1U << sig);
        }
        return 0;
    }

    /* Default handler actions */
    switch (sig) {
    case SIGTERM:
    case SIGINT:
    case SIGQUIT:
        return proc_kill(pid, sig);

    case SIGCHLD:
        /* Default for SIGCHLD is ignore */
        return 0;

    default:
        target->sig_pending |= (1U << sig);
        break;
    }

    return 0;
}

int sys_rt_sigaction(int signum, const struct sigaction *act, struct sigaction *oldact, size_t sigsetsize)
{
    (void)sigsetsize;

    if (signum <= 0 || signum >= CHERRY_NSIG) {
        return -EINVAL;
    }

    /* POSIX forbids altering disposition of SIGKILL and SIGSTOP */
    if (signum == SIGKILL || signum == SIGSTOP) {
        return -EINVAL;
    }

    struct process *p = proc_current();
    if (!p) {
        return -ESRCH;
    }

    if (oldact) {
        if (copy_to_user(oldact, &p->sig_actions[signum], sizeof(struct sigaction)) != 0) {
            return -EFAULT;
        }
    }

    if (act) {
        struct sigaction new_act;
        if (copy_from_user(&new_act, act, sizeof(struct sigaction)) != 0) {
            return -EFAULT;
        }
        p->sig_actions[signum] = new_act;
    }

    return 0;
}

int sys_rt_sigprocmask(int how, const sigset_t *set, sigset_t *oldset, size_t sigsetsize)
{
    (void)sigsetsize;

    struct process *p = proc_current();
    if (!p) {
        return -ESRCH;
    }

    if (oldset) {
        if (copy_to_user(oldset, &p->sig_blocked, sizeof(sigset_t)) != 0) {
            return -EFAULT;
        }
    }

    if (set) {
        sigset_t new_mask;
        if (copy_from_user(&new_mask, set, sizeof(sigset_t)) != 0) {
            return -EFAULT;
        }

        switch (how) {
        case SIG_BLOCK:
            p->sig_blocked |= new_mask;
            break;
        case SIG_UNBLOCK:
            p->sig_blocked &= ~new_mask;
            break;
        case SIG_SETMASK:
            p->sig_blocked = new_mask;
            break;
        default:
            return -EINVAL;
        }

        /* Unconditionally prevent SIGKILL and SIGSTOP from being masked */
        p->sig_blocked &= ~((1U << SIGKILL) | (1U << SIGSTOP));
    }

    return 0;
}

int sys_rt_sigpending(sigset_t *set, size_t sigsetsize)
{
    (void)sigsetsize;

    if (!set) {
        return -EINVAL;
    }

    struct process *p = proc_current();
    if (!p) {
        return -ESRCH;
    }

    sigset_t pending = p->sig_pending & p->sig_blocked;
    if (copy_to_user(set, &pending, sizeof(sigset_t)) != 0) {
        return -EFAULT;
    }

    return 0;
}
