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

typedef void (*shell_builtin_func_t)(int argc, char **argv);

typedef struct shell_builtin_entry {
    char                       *name;
    shell_builtin_func_t        func;
    struct shell_builtin_entry *next;
} shell_builtin_entry_t;

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
    int   input_fd;
    int   output_fd;
    int   bg;

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
 name.
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
