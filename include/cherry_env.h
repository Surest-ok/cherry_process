/*
 * CherryProcess - Environment Variables & Persistent Configuration
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#ifndef _CHERRY_ENV_H_
#define _CHERRY_ENV_H_

#include <stddef.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ENV_MAX_VARS    64
#define ENV_MAX_KEY     64
#define ENV_MAX_VAL     256
#define ENV_FILE_PATH   "/etc/environment"

/* Lifecycle & core APIs */
void        proc_env_init(void);
const char *proc_getenv(const char *name);
int         proc_setenv(const char *name, const char *value, int overwrite);
int         proc_unsetenv(const char *name);
int         proc_clearenv(void);

/* Iteration & envp array construction for execve / ELF */
typedef int (*proc_env_cb_t)(const char *key, const char *val, void *arg);
int         proc_for_each_env(proc_env_cb_t cb, void *arg);
char      **proc_env_build_envp(void);
void        proc_env_free_envp(char **envp);

/* Persistence to/from non-volatile VFS storage */
int         proc_env_save(const char *path);
int         proc_env_load(const char *path);

/* Self-test suite */
bool        proc_env_run_tests(void);

#ifdef __cplusplus
}
#endif

#endif /* _CHERRY_ENV_H_ */
