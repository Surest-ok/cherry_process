/*
 * CherrySyscall - System Call Dispatcher & Implementations
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_syscall.h"
#include "cherry_process.h"
#include "cherry_pipe.h"
#include "uart.h"
#include <string.h>

#define AT_FDCWD    -100

static void resolve_path(const char *in_path, char *out_path, size_t max_len)
{
    if (!in_path || in_path[0] == '\0') {
        out_path[0] = '\0';
        return;
    }

    if (in_path[0] == '/') {
        strncpy(out_path, in_path, max_len - 1);
        out_path[max_len - 1] = '\0';
        return;
    }

    struct process *p = proc_current();
    const char *cwd = p ? p->cwd : "/";

    if (strcmp(cwd, "/") == 0) {
        snprintf(out_path, max_len, "/%s", in_path);
    } else {
        snprintf(out_path, max_len, "%s/%s", cwd, in_path);
    }
}

int sys_openat(int dirfd, const char *path, int flags, mode_t mode)
{
    if (!path) return -EINVAL;

    char full_path[VFS_PATH_MAX];
    if (path[0] == '/' || dirfd != AT_FDCWD) {
        strncpy(full_path, path, sizeof(full_path) - 1);
        full_path[sizeof(full_path) - 1] = '\0';
    } else {
        resolve_path(path, full_path, sizeof(full_path));
    }

    int vfs_fd = open(full_path, flags, mode);
    if (vfs_fd < 0) return vfs_fd;

    struct process *p = proc_current();
    int pfd = proc_fd_install(p, vfs_fd);
    if (pfd < 0) {
        close(vfs_fd);
        return pfd;
    }
    return pfd;
}

int sys_open(const char *path, int flags, mode_t mode)
{
    return sys_openat(AT_FDCWD, path, flags, mode);
}

int sys_close(int fd)
{
    struct process *p = proc_current();
    return proc_fd_close(p, fd);
}

ssize_t sys_read(int fd, void *buf, size_t count)
{
    if (count > 0 && !access_ok(VERIFY_WRITE, buf, count)) return -EFAULT;
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd < 0) return -EBADF;
    return read(vfs_fd, buf, count);
}

ssize_t sys_write(int fd, const void *buf, size_t count)
{
    if (count > 0 && !access_ok(VERIFY_READ, buf, count)) return -EFAULT;
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd >= 0) {
        ssize_t ret = write(vfs_fd, buf, count);
        if (ret >= 0) return ret;
    }

    /* Fallback for stdout (1) and stderr (2) to direct UART output */
    if ((fd == 1 || fd == 2) && buf) {
        const char *cbuf = (const char *)buf;
        for (size_t i = 0; i < count; i++) {
            if (cbuf[i] == '\n') uart_putc('\r');
            uart_putc(cbuf[i]);
        }
        return (ssize_t)count;
    }
    return -EBADF;
}

off_t sys_lseek(int fd, off_t offset, int whence)
{
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd < 0) {
        if (fd == 0 || fd == 1 || fd == 2) return -ESPIPE;
        return -EBADF;
    }
    return lseek(vfs_fd, offset, whence);
}

int sys_stat(const char *path, struct stat *buf)
{
    if (!path || !buf) return -EINVAL;
    if (!access_ok(VERIFY_WRITE, buf, sizeof(struct stat))) return -EFAULT;
    char full_path[VFS_PATH_MAX];
    resolve_path(path, full_path, sizeof(full_path));
    return stat(full_path, buf);
}

int sys_fstat(int fd, struct stat *buf)
{
    if (!buf) return -EINVAL;
    if (!access_ok(VERIFY_WRITE, buf, sizeof(struct stat))) return -EFAULT;
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd < 0) {
        if (fd == 0 || fd == 1 || fd == 2) {
            memset(buf, 0, sizeof(*buf));
            buf->st_mode = S_IFCHR | 0666;
            buf->st_nlink = 1;
            return 0;
        }
        return -EBADF;
    }
    return fstat(vfs_fd, buf);
}

int sys_ioctl(int fd, unsigned long req, void *arg)
{
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd < 0) {
        if (fd == 0 || fd == 1 || fd == 2) return -ENOTTY;
        return -EBADF;
    }
    int ret = ioctl(vfs_fd, (unsigned int)req, arg);
    if (ret < 0 && (fd == 0 || fd == 1 || fd == 2)) {
        return -ENOTTY;
    }
    return ret;
}

int sys_pipe2(int pipefd[2], int flags)
{
    (void)flags;
    if (!pipefd) return -EINVAL;

    int r_vfs = -1, w_vfs = -1;
    int ret = cherry_pipe_create(&r_vfs, &w_vfs);
    if (ret != 0) return ret;

    struct process *p = proc_current();
    int pfd0 = proc_fd_install(p, r_vfs);
    if (pfd0 < 0) {
        close(r_vfs);
        close(w_vfs);
        return pfd0;
    }

    int pfd1 = proc_fd_install(p, w_vfs);
    if (pfd1 < 0) {
        proc_fd_close(p, pfd0);
        close(w_vfs);
        return pfd1;
    }

    pipefd[0] = pfd0;
    pipefd[1] = pfd1;
    return 0;
}

int sys_dup(int oldfd)
{
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, oldfd);
    if (vfs_fd < 0) return -EBADF;

    return proc_fd_install(p, vfs_fd);
}

int sys_dup2(int oldfd, int newfd)
{
    if (newfd < 0 || newfd >= PROC_MAX_FDS) return -EBADF;
    struct process *p = proc_current();
    int vfs_fd = proc_fd_get(p, oldfd);
    if (vfs_fd < 0) return -EBADF;

    if (oldfd == newfd) return newfd;

    if (p->fd_table[newfd] >= 0) {
        close(p->fd_table[newfd]);
    }
    p->fd_table[newfd] = vfs_fd;
    return newfd;
}

int sys_mkdir(const char *path, mode_t mode)
{
    if (!path) return -EINVAL;
    char full_path[VFS_PATH_MAX];
    resolve_path(path, full_path, sizeof(full_path));
    return mkdir(full_path, mode);
}

int sys_unlink(const char *path)
{
    if (!path) return -EINVAL;
    char full_path[VFS_PATH_MAX];
    resolve_path(path, full_path, sizeof(full_path));
    return unlink(full_path);
}

int sys_getcwd(char *buf, size_t size)
{
    if (!buf || size == 0) return -EINVAL;
    if (!access_ok(VERIFY_WRITE, buf, size)) return -EFAULT;
    return proc_get_cwd(buf, size);
}

int sys_chdir(const char *path)
{
    if (!path) return -EINVAL;
    char full_path[VFS_PATH_MAX];
    resolve_path(path, full_path, sizeof(full_path));
    return proc_set_cwd(full_path);
}

pid_t sys_getpid(void)
{
    return proc_getpid();
}

pid_t sys_getppid(void)
{
    return proc_getppid();
}

void sys_exit(int status)
{
    proc_exit(status);
}

pid_t sys_wait4(pid_t pid, int *wstatus, int options, void *rusage)
{
    (void)rusage;
    return proc_waitpid(pid, wstatus, options);
}

int sys_kill(pid_t pid, int sig)
{
    return proc_signal_send(pid, sig);
}

int sys_uname(struct utsname *buf)
{
    if (!buf) return -EINVAL;
    strncpy(buf->sysname, "Linux", sizeof(buf->sysname) - 1);
    strncpy(buf->nodename, "cherry-smp", sizeof(buf->nodename) - 1);
    strncpy(buf->release, "5.10.0-cherry", sizeof(buf->release) - 1);
    strncpy(buf->version, "#1 SMP FreeRTOS AArch64", sizeof(buf->version) - 1);
    strncpy(buf->machine, "aarch64", sizeof(buf->machine) - 1);
    buf->domainname[0] = '\0';
    return 0;
}

int sys_nanosleep(const struct timespec *req, struct timespec *rem)
{
    (void)rem;
    if (!req) return -EINVAL;
    uint32_t ms = (uint32_t)(req->tv_sec * 1000 + req->tv_nsec / 1000000);
    if (ms == 0 && req->tv_nsec > 0) ms = 1;
    kal_thread_sleep_ms(ms);
    return 0;
}

int sys_clock_gettime(int clk_id, struct timespec *tp)
{
    (void)clk_id;
    if (!tp) return -EINVAL;
    uint64_t ns = ktime_get_real_ns();
    tp->tv_sec = (int64_t)(ns / 1000000000ULL);
    tp->tv_nsec = (int64_t)(ns % 1000000000ULL);
    return 0;
}

void *sys_brk(void *addr)
{
    struct process *p = proc_current();
    if (!p) return NULL;

    if (!addr || (uintptr_t)addr == 0) {
        return (void *)p->brk;
    }
    if ((uintptr_t)addr >= p->heap_start && (uintptr_t)addr <= p->heap_end) {
        p->brk = (uintptr_t)addr;
        return addr;
    }
    if ((uintptr_t)addr < p->heap_start) {
        return (void *)p->heap_start;
    }
    return (void *)p->brk;
}

int sys_fcntl(int fd, int cmd, long arg)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd < 0) return -EBADF;

    struct vfs_file *file = vfs_file_get(vfs_fd);
    if (!file) return -EBADF;

    int ret = 0;
    switch (cmd) {
    case 0: /* F_DUPFD */
    case 1030: /* F_DUPFD_CLOEXEC */
        ret = proc_fd_install(p, vfs_fd);
        break;
    case 1: /* F_GETFD */
        ret = 0;
        break;
    case 2: /* F_SETFD */
        ret = 0;
        break;
    case 3: /* F_GETFL */
        ret = (int)file->f_flags;
        break;
    case 4: /* F_SETFL */
        file->f_flags = (file->f_flags & ~O_NONBLOCK) | ((uint32_t)arg & O_NONBLOCK);
        ret = 0;
        break;
    default:
        ret = 0;
        break;
    }
    vfs_file_put(file);
    return ret;
}

int sys_ftruncate(int fd, off_t length)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, fd);
    if (vfs_fd < 0) return -EBADF;

    struct vfs_file *file = vfs_file_get(vfs_fd);
    if (!file) return -EBADF;

    int ret = 0;
    if (file->f_inode && file->f_inode->i_ops && file->f_inode->i_ops->truncate) {
        ret = file->f_inode->i_ops->truncate(file->f_inode, length);
    }
    vfs_file_put(file);
    return ret;
}

ssize_t sys_readlinkat(int dirfd, const char *path, char *buf, size_t bufsiz)
{
    (void)dirfd;
    if (!path || !buf || bufsiz == 0) return -EINVAL;
    struct process *p = proc_current();

    if (strcmp(path, "/proc/self/exe") == 0) {
        const char *ep = (p && p->exec_path[0] == '/') ? p->exec_path : "/bin/busybox";
        size_t len = strlen(ep);
        if (len > bufsiz) len = bufsiz;
        memcpy(buf, ep, len);
        return (ssize_t)len;
    }
    return -ENOENT;
}

int sys_gettimeofday(struct timeval *tv, void *tz)
{
    (void)tz;
    if (!tv) return -EINVAL;
    uint64_t ns = ktime_get_real_ns();
    tv->tv_sec = (long)(ns / 1000000000ULL);
    tv->tv_usec = (long)((ns % 1000000000ULL) / 1000);
    return 0;
}

static ssize_t sys_writev(int fd, const struct iovec *iov, int iovcnt)
{
    if (!iov || iovcnt <= 0) return -EINVAL;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len > 0 && iov[i].iov_base) {
            ssize_t w = sys_write(fd, iov[i].iov_base, iov[i].iov_len);
            if (w < 0) return (total > 0) ? total : w;
            total += w;
        }
    }
    return total;
}

static ssize_t sys_readv(int fd, const struct iovec *iov, int iovcnt)
{
    if (!iov || iovcnt <= 0) return -EINVAL;
    ssize_t total = 0;
    for (int i = 0; i < iovcnt; i++) {
        if (iov[i].iov_len > 0 && iov[i].iov_base) {
            ssize_t r = sys_read(fd, iov[i].iov_base, iov[i].iov_len);
            if (r < 0) return (total > 0) ? total : r;
            total += r;
            if ((size_t)r < iov[i].iov_len) break;
        }
    }
    return total;
}

static long sys_getrandom(void *buf, size_t buflen, unsigned int flags)
{
    (void)flags;
    if (!buf) return -EINVAL;
    uint8_t *p = (uint8_t *)buf;
    uint64_t t = ktime_get_real_ns();
    for (size_t i = 0; i < buflen; i++) {
        p[i] = (uint8_t)((t >> ((i % 8) * 8)) ^ (i * 0x3d + 0xa5));
    }
    return (long)buflen;
}

static void *sys_mmap(void *addr, size_t length, int prot, int flags, int fd, off_t offset)
{
    (void)prot;
    if (length == 0) return (void *)-EINVAL;

    struct process *p = proc_current();
    if (!p) return (void *)-ESRCH;

    void *ptr = NULL;

    /* MAP_FIXED (0x10): use caller-requested address buffer */
    if ((flags & 0x10) && addr) {
        ptr = addr;
    } else {
        ptr = kmalloc(length, GFP_KERNEL);
        if (!ptr) return (void *)-ENOMEM;
        memset(ptr, 0, length);
    }

    /* File-backed mapping for shared libraries (.so / dynamic linker) */
    if (fd >= 0) {
        int vfs_fd = proc_fd_get(p, fd);
        if (vfs_fd >= 0) {
            lseek(vfs_fd, offset, 0);
            read(vfs_fd, ptr, length);
        }
    }

    return ptr;
}

static int sys_munmap(void *addr, size_t length)
{
    (void)length;
    (void)addr;
    /* In flat memory model without MMU paging, partial/interior pointer munmap cannot be passed to vPortFree directly.
     * Process memory allocations are reclaimed upon process exit.
     */
    return 0;
}

static int sys_prlimit64(pid_t pid, int resource, const void *new_limit, void *old_limit)
{
    (void)pid; (void)resource; (void)new_limit;
    if (old_limit) {
        struct {
            uint64_t rlim_cur;
            uint64_t rlim_max;
        } *lim = (void *)old_limit;
        lim->rlim_cur = 8 * 1024 * 1024;
        lim->rlim_max = 8 * 1024 * 1024;
    }
    return 0;
}

int sys_faccessat(int dirfd, const char *pathname, int mode, int flags)
{
    (void)dirfd; (void)flags; (void)mode;
    if (!pathname) return -EINVAL;
    struct stat st;
    int ret = sys_stat(pathname, &st);
    if (ret != 0) return -ENOENT;
    return 0;
}

int sys_sysinfo(struct linux_sysinfo *info)
{
    if (!info) return -EINVAL;
    memset(info, 0, sizeof(*info));
    info->uptime = (long)(ktime_get_real_ns() / 1000000000ULL);
    info->totalram = 512 * 1024 * 1024;
    info->freeram = 384 * 1024 * 1024;
    info->procs = 5;
    info->mem_unit = 1;
    return 0;
}

pid_t sys_getpgid(pid_t pid)
{
    if (pid == 0) return sys_getpid();
    return pid;
}

int sys_setpgid(pid_t pid, pid_t pgid)
{
    (void)pid; (void)pgid;
    return 0;
}

mode_t sys_umask(mode_t mask)
{
    (void)mask;
    return 0022;
}

/* =========================================================================
 * I/O Multiplexing System Calls (poll, select, epoll)
 * ========================================================================= */
int sys_poll(struct pollfd *fds, nfds_t nfds, int timeout)
{
    if (!fds && nfds > 0) return -EINVAL;
    struct process *p = proc_current();
    if (!p) return -ESRCH;

    struct pollfd *k_fds = (struct pollfd *)kmalloc(sizeof(struct pollfd) * (nfds ? nfds : 1), GFP_KERNEL);
    if (!k_fds && nfds > 0) return -ENOMEM;

    for (nfds_t i = 0; i < nfds; i++) {
        k_fds[i].events = fds[i].events;
        k_fds[i].revents = 0;
        if (fds[i].fd >= 0) {
            k_fds[i].fd = proc_fd_get(p, fds[i].fd);
            if (k_fds[i].fd < 0) {
                k_fds[i].fd = -1;
                fds[i].revents = POLLNVAL;
            }
        } else {
            k_fds[i].fd = -1;
        }
    }

    int ret = poll(k_fds, nfds, timeout);
    for (nfds_t i = 0; i < nfds; i++) {
        if (fds[i].revents != POLLNVAL) {
            fds[i].revents = k_fds[i].revents;
        }
    }

    if (k_fds) kfree(k_fds);
    return ret;
}

int sys_ppoll(struct pollfd *fds, nfds_t nfds, const struct timespec *tmo_p, const void *sigmask)
{
    (void)sigmask;
    int timeout_ms = -1;
    if (tmo_p) {
        timeout_ms = (int)(tmo_p->tv_sec * 1000 + tmo_p->tv_nsec / 1000000);
        if (timeout_ms < 0) timeout_ms = 0;
    }
    return sys_poll(fds, nfds, timeout_ms);
}

int sys_select(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds, struct timeval *timeout)
{
    if (nfds < 0 || nfds > PROC_MAX_FDS) return -EINVAL;
    struct process *p = proc_current();
    if (!p) return -ESRCH;

    fd_set vfs_r, vfs_w, vfs_e;
    FD_ZERO(&vfs_r);
    FD_ZERO(&vfs_w);
    FD_ZERO(&vfs_e);

    int max_vfs_fd = 0;
    for (int fd = 0; fd < nfds; fd++) {
        int vfs_fd = proc_fd_get(p, fd);
        if (vfs_fd >= 0) {
            if (readfds && FD_ISSET(fd, readfds))   FD_SET(vfs_fd, &vfs_r);
            if (writefds && FD_ISSET(fd, writefds))  FD_SET(vfs_fd, &vfs_w);
            if (exceptfds && FD_ISSET(fd, exceptfds)) FD_SET(vfs_fd, &vfs_e);
            if (vfs_fd > max_vfs_fd) max_vfs_fd = vfs_fd;
        }
    }

    int ret = select(max_vfs_fd + 1,
                     readfds ? &vfs_r : NULL,
                     writefds ? &vfs_w : NULL,
                     exceptfds ? &vfs_e : NULL,
                     timeout);

    if (readfds) FD_ZERO(readfds);
    if (writefds) FD_ZERO(writefds);
    if (exceptfds) FD_ZERO(exceptfds);

    if (ret > 0) {
        int ready = 0;
        for (int fd = 0; fd < nfds; fd++) {
            int vfs_fd = proc_fd_get(p, fd);
            if (vfs_fd >= 0) {
                if (readfds && FD_ISSET(vfs_fd, &vfs_r))   { FD_SET(fd, readfds); ready++; }
                if (writefds && FD_ISSET(vfs_fd, &vfs_w))  { FD_SET(fd, writefds); ready++; }
                if (exceptfds && FD_ISSET(vfs_fd, &vfs_e)) { FD_SET(fd, exceptfds); ready++; }
            }
        }
        return ready;
    }
    return ret;
}

int sys_pselect6(int nfds, fd_set *readfds, fd_set *writefds, fd_set *exceptfds,
                 const struct timespec *timeout, const void *sigmask)
{
    (void)sigmask;
    struct timeval tv;
    struct timeval *tv_p = NULL;
    if (timeout) {
        tv.tv_sec = (long)timeout->tv_sec;
        tv.tv_usec = (long)(timeout->tv_nsec / 1000);
        tv_p = &tv;
    }
    return sys_select(nfds, readfds, writefds, exceptfds, tv_p);
}

int sys_epoll_create1(int flags)
{
    int vfs_epfd = epoll_create1(flags);
    if (vfs_epfd < 0) return vfs_epfd;

    struct process *p = proc_current();
    int proc_epfd = proc_fd_install(p, vfs_epfd);
    if (proc_epfd < 0) {
        close(vfs_epfd);
        return proc_epfd;
    }
    return proc_epfd;
}

int sys_epoll_ctl(int epfd, int op, int fd, struct epoll_event *event)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;

    int vfs_epfd = proc_fd_get(p, epfd);
    if (vfs_epfd < 0) return -EBADF;

    int vfs_target_fd = proc_fd_get(p, fd);
    if (vfs_target_fd < 0) return -EBADF;

    return epoll_ctl(vfs_epfd, op, vfs_target_fd, event);
}

int sys_epoll_pwait(int epfd, struct epoll_event *events, int maxevents, int timeout, const void *sigmask)
{
    (void)sigmask;
    struct process *p = proc_current();
    if (!p) return -ESRCH;

    int vfs_epfd = proc_fd_get(p, epfd);
    if (vfs_epfd < 0) return -EBADF;

    return epoll_wait(vfs_epfd, events, maxevents, timeout);
}

/* =========================================================================
 * Socket System Calls
 * ========================================================================= */
int sys_socket(int domain, int type, int protocol)
{
    int vfs_fd = socket(domain, type, protocol);
    if (vfs_fd < 0) return vfs_fd;

    struct process *p = proc_current();
    int proc_fd = proc_fd_install(p, vfs_fd);
    if (proc_fd < 0) {
        close(vfs_fd);
        return proc_fd;
    }
    return proc_fd;
}

int sys_bind(int sockfd, const void *addr, socklen_t addrlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return bind(vfs_fd, addr, addrlen);
}

int sys_connect(int sockfd, const void *addr, socklen_t addrlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return connect(vfs_fd, addr, addrlen);
}

int sys_listen(int sockfd, int backlog)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return listen(vfs_fd, backlog);
}

int sys_accept(int sockfd, void *addr, socklen_t *addrlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;

    int new_vfs_fd = accept(vfs_fd, addr, addrlen);
    if (new_vfs_fd < 0) return new_vfs_fd;

    int proc_fd = proc_fd_install(p, new_vfs_fd);
    if (proc_fd < 0) {
        close(new_vfs_fd);
        return proc_fd;
    }
    return proc_fd;
}

ssize_t sys_sendto(int sockfd, const void *buf, size_t len, int flags, const void *dest_addr, socklen_t addrlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return sendto(vfs_fd, buf, len, flags, dest_addr, addrlen);
}

ssize_t sys_recvfrom(int sockfd, void *buf, size_t len, int flags, void *src_addr, socklen_t *addrlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return recvfrom(vfs_fd, buf, len, flags, src_addr, addrlen);
}

int sys_setsockopt(int sockfd, int level, int optname, const void *optval, socklen_t optlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return setsockopt(vfs_fd, level, optname, optval, optlen);
}

int sys_getsockopt(int sockfd, int level, int optname, void *optval, socklen_t *optlen)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return getsockopt(vfs_fd, level, optname, optval, optlen);
}

int sys_shutdown(int sockfd, int how)
{
    struct process *p = proc_current();
    if (!p) return -ESRCH;
    int vfs_fd = proc_fd_get(p, sockfd);
    if (vfs_fd < 0) return -EBADF;
    return shutdown(vfs_fd, how);
}

__attribute__((weak)) void cherry_trace_sys_enter(long id, long a0, long a1, long a2, long a3, long a4, long a5)
{
    (void)id; (void)a0; (void)a1; (void)a2; (void)a3; (void)a4; (void)a5;
}
__attribute__((weak)) void cherry_trace_sys_exit(long id, long ret)
{
    (void)id; (void)ret;
}

static long cherry_syscall_internal(long nr, long a0, long a1, long a2, long a3, long a4, long a5);

long cherry_syscall(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
    cherry_trace_sys_enter(nr, a0, a1, a2, a3, a4, a5);
    long ret = cherry_syscall_internal(nr, a0, a1, a2, a3, a4, a5);
    cherry_trace_sys_exit(nr, ret);
    return ret;
}

static long cherry_syscall_internal(long nr, long a0, long a1, long a2, long a3, long a4, long a5)
{
    switch (nr) {
    case __NR_getcwd:
        return (long)sys_getcwd((char *)a0, (size_t)a1);
    case __NR_dup:
        return (long)sys_dup((int)a0);
    case __NR_dup3:
        return (long)sys_dup2((int)a0, (int)a1);
    case __NR_fcntl:
        return (long)sys_fcntl((int)a0, (int)a1, a2);
    case __NR_ioctl:
        return (long)sys_ioctl((int)a0, (unsigned long)a1, (void *)a2);
    case __NR_mkdirat:
        return (long)sys_mkdir((const char *)a1, (mode_t)a2);
    case __NR_unlinkat:
        return (long)sys_unlink((const char *)a1);
    case __NR_stat:
        return (long)sys_stat((const char *)a0, (struct stat *)a1);
    case __NR_ftruncate:
        return (long)sys_ftruncate((int)a0, (off_t)a1);
    case __NR_chdir:
        return (long)sys_chdir((const char *)a0);
    case __NR_openat:
        return (long)sys_openat((int)a0, (const char *)a1, (int)a2, (mode_t)a3);
    case __NR_close:
        return (long)sys_close((int)a0);
    case __NR_pipe2:
        return (long)sys_pipe2((int *)a0, (int)a1);
    case __NR_lseek:
        return (long)sys_lseek((int)a0, (off_t)a1, (int)a2);
    case __NR_read:
        return (long)sys_read((int)a0, (void *)a1, (size_t)a2);
    case __NR_write:
        return (long)sys_write((int)a0, (const void *)a1, (size_t)a2);
    case __NR_writev:
        return (long)sys_writev((int)a0, (const struct iovec *)a1, (int)a2);
    case __NR_readv:
        return (long)sys_readv((int)a0, (const struct iovec *)a1, (int)a2);
    case __NR_readlinkat:
        return (long)sys_readlinkat((int)a0, (const char *)a1, (char *)a2, (size_t)a3);
    case __NR_newfstatat:
        if ((a3 & 0x1000) || !a1 || *(const char *)a1 == '\0') {
            return (long)sys_fstat((int)a0, (struct stat *)a2);
        }
        return (long)sys_stat((const char *)a1, (struct stat *)a2);
    case __NR_fstat:
        return (long)sys_fstat((int)a0, (struct stat *)a1);
    case __NR_exit:
    case __NR_exit_group:
        sys_exit((int)a0);
        return 0;
    case __NR_set_tid_address:
        return (long)sys_getpid();
    case __NR_futex:
    case __NR_set_robust_list:
    case __NR_setitimer:
        return 0;
    case __NR_nanosleep:
        return (long)sys_nanosleep((const struct timespec *)a0, (struct timespec *)a1);
    case __NR_clock_gettime:
        return (long)sys_clock_gettime((int)a0, (struct timespec *)a1);
    case __NR_gettimeofday:
        return (long)sys_gettimeofday((struct timeval *)a0, (void *)a1);
    case __NR_sched_yield:
        kal_thread_yield();
        return 0;
    case __NR_kill:
        return (long)sys_kill((pid_t)a0, (int)a1);
    case __NR_rt_sigaction:
        return (long)sys_rt_sigaction((int)a0, (const struct sigaction *)a1, (struct sigaction *)a2, (size_t)a3);
    case __NR_rt_sigprocmask:
        return (long)sys_rt_sigprocmask((int)a0, (const sigset_t *)a1, (sigset_t *)a2, (size_t)a3);
    case __NR_rt_sigpending:
        return (long)sys_rt_sigpending((sigset_t *)a0, (size_t)a1);
    case __NR_uname:
        return (long)sys_uname((struct utsname *)a0);
    case __NR_getpid:
        return (long)sys_getpid();
    case __NR_getppid:
        return (long)sys_getppid();
    case __NR_getuid:
    case __NR_geteuid:
    case __NR_getgid:
    case __NR_getegid:
        return 0;
    case __NR_faccessat:
    case __NR_faccessat2:
        return (long)sys_faccessat((int)a0, (const char *)a1, (int)a2, (int)a3);
    case __NR_sysinfo:
        return (long)sys_sysinfo((struct linux_sysinfo *)a0);
    case __NR_getpgid:
        return (long)sys_getpgid((pid_t)a0);
    case __NR_setpgid:
        return (long)sys_setpgid((pid_t)a0, (pid_t)a1);
    case __NR_umask:
        return (long)sys_umask((mode_t)a0);
    case __NR_brk:
        return (long)sys_brk((void *)a0);
    case __NR_mmap:
        return (long)sys_mmap((void *)a0, (size_t)a1, (int)a2, (int)a3, (int)a4, (off_t)a5);
    case __NR_munmap:
        return (long)sys_munmap((void *)a0, (size_t)a1);
    case __NR_mprotect:
        return 0;
    case __NR_prlimit64:
        return (long)sys_prlimit64((pid_t)a0, (int)a1, (const void *)a2, (void *)a3);
    case __NR_getrandom:
        return (long)sys_getrandom((void *)a0, (size_t)a1, (unsigned int)a2);
    case __NR_rseq:
        return -ENOSYS;
    case __NR_wait4:
        return (long)sys_wait4((pid_t)a0, (int *)a1, (int)a2, (void *)a3);
    case __NR_epoll_create1:
        return (long)sys_epoll_create1((int)a0);
    case __NR_epoll_ctl:
        return (long)sys_epoll_ctl((int)a0, (int)a1, (int)a2, (struct epoll_event *)a3);
    case __NR_epoll_pwait:
        return (long)sys_epoll_pwait((int)a0, (struct epoll_event *)a1, (int)a2, (int)a3, (const void *)a4);
    case __NR_ppoll:
        return (long)sys_ppoll((struct pollfd *)a0, (nfds_t)a1, (const struct timespec *)a2, (const void *)a3);
    case __NR_pselect6:
        return (long)sys_pselect6((int)a0, (fd_set *)a1, (fd_set *)a2, (fd_set *)a3, (const struct timespec *)a4, (const void *)a5);
    case __NR_socket:
        return (long)sys_socket((int)a0, (int)a1, (int)a2);
    case __NR_bind:
        return (long)sys_bind((int)a0, (const void *)a1, (socklen_t)a2);
    case __NR_connect:
        return (long)sys_connect((int)a0, (const void *)a1, (socklen_t)a2);
    case __NR_listen:
        return (long)sys_listen((int)a0, (int)a1);
    case __NR_accept:
    case __NR_accept4:
        return (long)sys_accept((int)a0, (void *)a1, (socklen_t *)a2);
    case __NR_sendto:
        return (long)sys_sendto((int)a0, (const void *)a1, (size_t)a2, (int)a3, (const void *)a4, (socklen_t)a5);
    case __NR_recvfrom:
        return (long)sys_recvfrom((int)a0, (void *)a1, (size_t)a2, (int)a3, (void *)a4, (socklen_t *)a5);
    case __NR_setsockopt:
        return (long)sys_setsockopt((int)a0, (int)a1, (int)a2, (const void *)a3, (socklen_t)a4);
    case __NR_getsockopt:
        return (long)sys_getsockopt((int)a0, (int)a1, (int)a2, (void *)a3, (socklen_t *)a4);
    case __NR_getsockname:
    case __NR_getpeername:
        if (a1 && a2 && *(uint32_t *)a2 >= 16) {
            uint8_t *sin = (uint8_t *)a1;
            sin[0] = 2; sin[1] = 0;             /* AF_INET */
            sin[2] = 0x1f; sin[3] = 0x90;       /* Port 8080 */
            sin[4] = 127; sin[5] = 0; sin[6] = 0; sin[7] = 1; /* 127.0.0.1 */
            memset(&sin[8], 0, 8);
            *(uint32_t *)a2 = 16;
        }
        return 0;
    case __NR_statx:
        return -ENOSYS;
    case __NR_shutdown:
        return (long)sys_shutdown((int)a0, (int)a1);
    default:
        return -ENOSYS;
    }
}

extern void cherry_arch_exit_elf(void);

long cherry_syscall_dispatch_trap(struct syscall_frame *frame)
{
    if (!frame) return -EINVAL;

    if (frame->x8 == __NR_exit || frame->x8 == __NR_exit_group) {
        int code = (int)frame->x0;
        frame->pc = (uint64_t)cherry_arch_exit_elf;
        frame->x0 = (uint64_t)code;
        return code;
    }

    long ret = cherry_syscall((long)frame->x8,
                              (long)frame->x0, (long)frame->x1, (long)frame->x2,
                              (long)frame->x3, (long)frame->x4, (long)frame->x5);
    frame->x0 = (uint64_t)ret;
    return ret;
}


