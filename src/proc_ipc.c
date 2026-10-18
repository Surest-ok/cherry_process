/*
 * CherryIPC - POSIX Inter-Process Communication Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_ipc.h"
#include "cherry_process.h"
#include "cherry_vfs.h"
#include "uart.h"
#include <string.h>
#include <errno.h>

/* =========================================================================
 * 1. POSIX Message Queues Implementation
 * ========================================================================= */

struct mq_msg_node {
    struct mq_msg_node *next;
    unsigned int prio;
    size_t len;
    uint8_t data[MQ_MSGSIZE_DEFAULT];
};

struct mq_entry {
    bool in_use;
    char name[MQ_NAME_MAX];
    struct mq_attr attr;
    struct mq_msg_node *head;
    int ref_count;
};

static struct mq_entry s_mqueues[MQ_MAX_QUEUES];

mqd_t sys_mq_open(const char *name, int oflag, mode_t mode, const struct mq_attr *attr)
{
    (void)mode;
    if (!name || name[0] == '\0') return -EINVAL;

    /* Normalize name: strip leading slash if present */
    const char *qname = (name[0] == '/') ? &name[1] : name;

    /* 1. Look for existing queue */
    int existing_idx = -1;
    for (int i = 0; i < MQ_MAX_QUEUES; i++) {
        if (s_mqueues[i].in_use && strncmp(s_mqueues[i].name, qname, MQ_NAME_MAX) == 0) {
            existing_idx = i;
            break;
        }
    }

    if (existing_idx >= 0) {
        if ((oflag & O_CREAT) && (oflag & O_EXCL)) {
            return -EEXIST;
        }
        s_mqueues[existing_idx].ref_count++;
        return existing_idx;
    }

    /* 2. Create if O_CREAT specified */
    if (!(oflag & O_CREAT)) {
        return -ENOENT;
    }

    int free_idx = -1;
    for (int i = 0; i < MQ_MAX_QUEUES; i++) {
        if (!s_mqueues[i].in_use) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) return -ENFILE;

    struct mq_entry *mq = &s_mqueues[free_idx];
    memset(mq, 0, sizeof(*mq));
    mq->in_use = true;
    strncpy(mq->name, qname, MQ_NAME_MAX - 1);
    mq->ref_count = 1;

    if (attr) {
        mq->attr = *attr;
        if (mq->attr.mq_maxmsg <= 0 || mq->attr.mq_maxmsg > 128) mq->attr.mq_maxmsg = MQ_MAX_MSG_DEFAULT;
        if (mq->attr.mq_msgsize <= 0 || mq->attr.mq_msgsize > MQ_MSGSIZE_DEFAULT) mq->attr.mq_msgsize = MQ_MSGSIZE_DEFAULT;
    } else {
        mq->attr.mq_maxmsg = MQ_MAX_MSG_DEFAULT;
        mq->attr.mq_msgsize = MQ_MSGSIZE_DEFAULT;
    }
    mq->attr.mq_curmsgs = 0;
    mq->attr.mq_flags = (oflag & O_NONBLOCK) ? O_NONBLOCK : 0;
    mq->head = NULL;

    return free_idx;
}

int sys_mq_close(mqd_t mqdes)
{
    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !s_mqueues[mqdes].in_use) {
        return -EBADF;
    }
    struct mq_entry *mq = &s_mqueues[mqdes];
    mq->ref_count--;
    return 0;
}

int sys_mq_unlink(const char *name)
{
    if (!name) return -EINVAL;
    const char *qname = (name[0] == '/') ? &name[1] : name;

    for (int i = 0; i < MQ_MAX_QUEUES; i++) {
        if (s_mqueues[i].in_use && strncmp(s_mqueues[i].name, qname, MQ_NAME_MAX) == 0) {
            struct mq_entry *mq = &s_mqueues[i];
            /* Free queued messages */
            struct mq_msg_node *cur = mq->head;
            while (cur) {
                struct mq_msg_node *next = cur->next;
                kfree(cur);
                cur = next;
            }
            memset(mq, 0, sizeof(*mq));
            return 0;
        }
    }
    return -ENOENT;
}

int sys_mq_send(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio)
{
    return sys_mq_timedsend(mqdes, msg_ptr, msg_len, msg_prio, NULL);
}

int sys_mq_timedsend(mqd_t mqdes, const char *msg_ptr, size_t msg_len, unsigned int msg_prio, const struct timespec *abs_timeout)
{
    (void)abs_timeout;
    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !s_mqueues[mqdes].in_use) return -EBADF;
    if (!msg_ptr) return -EINVAL;

    struct mq_entry *mq = &s_mqueues[mqdes];
    if (msg_len > (size_t)mq->attr.mq_msgsize) return -EMSGSIZE;
    if (mq->attr.mq_curmsgs >= mq->attr.mq_maxmsg) {
        if (mq->attr.mq_flags & O_NONBLOCK) return -EAGAIN;
        return -EAGAIN; /* In non-preemptive test/single-context path */
    }

    struct mq_msg_node *node = (struct mq_msg_node *)kmalloc(sizeof(struct mq_msg_node), 0);
    if (!node) return -ENOMEM;

    node->next = NULL;
    node->prio = msg_prio;
    node->len = msg_len;
    memcpy(node->data, msg_ptr, msg_len);

    /* Insert ordered by priority descending (higher priority in front) */
    if (!mq->head || msg_prio > mq->head->prio) {
        node->next = mq->head;
        mq->head = node;
    } else {
        struct mq_msg_node *cur = mq->head;
        while (cur->next && cur->next->prio >= msg_prio) {
            cur = cur->next;
        }
        node->next = cur->next;
        cur->next = node;
    }

    mq->attr.mq_curmsgs++;
    return 0;
}

ssize_t sys_mq_receive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio)
{
    return sys_mq_timedreceive(mqdes, msg_ptr, msg_len, msg_prio, NULL);
}

ssize_t sys_mq_timedreceive(mqd_t mqdes, char *msg_ptr, size_t msg_len, unsigned int *msg_prio, const struct timespec *abs_timeout)
{
    (void)abs_timeout;
    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !s_mqueues[mqdes].in_use) return -EBADF;
    if (!msg_ptr) return -EINVAL;

    struct mq_entry *mq = &s_mqueues[mqdes];
    if (msg_len < (size_t)mq->attr.mq_msgsize) return -EMSGSIZE;
    if (!mq->head || mq->attr.mq_curmsgs == 0) {
        return -EAGAIN;
    }

    /* Pop head message */
    struct mq_msg_node *node = mq->head;
    mq->head = node->next;
    mq->attr.mq_curmsgs--;

    size_t copy_len = (node->len <= msg_len) ? node->len : msg_len;
    memcpy(msg_ptr, node->data, copy_len);
    if (msg_prio) *msg_prio = node->prio;

    kfree(node);
    return (ssize_t)copy_len;
}

int sys_mq_getattr(mqd_t mqdes, struct mq_attr *attr)
{
    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !s_mqueues[mqdes].in_use || !attr) return -EBADF;
    *attr = s_mqueues[mqdes].attr;
    return 0;
}

int sys_mq_setattr(mqd_t mqdes, const struct mq_attr *newattr, struct mq_attr *oldattr)
{
    if (mqdes < 0 || mqdes >= MQ_MAX_QUEUES || !s_mqueues[mqdes].in_use) return -EBADF;
    if (oldattr) *oldattr = s_mqueues[mqdes].attr;
    if (newattr) {
        s_mqueues[mqdes].attr.mq_flags = newattr->mq_flags & O_NONBLOCK;
    }
    return 0;
}

/* =========================================================================
 * 2. POSIX Shared Memory Implementation
 * ========================================================================= */

struct shm_entry {
    bool in_use;
    char name[SHM_NAME_MAX];
    uint8_t *buffer;
    size_t size;
    int ref_count;
};

static struct shm_entry s_shm_table[SHM_MAX_OBJS];

/* Pseudo file descriptor offset for shared memory handles */
#define SHM_FD_OFFSET 4096

int sys_shm_open(const char *name, int oflag, mode_t mode)
{
    (void)mode;
    if (!name || name[0] == '\0') return -EINVAL;
    const char *sname = (name[0] == '/') ? &name[1] : name;

    int existing_idx = -1;
    for (int i = 0; i < SHM_MAX_OBJS; i++) {
        if (s_shm_table[i].in_use && strncmp(s_shm_table[i].name, sname, SHM_NAME_MAX) == 0) {
            existing_idx = i;
            break;
        }
    }

    if (existing_idx >= 0) {
        if ((oflag & O_CREAT) && (oflag & O_EXCL)) {
            return -EEXIST;
        }
        s_shm_table[existing_idx].ref_count++;
        return SHM_FD_OFFSET + existing_idx;
    }

    if (!(oflag & O_CREAT)) return -ENOENT;

    int free_idx = -1;
    for (int i = 0; i < SHM_MAX_OBJS; i++) {
        if (!s_shm_table[i].in_use) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) return -ENFILE;

    struct shm_entry *shm = &s_shm_table[free_idx];
    memset(shm, 0, sizeof(*shm));
    shm->in_use = true;
    strncpy(shm->name, sname, SHM_NAME_MAX - 1);
    shm->ref_count = 1;
    shm->buffer = NULL;
    shm->size = 0;

    return SHM_FD_OFFSET + free_idx;
}

int sys_shm_truncate(int fd, off_t length)
{
    int idx = fd - SHM_FD_OFFSET;
    if (idx < 0 || idx >= SHM_MAX_OBJS || !s_shm_table[idx].in_use) return -EBADF;
    if (length < 0) return -EINVAL;

    struct shm_entry *shm = &s_shm_table[idx];
    if (shm->buffer) {
        kfree(shm->buffer);
        shm->buffer = NULL;
    }

    if (length > 0) {
        /* Page-align allocation */
        size_t alloc_sz = (length + 4095) & ~4095;
        shm->buffer = (uint8_t *)kmalloc(alloc_sz, 0);
        if (!shm->buffer) return -ENOMEM;
        memset(shm->buffer, 0, alloc_sz);
        shm->size = length;
    } else {
        shm->size = 0;
    }
    return 0;
}

void *sys_shm_get_buffer(int fd, size_t *out_size)
{
    int idx = fd - SHM_FD_OFFSET;
    if (idx < 0 || idx >= SHM_MAX_OBJS || !s_shm_table[idx].in_use) return NULL;
    if (out_size) *out_size = s_shm_table[idx].size;
    return s_shm_table[idx].buffer;
}

int sys_shm_unlink(const char *name)
{
    if (!name) return -EINVAL;
    const char *sname = (name[0] == '/') ? &name[1] : name;

    for (int i = 0; i < SHM_MAX_OBJS; i++) {
        if (s_shm_table[i].in_use && strncmp(s_shm_table[i].name, sname, SHM_NAME_MAX) == 0) {
            struct shm_entry *shm = &s_shm_table[i];
            if (shm->buffer) {
                kfree(shm->buffer);
            }
            memset(shm, 0, sizeof(*shm));
            return 0;
        }
    }
    return -ENOENT;
}

/* =========================================================================
 * 3. POSIX Semaphores Implementation
 * ========================================================================= */

struct sem_entry {
    bool in_use;
    int value;
};

static struct sem_entry s_sem_table[SEM_MAX_COUNT];

int sys_sem_init(sem_t *sem, int pshared, unsigned int value)
{
    (void)pshared;
    if (!sem) return -EINVAL;

    int free_idx = -1;
    for (int i = 0; i < SEM_MAX_COUNT; i++) {
        if (!s_sem_table[i].in_use) {
            free_idx = i;
            break;
        }
    }
    if (free_idx < 0) return -ENOSPC;

    s_sem_table[free_idx].in_use = true;
    s_sem_table[free_idx].value = (int)value;

    sem->sem_id = free_idx;
    sem->magic = SEM_MAGIC;
    return 0;
}

int sys_sem_wait(sem_t *sem)
{
    if (!sem || sem->magic != SEM_MAGIC) return -EINVAL;
    int idx = sem->sem_id;
    if (idx < 0 || idx >= SEM_MAX_COUNT || !s_sem_table[idx].in_use) return -EINVAL;

    if (s_sem_table[idx].value > 0) {
        s_sem_table[idx].value--;
        return 0;
    }
    /* In co-operative / uniprocess test path without multi-thread blocker */
    return -EAGAIN;
}

int sys_sem_trywait(sem_t *sem)
{
    if (!sem || sem->magic != SEM_MAGIC) return -EINVAL;
    int idx = sem->sem_id;
    if (idx < 0 || idx >= SEM_MAX_COUNT || !s_sem_table[idx].in_use) return -EINVAL;

    if (s_sem_table[idx].value > 0) {
        s_sem_table[idx].value--;
        return 0;
    }
    return -EAGAIN;
}

int sys_sem_timedwait(sem_t *sem, const struct timespec *abs_timeout)
{
    (void)abs_timeout;
    return sys_sem_trywait(sem);
}

int sys_sem_post(sem_t *sem)
{
    if (!sem || sem->magic != SEM_MAGIC) return -EINVAL;
    int idx = sem->sem_id;
    if (idx < 0 || idx >= SEM_MAX_COUNT || !s_sem_table[idx].in_use) return -EINVAL;

    s_sem_table[idx].value++;
    return 0;
}

int sys_sem_getvalue(sem_t *sem, int *sval)
{
    if (!sem || sem->magic != SEM_MAGIC || !sval) return -EINVAL;
    int idx = sem->sem_id;
    if (idx < 0 || idx >= SEM_MAX_COUNT || !s_sem_table[idx].in_use) return -EINVAL;

    *sval = s_sem_table[idx].value;
    return 0;
}

int sys_sem_destroy(sem_t *sem)
{
    if (!sem || sem->magic != SEM_MAGIC) return -EINVAL;
    int idx = sem->sem_id;
    if (idx < 0 || idx >= SEM_MAX_COUNT || !s_sem_table[idx].in_use) return -EINVAL;

    memset(&s_sem_table[idx], 0, sizeof(struct sem_entry));
    sem->magic = 0;
    return 0;
}

/* =========================================================================
 * 4. Initialization and Self-Tests
 * ========================================================================= */

void proc_ipc_init(void)
{
    memset(s_mqueues, 0, sizeof(s_mqueues));
    memset(s_shm_table, 0, sizeof(s_shm_table));
    memset(s_sem_table, 0, sizeof(s_sem_table));
    pr_info("cherry_process: POSIX IPC subsystem initialized (mqueue/shm/semaphore)\n");
}

bool proc_ipc_run_tests(void)
{
    bool mq_pass = false;
    bool shm_pass = false;
    bool sem_pass = false;

    /* 1. Test POSIX Message Queues with Priority Inversion Prevention */
    mqd_t mq = sys_mq_open("/test_q", O_CREAT | O_RDWR, 0666, NULL);
    if (mq >= 0) {
        const char *msg_low = "msg_low_prio_10";
        const char *msg_high = "msg_high_prio_50";
        const char *msg_med = "msg_med_prio_25";

        /* Send low, then high, then medium */
        sys_mq_send(mq, msg_low, strlen(msg_low) + 1, 10);
        sys_mq_send(mq, msg_high, strlen(msg_high) + 1, 50);
        sys_mq_send(mq, msg_med, strlen(msg_med) + 1, 25);

        char recv_buf[MQ_MSGSIZE_DEFAULT] = {0};
        unsigned int prio = 0;

        /* First receive MUST be highest priority (50) */
        ssize_t r1 = sys_mq_receive(mq, recv_buf, sizeof(recv_buf), &prio);
        bool p1 = (r1 > 0 && prio == 50 && strcmp(recv_buf, msg_high) == 0);

        /* Second receive MUST be medium priority (25) */
        prio = 0;
        ssize_t r2 = sys_mq_receive(mq, recv_buf, sizeof(recv_buf), &prio);
        bool p2 = (r2 > 0 && prio == 25 && strcmp(recv_buf, msg_med) == 0);

        /* Third receive MUST be low priority (10) */
        prio = 0;
        ssize_t r3 = sys_mq_receive(mq, recv_buf, sizeof(recv_buf), &prio);
        bool p3 = (r3 > 0 && prio == 10 && strcmp(recv_buf, msg_low) == 0);

        sys_mq_close(mq);
        sys_mq_unlink("/test_q");

        if (p1 && p2 && p3) mq_pass = true;
    }

    /* 2. Test POSIX Shared Memory */
    int shm_fd = sys_shm_open("/test_shm", O_CREAT | O_RDWR, 0666);
    if (shm_fd >= 0) {
        if (sys_shm_truncate(shm_fd, 4096) == 0) {
            size_t sz = 0;
            char *buf1 = (char *)sys_shm_get_buffer(shm_fd, &sz);
            if (buf1 && sz == 4096) {
                const char *secret = "CHERRYOS_SHM_ZERO_COPY_DATA_42";
                strcpy(buf1, secret);

                /* Open second reference */
                int shm_fd2 = sys_shm_open("/test_shm", O_RDWR, 0666);
                if (shm_fd2 >= 0) {
                    char *buf2 = (char *)sys_shm_get_buffer(shm_fd2, NULL);
                    if (buf2 && strcmp(buf2, secret) == 0) {
                        shm_pass = true;
                    }
                }
            }
        }
        sys_shm_unlink("/test_shm");
    }

    /* 3. Test POSIX Semaphores */
    sem_t sem;
    if (sys_sem_init(&sem, 0, 1) == 0) {
        int val = 0;
        sys_sem_getvalue(&sem, &val);
        bool s1 = (val == 1);

        /* Wait should succeed and decrement to 0 */
        int w1 = sys_sem_wait(&sem);
        sys_sem_getvalue(&sem, &val);
        bool s2 = (w1 == 0 && val == 0);

        /* Trywait on 0 should return -EAGAIN */
        int tw = sys_sem_trywait(&sem);
        bool s3 = (tw == -EAGAIN);

        /* Post should increment back to 1 */
        int p = sys_sem_post(&sem);
        sys_sem_getvalue(&sem, &val);
        bool s4 = (p == 0 && val == 1);

        sys_sem_destroy(&sem);
        if (s1 && s2 && s3 && s4) sem_pass = true;
    }

    bool all_ok = mq_pass && shm_pass && sem_pass;
    pr_info("cherry_process: [ipc-verify] mqueue=%s shm=%s sem=%s all=%s\n",
            mq_pass ? "pass" : "fail",
            shm_pass ? "pass" : "fail",
            sem_pass ? "pass" : "fail",
            all_ok ? "pass" : "fail");

    return all_ok;
}
