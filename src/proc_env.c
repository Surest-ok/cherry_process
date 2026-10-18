/*
 * CherryProcess - Environment Variables & Persistent Configuration Implementation
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_env.h"
#include "cherry_kal.h"
#include "cherry_printk.h"
#include "vfs_posix.h"
#include <string.h>
#include <errno.h>

extern int snprintf(char *str, size_t size, const char *format, ...);

struct env_entry {
    char key[ENV_MAX_KEY];
    char val[ENV_MAX_VAL];
    bool in_use;
};

static struct env_entry s_env_table[ENV_MAX_VARS];
static struct mutex s_env_lock;
static bool s_env_initialized = false;

void proc_env_init(void)
{
    if (s_env_initialized) return;

    mutex_init(&s_env_lock);
    memset(s_env_table, 0, sizeof(s_env_table));
    s_env_initialized = true;

    /* Populate default POSIX / Linux environment */
    proc_setenv("PATH", "/bin:/usr/bin:/sbin:/usr/sbin", 1);
    proc_setenv("LD_LIBRARY_PATH", "/lib/aarch64-linux-gnu:/usr/lib/aarch64-linux-gnu:/lib:/usr/lib", 1);
    proc_setenv("HOME", "/root", 1);
    proc_setenv("USER", "root", 1);
    proc_setenv("SHELL", "/bin/bash", 1);
    proc_setenv("TERM", "linux", 1);
    proc_setenv("PWD", "/", 1);
    proc_setenv("OS", "CherryOS", 1);
    proc_setenv("ARCH", "aarch64", 1);

    /* Restore persisted environment variables from /etc/environment if present */
    proc_env_load(ENV_FILE_PATH);
}

const char *proc_getenv(const char *name)
{
    if (!name || !name[0] || !s_env_initialized) return NULL;

    mutex_lock(&s_env_lock);
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use && strcmp(s_env_table[i].key, name) == 0) {
            const char *val = s_env_table[i].val;
            mutex_unlock(&s_env_lock);
            return val;
        }
    }
    mutex_unlock(&s_env_lock);
    return NULL;
}

int proc_setenv(const char *name, const char *value, int overwrite)
{
    if (!name || !name[0] || strchr(name, '=') != NULL) return -EINVAL;
    if (!value) value = "";
    if (!s_env_initialized) proc_env_init();

    mutex_lock(&s_env_lock);
    int free_slot = -1;

    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use) {
            if (strcmp(s_env_table[i].key, name) == 0) {
                if (!overwrite) {
                    mutex_unlock(&s_env_lock);
                    return 0;
                }
                strncpy(s_env_table[i].val, value, ENV_MAX_VAL - 1);
                s_env_table[i].val[ENV_MAX_VAL - 1] = '\0';
                mutex_unlock(&s_env_lock);
                return 0;
            }
        } else if (free_slot < 0) {
            free_slot = i;
        }
    }

    if (free_slot < 0) {
        mutex_unlock(&s_env_lock);
        return -ENOMEM;
    }

    s_env_table[free_slot].in_use = true;
    strncpy(s_env_table[free_slot].key, name, ENV_MAX_KEY - 1);
    s_env_table[free_slot].key[ENV_MAX_KEY - 1] = '\0';
    strncpy(s_env_table[free_slot].val, value, ENV_MAX_VAL - 1);
    s_env_table[free_slot].val[ENV_MAX_VAL - 1] = '\0';

    mutex_unlock(&s_env_lock);
    return 0;
}

int proc_unsetenv(const char *name)
{
    if (!name || !name[0] || !s_env_initialized) return -EINVAL;

    mutex_lock(&s_env_lock);
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use && strcmp(s_env_table[i].key, name) == 0) {
            s_env_table[i].in_use = false;
            s_env_table[i].key[0] = '\0';
            s_env_table[i].val[0] = '\0';
            mutex_unlock(&s_env_lock);
            return 0;
        }
    }
    mutex_unlock(&s_env_lock);
    return 0;
}

int proc_clearenv(void)
{
    if (!s_env_initialized) return 0;
    mutex_lock(&s_env_lock);
    memset(s_env_table, 0, sizeof(s_env_table));
    mutex_unlock(&s_env_lock);
    return 0;
}

int proc_for_each_env(proc_env_cb_t cb, void *arg)
{
    if (!cb || !s_env_initialized) return -EINVAL;

    mutex_lock(&s_env_lock);
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use) {
            int ret = cb(s_env_table[i].key, s_env_table[i].val, arg);
            if (ret != 0) {
                mutex_unlock(&s_env_lock);
                return ret;
            }
        }
    }
    mutex_unlock(&s_env_lock);
    return 0;
}

char **proc_env_build_envp(void)
{
    if (!s_env_initialized) proc_env_init();

    mutex_lock(&s_env_lock);
    int count = 0;
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use) count++;
    }

    char **envp = (char **)kmalloc(sizeof(char *) * (count + 1), GFP_KERNEL);
    if (!envp) {
        mutex_unlock(&s_env_lock);
        return NULL;
    }

    int idx = 0;
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use) {
            size_t klen = strlen(s_env_table[i].key);
            size_t vlen = strlen(s_env_table[i].val);
            size_t total = klen + 1 + vlen + 1;
            char *s = (char *)kmalloc(total, GFP_KERNEL);
            if (s) {
                snprintf(s, total, "%s=%s", s_env_table[i].key, s_env_table[i].val);
                envp[idx++] = s;
            }
        }
    }
    envp[idx] = NULL;
    mutex_unlock(&s_env_lock);
    return envp;
}

void proc_env_free_envp(char **envp)
{
    if (!envp) return;
    for (int i = 0; envp[i] != NULL; i++) {
        kfree(envp[i]);
    }
    kfree(envp);
}

int proc_env_save(const char *path)
{
    if (!path) path = ENV_FILE_PATH;
    if (!s_env_initialized) proc_env_init();

    /* Ensure parent directory exists */
    mkdir("/etc", 0755);

    int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) return -EIO;

    char buf[512];
    const char *header = "# CherryOS Persistent Environment Configuration\n";
    write(fd, header, strlen(header));

    mutex_lock(&s_env_lock);
    for (int i = 0; i < ENV_MAX_VARS; i++) {
        if (s_env_table[i].in_use) {
            int n = snprintf(buf, sizeof(buf), "%s=%s\n", s_env_table[i].key, s_env_table[i].val);
            if (n > 0) {
                write(fd, buf, (size_t)n);
            }
        }
    }
    mutex_unlock(&s_env_lock);

    close(fd);
    return 0;
}

int proc_env_load(const char *path)
{
    if (!path) path = ENV_FILE_PATH;
    int fd = open(path, O_RDONLY);
    if (fd < 0) return -ENOENT;

    char file_buf[2048];
    ssize_t n = read(fd, file_buf, sizeof(file_buf) - 1);
    close(fd);

    if (n <= 0) return 0;
    file_buf[n] = '\0';

    char *line = file_buf;
    while (*line) {
        char *eol = strchr(line, '\n');
        if (eol) *eol = '\0';

        char *cr = strchr(line, '\r');
        if (cr) *cr = '\0';

        while (*line == ' ' || *line == '\t') line++;

        if (*line != '#' && *line != '\0') {
            char *eq = strchr(line, '=');
            if (eq) {
                *eq = '\0';
                char *k = line;
                char *v = eq + 1;

                char *k_end = eq - 1;
                while (k_end > k && (*k_end == ' ' || *k_end == '\t')) {
                    *k_end = '\0';
                    k_end--;
                }
                proc_setenv(k, v, 1);
            }
        }

        if (!eol) break;
        line = eol + 1;
    }
    return 0;
}

bool proc_env_run_tests(void)
{
    proc_env_init();

    /* 1. Test set and get */
    int s1 = proc_setenv("TEST_OS_ENV", "alpha_100", 1);
    const char *g1 = proc_getenv("TEST_OS_ENV");
    bool pass_set_get = (s1 == 0 && g1 && strcmp(g1, "alpha_100") == 0);

    /* 2. Test overwrite flag */
    proc_setenv("TEST_OS_ENV", "beta_200", 0);
    const char *g2 = proc_getenv("TEST_OS_ENV");
    bool pass_no_overwrite = (g2 && strcmp(g2, "alpha_100") == 0);

    proc_setenv("TEST_OS_ENV", "beta_200", 1);
    const char *g3 = proc_getenv("TEST_OS_ENV");
    bool pass_overwrite = (g3 && strcmp(g3, "beta_200") == 0);

    /* 3. Test persistence: Save to non-volatile VFS storage */
    int save_ret = proc_env_save("/cherry_test_dir/env_test.conf");
    bool pass_save = (save_ret == 0);

    /* 4. Test unset */
    proc_unsetenv("TEST_OS_ENV");
    const char *g4 = proc_getenv("TEST_OS_ENV");
    bool pass_unset = (g4 == NULL);

    /* 5. Test persistence: Reload from VFS file */
    int load_ret = proc_env_load("/cherry_test_dir/env_test.conf");
    const char *g5 = proc_getenv("TEST_OS_ENV");
    bool pass_load = (load_ret == 0 && g5 && strcmp(g5, "beta_200") == 0);

    /* Cleanup test variable */
    proc_unsetenv("TEST_OS_ENV");

    /* 6. Verify default environment variables */
    const char *g_path = proc_getenv("PATH");
    const char *g_os = proc_getenv("OS");
    bool pass_defaults = (g_path != NULL && g_os != NULL && strcmp(g_os, "CherryOS") == 0);

    bool all_pass = pass_set_get && pass_no_overwrite && pass_overwrite &&
                    pass_save && pass_unset && pass_load && pass_defaults;

    pr_info("cherry_process: [env-verify] set=%s no_ovr=%s ovr=%s save=%s unset=%s load=%s def=%s all=%s\n",
            pass_set_get ? "pass" : "fail",
            pass_no_overwrite ? "pass" : "fail",
            pass_overwrite ? "pass" : "fail",
            pass_save ? "pass" : "fail",
            pass_unset ? "pass" : "fail",
            pass_load ? "pass" : "fail",
            pass_defaults ? "pass" : "fail",
            all_pass ? "pass" : "fail");

    return all_pass;
}
