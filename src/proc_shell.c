/*
 * CherryProcess - CherryShell Command Bindings (busybox, ps, kill, sh)
 *
 * Copyright (c) 2026 ForgeStitch
 * SPDX-License-Identifier: MIT
 */

#include "cherry_process.h"
#include "cherry_busybox.h"
#include "cherry_shell.h"
#include "cherry_printk.h"

int cmd_busybox(int argc, char **argv)
{
    return busybox_main(argc, argv);
}

int cmd_ps(int argc, char **argv)
{
    return applet_ps(argc, argv);
}

int cmd_kill(int argc, char **argv)
{
    return applet_kill(argc, argv);
}

int cmd_sh(int argc, char **argv)
{
    return applet_sh(argc, argv);
}

#include "cherry_elf.h"
#include "cherry_env.h"

static int env_print_cb(const char *key, const char *val, void *arg)
{
    (void)arg;
    printk("%s=%s\n", key, val);
    return 0;
}

int cmd_env(int argc, char **argv)
{
    (void)argc; (void)argv;
    proc_for_each_env(env_print_cb, NULL);
    return 0;
}

int cmd_export(int argc, char **argv)
{
    if (argc < 2) {
        printk("Usage: export KEY=VALUE\n");
        return -1;
    }
    char *eq = strchr(argv[1], '=');
    if (!eq) {
        printk("export: invalid assignment '%s' (format: KEY=VALUE)\n", argv[1]);
        return -1;
    }
    *eq = '\0';
    return proc_setenv(argv[1], eq + 1, 1);
}

int cmd_saveenv(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ENV_FILE_PATH;
    int ret = proc_env_save(path);
    printk("saveenv: saved environment to '%s' (ret=%d)\n", path, ret);
    return ret;
}

int cmd_loadenv(int argc, char **argv)
{
    const char *path = (argc > 1) ? argv[1] : ENV_FILE_PATH;
    int ret = proc_env_load(path);
    printk("loadenv: loaded environment from '%s' (ret=%d)\n", path, ret);
    return ret;
}

int cmd_elf_run(int argc, char **argv)
{
    if (argc < 2) {
        printk("Usage: elf_run <path/to/elf> [args...]\n");
        return -1;
    }
    pid_t pid = proc_spawn_elf(argv[1], argc - 1, &argv[1], NULL, -1, -1, -1);
    if (pid <= 0) {
        printk("elf_run: failed to spawn ELF '%s' (err=%d)\n", argv[1], (int)pid);
        return -1;
    }
    int status = 0;
    proc_waitpid(pid, &status, 0);
    return status;
}

void proc_register_shell_commands(void)
{
    cherry_shell_register_symbol("busybox", (uintptr_t)cmd_busybox, CHERRY_SHELL_SYM_CMD, "busybox multi-call utility suite");
    cherry_shell_register_symbol("ps",      (uintptr_t)cmd_ps,      CHERRY_SHELL_SYM_CMD, "report process snapshot");
    cherry_shell_register_symbol("kill",    (uintptr_t)cmd_kill,    CHERRY_SHELL_SYM_CMD, "send signal to process: kill [-9] PID");
    cherry_shell_register_symbol("sh",      (uintptr_t)cmd_sh,      CHERRY_SHELL_SYM_CMD, "interactive or scripted UNIX shell");
    cherry_shell_register_symbol("env",     (uintptr_t)cmd_env,     CHERRY_SHELL_SYM_CMD, "print environment variables");
    cherry_shell_register_symbol("export",  (uintptr_t)cmd_export,  CHERRY_SHELL_SYM_CMD, "set environment variable: export KEY=VALUE");
    cherry_shell_register_symbol("saveenv", (uintptr_t)cmd_saveenv, CHERRY_SHELL_SYM_CMD, "persist environment to storage: saveenv [path]");
    cherry_shell_register_symbol("loadenv", (uintptr_t)cmd_loadenv, CHERRY_SHELL_SYM_CMD, "reload environment from storage: loadenv [path]");
    cherry_shell_register_symbol("elf_run", (uintptr_t)cmd_elf_run, CHERRY_SHELL_SYM_CMD, "execute Linux AArch64 ELF binary: elf_run <path> [args...]");
    cherry_shell_register_symbol("proc_spawn_elf", (uintptr_t)proc_spawn_elf, CHERRY_SHELL_SYM_FUNC, "proc_spawn_elf(path, argc, argv, envp, in, out, err)");
    cherry_shell_register_symbol("proc_exec_elf",  (uintptr_t)proc_exec_elf,  CHERRY_SHELL_SYM_FUNC, "proc_exec_elf(path, argc, argv, envp)");
}
