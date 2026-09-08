/* paul_shell.h -- https://git.sr.ht/~takeiteasy/paul
   Vendored into CCCC from the original at ../paul/paul_shell.h.

   CCCC-local patches on top of the original:
     - Added `cmd_allowlist` field to shell_ctx (allowlist of permitted
 commands).
     - Added `shell_ctx_allowlist_cmd()` to populate the allowlist.
     - Patched exit-code threading throughout: command_execute /
 eval_commandtail / eval_sequence / eval_redirection / eval_pipeline / ast_exec
 all propagate real child exit codes rather than always returning EXIT_SUCCESS.
       (v1 limitation: die() still calls exit(EXIT_FAILURE) on OOM/open failure,
       which terminates the entire process rather than failing one build step.
       Filed: see BUILDMODE ticket for die()-abort-on-error improvement.)
     - Added `defer_wait`/`deferred_pids`/`deferred_pid_count` to shell_ctx so
       eval_pipeline() can fork every stage of a pipeline before waiting on
       any of them, instead of the original fork-then-wait-immediately
       sequencing (which deadlocked on any pipeline payload larger than a
       pipe buffer).
     - shell_builtin_func_t returns `int` (an exit status) rather than `void`,
       so command_execute() reports a builtin's real result; builtins also
       honour a stage's redirect/pipe fds (run via a forked subshell inside a
       pipeline, via a temporary dup2 of the shell's own std fds otherwise).
     - eval_pipeline() sets FD_CLOEXEC on every pipe it creates, so a forked
       stage does not inherit unrelated pipe ends across exec() -- an
       early-exiting downstream reader (`producer | head`) now lets the
       producer see EOF/SIGPIPE instead of hanging forever.

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
 along with this program.  If not, see <https://www.gnu.org/licenses/>. */

/*!
 @header paul_shell.h
 @copyright George Watson GPLv3
 @updated 2025-09-29
 @brief Embeddable bourne-like shell (posix+windows)
 @discussion
    Implementation is included when PAUL_SHELL_IMPLEMENTATION or
 PAUL_IMPLEMENTATION is defined.
*/

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#ifdef _POSIX_VERSION
#include <unistd.h>
#include <fnmatch.h>
#include <glob.h>
#include <sys/wait.h>
#endif

#ifndef PAUL_SHELL_H
#define PAUL_SHELL_H
#ifdef __cplusplus
extern "C" {
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <wchar.h>
#include <errno.h>
#include <signal.h>
#include <poll.h>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#include <io.h>
#include <fcntl.h>
#include <processthreadsapi.h>
#include <synchapi.h>
#else
/* POSIX headers */
#include <unistd.h>
#include <fcntl.h>
#endif

/* Return codes for shell(): non-negative values are child exit codes.
 * Negative values are library error codes.
 */
#define SHELL_OK           0
#define SHELL_ERR_GENERIC  -1
#define SHELL_ERR_TOKENIZE -2
#define SHELL_ERR_EVAL     -3
#define SHELL_ERR_PIPE     -4
#define SHELL_ERR_FORK     -5
#define SHELL_ERR_READ     -6
#define SHELL_ERR_PERM     -7

typedef void (*shell_stream_cb_t)(const char *data, size_t len, void *userdata);

typedef struct shell_io {
    /* I/O capture and streaming control structure */
    char  *out; /* captured stdout (NUL-terminated) */
    size_t out_len;
    char  *err; /* captured stderr (NUL-terminated) */
    size_t err_len;

    /* Input to be written to child's STDIN before closing it. */
    const char *in;
    size_t      in_len;

    /* Streaming callbacks. */
    shell_stream_cb_t out_cb;
    shell_stream_cb_t err_cb;
    void             *userdata;
} shell_io;

/* CCCC patch (#1325): returns an exit status (0 = success), not void, so a
 * builtin's real result reaches command_execute() instead of a hardcoded 0. */
typedef int (*shell_builtin_func_t)(int argc, char **argv);

typedef struct shell_builtin_entry {
    char                       *name;
    shell_builtin_func_t        func;
    struct shell_builtin_entry *next;
} shell_builtin_entry_t;

/* #1322: cap on how many pipeline stages eval_pipeline() can defer waiting
 * on at once (see shell_ctx.deferred_pids below). Kept outside the struct
 * body so a future re-vendor of this file against the upstream original
 * diffs cleanly on the struct itself. */
#define SHELL_MAX_DEFERRED_PIDS 64

typedef struct shell_ctx {
    /* Configuration */
    bool builtin_only; /* If true, only builtins are executed */

    /* Lists */
    shell_builtin_entry_t *builtins; /* Linked list of builtins */
    char **cmd_blacklist; /* NULL-terminated array of forbidden commands */
    char *
        *cmd_allowlist;   /* NULL-terminated allowlist; if non-NULL & non-empty,
                             only listed commands may execute (builtins exempt) */
    char **path_blacklist; /* NULL-terminated array of forbidden paths
                              (read/write/exec) */

    /* Internal Execution State */
    int input_fd;
    int output_fd;
    int bg;

    /* #1322: pipeline concurrency. When set, command_execute() records a
     * forked stage's pid instead of waiting on it immediately, so
     * eval_pipeline() can fork every stage before waiting on any of them
     * (fixing the sequential fork+wait deadlock on payloads bigger than a
     * pipe buffer). Saved/restored (not set/cleared) around a pipeline,
     * since `$(...)` command substitution re-enters the shell with this
     * same ctx. */
    bool  defer_wait;
    pid_t deferred_pids[SHELL_MAX_DEFERRED_PIDS];
    int   deferred_pid_count;

    void *userdata; /* For custom use in builtins */
} shell_ctx;

/*!
 @function shell_ctx_create
 @return A new shell context with default settings (standard builtins enabled).
 */
shell_ctx *shell_ctx_create(void);

/*!
 @function shell_ctx_destroy
 @param ctx The context to destroy.
 */
void shell_ctx_destroy(shell_ctx *ctx);

/*!
 @function shell_ctx_add_builtin
 @brief Register a builtin command. Overrides existing builtins with the same
 name. The callback returns an exit status (0 = success); it is honoured
 for redirects and pipes just like an external command.
 */
void shell_ctx_add_builtin(shell_ctx *ctx, const char *name,
                           shell_builtin_func_t func);

/*!
 @function shell_ctx_blacklist_cmd
 @brief Add a command name to the blacklist (e.g., "rm").
 */
void shell_ctx_blacklist_cmd(shell_ctx *ctx, const char *cmd);

/*!
 @function shell_ctx_allowlist_cmd
 @brief Add a command name to the allowlist.  When the allowlist is non-empty
 only listed external commands may be executed; shell builtins are always
 permitted.
 */
void shell_ctx_allowlist_cmd(shell_ctx *ctx, const char *cmd);

/*!
 @function shell_ctx_blacklist_path
 @brief Add a path to the blacklist (preventing redirection or execution).
 */
void shell_ctx_blacklist_path(shell_ctx *ctx, const char *path);

/*!
 @function shell_set_default_ctx
 @brief Sets the global default context used by `shell()`.
 @param ctx The context to use. If NULL, `shell()` creates a temporary context
 per call.
 */
void shell_set_default_ctx(shell_ctx *ctx);

/*!
 @function shell_with_ctx
 @brief Execute command using a specific context.
 */
int shell_with_ctx(const char *cmd, shell_io *io, shell_ctx *ctx);

/*!
 @function shell
 @brief Execute command using the default context.
 */
int shell(const char *cmd, shell_io *io);

/*!
 @function shell_fmt
 @brief Format and execute command using the default context.
 */
int shell_fmt(shell_io *io, const char *fmt, ...);

#ifdef __cplusplus
}
#endif
#endif // PAUL_SHELL_H
