/* paul_shell.h -- https://git.sr.ht/~takeiteasy/paul
   Vendored into CCCC from the original at ../paul/paul_shell.h.

   CCCC-local patches on top of the original:
     - Added `cmd_allowlist` field to shell_ctx (allowlist of permitted commands).
     - Added `shell_ctx_allowlist_cmd()` to populate the allowlist.
     - Patched exit-code threading throughout: command_execute / eval_commandtail /
       eval_sequence / eval_redirection / eval_pipeline / ast_exec all propagate
       real child exit codes rather than always returning EXIT_SUCCESS.
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
    Implementation is included when PAUL_SHELL_IMPLEMENTATION or PAUL_IMPLEMENTATION is defined.
*/

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
#define SHELL_OK 0
#define SHELL_ERR_GENERIC -1
#define SHELL_ERR_TOKENIZE -2
#define SHELL_ERR_EVAL -3
#define SHELL_ERR_PIPE -4
#define SHELL_ERR_FORK -5
#define SHELL_ERR_READ -6
#define SHELL_ERR_PERM -7

typedef void (*shell_stream_cb_t)(const char *data, size_t len, void *userdata);

typedef struct shell_io {
    /* I/O capture and streaming control structure */
    char *out;    /* captured stdout (NUL-terminated) */
    size_t out_len;
    char *err;    /* captured stderr (NUL-terminated) */
    size_t err_len;

    /* Input to be written to child's STDIN before closing it. */
    const char *in;
    size_t in_len;

    /* Streaming callbacks. */
    shell_stream_cb_t out_cb;
    shell_stream_cb_t err_cb;
    void *userdata;
} shell_io;

typedef void (*shell_builtin_func_t)(int argc, char **argv);

typedef struct shell_builtin_entry {
    char *name;
    shell_builtin_func_t func;
    struct shell_builtin_entry *next;
} shell_builtin_entry_t;

typedef struct shell_ctx {
    /* Configuration */
    bool builtin_only;     /* If true, only builtins are executed */

    /* Lists */
    shell_builtin_entry_t *builtins; /* Linked list of builtins */
    char **cmd_blacklist;            /* NULL-terminated array of forbidden commands */
    char **cmd_allowlist;            /* NULL-terminated allowlist; if non-NULL & non-empty,
                                        only listed commands may execute (builtins exempt) */
    char **path_blacklist;           /* NULL-terminated array of forbidden paths (read/write/exec) */

    /* Internal Execution State */
    int input_fd;
    int output_fd;
    int bg;

    void *userdata; /* For custom use in builtins */
} shell_ctx;

/*!
 @function shell_ctx_create
 @return A new shell context with default settings (standard builtins enabled).
 */
shell_ctx* shell_ctx_create(void);

/*!
 @function shell_ctx_destroy
 @param ctx The context to destroy.
 */
void shell_ctx_destroy(shell_ctx *ctx);

/*!
 @function shell_ctx_add_builtin
 @brief Register a builtin command. Overrides existing builtins with the same name.
 */
void shell_ctx_add_builtin(shell_ctx *ctx, const char *name, shell_builtin_func_t func);

/*!
 @function shell_ctx_blacklist_cmd
 @brief Add a command name to the blacklist (e.g., "rm").
 */
void shell_ctx_blacklist_cmd(shell_ctx *ctx, const char *cmd);

/*!
 @function shell_ctx_allowlist_cmd
 @brief Add a command name to the allowlist.  When the allowlist is non-empty only
        listed external commands may be executed; shell builtins are always permitted.
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
 @param ctx The context to use. If NULL, `shell()` creates a temporary context per call.
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

#if defined(PAUL_SHELL_IMPLEMENTATION) || defined(PAUL_IMPLEMENTATION)

/* --- Globals --- */
static shell_ctx *g_default_ctx = NULL;

/* --- Context Management --- */

static void builtin_exit(int argc, char **argv);
static void builtin_cd(int argc, char **argv);
static void builtin_pwd(int argc, char **argv);

shell_ctx* shell_ctx_create(void) {
    shell_ctx *ctx = (shell_ctx*)malloc(sizeof(shell_ctx));
    if (!ctx) return NULL;
    memset(ctx, 0, sizeof(shell_ctx));

    ctx->input_fd = -1;
    ctx->output_fd = -1;

    /* Add default builtins */
    shell_ctx_add_builtin(ctx, "exit", builtin_exit);
    shell_ctx_add_builtin(ctx, "cd", builtin_cd);
    shell_ctx_add_builtin(ctx, "pwd", builtin_pwd);

    return ctx;
}

void shell_ctx_destroy(shell_ctx *ctx) {
    if (!ctx) return;

    /* Free builtins */
    shell_builtin_entry_t *b = ctx->builtins;
    while (b) {
        shell_builtin_entry_t *next = b->next;
        free(b->name);
        free(b);
        b = next;
    }

    /* Free cmd blacklist */
    if (ctx->cmd_blacklist) {
        for (int i = 0; ctx->cmd_blacklist[i]; ++i)
            free(ctx->cmd_blacklist[i]);
        free(ctx->cmd_blacklist);
    }

    /* Free cmd allowlist */
    if (ctx->cmd_allowlist) {
        for (int i = 0; ctx->cmd_allowlist[i]; ++i)
            free(ctx->cmd_allowlist[i]);
        free(ctx->cmd_allowlist);
    }

    /* Free path blacklist */
    if (ctx->path_blacklist) {
        for (int i = 0; ctx->path_blacklist[i]; ++i)
            free(ctx->path_blacklist[i]);
        free(ctx->path_blacklist);
    }

    free(ctx);
}

void shell_ctx_add_builtin(shell_ctx *ctx, const char *name, shell_builtin_func_t func) {
    shell_builtin_entry_t *entry = (shell_builtin_entry_t*)malloc(sizeof(shell_builtin_entry_t));
    if (!entry) return; // OOM
    entry->name = strdup(name);
    entry->func = func;

    /* Prepend to list to ensure overriding behavior (LIFO check) */
    entry->next = ctx->builtins;
    ctx->builtins = entry;
}

static void _append_str_array(char ***arr, const char *str) {
    int count = 0;
    if (*arr) {
        while ((*arr)[count]) count++;
    }

    char **new_arr = (char**)realloc(*arr, sizeof(char*) * (count + 2));
    if (!new_arr) return; // OOM

    new_arr[count] = strdup(str);
    new_arr[count+1] = NULL;
    *arr = new_arr;
}

void shell_ctx_blacklist_cmd(shell_ctx *ctx, const char *cmd) {
    _append_str_array(&ctx->cmd_blacklist, cmd);
}

void shell_ctx_allowlist_cmd(shell_ctx *ctx, const char *cmd) {
    _append_str_array(&ctx->cmd_allowlist, cmd);
}

void shell_ctx_blacklist_path(shell_ctx *ctx, const char *path) {
    _append_str_array(&ctx->path_blacklist, path);
}

void shell_set_default_ctx(shell_ctx *ctx) {
    g_default_ctx = ctx;
}

/* --- Lexer / Parser (Same as before, largely unchanged) --- */

typedef enum shell_token_type {
    SHELL_TOKEN_ERROR,
    SHELL_TOKEN_EOL,
    SHELL_TOKEN_ATOM,
    SHELL_TOKEN_PIPE = '|',
    SHELL_TOKEN_AMPERSAND = '&',
    SHELL_TOKEN_GREATER = '>',
    SHELL_TOKEN_LESSER = '<',
    SHELL_TOKEN_SEMICOLON = ';'
} shell_token_type;

typedef struct shell_token {
    shell_token_type type;
    unsigned char *begin;
    int length;
} shell_token_t;

typedef struct shell_lexer {
    unsigned char *begin;
    struct {
        unsigned char *ptr;
        wchar_t ch;
        int ch_length;
    } cursor;
    char *error;
    unsigned char *input_begin;
    size_t error_pos;
} shell_lexer_t;

typedef enum shell_ast_type {
    SHELL_AST_CMD,
    SHELL_AST_BACKGROUND,
    SHELL_AST_SEQ,
    SHELL_AST_REDIR_IN,
    SHELL_AST_REDIR_OUT,
    SHELL_AST_PIPE
} shell_ast_type_t;

typedef struct shell_ast {
    shell_ast_type_t type;
    shell_token_t *token;
    struct shell_ast *left, *right;
} shell_ast_t;

typedef struct shell_token_array {
    shell_token_t *data;
    size_t count;
    size_t capacity;
} shell_token_array_t;

typedef struct shell_parser {
    shell_token_array_t tokens;
    shell_token_t *current;
    size_t cursor;
} shell_parser;

static void eprintf(const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    vfprintf(stderr, fmt, ap);
    va_end(ap);
}

static void _perror(const char *msg) {
    eprintf("shell: %s: %s\n", msg, strerror(errno));
}

/* v1 limitation: die() terminates the entire process on OOM or failed open.
 * This means a build step that triggers one of these failures will kill the
 * whole compiler rather than cleanly failing that step.  A future version
 * should use longjmp or an error-return path instead.  TODO: file ticket. */
static void die(const char *msg) {
    if (errno)
        _perror(msg);
    exit(EXIT_FAILURE);
}

static void *xmalloc(size_t size) {
    void *p = malloc(size);
    if (!p)
        die("malloc");
    return p;
}

static void *xrealloc(void *ptr, size_t size) {
    void *p = realloc(ptr, size);
    if (!p)
        die("realloc");
    return p;
}

static int xopen(const char *path, int flags, mode_t mode) {
    int fd = open(path, flags, mode);
    if (fd == -1)
        die("open");
    return fd;
}

static int utf8read(const unsigned char* c, wchar_t* out) {
    wchar_t u = *c, l = 1;
    if ((u & 0xC0) == 0xC0) {
        int a = (u & 0x20) ? ((u & 0x10) ? ((u & 0x08) ? ((u & 0x04) ? 6 : 5) : 4) : 3) : 2;
        if (a < 6 || !(u & 0x02)) {
            u = ((u << (a + 1)) & 0xFF) >> (a + 1);
            for (int b = 1; b < a; ++b)
                u = (u << 6) | (c[l++] & 0x3F);
        }
    }
    if (out)
        *out = u;
    return l;
}

static void shell_lexer(shell_lexer_t *l, unsigned char* line) {
    l->begin = line;
    l->cursor.ptr = line;
    l->cursor.ch_length = utf8read(line, &l->cursor.ch);
    l->error = NULL;
    l->input_begin = line;
    l->error_pos = (size_t)-1;
}

static inline wchar_t peek(shell_lexer_t *l) {
    return l->cursor.ch;
}

static inline bool is_eof(shell_lexer_t *l) {
    return peek(l) == '\0';
}

static inline void update(shell_lexer_t *l) {
    l->begin = l->cursor.ptr;
    l->cursor.ch_length = utf8read(l->cursor.ptr, &l->cursor.ch);
    l->error = NULL;
}

static inline wchar_t advance(shell_lexer_t *l) {
    l->cursor.ptr += utf8read(l->cursor.ptr, NULL);
    l->cursor.ch_length = utf8read(l->cursor.ptr, &l->cursor.ch);
    return l->cursor.ch;
}

static inline wchar_t next(shell_lexer_t *l) {
    if (is_eof(l))
        return '\0';
    wchar_t next;
    utf8read(l->cursor.ptr + l->cursor.ch_length, &next);
    return next;
}

static void skip_whitespace(shell_lexer_t *l) {
    for (;;) {
        if (is_eof(l))
            return;
        switch (peek(l)) {
            case ' ':
            case '\t':
            case '\v':
            case '\r':
            case '\n':
            case '\f':
                advance(l);
                break;
            default:
                return;
        }
    }
}

static shell_token_t new_token(shell_lexer_t *l, shell_token_type type) {
    return (shell_token_t) {
        .type = type,
        .begin = l->begin,
        .length = (int)(l->cursor.ptr - l->begin)
    };
}

static shell_token_t read_atom(shell_lexer_t *l) {
    for (;;) {
        if (is_eof(l))
            goto BAIL;
        switch (peek(l)) {
            case ' ':
            case '\t':
            case '\v':
            case '\r':
            case '\n':
            case '\f':
            case '|':
            case '&':
            case '<':
            case '>':
            case ';':
                goto BAIL;
            default:
                advance(l);
                break;
        }
    }
BAIL:
    return new_token(l, SHELL_TOKEN_ATOM);
}

static shell_token_t read_token(shell_lexer_t *l) {
    if (is_eof(l))
        return new_token(l, SHELL_TOKEN_EOL);
    update(l);
    wchar_t wc = peek(l);
    switch (wc) {
        case ' ':
        case '\t':
        case '\v':
        case '\r':
        case '\n':
        case '\f':
            skip_whitespace(l);
            update(l);
            return read_token(l);
        case '"':
        case '\'': {
            wchar_t quote = wc;
            unsigned char *start = l->cursor.ptr + l->cursor.ch_length;
            unsigned char *p = start;
            for (;;) {
                wchar_t ch;
                int len = utf8read(p, &ch);
                if (ch == '\0') {
                    l->error = "unterminated quote";
                    l->begin = start;
                    l->cursor.ptr = p;
                    if (l->input_begin)
                        l->error_pos = (size_t)(p - l->input_begin);
                    return new_token(l, SHELL_TOKEN_ERROR);
                }
                if (ch == quote) {
                    l->begin = start;
                    l->cursor.ptr = p;
                    shell_token_t tok = new_token(l, SHELL_TOKEN_ATOM);
                    advance(l);
                    return tok;
                }
                p += len;
            }
        }
        case '|':
        case '&':
        case '<':
        case '>':
        case ';':
            advance(l);
            return new_token(l, (shell_token_type)wc);
        default:
            return read_atom(l);
    }
    return new_token(l, SHELL_TOKEN_ERROR);
}

static inline const char* token_string(shell_token_t *t) {
    switch (t->type) {
        case SHELL_TOKEN_ERROR: return "ERROR";
        case SHELL_TOKEN_EOL: return "END";
        case SHELL_TOKEN_ATOM: return "ATOM";
        case SHELL_TOKEN_PIPE: return "PIPE";
        case SHELL_TOKEN_AMPERSAND: return "AMPERSAND";
        case SHELL_TOKEN_GREATER: return "GREATER";
        case SHELL_TOKEN_LESSER: return "LESSER";
        case SHELL_TOKEN_SEMICOLON: return "SEMICOLON";
    }
    return "UNKNOWN";
}

static void shell_token_array_init(shell_token_array_t *arr) {
    arr->data = NULL;
    arr->count = 0;
    arr->capacity = 0;
}

static bool shell_token_array_append(shell_token_array_t *arr, shell_token_t token) {
    if (arr->count + 1 > arr->capacity) {
        size_t new_capacity = arr->capacity == 0 ? 8 : arr->capacity * 2;
        shell_token_t *new_data = xrealloc(arr->data, sizeof(shell_token_t) * new_capacity);
        if (!new_data)
            return false;
        arr->data = new_data;
        arr->capacity = new_capacity;
    }
    arr->data[arr->count++] = token;
    return true;
}

static shell_token_array_t shell_parse(shell_lexer_t *l) {
    shell_token_array_t tokens;
    shell_token_array_init(&tokens);
    for (;;) {
        shell_token_t token = read_token(l);
        switch (token.type) {
            default:
                l->error = "unknown token";
            case SHELL_TOKEN_ERROR:
            case SHELL_TOKEN_EOL:
                goto BAIL;
            case SHELL_TOKEN_ATOM:
            case SHELL_TOKEN_PIPE:
            case SHELL_TOKEN_AMPERSAND:
            case SHELL_TOKEN_GREATER:
            case SHELL_TOKEN_LESSER:
            case SHELL_TOKEN_SEMICOLON:
                if (!shell_token_array_append(&tokens, token))
                    l->error = "out of memory";
                break;
        }
    }
BAIL:
    return tokens;
}

static inline shell_ast_t* new_ast(void) {
    shell_ast_t *result = malloc(sizeof(shell_ast_t));
    memset(result, 0, sizeof(shell_ast_t));
    return result;
}

static void free_ast(shell_ast_t *node) {
    if (!node)
        return;
    free_ast(node->left);
    free_ast(node->right);
    free(node);
}

static shell_token_t* parser_peek(shell_parser *p) {
    return p->cursor < p->tokens.count ? &p->tokens.data[p->cursor] : NULL;
}

static shell_token_t* parser_next(shell_parser *p) {
    if (p->cursor + 1 >= p->tokens.count)
        return NULL;
    ++p->cursor;
    return &p->tokens.data[p->cursor];
}

static int match_token(shell_parser *p, shell_token_type type) {
        shell_token_t *t = parser_peek(p);
        return t != NULL && t->type == type;
}

static int expect_token(shell_parser *p, shell_token_type type) {
    shell_token_t *t = parser_peek(p);
    return t != NULL && t->type == type;
}

static shell_ast_t *simple_command(shell_parser *p) {
    shell_token_t *t = parser_peek(p);
    if (t->type != SHELL_TOKEN_ATOM)
        return NULL;
    shell_ast_t *ast = new_ast();
    ast->type = SHELL_AST_CMD;
    ast->token = t;
    if (parser_next(p) != NULL)
        ast->right = simple_command(p);
    return ast;
}

static inline shell_ast_t *handle_redirection(shell_parser *p, shell_ast_type_t type, shell_ast_t *simple) {
    if (!expect_token(p, SHELL_TOKEN_ATOM))
        return NULL;
    shell_ast_t *ast = new_ast();
    ast->type = type;
    ast->right = simple;
    ast->token = parser_peek(p);
    parser_next(p);
    return ast;
}

static shell_ast_t *command(shell_parser *p) {
    shell_ast_t *simple = simple_command(p);
    if (!simple)
        return NULL;
    if (match_token(p, SHELL_TOKEN_GREATER)) {
        parser_next(p);
        shell_ast_t *ast = handle_redirection(p, SHELL_AST_REDIR_OUT, simple);
        if (ast == NULL)
            free_ast(simple);
        return ast;
    }
    if (match_token(p, SHELL_TOKEN_LESSER)) {
        parser_next(p);
        shell_ast_t *ast = handle_redirection(p, SHELL_AST_REDIR_IN, simple);
        if (!ast)
            free_ast(simple);
        return ast;
    }
    return simple;
}

static shell_ast_t *_pipe(shell_parser *p) {
    shell_ast_t *left = command(p);
    if (left == NULL)
        return NULL;
    if (match_token(p, SHELL_TOKEN_PIPE)) {
        parser_next(p);
        if (!expect_token(p, SHELL_TOKEN_ATOM)) {
            free_ast(left);
            return NULL;
        }
        shell_ast_t *ast = new_ast();
        ast->type = SHELL_AST_PIPE;
        ast->left = left;
        ast->right = _pipe(p);
        return ast;
    }
    return left;
}

static shell_ast_t *full_command(shell_parser *p) {
    shell_ast_t *left = _pipe(p);
    if (left == NULL)
        return NULL;
    if (match_token(p, SHELL_TOKEN_AMPERSAND)) {
        if (!expect_token(p, SHELL_TOKEN_ATOM) && parser_peek(p) != NULL) {
            free_ast(left);
            return NULL;
        }
        shell_ast_t *ast = new_ast();
        ast->type = SHELL_AST_BACKGROUND;
        ast->left = left;
        parser_next(p);
        ast->right = full_command(p);
        return ast;
    }
    if (match_token(p, SHELL_TOKEN_SEMICOLON)) {
        if (!expect_token(p, SHELL_TOKEN_ATOM) && parser_peek(p) != NULL) {
            free_ast(left);
            return NULL;
        }
        shell_ast_t *ast = new_ast();
        ast->type = SHELL_AST_SEQ;
        ast->left = left;
        parser_next(p);
        ast->right = full_command(p);
        return ast;
    }
    return left;
}

static shell_ast_t* shell_eval_parser(shell_token_array_t tokens) {
    shell_parser parser = {
        .tokens = tokens,
        .cursor = 0
    };
    shell_ast_t *result = full_command(&parser);
    return tokens.count == 0 ? NULL : parser.cursor == tokens.count - 1 ? result : NULL;
}

/* --- Execution --- */

typedef struct shell_command {
    int argc;
    char **argv;
    int input_fd;
    int output_fd;
    int bg;
} shell_command_t;

/* Forward declare internal AST executor */
static int ast_exec(shell_ctx *ctx, shell_ast_t *ast);

static void command_argv_from_ast(shell_command_t *cmd, shell_ast_t *ast) {
    cmd->argc = 0;
    cmd->argv = NULL;

    for (shell_ast_t *n = ast; n != NULL; n = n->right)
        cmd->argc++;

    if (cmd->argc == 0) {
        cmd->argv = xmalloc(sizeof(char *));
        cmd->argv[0] = NULL;
        return;
    }

    cmd->argv = xmalloc(sizeof(char *) * (cmd->argc + 1));
    for (int i = 0; i < cmd->argc; i++) {
        if (ast == NULL || ast->token == NULL) {
            cmd->argv[i] = xmalloc(1);
            cmd->argv[i][0] = '\0';
        } else {
            size_t len = (size_t)ast->token->length;
            cmd->argv[i] = xmalloc(len + 1);
            memcpy(cmd->argv[i], ast->token->begin, len);
            cmd->argv[i][len] = '\0';
        }
        ast = ast ? ast->right : NULL;
    }
    cmd->argv[cmd->argc] = NULL;
}

static void builtin_exit(int argc, char **argv) {
    (void)argc; (void)argv;
    exit(0);
}

static void builtin_cd(int argc, char **argv) {
    if (argc == 1) {
        if (chdir(getenv("HOME")) == -1)
            perror("cd");
    } else if (argc == 2) {
        if (chdir(argv[1]) == -1)
            perror("cd");
    } else
        eprintf("cd: too many arguments\n");
}

static void builtin_pwd(int argc, char **argv) {
    (void)argv;
    if (argc > 1) {
        eprintf("pwd: too many arguments\n");
        return;
    }
    char *cwd = getcwd(NULL, 0);
    printf("%s\n", cwd);
    free(cwd);
}

static shell_builtin_entry_t *builtin_find(shell_ctx *ctx, const char *name) {
    for (shell_builtin_entry_t *b = ctx->builtins; b != NULL; b = b->next) {
        if (strcmp(b->name, name) == 0)
            return b;
    }
    return NULL;
}

static bool is_blacklisted_cmd(shell_ctx *ctx, const char *cmd) {
    if (!ctx->cmd_blacklist) return false;
    for (int i = 0; ctx->cmd_blacklist[i]; i++) {
        if (strcmp(ctx->cmd_blacklist[i], cmd) == 0)
            return true;
    }
    return false;
}

/* Returns true if cmd is in the allowlist.  If allowlist is NULL or empty,
 * all commands are allowed (allow-all mode). */
static bool is_allowlisted_cmd(shell_ctx *ctx, const char *cmd) {
    if (!ctx->cmd_allowlist || !ctx->cmd_allowlist[0])
        return true; /* no allowlist = allow all */
    for (int i = 0; ctx->cmd_allowlist[i]; i++) {
        if (strcmp(ctx->cmd_allowlist[i], cmd) == 0)
            return true;
    }
    return false;
}

static bool is_blacklisted_path(shell_ctx *ctx, const char *path) {
    if (!ctx->path_blacklist) return false;
    for (int i = 0; ctx->path_blacklist[i]; i++) {
        if (strstr(path, ctx->path_blacklist[i]) != NULL)
            return true;
    }
    return false;
}

/* CCCC patch: returns the child's real exit code (non-negative) or a
 * SHELL_ERR_* constant (negative) on error.  Builtins return 0 (their
 * return type is void so a real code is unavailable).
 * Background processes return 0 immediately (no wait). */
static int command_execute(shell_ctx *ctx, shell_command_t *cmd) {
    if (cmd->argc == 0) return 0;
    char *exec_name = cmd->argv[0];

    /* 1. Check Blacklist */
    if (is_blacklisted_cmd(ctx, exec_name)) {
        eprintf("shell: command '%s' is blacklisted\n", exec_name);
        return SHELL_ERR_PERM;
    }

    /* Check all arguments for path restrictions */
    for (int i = 0; i < cmd->argc; i++) {
        if (is_blacklisted_path(ctx, cmd->argv[i])) {
            eprintf("shell: argument '%s' contains blacklisted path\n", cmd->argv[i]);
            return SHELL_ERR_PERM;
        }
    }

    /* 2. Check Builtins (User defined builtins override everything; always allowed) */
    shell_builtin_entry_t *builtin = builtin_find(ctx, exec_name);
    if (builtin) {
        builtin->func(cmd->argc, cmd->argv);
        return 0; /* builtins are void; treat as success */
    }

    /* 3. Allowlist check for external commands */
    if (!is_allowlisted_cmd(ctx, exec_name)) {
        eprintf("shell: command '%s' not in allowlist\n", exec_name);
        return SHELL_ERR_PERM;
    }

    /* 4. Check Builtin-Only Mode */
    if (ctx->builtin_only) {
        eprintf("shell: command '%s' not found (builtin-only mode)\n", exec_name);
        return SHELL_ERR_PERM;
    }

    /* 5. External Execution */
    pid_t pid = fork();
    if (pid == -1) {
        _perror("fork");
        return SHELL_ERR_FORK;
    }

    if (pid == 0) {
        /* Child */
        if (cmd->bg) {
            printf("\n[BACKGROUND] started backgroundjob: %s\n", exec_name);
            setpgid(0, 0);
            int fd = xopen("/dev/null", O_RDONLY, 0);
            dup2(fd, STDIN_FILENO);
            close(fd);
        }

        if (cmd->input_fd != -1) {
            dup2(cmd->input_fd, STDIN_FILENO);
            close(cmd->input_fd);
        }
        if (cmd->output_fd != -1) {
            dup2(cmd->output_fd, STDOUT_FILENO);
            close(cmd->output_fd);
        }

        if (execvp(exec_name, cmd->argv) == -1) {
            die("execvp");
        }
    }

    /* Parent */
    if (!cmd->bg) {
        int status;
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        if (WIFEXITED(status))
            return WEXITSTATUS(status);
        if (WIFSIGNALED(status))
            return 128 + WTERMSIG(status); /* conventional signal exit code */
        return SHELL_ERR_GENERIC;
    }

    /* Background: register reaper and return immediately */
    struct sigaction sa;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART | SA_NOCLDSTOP;
    sigaction(SIGCHLD, &sa, NULL);
    return 0;
}

static void command_destroy(shell_command_t *cmd) {
    if (cmd->argv) {
        for (int i = 0; i < cmd->argc; i++)
            free(cmd->argv[i]);
        free(cmd->argv);
    }
}

/* CCCC patch: returns exit code from command_execute */
static int eval_commandtail(shell_ctx *ctx, shell_ast_t *ast) {
    shell_command_t cmd;
    command_argv_from_ast(&cmd, ast);
    cmd.input_fd = ctx->input_fd;
    cmd.output_fd = ctx->output_fd;
    cmd.bg = ctx->bg;
    int rc = command_execute(ctx, &cmd);
    command_destroy(&cmd);
    return rc;
}

/* CCCC patch: returns exit code of last command (shell `;` semantics). */
static int eval_sequence(shell_ctx *ctx, shell_ast_t *ast) {
    if (ast->type == SHELL_AST_BACKGROUND)
        ctx->bg = 1;
    ast_exec(ctx, ast->left);   /* left runs; `;` ignores its exit code */
    int rc = ast_exec(ctx, ast->right);
    if (ast->type == SHELL_AST_BACKGROUND)
        ctx->bg = 0;
    return rc;
}

/* CCCC patch: returns exit code of the redirected command. */
static int eval_redirection(shell_ctx *ctx, shell_ast_t *ast) {
    int fd;
    unsigned char c = ast->token->begin[ast->token->length];
    ast->token->begin[ast->token->length] = '\0';
    const char *filename = (const char*)ast->token->begin;

    /* Security Check for File Access */
    if (is_blacklisted_path(ctx, filename)) {
        eprintf("shell: file access '%s' is blacklisted\n", filename);
        ast->token->begin[ast->token->length] = c;
        return SHELL_ERR_PERM;
    }

    if (ast->type == SHELL_AST_REDIR_IN) {
        fd = open(filename, O_RDONLY);
        if (fd == -1) {
            _perror("open");
            ast->token->begin[ast->token->length] = c;
            return SHELL_ERR_GENERIC;
        }
        ctx->input_fd = fd;
    } else {
        fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC,
                  S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
        if (fd == -1) {
            _perror("open");
            ast->token->begin[ast->token->length] = c;
            return SHELL_ERR_GENERIC;
        }
        ctx->output_fd = fd;
    }
    ast->token->begin[ast->token->length] = c;

    int rc = ast_exec(ctx, ast->right);
    close(fd);
    ctx->input_fd = -1;
    ctx->output_fd = -1;
    return rc;
}

/* CCCC patch: returns the exit code of the last command in the pipeline. */
static int eval_pipeline(shell_ctx *ctx, shell_ast_t *ast) {
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        _perror("pipe");
        return SHELL_ERR_PIPE;
    }

    ctx->input_fd = -1;
    ctx->output_fd = pipefd[1];
    ast_exec(ctx, ast->left);
    close(pipefd[1]);

    ast = ast->right;

    ctx->input_fd = pipefd[0];

    while (ast->type == SHELL_AST_PIPE) {
        if (pipe(pipefd) == -1) {
            _perror("pipe");
            close(ctx->input_fd);
            return SHELL_ERR_PIPE;
        }
        ctx->output_fd = pipefd[1];
        ast_exec(ctx, ast->left);
        close(pipefd[1]);
        close(ctx->input_fd);
        ctx->input_fd = pipefd[0];
        ast = ast->right;
    }
    ctx->output_fd = -1;
    ctx->input_fd = pipefd[0];
    int rc = ast_exec(ctx, ast); /* exit code of last command in pipeline */
    close(pipefd[0]);
    return rc;
}

/* CCCC patch: propagates real exit codes from all eval_* functions. */
static int ast_exec(shell_ctx *ctx, shell_ast_t *ast) {
    if (!ast) return 0;
    switch (ast->type) {
        case SHELL_AST_PIPE:
            return eval_pipeline(ctx, ast);
        case SHELL_AST_REDIR_IN:
        case SHELL_AST_REDIR_OUT:
            return eval_redirection(ctx, ast);
        case SHELL_AST_SEQ:
        case SHELL_AST_BACKGROUND:
            return eval_sequence(ctx, ast);
        case SHELL_AST_CMD:
            return eval_commandtail(ctx, ast);
        default:
            break;
    }
    return 0;
}

#if defined(_WIN32) || defined(_WIN64)
/* Windows-specific implementation (Abbreviated update for context passing) */

/* ... (Win32 reader threads and helpers same as before) ... */

typedef struct {
    HANDLE handle;
    shell_stream_cb_t callback;
    void *userdata;
    char **buffer;
    size_t *length;
    size_t *capacity;
    int use_callback;
} win_reader_args_t;

static DWORD WINAPI win_reader_thread(LPVOID arg) {
    /* Same implementation as before */
    win_reader_args_t *args = (win_reader_args_t*)arg;
    char buffer[4096];
    DWORD bytes_read = 0;
    while (ReadFile(args->handle, buffer, sizeof(buffer), &bytes_read, NULL) && bytes_read > 0)
        if (args->use_callback) {
            args->callback(buffer, bytes_read, args->userdata);
        } else {
            if (*args->capacity - *args->length < bytes_read) {
                size_t new_capacity = (*args->capacity == 0) ? 4096 : *args->capacity * 2;
                while (new_capacity - *args->length < bytes_read) new_capacity *= 2;
                *args->buffer = xrealloc(*args->buffer, new_capacity + 1);
                *args->capacity = new_capacity;
            }
            memcpy(*args->buffer + *args->length, buffer, bytes_read);
            *args->length += bytes_read;
        }
    return 0;
}

static int create_pipe_pair(HANDLE *read_handle, HANDLE *write_handle, int inherit_read) {
    SECURITY_ATTRIBUTES sa = { .nLength = sizeof(SECURITY_ATTRIBUTES), .bInheritHandle = TRUE, .lpSecurityDescriptor = NULL };
    if (!CreatePipe(read_handle, write_handle, &sa, 0)) return SHELL_ERR_PIPE;
    HANDLE non_inherit = inherit_read ? *write_handle : *read_handle;
    SetHandleInformation(non_inherit, HANDLE_FLAG_INHERIT, 0);
    return SHELL_OK;
}

static void cleanup_handles(HANDLE *handles, size_t count) {
    for (size_t i = 0; i < count; i++)
        if (handles[i] != NULL && handles[i] != INVALID_HANDLE_VALUE) {
            CloseHandle(handles[i]);
            handles[i] = NULL;
        }
}

static int win_shell_with_io(const char *cmd, shell_io *io, shell_ctx *ctx) {
    (void)ctx; /* Warning: Context logic not fully applied to Win32 path in this simplified port */

    if (!cmd) return SHELL_ERR_GENERIC;

    HANDLE pipes[6] = {0};
    HANDLE *stdin_r = &pipes[0], *stdin_w = &pipes[1];
    HANDLE *stdout_r = &pipes[2], *stdout_w = &pipes[3];
    HANDLE *stderr_r = &pipes[4], *stderr_w = &pipes[5];

    if (create_pipe_pair(stdout_r, stdout_w, 0) != SHELL_OK ||
        create_pipe_pair(stderr_r, stderr_w, 0) != SHELL_OK ||
        create_pipe_pair(stdin_r, stdin_w, 0) != SHELL_OK) {
        cleanup_handles(pipes, 6);
        return SHELL_ERR_PIPE;
    }

    size_t cmd_len = strlen(cmd);
    char *cmdline = xmalloc(cmd_len + 1);
    strcpy(cmdline, cmd);

    STARTUPINFOA si = {
        .cb = sizeof(STARTUPINFOA),
        .dwFlags = STARTF_USESTDHANDLES,
        .hStdInput = *stdin_r,
        .hStdOutput = *stdout_w,
        .hStdError = *stderr_w
    };

    PROCESS_INFORMATION pi = {0};
    BOOL success = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL, NULL, &si, &pi);
    free(cmdline);

    if (!success) {
        cleanup_handles(pipes, 6);
        return SHELL_ERR_FORK;
    }

    CloseHandle(*stdout_w); *stdout_w = NULL;
    CloseHandle(*stderr_w); *stderr_w = NULL;
    CloseHandle(*stdin_r); *stdin_r = NULL;

    char *outbuf = NULL, *errbuf = NULL;
    size_t out_len = 0, out_cap = 0, err_len = 0, err_cap = 0;

    win_reader_args_t out_args = { .handle = *stdout_r, .callback = io ? io->out_cb : NULL, .userdata = io ? io->userdata : NULL, .buffer = &outbuf, .length = &out_len, .capacity = &out_cap, .use_callback = (io && io->out_cb) };
    win_reader_args_t err_args = { .handle = *stderr_r, .callback = io ? io->err_cb : NULL, .userdata = io ? io->userdata : NULL, .buffer = &errbuf, .length = &err_len, .capacity = &err_cap, .use_callback = (io && io->err_cb) };

    HANDLE threads[2];
    threads[0] = CreateThread(NULL, 0, win_reader_thread, &out_args, 0, NULL);
    threads[1] = CreateThread(NULL, 0, win_reader_thread, &err_args, 0, NULL);

    if (io && io->in && io->in_len > 0) {
        DWORD written;
        WriteFile(*stdin_w, io->in, (DWORD)io->in_len, &written, NULL);
    }
    CloseHandle(*stdin_w); *stdin_w = NULL;

    WaitForSingleObject(pi.hProcess, INFINITE);
    WaitForMultipleObjects(2, threads, TRUE, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    if (io) {
        if (!out_args.use_callback) { io->out = outbuf ? (outbuf[out_len] = '\0', outbuf) : (outbuf = xmalloc(1), outbuf[0] = '\0', outbuf); io->out_len = out_len; }
        else { io->out = NULL; io->out_len = 0; }
        if (!err_args.use_callback) { io->err = errbuf ? (errbuf[err_len] = '\0', errbuf) : (errbuf = xmalloc(1), errbuf[0] = '\0', errbuf); io->err_len = err_len; }
        else { io->err = NULL; io->err_len = 0; }
    }

    CloseHandle(pi.hProcess); CloseHandle(pi.hThread); cleanup_handles(pipes, 6);
    CloseHandle(threads[0]); CloseHandle(threads[1]);

    return (int)exit_code;
}

#else
/* POSIX implementation */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags == -1 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_pipe_pair(int pipe_fds[2]) {
    if (pipe_fds[0] != -1) { close(pipe_fds[0]); pipe_fds[0] = -1; }
    if (pipe_fds[1] != -1) { close(pipe_fds[1]); pipe_fds[1] = -1; }
}

static int write_input_to_child(int write_fd, const char *input, size_t input_len) {
    size_t written = 0;
    while (written < input_len) {
        ssize_t result = write(write_fd, input + written, input_len - written);
        if (result == -1) {
            if (errno == EAGAIN || errno == EINTR) continue;
            return -1;
        }
        written += result;
    }
    return 0;
}

static int ensure_buffer_capacity(char **buffer, size_t *capacity, size_t current_len, size_t needed) {
    if (*capacity - current_len >= needed) return 0;
    size_t new_capacity = (*capacity == 0) ? 4096 : *capacity * 2;
    while (new_capacity - current_len < needed) new_capacity *= 2;
    *buffer = xrealloc(*buffer, new_capacity + 1);
    *capacity = new_capacity;
    return 0;
}

static int posix_shell_with_io(const char *cmd, shell_io *io, shell_ctx *ctx) {
    int pipes[6] = {-1, -1, -1, -1, -1, -1}; /* in[2], out[2], err[2] */
    int *inpipe = &pipes[0], *outpipe = &pipes[2], *errpipe = &pipes[4];

    if (pipe(inpipe) == -1 || pipe(outpipe) == -1 || pipe(errpipe) == -1) {
        for (int i = 0; i < 6; i += 2) close_pipe_pair(&pipes[i]);
        return SHELL_ERR_PIPE;
    }

    pid_t pid = fork();
    if (pid == -1) {
        for (int i = 0; i < 6; i += 2) close_pipe_pair(&pipes[i]);
        return SHELL_ERR_FORK;
    }

    if (pid == 0) {
        /* Child process */
        close(inpipe[1]); close(outpipe[0]); close(errpipe[0]);

        if (dup2(inpipe[0], STDIN_FILENO) == -1 ||
            dup2(outpipe[1], STDOUT_FILENO) == -1 ||
            dup2(errpipe[1], STDERR_FILENO) == -1) {
            die("dup2");
        }
        close(inpipe[0]); close(outpipe[1]); close(errpipe[1]);

        /* Tokenize and Parse */
        char *cmd_copy = strdup(cmd);
        if (!cmd_copy) die("strdup");

        shell_lexer_t lexer;
        shell_lexer(&lexer, (unsigned char*)cmd_copy);
        shell_token_array_t tokens = shell_parse(&lexer);

        if (!tokens.data || lexer.error) {
            if (tokens.data) free(tokens.data);
            free(cmd_copy);
            _exit(SHELL_ERR_TOKENIZE);
        }

        shell_ast_t *ast = shell_eval_parser(tokens);
        if (!ast) {
            free(tokens.data);
            free(cmd_copy);
            _exit(SHELL_ERR_EVAL);
        }

        /* EXECUTE with Context; CCCC patch: propagate real exit code */
        int result = ast_exec(ctx, ast);

        free(tokens.data);
        free_ast(ast);
        free(cmd_copy);
        _exit(result >= 0 ? result : 1);
    }

    /* Parent process */
    close(inpipe[0]); close(outpipe[1]); close(errpipe[1]);

    set_nonblocking(inpipe[1]);
    set_nonblocking(outpipe[0]);
    set_nonblocking(errpipe[0]);

    if (io && io->in && io->in_len > 0)
        write_input_to_child(inpipe[1], io->in, io->in_len);
    close(inpipe[1]);

    char *outbuf = NULL, *errbuf = NULL;
    size_t out_len = 0, out_cap = 0, err_len = 0, err_cap = 0;
    int use_out_cb = io && io->out_cb != NULL;
    int use_err_cb = io && io->err_cb != NULL;

    struct pollfd fds[2] = { {.fd = outpipe[0], .events = POLLIN | POLLHUP}, {.fd = errpipe[0], .events = POLLIN | POLLHUP} };
    int active_fds = 2;

    while (active_fds > 0) {
        int poll_result = poll(fds, 2, -1);
        if (poll_result == -1) {
            if (errno == EINTR) continue;
            break;
        }

        for (int i = 0; i < 2; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP)) || fds[i].fd == -1) continue;
            char buffer[4096];
            ssize_t bytes_read;
            while ((bytes_read = read(fds[i].fd, buffer, sizeof(buffer))) > 0) {
                if (i == 0) {
                    if (use_out_cb) io->out_cb(buffer, bytes_read, io->userdata);
                    else { ensure_buffer_capacity(&outbuf, &out_cap, out_len, bytes_read); memcpy(outbuf + out_len, buffer, bytes_read); out_len += bytes_read; }
                } else {
                    if (use_err_cb) io->err_cb(buffer, bytes_read, io->userdata);
                    else { ensure_buffer_capacity(&errbuf, &err_cap, err_len, bytes_read); memcpy(errbuf + err_len, buffer, bytes_read); err_len += bytes_read; }
                }
            }
            if (bytes_read == 0 || (bytes_read == -1 && errno != EAGAIN && errno != EINTR)) { fds[i].fd = -1; active_fds--; }
        }
    }

    if (io) {
        if (!use_out_cb) { io->out = outbuf ? (outbuf[out_len] = '\0', outbuf) : (outbuf = xmalloc(1), outbuf[0] = '\0', outbuf); io->out_len = out_len; }
        else { io->out = NULL; io->out_len = 0; }
        if (!use_err_cb) { io->err = errbuf ? (errbuf[err_len] = '\0', errbuf) : (errbuf = xmalloc(1), errbuf[0] = '\0', errbuf); io->err_len = err_len; }
        else { io->err = NULL; io->err_len = 0; }
    }

    close(outpipe[0]); close(errpipe[0]);
    int status;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}

static int posix_shell_inline(const char *cmd, shell_ctx *ctx) {
    char *cmd_copy = strdup(cmd);
    if (!cmd_copy) {
        printf("error: out of memory\n");
        return SHELL_ERR_GENERIC;
    }

    shell_lexer_t lexer;
    shell_lexer(&lexer, (unsigned char*)cmd_copy);

    shell_token_array_t tokens = shell_parse(&lexer);
    if (!tokens.data || lexer.error) {
        if (lexer.error) {
            printf("error: '%s'\n", lexer.error);
        }
        if (tokens.data) free(tokens.data);
        free(cmd_copy);
        return -1;
    }

    shell_ast_t *ast = shell_eval_parser(tokens);
    if (!ast) {
        free(tokens.data);
        free(cmd_copy);
        return -1;
    }

    /* CCCC patch: ast_exec now returns the real exit code */
    int result = ast_exec(ctx, ast);
    free(tokens.data);
    free_ast(ast);
    free(cmd_copy);
    return result;
}

#endif /* _WIN32 */

int shell_with_ctx(const char *cmd, shell_io *io, shell_ctx *ctx) {
    if (!cmd) return SHELL_ERR_GENERIC;

#if defined(_WIN32) || defined(_WIN64)
    return win_shell_with_io(cmd, io, ctx);
#else
    return io ? posix_shell_with_io(cmd, io, ctx) : posix_shell_inline(cmd, ctx);
#endif
}

int shell(const char *cmd, shell_io *io) {
    if (g_default_ctx) {
        return shell_with_ctx(cmd, io, g_default_ctx);
    } else {
        /* Temporary context */
        shell_ctx *tmp = shell_ctx_create();
        int res = shell_with_ctx(cmd, io, tmp);
        shell_ctx_destroy(tmp);
        return res;
    }
}

int shell_fmt(shell_io *io, const char *fmt, ...) {
    if (!fmt) return SHELL_ERR_GENERIC;
    va_list args;
    va_start(args, fmt);
    char *cmd = NULL;
    if (vasprintf(&cmd, fmt, args) == -1) {
        va_end(args);
        return SHELL_ERR_GENERIC;
    }
    va_end(args);
    int result = shell(cmd, io);
    free(cmd);
    return result;
}
#endif // PAUL_SHELL_IMPLEMENTATION
