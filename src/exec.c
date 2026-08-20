/*
 CCCC: Comprehensiev C Compensation Compiler

 Copyright (C) 2025 George Watson

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <https://www.gnu.org/licenses/>.
*/

#include "./internal.h"
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

void argv_push(ArgVec *args, const char *arg) {
    if (args->len + 1 >= args->cap) {
        int          new_cap  = args->cap ? args->cap * 2 : 16;
        const char **new_data = realloc(args->data, sizeof(char *) * new_cap);
        if (!new_data)
            error("failed to allocate argument vector");
        args->data = new_data;
        args->cap  = new_cap;
    }
    args->data[args->len++] = arg;
    args->data[args->len]   = NULL;
}

char *make_tmp_path(const char *suffix) {
#if defined(_WIN32)
    (void)suffix;
    return NULL;
#else
    char template[] = "/tmp/cccc-native-XXXXXX";
    int  fd         = mkstemp(template);
    if (fd < 0)
        return NULL;
    close(fd);

    size_t len  = strlen(template) + strlen(suffix) + 1;
    char  *path = malloc(len);
    if (!path) {
        unlink(template);
        return NULL;
    }
    snprintf(path, len, "%s%s", template, suffix);
    if (rename(template, path) != 0) {
        unlink(template);
        free(path);
        return NULL;
    }
    return path;
#endif
}

#if !defined(_WIN32)
extern char **environ;

// Merge the process environment with a NULL-terminated array of "NAME=VALUE"
// overrides (extra_env). Entries in extra_env shadow same-named entries from
// environ. Returns NULL if extra_env is NULL/empty (caller should fall back
// to plain execvp, which inherits environ unmodified). The returned array's
// pointers alias environ/extra_env strings — only the array itself is owned
// by the caller.
static char **merge_env(char *const extra_env[]) {
    if (!extra_env || !extra_env[0])
        return NULL;
    int base_count = 0;
    while (environ[base_count])
        base_count++;
    int extra_count = 0;
    while (extra_env[extra_count])
        extra_count++;
    char **merged =
        malloc(sizeof(char *) * (size_t)(base_count + extra_count + 1));
    if (!merged)
        return NULL;
    int n = 0;
    for (int i = 0; i < base_count; i++) {
        int shadowed = 0;
        for (int j = 0; j < extra_count; j++) {
            const char *eq = strchr(extra_env[j], '=');
            size_t      namelen =
                eq ? (size_t)(eq - extra_env[j]) : strlen(extra_env[j]);
            if (strncmp(environ[i], extra_env[j], namelen) == 0 &&
                environ[i][namelen] == '=') {
                shadowed = 1;
                break;
            }
        }
        if (!shadowed)
            merged[n++] = environ[i];
    }
    for (int j = 0; j < extra_count; j++)
        merged[n++] = extra_env[j];
    merged[n] = NULL;
    return merged;
}
#endif

// Like run_argv, but with an optional NULL-terminated "NAME=VALUE" array of
// environment overrides applied on top of the process environment (envp may
// be NULL for "inherit environment unmodified", identical to run_argv).
// Used by the build.c SetTargetEnv() API (#842) — e.g. AFL_USE_ASAN=1 for the
// afl-asan target — since neither RunCustom's vendored shell nor run_argv's
// plain execvp gives a target's compiler child its own environment.
int run_argv_env(char *const argv[], char *const envp[]) {
#if defined(_WIN32)
    (void)argv;
    (void)envp;
    fprintf(stderr, "error: -c=native is not supported on Windows yet\n");
    return 1;
#else
    pid_t pid = fork();
    if (pid < 0) {
        fprintf(stderr, "error: failed to fork native compiler: %s\n",
                strerror(errno));
        return 1;
    }
    if (pid == 0) {
        char **merged = merge_env(envp);
        if (merged)
            execve(argv[0], argv, merged);
        else
            execvp(argv[0], argv);
        fprintf(stderr, "error: failed to execute %s: %s\n", argv[0],
                strerror(errno));
        _exit(127);
    }

    int status = 0;
    if (waitpid(pid, &status, 0) < 0) {
        fprintf(stderr, "error: failed to wait for %s: %s\n", argv[0],
                strerror(errno));
        return 1;
    }
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    if (WIFSIGNALED(status))
        return 128 + WTERMSIG(status);
    return 1;
#endif
}

int run_argv(char *const argv[]) {
    return run_argv_env(argv, NULL);
}

// #1053: used to probe a host cc for which -std= spelling it accepts,
// without spilling the (expected, often-rejected) diagnostic from a
// rejected rung onto the user's terminal -- stdout/stderr are redirected
// to /dev/null in the child before exec, unlike run_argv_env().
int run_argv_quiet(char *const argv[]) {
#if defined(_WIN32)
    (void)argv;
    return 1;
#else
    pid_t pid = fork();
    if (pid < 0)
        return 1;
    if (pid == 0) {
        int devnull = open("/dev/null", O_WRONLY);
        if (devnull >= 0) {
            dup2(devnull, STDOUT_FILENO);
            dup2(devnull, STDERR_FILENO);
            close(devnull);
        }
        execvp(argv[0], argv);
        _exit(127);
    }
    int status = 0;
    if (waitpid(pid, &status, 0) < 0)
        return 1;
    if (WIFEXITED(status))
        return WEXITSTATUS(status);
    return 1;
#endif
}
