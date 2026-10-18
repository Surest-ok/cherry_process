/*
 * CherryIPC - POSIX Inter-Process Communication & Synchronization
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 *
 * Implements:
 *   1. POSIX Message Queues (mq_open, mq_send, mq_receive, mq_unlink)
 *   2. POSIX Shared Memory (shm_open, shm_unlink, MAP_SHARED)
 *   3. POSIX Semaphores (sem_init, sem_wait, sem_post, sem_timedwait)
 */

#ifndef _CHERRY_IPC_H_
#define _CHERRY_IPC_H_

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include "cherry_vfs.h"
#include "cherry_syscall.h"

#ifdef __cplusplus
extern "C" {
#endif

/* -------------------------------------------------------------------------
 * 1. POSIX Message Queues
 * ------------------------------------------------------------------------- */
#define MQ_MAX_QUEUES          16
#define MQ_MAX_MSG_DEFAULT     16
#define MQ_MSGSIZE_DEFAULT     256
#define MQ_NAME_MAX            64

struct mq_attr {
    long mq_flags;    /* Flags: 0 or O_NONBLOCK */
    long mq_maxmsg;   /* Max number of messages on queue */
    long mq_msgsize;  /* Max message size in bytes */
    long mq_curmsgs;  /* Current number of messages */
};

typedef int mqd_t;

mqd_t   sys_mq_open(const char *name, int oflag, mode_t mode, const struct mq_attr *attr);
int     sys_mq_close(mqd_t mqdes);
int     sys_mq_unlink(const char *name);
int     sys_mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio);
int     sys_mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio, const struct timespec *abs_timeout);
ssize_t sys_mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio);
ssize_t sys_mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio, const struct timespec *abs_timeout);
int     sys_mq_getattr(mqd_t mqdes, struct mq_attr *attr);
int     sys_mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr);

/* -------------------------------------------------------------------------
 * 2. POSIX Shared Memory
 * ------------------------------------------------------------------------- */
#define SHM_MAX_OBJS           16
#define SHM_NAME_MAX           64

int   sys_shm_open(const char *name, int oflag, mode_t mode);
int   sys_shm_unlink(const char *name);
void *sys_shm_get_buffer(int fd, size_t *out_size);
int   sys_shm_truncate(int fd, off_t length);

/* -------------------------------------------------------------------------
 * 3. POSIX Semaphores
 * ------------------------------------------------------------------------- */
#define SEM_MAX_COUNT          32
#define SEM_MAGIC              0x53454D50 /* "SEMP" */

typedef struct sem_t {
    int sem_id;
    uint32_t magic;
} sem_t;

int sys_sem_init(sem_t *sem, int pshared, unsigned int value);
int sys_sem_wait(sem_t *sem);
int sys_sem_trywait(sem_t *sem);
int sys_sem_timedwait(sem_t *sem, const struct timespec *abs_timeout);
int sys_sem_post(sem_t *sem);
int sys_sem_getvalue(sem_t *sem, int *sval);
int sys_sem_destroy(sem_t *sem);

/* -------------------------------------------------------------------------
 * Initialization and Self-Verification
 * ------------------------------------------------------------------------- */
void proc_ipc_init(void);
bool proc_ipc_run_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_IPC_H_ */
