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
#include <sys/wait.h>
#include <unistd.h>
#endif

void argv_push(ArgVec *args, const char *arg) {
    if (args->len + 1 >= args->cap) {
        int new_cap = args->cap ? args->cap * 2 : 16;
        const char **new_data = realloc(args->data, sizeof(char *) * new_cap);
        if (!new_data)
            error("failed to allocate argument vector");
        args->data = new_data;
        args->cap = new_cap;
    }
    args->data[args->len++] = arg;
    args->data[args->len] = NULL;
}

char *make_tmp_path(const char *suffix) {
#if defined(_WIN32)
    (void)suffix;
    return NULL;
#else
    char template[] = "/tmp/cccc-native-XXXXXX";
    int fd = mkstemp(template);
    if (fd < 0)
        return NULL;
    close(fd);

    size_t len = strlen(template) + strlen(suffix) + 1;
    char *path = malloc(len);
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

int run_argv(char *const argv[]) {
#if defined(_WIN32)
    (void)argv;
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
