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

// build_shell.c — implementation TU for the vendored paul_shell.h.
//
// paul_shell.h is a single-header library; defining PAUL_SHELL_IMPLEMENTATION
// here pulls in the implementation in a dedicated translation unit, keeping
// its file-static helpers (die, xmalloc, eprintf, peek, advance, …) isolated
// from build.c.

#include "./internal.h"
#include <stdarg.h>
#include <sys/wait.h>
#include "paul_shell.h"

/* --- Globals --- */
static shell_ctx *g_default_ctx = NULL;

/* --- Context Management --- */

static int builtin_exit(int argc, char **argv);
static int builtin_cd(int argc, char **argv);
static int builtin_pwd(int argc, char **argv);

shell_ctx *shell_ctx_create(void) {
    shell_ctx *ctx = (shell_ctx *)malloc(sizeof(shell_ctx));
    if (!ctx)
        return NULL;
    memset(ctx, 0, sizeof(shell_ctx));

    ctx->input_fd  = -1;
    ctx->output_fd = -1;

    /* Add default builtins */
    shell_ctx_add_builtin(ctx, "exit", builtin_exit);
    shell_ctx_add_builtin(ctx, "cd", builtin_cd);
    shell_ctx_add_builtin(ctx, "pwd", builtin_pwd);

    return ctx;
}

void shell_ctx_destroy(shell_ctx *ctx) {
    if (!ctx)
        return;

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

void shell_ctx_add_builtin(shell_ctx *ctx, const char *name,
                           shell_builtin_func_t func) {
    shell_builtin_entry_t *entry =
        (shell_builtin_entry_t *)malloc(sizeof(shell_builtin_entry_t));
    if (!entry)
        return; // OOM
    entry->name = strdup(name);
    entry->func = func;

    /* Prepend to list to ensure overriding behavior (LIFO check) */
    entry->next   = ctx->builtins;
    ctx->builtins = entry;
}

static void _append_str_array(char ***arr, const char *str) {
    int count = 0;
    if (*arr) {
        while ((*arr)[count])
            count++;
    }

    char **new_arr = (char **)realloc(*arr, sizeof(char *) * (count + 2));
    if (!new_arr)
        return; // OOM

    new_arr[count]     = strdup(str);
    new_arr[count + 1] = NULL;
    *arr               = new_arr;
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
    SHELL_TOKEN_PIPE      = '|',
    SHELL_TOKEN_AMPERSAND = '&',
    SHELL_TOKEN_GREATER   = '>',
    SHELL_TOKEN_LESSER    = '<',
    SHELL_TOKEN_SEMICOLON = ';',
    /* Two-character operators; values outside the ASCII range used by the
     * single-character tokens above so they can never collide. */
    SHELL_TOKEN_AND = 256, /* && */
    SHELL_TOKEN_OR  = 257, /* || */
} shell_token_type;

typedef struct shell_token {
    shell_token_type type;
    unsigned char   *begin;
    int              length;
    /* True when `begin` points at a heap buffer owned by this token (quote
     * removal / backslash / $VAR decoding can shrink or grow a word relative
     * to its source slice, so decoded ATOM tokens can no longer point into
     * cmd_copy). False for tokens that still alias the source buffer
     * (operators, and any ATOM whose raw slice happens to equal its decoded
     * form is still built through the owned path for simplicity). */
    bool owned;
} shell_token_t;

typedef struct shell_lexer {
    unsigned char *begin;
    struct {
        unsigned char *ptr;
        wchar_t        ch;
        int            ch_length;
    } cursor;
    char          *error;
    unsigned char *input_begin;
    size_t         error_pos;
    /* #1311: the sandbox context substituted commands run under, so
     * `$(cmd)` inherits the same allowlist/blacklist as the command it
     * appears in. NULL for a lexer that never calls
     * shell_lexer_with_ctx() -- command substitution is simply unavailable
     * in that case (see expand_cmd_subst()). */
    struct shell_ctx *ctx;
} shell_lexer_t;

typedef enum shell_ast_type {
    SHELL_AST_CMD,
    SHELL_AST_BACKGROUND,
    SHELL_AST_SEQ,
    SHELL_AST_REDIR_IN,
    SHELL_AST_REDIR_OUT,
    SHELL_AST_PIPE,
    SHELL_AST_AND,
    SHELL_AST_OR
} shell_ast_type_t;

typedef struct shell_ast {
    shell_ast_type_t  type;
    shell_token_t    *token;
    struct shell_ast *left, *right;
} shell_ast_t;

typedef struct shell_token_array {
    shell_token_t *data;
    size_t         count;
    size_t         capacity;
} shell_token_array_t;

typedef struct shell_parser {
    shell_token_array_t tokens;
    shell_token_t      *current;
    size_t              cursor;
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

/* die() is only called from forked child processes (shell_with_io forks before
 * invoking the shell); xmalloc/xrealloc failures here exit the child, not the
 * parent cccc process. */
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

static int utf8read(const unsigned char *c, wchar_t *out) {
    wchar_t u = *c, l = 1;
    if ((u & 0xC0) == 0xC0) {
        int a = (u & 0x20)
                    ? ((u & 0x10) ? ((u & 0x08) ? ((u & 0x04) ? 6 : 5) : 4) : 3)
                    : 2;
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

static void shell_lexer(shell_lexer_t *l, unsigned char *line) {
    l->begin            = line;
    l->cursor.ptr       = line;
    l->cursor.ch_length = utf8read(line, &l->cursor.ch);
    l->error            = NULL;
    l->input_begin      = line;
    l->error_pos        = (size_t)-1;
    l->ctx              = NULL;
}

/* #1311: like shell_lexer(), but records the sandbox context so
 * expand_cmd_subst() can run a substituted `$(...)` command under the same
 * allowlist/blacklist as the command it appears in. */
static void shell_lexer_with_ctx(shell_lexer_t *l, unsigned char *line,
                                 struct shell_ctx *ctx) {
    shell_lexer(l, line);
    l->ctx = ctx;
}

/* #1311: bounds `$(echo $(echo $(...)))`-style nesting. Each level forks a
 * fresh process (see expand_cmd_subst()), and this counter is inherited
 * across that fork -- incrementing it before the inner shell_with_ctx()
 * call and forking means the child that lexes the inner command already
 * sees the incremented value, so depth tracking needs no explicit
 * threading through shell_ctx or the lexer. */
#define SHELL_SUBST_MAX_DEPTH 8
static int g_shell_subst_depth = 0;

static inline wchar_t peek(shell_lexer_t *l) {
    return l->cursor.ch;
}

static inline bool is_eof(shell_lexer_t *l) {
    return peek(l) == '\0';
}

static inline void update(shell_lexer_t *l) {
    l->begin            = l->cursor.ptr;
    l->cursor.ch_length = utf8read(l->cursor.ptr, &l->cursor.ch);
    l->error            = NULL;
}

static inline wchar_t advance(shell_lexer_t *l) {
    l->cursor.ptr       += utf8read(l->cursor.ptr, NULL);
    l->cursor.ch_length  = utf8read(l->cursor.ptr, &l->cursor.ch);
    return l->cursor.ch;
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
    return (shell_token_t){.type   = type,
                           .begin  = l->begin,
                           .length = (int)(l->cursor.ptr - l->begin)};
}

/* --- Word buffer: growable byte buffer used to build a decoded ATOM's
 * content while the lexer walks quotes/escapes/expansions. --- */
typedef struct word_buf {
    unsigned char *buf;
    size_t         len;
    size_t         cap;
} word_buf_t;

static void word_buf_init(word_buf_t *b) {
    b->cap = 32;
    b->buf = xmalloc(b->cap);
    b->len = 0;
}

static void word_buf_push(word_buf_t *b, unsigned char c) {
    if (b->len + 1 > b->cap) {
        b->cap *= 2;
        b->buf  = xrealloc(b->buf, b->cap);
    }
    b->buf[b->len++] = c;
}

static void word_buf_push_str(word_buf_t *b, const char *s) {
    while (*s)
        word_buf_push(b, (unsigned char)*s++);
}

/* Copy the raw bytes of the utf8 codepoint currently under the cursor into
 * `out`, then advance past it. */
static void word_buf_push_current(shell_lexer_t *l, word_buf_t *out) {
    int clen = l->cursor.ch_length;
    for (int i = 0; i < clen; i++)
        word_buf_push(out, l->cursor.ptr[i]);
    advance(l);
}

static inline bool is_name_start_char(wchar_t c) {
    return c == '_' || (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z');
}

static inline bool is_name_char(wchar_t c) {
    return is_name_start_char(c) || (c >= '0' && c <= '9');
}

/* #1311: `$(cmd)` command substitution. Assumes the '$' has already been
 * consumed and the cursor is positioned on '('. Captures the raw text up to
 * the matching ')' -- honoring nested parens and quotes, so a `)` inside
 * e.g. a quoted argument to the inner command doesn't terminate the
 * substitution early -- runs it through the same shell_with_ctx() capture
 * path RunCustom itself uses (so the substituted command inherits the
 * calling command's allowlist/blacklist, and no separate execution path
 * needs maintaining), strips trailing newlines from its stdout the way
 * every other shell's `$(...)` does, and appends the result to `out` as a
 * single literal chunk -- like $VAR, never re-split or globbed. Returns
 * false and sets l->error on an unterminated `$(`/quote or once
 * SHELL_SUBST_MAX_DEPTH is exceeded.
 *
 * Substitution happens here, at lex time -- eagerly, before the surrounding
 * command's `&&`/`||`/`;` structure is even parsed. Unlike a real shell,
 * `false && $(cmd)` still runs `cmd`. This matches how $VAR already
 * expands unconditionally and keeps the change small; documented as a
 * known limitation in man/BUILD_MODE.md. */
static bool expand_cmd_subst(shell_lexer_t *l, word_buf_t *out) {
    advance(l); /* consume '(' */

    if (g_shell_subst_depth >= SHELL_SUBST_MAX_DEPTH) {
        l->error     = "command substitution nested too deeply";
        l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
        return false;
    }

    word_buf_t sub;
    word_buf_init(&sub);
    int paren_depth = 1;
    for (;;) {
        if (is_eof(l)) {
            l->error     = "unterminated $(";
            l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
            free(sub.buf);
            return false;
        }
        wchar_t c = peek(l);
        if (c == '(') {
            paren_depth++;
            word_buf_push_current(l, &sub);
            continue;
        }
        if (c == ')') {
            paren_depth--;
            if (paren_depth == 0) {
                advance(l); /* consume closing ')' */
                break;
            }
            word_buf_push_current(l, &sub);
            continue;
        }
        if (c == '\'' || c == '"') {
            wchar_t quote = c;
            word_buf_push_current(l, &sub);             /* opening quote */
            while (!is_eof(l) && peek(l) != quote) {
                if (peek(l) == '\\' && quote == '"') {
                    word_buf_push_current(l, &sub);     /* backslash */
                    if (!is_eof(l))
                        word_buf_push_current(l, &sub); /* escaped char */
                    continue;
                }
                word_buf_push_current(l, &sub);
            }
            if (is_eof(l)) {
                l->error     = "unterminated quote in $(...)";
                l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
                free(sub.buf);
                return false;
            }
            word_buf_push_current(l, &sub); /* closing quote */
            continue;
        }
        word_buf_push_current(l, &sub);
    }
    word_buf_push(&sub, '\0');

    if (!l->ctx) {
        /* A lexer built via plain shell_lexer() (no sandbox context) has
         * nothing to run the substituted command under. */
        l->error     = "command substitution is not supported here";
        l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
        free(sub.buf);
        return false;
    }

    shell_io io = {0};
    g_shell_subst_depth++;
    shell_with_ctx((const char *)sub.buf, &io, l->ctx);
    g_shell_subst_depth--;
    free(sub.buf);

    if (io.out) {
        size_t n = io.out_len;
        while (n > 0 && io.out[n - 1] == '\n')
            n--;
        for (size_t i = 0; i < n; i++)
            word_buf_push(out, (unsigned char)io.out[i]);
        free(io.out);
    }
    /* $(...)'s exit status is discarded, matching every other shell --
     * only stdout is substituted. */
    free(io.err);
    return true;
}

/* `$NAME` / `${NAME}` / `$(cmd)` expansion (unquoted and double-quoted
 * words only). Assumes the cursor is currently on the '$'. Appends the
 * expanded value (or a literal '$' if it isn't followed by a name or '(')
 * to `out`. Returns false and sets l->error on a malformed `${...}`/`$(`.
 * Expansion is a single literal chunk: the result is never re-split or
 * globbed. */
static bool expand_var(shell_lexer_t *l, word_buf_t *out) {
    advance(l); /* consume '$' */
    wchar_t c = peek(l);
    if (c == '(')
        return expand_cmd_subst(l, out);
    bool braced = (c == '{');
    if (braced)
        advance(l);
    if (!braced && !is_name_start_char(c)) {
        /* '$' not followed by a name: literal. */
        word_buf_push(out, '$');
        return true;
    }
    unsigned char *name_start = l->cursor.ptr;
    while (!is_eof(l) && is_name_char(peek(l)))
        advance(l);
    size_t namelen = (size_t)(l->cursor.ptr - name_start);
    if (braced) {
        if (peek(l) != '}') {
            l->error     = "unterminated ${";
            l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
            return false;
        }
        advance(l); /* consume '}' */
    }
    char namebuf[256];
    if (namelen >= sizeof(namebuf))
        namelen = sizeof(namebuf) - 1;
    memcpy(namebuf, name_start, namelen);
    namebuf[namelen] = '\0';
    const char *val  = getenv(namebuf);
    if (val)
        word_buf_push_str(out, val);
    return true;
}

/* Reads one shell word, performing quote removal, backslash escaping and
 * $VAR/${VAR} expansion as it goes (see the module-level RunCustom grammar
 * notes in man/BUILD_MODE.md). Unlike the old raw-slice reader, the decoded
 * content can differ in length from the source text, so the returned token
 * always owns a heap buffer (`owned = true`). */
static shell_token_t read_word(shell_lexer_t *l) {
    word_buf_t out;
    word_buf_init(&out);

    for (;;) {
        if (is_eof(l))
            break;
        wchar_t wc = peek(l);
        switch (wc) {
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
                goto DONE;
            case '\'': {
                advance(l); /* consume opening quote */
                for (;;) {
                    if (is_eof(l)) {
                        l->error     = "unterminated quote";
                        l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
                        free(out.buf);
                        return new_token(l, SHELL_TOKEN_ERROR);
                    }
                    if (peek(l) == '\'') {
                        advance(l);
                        break;
                    }
                    word_buf_push_current(l, &out);
                }
                break;
            }
            case '"': {
                advance(l); /* consume opening quote */
                for (;;) {
                    if (is_eof(l)) {
                        l->error     = "unterminated quote";
                        l->error_pos = (size_t)(l->cursor.ptr - l->input_begin);
                        free(out.buf);
                        return new_token(l, SHELL_TOKEN_ERROR);
                    }
                    wchar_t c = peek(l);
                    if (c == '"') {
                        advance(l);
                        break;
                    }
                    if (c == '\\') {
                        advance(l);
                        if (is_eof(l)) {
                            l->error = "unterminated quote";
                            l->error_pos =
                                (size_t)(l->cursor.ptr - l->input_begin);
                            free(out.buf);
                            return new_token(l, SHELL_TOKEN_ERROR);
                        }
                        wchar_t nc = peek(l);
                        /* Inside double quotes, backslash is only special
                         * before " \ $ or a newline (POSIX); otherwise the
                         * backslash itself is kept literal. */
                        if (nc == '"' || nc == '\\' || nc == '$' ||
                            nc == '\n') {
                            if (nc == '\n')
                                advance(l); /* line continuation: drop both */
                            else
                                word_buf_push_current(l, &out);
                        } else {
                            word_buf_push(&out, '\\');
                            word_buf_push_current(l, &out);
                        }
                        continue;
                    }
                    if (c == '$') {
                        if (!expand_var(l, &out)) {
                            free(out.buf);
                            return new_token(l, SHELL_TOKEN_ERROR);
                        }
                        continue;
                    }
                    word_buf_push_current(l, &out);
                }
                break;
            }
            case '\\': {
                advance(l); /* consume backslash */
                if (is_eof(l)) {
                    word_buf_push(&out, '\\');
                    goto DONE;
                }
                if (peek(l) == '\n')
                    advance(l); /* line continuation: drop both */
                else
                    word_buf_push_current(l, &out);
                break;
            }
            case '$': {
                if (!expand_var(l, &out)) {
                    free(out.buf);
                    return new_token(l, SHELL_TOKEN_ERROR);
                }
                break;
            }
            default:
                word_buf_push_current(l, &out);
                break;
        }
    }
DONE: {
    int length = (int)out.len;
    word_buf_push(&out, '\0');
    return (shell_token_t){.type   = SHELL_TOKEN_ATOM,
                           .begin  = out.buf,
                           .length = length,
                           .owned  = true};
}
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
        case '&':
            advance(l);
            if (peek(l) == '&') {
                advance(l);
                return new_token(l, SHELL_TOKEN_AND);
            }
            return new_token(l, SHELL_TOKEN_AMPERSAND);
        case '|':
            advance(l);
            if (peek(l) == '|') {
                advance(l);
                return new_token(l, SHELL_TOKEN_OR);
            }
            return new_token(l, SHELL_TOKEN_PIPE);
        case '<':
        case '>':
        case ';':
            advance(l);
            return new_token(l, (shell_token_type)wc);
        default:
            return read_word(l);
    }
    return new_token(l, SHELL_TOKEN_ERROR);
}

static void shell_token_array_init(shell_token_array_t *arr) {
    arr->data     = NULL;
    arr->count    = 0;
    arr->capacity = 0;
}

static bool shell_token_array_append(shell_token_array_t *arr,
                                     shell_token_t        token) {
    if (arr->count + 1 > arr->capacity) {
        size_t new_capacity = arr->capacity == 0 ? 8 : arr->capacity * 2;
        shell_token_t *new_data =
            xrealloc(arr->data, sizeof(shell_token_t) * new_capacity);
        if (!new_data)
            return false;
        arr->data     = new_data;
        arr->capacity = new_capacity;
    }
    arr->data[arr->count++] = token;
    return true;
}

/* Frees any decoded (owned) token buffers plus the array itself. Word
 * decoding (quote removal / escapes / $VAR expansion) means ATOM tokens can
 * now own heap storage instead of aliasing cmd_copy -- see shell_token_t. */
static void shell_token_array_free(shell_token_array_t *arr) {
    if (!arr)
        return;
    for (size_t i = 0; i < arr->count; i++)
        if (arr->data[i].owned)
            free(arr->data[i].begin);
    free(arr->data);
    arr->data     = NULL;
    arr->count    = 0;
    arr->capacity = 0;
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
            case SHELL_TOKEN_AND:
            case SHELL_TOKEN_OR:
                if (!shell_token_array_append(&tokens, token))
                    l->error = "out of memory";
                break;
        }
    }
BAIL:
    return tokens;
}

static inline shell_ast_t *new_ast(void) {
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

static shell_token_t *parser_peek(shell_parser *p) {
    return p->cursor < p->tokens.count ? &p->tokens.data[p->cursor] : NULL;
}

static shell_token_t *parser_next(shell_parser *p) {
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
    ast->type        = SHELL_AST_CMD;
    ast->token       = t;
    if (parser_next(p) != NULL)
        ast->right = simple_command(p);
    return ast;
}

// Builds one bare REDIR node (its ->right is filled in by command()).
static inline shell_ast_t *handle_redirection(shell_parser    *p,
                                              shell_ast_type_t type) {
    if (!expect_token(p, SHELL_TOKEN_ATOM))
        return NULL;
    shell_ast_t *ast = new_ast();
    ast->type        = type;
    ast->token       = parser_peek(p);
    parser_next(p);
    return ast;
}

// #1311: a simple command may carry any number of redirections, in any mix
// and order (`cmd < in > out`, `cmd > out < in`, ...) -- loop rather than
// checking '<'/'>' once each, which only ever absorbed a single redirect
// and left the rest of the tokens unconsumed, tripping shell_eval_parser()'s
// whole-input check and failing the whole command with no diagnostic
// (SHELL_ERR_EVAL, exit 253).
//
// #1326: each new redirect is spliced *innermost* -- just above the command
// node, below every earlier redirect -- so `cmd > o1 > o2` parses to
// REDIR_OUT(o1) -> REDIR_OUT(o2) -> CMD. eval_redirection() descends
// outer-to-inner, opening (and O_TRUNC'ing) o1 first then o2, so the fd the
// command inherits is o2's: the *last* same-direction redirect wins and the
// earlier one is left created-but-empty, matching real `sh`/`bash`. Mixed
// directions independently keep last-of-each-direction.
static shell_ast_t *command(shell_parser *p) {
    shell_ast_t *root = simple_command(p);
    if (!root)
        return NULL;
    shell_ast_t *cmd_head  = root; // the CMD chain; redirects splice above it
    shell_ast_t *innermost = NULL; // deepest REDIR node so far, or NULL
    for (;;) {
        shell_ast_type_t type;
        if (match_token(p, SHELL_TOKEN_GREATER))
            type = SHELL_AST_REDIR_OUT;
        else if (match_token(p, SHELL_TOKEN_LESSER))
            type = SHELL_AST_REDIR_IN;
        else
            break;
        parser_next(p);
        shell_ast_t *node = handle_redirection(p, type);
        if (!node) {
            free_ast(root);
            return NULL;
        }
        node->right = cmd_head;
        if (innermost)
            innermost->right = node;
        else
            root = node;
        innermost = node;
    }
    return root;
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
        ast->type        = SHELL_AST_PIPE;
        ast->left        = left;
        ast->right       = _pipe(p);
        return ast;
    }
    return left;
}

static shell_ast_t *full_command(shell_parser *p) {
    shell_ast_t *left = _pipe(p);
    if (left == NULL)
        return NULL;
    /* `&` and `;` allow a trailing operator (nothing after them is fine —
     * parser_next(p) returning NULL means the operator was the last token,
     * so the cursor is correctly left on it and ast->right stays NULL). Must
     * consume the operator via parser_next() BEFORE checking what follows —
     * checking first (the previous bug here) always sees the operator token
     * itself, which is never SHELL_TOKEN_ATOM, so every `&`/`;` command was
     * unconditionally rejected. */
    if (match_token(p, SHELL_TOKEN_AMPERSAND)) {
        shell_ast_t *ast = new_ast();
        ast->type        = SHELL_AST_BACKGROUND;
        ast->left        = left;
        ast->right       = parser_next(p) != NULL ? full_command(p) : NULL;
        return ast;
    }
    if (match_token(p, SHELL_TOKEN_SEMICOLON)) {
        shell_ast_t *ast = new_ast();
        ast->type        = SHELL_AST_SEQ;
        ast->left        = left;
        ast->right       = parser_next(p) != NULL ? full_command(p) : NULL;
        return ast;
    }
    /* `&&` and `||` require a right-hand command — a trailing `&&`/`||` is
     * malformed, unlike `&`/`;`. */
    if (match_token(p, SHELL_TOKEN_AND)) {
        parser_next(p);
        if (!expect_token(p, SHELL_TOKEN_ATOM)) {
            free_ast(left);
            return NULL;
        }
        shell_ast_t *ast = new_ast();
        ast->type        = SHELL_AST_AND;
        ast->left        = left;
        ast->right       = full_command(p);
        return ast;
    }
    if (match_token(p, SHELL_TOKEN_OR)) {
        parser_next(p);
        if (!expect_token(p, SHELL_TOKEN_ATOM)) {
            free_ast(left);
            return NULL;
        }
        shell_ast_t *ast = new_ast();
        ast->type        = SHELL_AST_OR;
        ast->left        = left;
        ast->right       = full_command(p);
        return ast;
    }
    return left;
}

static shell_ast_t *shell_eval_parser(shell_token_array_t tokens) {
    shell_parser parser = {.tokens = tokens, .cursor = 0};
    shell_ast_t *result = full_command(&parser);
    return tokens.count == 0                   ? NULL
           : parser.cursor == tokens.count - 1 ? result
                                               : NULL;
}

/* --- Execution --- */

typedef struct shell_command {
    int    argc;
    char **argv;
    int    input_fd;
    int    output_fd;
    int    bg;
} shell_command_t;

/* Forward declare internal AST executor */
static int ast_exec(shell_ctx *ctx, shell_ast_t *ast);

/* #1327: mark an fd close-on-exec. macOS has no pipe2(), so eval_pipeline()
 * creates plain pipe()s and sets FD_CLOEXEC here; dup2() clears it on the
 * destination, so a stage keeps its own dup2'd stdin/stdout across exec()
 * while every unrelated inherited pipe end closes automatically. */
static int set_cloexec(int fd) {
    int flags = fcntl(fd, F_GETFD, 0);
    return flags == -1 ? -1 : fcntl(fd, F_SETFD, flags | FD_CLOEXEC);
}

static void command_argv_from_ast(shell_command_t *cmd, shell_ast_t *ast) {
    cmd->argc = 0;
    cmd->argv = NULL;

    for (shell_ast_t *n = ast; n != NULL; n = n->right)
        cmd->argc++;

    if (cmd->argc == 0) {
        cmd->argv    = xmalloc(sizeof(char *));
        cmd->argv[0] = NULL;
        return;
    }

    cmd->argv = xmalloc(sizeof(char *) * (cmd->argc + 1));
    for (int i = 0; i < cmd->argc; i++) {
        if (ast == NULL || ast->token == NULL) {
            cmd->argv[i]    = xmalloc(1);
            cmd->argv[i][0] = '\0';
        } else {
            size_t len   = (size_t)ast->token->length;
            cmd->argv[i] = xmalloc(len + 1);
            memcpy(cmd->argv[i], ast->token->begin, len);
            cmd->argv[i][len] = '\0';
        }
        ast = ast ? ast->right : NULL;
    }
    cmd->argv[cmd->argc] = NULL;
}

/* CCCC patch (#1325): builtins return an int exit status (0 = success)
 * instead of void, so command_execute() can report what a builtin actually
 * did rather than an unconditional 0. */
static int builtin_exit(int argc, char **argv) {
    exit(argc >= 2 ? atoi(argv[1]) : 0);
}

static int builtin_cd(int argc, char **argv) {
    if (argc == 1) {
        if (chdir(getenv("HOME")) == -1) {
            perror("cd");
            return 1;
        }
    } else if (argc == 2) {
        if (chdir(argv[1]) == -1) {
            perror("cd");
            return 1;
        }
    } else {
        eprintf("cd: too many arguments\n");
        return 1;
    }
    return 0;
}

static int builtin_pwd(int argc, char **argv) {
    (void)argv;
    if (argc > 1) {
        eprintf("pwd: too many arguments\n");
        return 1;
    }
    char *cwd = getcwd(NULL, 0);
    if (!cwd) {
        _perror("pwd");
        return 1;
    }
    printf("%s\n", cwd);
    free(cwd);
    return 0;
}

static shell_builtin_entry_t *builtin_find(shell_ctx *ctx, const char *name) {
    for (shell_builtin_entry_t *b = ctx->builtins; b != NULL; b = b->next) {
        if (strcmp(b->name, name) == 0)
            return b;
    }
    return NULL;
}

static bool is_blacklisted_cmd(shell_ctx *ctx, const char *cmd) {
    if (!ctx->cmd_blacklist)
        return false;
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
    if (!ctx->path_blacklist)
        return false;
    for (int i = 0; ctx->path_blacklist[i]; i++) {
        if (strstr(path, ctx->path_blacklist[i]) != NULL)
            return true;
    }
    return false;
}

/* Reaps terminated background children so they do not linger as zombies;
 * WNOHANG leaves live children alone and keeps foreground waitpid() working. */
static void sigchld_reaper(int sig) {
    (void)sig;
    int saved_errno = errno;
    while (waitpid(-1, NULL, WNOHANG) > 0)
        ;
    errno = saved_errno;
}

/* CCCC patch: returns the child's real exit code (non-negative) or a
 * SHELL_ERR_* constant (negative) on error.  Builtins return 0 (their
 * return type is void so a real code is unavailable).
 * Background processes return 0 immediately (no wait). */
static int command_execute(shell_ctx *ctx, shell_command_t *cmd) {
    if (cmd->argc == 0)
        return 0;
    char *exec_name = cmd->argv[0];

    /* 1. Check Blacklist */
    if (is_blacklisted_cmd(ctx, exec_name)) {
        eprintf("shell: command '%s' is blacklisted\n", exec_name);
        return SHELL_ERR_PERM;
    }

    /* Check all arguments for path restrictions */
    for (int i = 0; i < cmd->argc; i++) {
        if (is_blacklisted_path(ctx, cmd->argv[i])) {
            eprintf("shell: argument '%s' contains blacklisted path\n",
                    cmd->argv[i]);
            return SHELL_ERR_PERM;
        }
    }

    /* 2. Check Builtins (User defined builtins override everything; always
     * allowed).
     *
     * #1325: a builtin honours the stage's redirect/pipe fds and reports its
     * real exit status. Inside a pipeline it runs in a forked subshell
     * (POSIX semantics -- `cd x | y` doesn't change the step's later
     * commands, and an in-process builtin writing more than one pipe buffer
     * would deadlock before the reader stage is forked); outside a pipeline
     * it runs in-process with the shell's own std fds temporarily dup2'd
     * onto cmd->input_fd/output_fd, so `cd x > log` still changes this
     * shell's cwd. */
    shell_builtin_entry_t *builtin = builtin_find(ctx, exec_name);
    if (builtin) {
        int has_redir = (cmd->input_fd != -1 || cmd->output_fd != -1);

        if (ctx->defer_wait && !cmd->bg) {
            /* Pipeline stage: fork a subshell, exactly like an external
             * command, so eval_pipeline()'s stage_deferred[] accounting is
             * unchanged. */
            if (ctx->deferred_pid_count >= SHELL_MAX_DEFERRED_PIDS) {
                eprintf("shell: pipeline exceeds %d stages\n",
                        SHELL_MAX_DEFERRED_PIDS);
                return SHELL_ERR_PIPE;
            }
            fflush(NULL); /* parent-side: don't let the child re-flush our
                           * pending (pipe-buffered) stdout */
            pid_t pid = fork();
            if (pid == -1) {
                _perror("fork");
                return SHELL_ERR_FORK;
            }
            if (pid == 0) {
                if (cmd->input_fd != -1) {
                    dup2(cmd->input_fd, STDIN_FILENO);
                    close(cmd->input_fd);
                }
                if (cmd->output_fd != -1) {
                    dup2(cmd->output_fd, STDOUT_FILENO);
                    close(cmd->output_fd);
                }
                /* This child does not exec(), so the FD_CLOEXEC on the
                 * other stages' pipe ends (#1327) doesn't fire here -- but
                 * the builtins (cd/pwd/exit) all terminate immediately on
                 * their own, so no downstream reader is left waiting. */
                int brc = builtin->func(cmd->argc, cmd->argv);
                fflush(stdout);
                _exit(brc < 0 ? 1 : brc);
            }
            ctx->deferred_pids[ctx->deferred_pid_count++] = pid;
            return 0;
        }

        if (!has_redir)
            return builtin->func(cmd->argc, cmd->argv);

        /* Redirect, no pipe: temporarily point the shell's own std fds at
         * the redirect targets around the in-process call. */
        int saved_in = -1, saved_out = -1;
        if (cmd->input_fd != -1) {
            saved_in = dup(STDIN_FILENO);
            dup2(cmd->input_fd, STDIN_FILENO);
        }
        if (cmd->output_fd != -1) {
            fflush(stdout);
            saved_out = dup(STDOUT_FILENO);
            dup2(cmd->output_fd, STDOUT_FILENO);
        }
        int brc = builtin->func(cmd->argc, cmd->argv);
        if (saved_out != -1) {
            fflush(stdout);
            dup2(saved_out, STDOUT_FILENO);
            close(saved_out);
        }
        if (saved_in != -1) {
            dup2(saved_in, STDIN_FILENO);
            close(saved_in);
        }
        return brc;
    }

    /* 3. Allowlist check for external commands */
    if (!is_allowlisted_cmd(ctx, exec_name)) {
        eprintf("shell: command '%s' not in allowlist\n", exec_name);
        return SHELL_ERR_PERM;
    }

    /* 4. Check Builtin-Only Mode */
    if (ctx->builtin_only) {
        eprintf("shell: command '%s' not found (builtin-only mode)\n",
                exec_name);
        return SHELL_ERR_PERM;
    }

    /* 5. External Execution */
    if (ctx->defer_wait && !cmd->bg &&
        ctx->deferred_pid_count >= SHELL_MAX_DEFERRED_PIDS) {
        eprintf("shell: pipeline exceeds %d stages\n", SHELL_MAX_DEFERRED_PIDS);
        return SHELL_ERR_PIPE;
    }

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
        /* #1322: under a pipeline (ctx->defer_wait), record the pid and
         * return immediately instead of waiting here -- eval_pipeline()
         * needs every stage forked before it waits on any of them, or a
         * stage whose output exceeds one pipe buffer deadlocks (this
         * stage blocked in write(), the shell blocked in waitpid() before
         * the next stage -- the reader -- is even forked). */
        if (ctx->defer_wait) {
            ctx->deferred_pids[ctx->deferred_pid_count++] = pid;
            return 0;
        }
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
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = sigchld_reaper;
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
    cmd.input_fd  = ctx->input_fd;
    cmd.output_fd = ctx->output_fd;
    cmd.bg        = ctx->bg;
    int rc        = command_execute(ctx, &cmd);
    command_destroy(&cmd);
    return rc;
}

/* CCCC patch: returns exit code of last command (shell `;` semantics). */
static int eval_sequence(shell_ctx *ctx, shell_ast_t *ast) {
    if (ast->type == SHELL_AST_BACKGROUND)
        ctx->bg = 1;
    ast_exec(ctx, ast->left); /* left runs; `;` ignores its exit code */
    int rc = ast_exec(ctx, ast->right);
    if (ast->type == SHELL_AST_BACKGROUND)
        ctx->bg = 0;
    return rc;
}

/* `&&`: short-circuits — right runs only if left succeeded (exit 0). */
static int eval_and(shell_ctx *ctx, shell_ast_t *ast) {
    int rc = ast_exec(ctx, ast->left);
    if (rc != 0)
        return rc;
    return ast_exec(ctx, ast->right);
}

/* `||`: short-circuits — right runs only if left failed (exit != 0). */
static int eval_or(shell_ctx *ctx, shell_ast_t *ast) {
    int rc = ast_exec(ctx, ast->left);
    if (rc == 0)
        return rc;
    return ast_exec(ctx, ast->right);
}

/* CCCC patch: returns exit code of the redirected command. */
static int eval_redirection(shell_ctx *ctx, shell_ast_t *ast) {
    int           fd;
    unsigned char c = ast->token->begin[ast->token->length];
    ast->token->begin[ast->token->length] = '\0';
    const char *filename                  = (const char *)ast->token->begin;

    /* Security Check for File Access */
    if (is_blacklisted_path(ctx, filename)) {
        eprintf("shell: file access '%s' is blacklisted\n", filename);
        ast->token->begin[ast->token->length] = c;
        return SHELL_ERR_PERM;
    }

    /* #1322: save the fds this node is about to overwrite, so they can be
     * restored on the way out instead of being hardcoded to -1. A
     * redirect nested inside a pipeline stage (`a | b > f | c`) must not
     * wipe out the pipe fd eval_pipeline() put here before descending into
     * this stage -- doing so left eval_pipeline()'s own cleanup closing -1
     * instead of the previous stage's pipe read end, leaking it. */
    int saved_input_fd  = ctx->input_fd;
    int saved_output_fd = ctx->output_fd;

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

    int rc                                = ast_exec(ctx, ast->right);
    close(fd);
    ctx->input_fd  = saved_input_fd;
    ctx->output_fd = saved_output_fd;
    return rc;
}

/* #1322: reaps every pid this pipeline forked, in order, and returns the
 * exit status of the last stage (matching command_execute()'s own
 * WEXITSTATUS/WTERMSIG convention).
 *
 * `stage_deferred[i]` says whether stage i actually pushed a pid onto
 * ctx->deferred_pids -- it is NOT inferable from `stage_rc[i] >= 0` alone.
 * A stage can return >= 0 without deferring anything: chiefly a command
 * (external or builtin) that inherited ctx->bg == 1 from an enclosing `&`
 * (the `&`/`;` quirk in eval_sequence() leaves ctx->bg set to 1 across
 * *both* sides of the `&`, so `bg-cmd & a | b | c` runs the whole pipeline
 * in command_execute()'s background branch, which forks, doesn't wait, and
 * returns 0 immediately without touching ctx->deferred_pids at all).
 * (Builtins used to be another such case -- they ran synchronously before
 * the fork -- but #1325 makes a builtin in a pipeline fork a subshell and
 * push a pid like any other stage.) Any stage with
 * `stage_deferred[i] == false` already carries its final result in
 * `stage_rc[i]` and must not consume a pid. */
static int wait_deferred_pipeline(shell_ctx *ctx, const int *stage_rc,
                                  const bool *stage_deferred, int stage_count) {
    int last_status_rc = 0;
    int error_rc       = 0; /* 0 = none seen yet; SHELL_ERR_* are all < 0 */
    for (int i = 0, pid_idx = 0; i < stage_count; i++) {
        if (!stage_deferred[i]) {
            if (stage_rc[i] < 0) {
                /* A pre-fork failure (blacklist/allowlist/builtin-only/
                 * fork) or a builtin/backgrounded stage's already-final
                 * result. The first negative result aborts the pipeline's
                 * reported outcome; later stages may still have forked
                 * (the original loop doesn't short-circuit on a
                 * mid-pipeline error) and must still be reaped below. */
                if (!error_rc)
                    error_rc = stage_rc[i];
            } else {
                last_status_rc = stage_rc[i];
            }
            continue;
        }
        pid_t pid = ctx->deferred_pids[pid_idx++];
        int   status;
        do {
            waitpid(pid, &status, WUNTRACED);
        } while (!WIFEXITED(status) && !WIFSIGNALED(status));
        if (WIFEXITED(status))
            last_status_rc = WEXITSTATUS(status);
        else if (WIFSIGNALED(status))
            last_status_rc = 128 + WTERMSIG(status);
        else
            last_status_rc = SHELL_ERR_GENERIC;
    }
    ctx->deferred_pid_count = 0;
    return error_rc ? error_rc : last_status_rc;
}

/* CCCC patch: returns the exit code of the last command in the pipeline.
 *
 * #1322: forks every stage before waiting on any of them (ctx->defer_wait,
 * see command_execute()) -- the original code forked and waited on one
 * stage at a time, so a stage writing more than one pipe buffer's worth of
 * output (~16KB macOS / 64KB Linux) blocked in write() with nobody yet
 * forked to read it, and the shell blocked right behind it in waitpid();
 * `a | b` deadlocked the whole build on any non-trivial payload. Pipe fds
 * are now tracked in a local (prev_read) rather than read back out of ctx
 * for closing -- ctx->input_fd/output_fd get clobbered to whatever a
 * stage's own redirect last touched, so closing "ctx->input_fd" after a
 * redirect-carrying stage closed the wrong fd (or -1) and leaked the real
 * pipe read end. SIGCHLD is blocked for the whole fork-and-wait span so
 * sigchld_reaper() (installed once any `&` background job has run) can't
 * reap a stage out from under this function's own waitpid() calls. */
static int eval_pipeline(shell_ctx *ctx, shell_ast_t *ast) {
    int      saved_input_fd   = ctx->input_fd;
    int      saved_output_fd  = ctx->output_fd;
    bool     saved_defer_wait = ctx->defer_wait;

    sigset_t block_set, saved_set;
    sigemptyset(&block_set);
    sigaddset(&block_set, SIGCHLD);
    sigprocmask(SIG_BLOCK, &block_set, &saved_set);

    ctx->defer_wait         = true;
    ctx->deferred_pid_count = 0;

    /* One slot per stage in the pipeline. `stage_rc[i]` is that stage's
     * ast_exec() return; `stage_deferred[i]` says whether it actually
     * pushed a pid (see wait_deferred_pipeline()'s comment for why that
     * can't be inferred from stage_rc alone). Sized to
     * SHELL_MAX_DEFERRED_PIDS, enforced below independently of
     * command_execute()'s own pid-count check (a stage can take a slot
     * here without ever forking). */
    int  stage_rc[SHELL_MAX_DEFERRED_PIDS];
    bool stage_deferred[SHELL_MAX_DEFERRED_PIDS];
    int  stage_count = 0;
    int  prev_read   = -1;
    int  rc          = SHELL_ERR_PIPE;
    bool waited      = false;

#define PIPELINE_RUN_STAGE(stage_ast)                                          \
    do {                                                                       \
        if (stage_count >= SHELL_MAX_DEFERRED_PIDS) {                          \
            eprintf("shell: pipeline exceeds %d stages\n",                     \
                    SHELL_MAX_DEFERRED_PIDS);                                  \
            goto cleanup;                                                      \
        }                                                                      \
        int before                  = ctx->deferred_pid_count;                 \
        stage_rc[stage_count]       = ast_exec(ctx, (stage_ast));              \
        stage_deferred[stage_count] = ctx->deferred_pid_count > before;        \
        stage_count++;                                                         \
    } while (0)

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        _perror("pipe");
        goto cleanup;
    }
    set_cloexec(pipefd[0]); /* #1327 */
    set_cloexec(pipefd[1]);

    ctx->input_fd  = -1;
    ctx->output_fd = pipefd[1];
    PIPELINE_RUN_STAGE(ast->left);
    close(pipefd[1]);

    ast           = ast->right;
    prev_read     = pipefd[0];
    ctx->input_fd = prev_read;

    while (ast->type == SHELL_AST_PIPE) {
        if (pipe(pipefd) == -1) {
            _perror("pipe");
            goto cleanup;
        }
        set_cloexec(pipefd[0]); /* #1327 */
        set_cloexec(pipefd[1]);
        ctx->output_fd = pipefd[1];
        PIPELINE_RUN_STAGE(ast->left);
        close(pipefd[1]);
        close(prev_read);
        prev_read     = pipefd[0];
        ctx->input_fd = prev_read;
        ast           = ast->right;
    }
    ctx->output_fd = -1;
    ctx->input_fd  = prev_read;
    PIPELINE_RUN_STAGE(ast); /* last stage */

#undef PIPELINE_RUN_STAGE

    /* Close the parent's own copy of the final read end *before* waiting,
     * not after -- every other pipe fd this function creates is closed as
     * soon as no stage needs it. #1327: combined with set_cloexec() on
     * every pipe end above, this is what lets an early-exiting reader
     * (`producer | head`) actually stop a still-running producer: each
     * forked stage's own copies of the unrelated pipe ends now close
     * automatically at exec(), so once the real reader exits the kernel
     * sees no readers left and the producer's write() gets EPIPE/SIGPIPE
     * instead of blocking forever. */
    close(prev_read);
    prev_read = -1;

    rc     = wait_deferred_pipeline(ctx, stage_rc, stage_deferred, stage_count);
    waited = true;

cleanup:
    if (prev_read != -1)
        close(prev_read);
    /* Reap whatever already forked before returning, even on an early
     * (pipe()-failure or too-many-stages) exit -- discard those statuses,
     * the error already produced above (rc stays SHELL_ERR_PIPE) is the
     * more meaningful diagnostic. */
    if (!waited && stage_count > 0)
        wait_deferred_pipeline(ctx, stage_rc, stage_deferred, stage_count);
    ctx->defer_wait = saved_defer_wait;
    sigprocmask(SIG_SETMASK, &saved_set, NULL);
    ctx->input_fd  = saved_input_fd;
    ctx->output_fd = saved_output_fd;
    return rc;
}

/* CCCC patch: propagates real exit codes from all eval_* functions. */
static int ast_exec(shell_ctx *ctx, shell_ast_t *ast) {
    if (!ast)
        return 0;
    switch (ast->type) {
        case SHELL_AST_PIPE:
            return eval_pipeline(ctx, ast);
        case SHELL_AST_REDIR_IN:
        case SHELL_AST_REDIR_OUT:
            return eval_redirection(ctx, ast);
        case SHELL_AST_SEQ:
        case SHELL_AST_BACKGROUND:
            return eval_sequence(ctx, ast);
        case SHELL_AST_AND:
            return eval_and(ctx, ast);
        case SHELL_AST_OR:
            return eval_or(ctx, ast);
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
    HANDLE            handle;
    shell_stream_cb_t callback;
    void             *userdata;
    char            **buffer;
    size_t           *length;
    size_t           *capacity;
    int               use_callback;
} win_reader_args_t;

static DWORD WINAPI win_reader_thread(LPVOID arg) {
    /* Same implementation as before */
    win_reader_args_t *args = (win_reader_args_t *)arg;
    char               buffer[4096];
    DWORD              bytes_read = 0;
    while (ReadFile(args->handle, buffer, sizeof(buffer), &bytes_read, NULL) &&
           bytes_read > 0)
        if (args->use_callback) {
            args->callback(buffer, bytes_read, args->userdata);
        } else {
            if (*args->capacity - *args->length < bytes_read) {
                size_t new_capacity =
                    (*args->capacity == 0) ? 4096 : *args->capacity * 2;
                while (new_capacity - *args->length < bytes_read)
                    new_capacity *= 2;
                *args->buffer   = xrealloc(*args->buffer, new_capacity + 1);
                *args->capacity = new_capacity;
            }
            memcpy(*args->buffer + *args->length, buffer, bytes_read);
            *args->length += bytes_read;
        }
    return 0;
}

static int create_pipe_pair(HANDLE *read_handle, HANDLE *write_handle,
                            int inherit_read) {
    SECURITY_ATTRIBUTES sa = {.nLength        = sizeof(SECURITY_ATTRIBUTES),
                              .bInheritHandle = TRUE,
                              .lpSecurityDescriptor = NULL};
    if (!CreatePipe(read_handle, write_handle, &sa, 0))
        return SHELL_ERR_PIPE;
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
    (void)ctx; /* Warning: Context logic not fully applied to Win32 path in this
                  simplified port */

    if (!cmd)
        return SHELL_ERR_GENERIC;

    HANDLE  pipes[6] = {0};
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
    char  *cmdline = xmalloc(cmd_len + 1);
    strcpy(cmdline, cmd);

    STARTUPINFOA        si = {.cb         = sizeof(STARTUPINFOA),
                              .dwFlags    = STARTF_USESTDHANDLES,
                              .hStdInput  = *stdin_r,
                              .hStdOutput = *stdout_w,
                              .hStdError  = *stderr_w};

    PROCESS_INFORMATION pi = {0};
    BOOL success = CreateProcessA(NULL, cmdline, NULL, NULL, TRUE, 0, NULL,
                                  NULL, &si, &pi);
    free(cmdline);

    if (!success) {
        cleanup_handles(pipes, 6);
        return SHELL_ERR_FORK;
    }

    CloseHandle(*stdout_w);
    *stdout_w = NULL;
    CloseHandle(*stderr_w);
    *stderr_w = NULL;
    CloseHandle(*stdin_r);
    *stdin_r                 = NULL;

    char             *outbuf = NULL, *errbuf = NULL;
    size_t            out_len = 0, out_cap = 0, err_len = 0, err_cap = 0;

    win_reader_args_t out_args = {.handle       = *stdout_r,
                                  .callback     = io ? io->out_cb : NULL,
                                  .userdata     = io ? io->userdata : NULL,
                                  .buffer       = &outbuf,
                                  .length       = &out_len,
                                  .capacity     = &out_cap,
                                  .use_callback = (io && io->out_cb)};
    win_reader_args_t err_args = {.handle       = *stderr_r,
                                  .callback     = io ? io->err_cb : NULL,
                                  .userdata     = io ? io->userdata : NULL,
                                  .buffer       = &errbuf,
                                  .length       = &err_len,
                                  .capacity     = &err_cap,
                                  .use_callback = (io && io->err_cb)};

    HANDLE            threads[2];
    threads[0] = CreateThread(NULL, 0, win_reader_thread, &out_args, 0, NULL);
    threads[1] = CreateThread(NULL, 0, win_reader_thread, &err_args, 0, NULL);

    if (io && io->in && io->in_len > 0) {
        DWORD written;
        WriteFile(*stdin_w, io->in, (DWORD)io->in_len, &written, NULL);
    }
    CloseHandle(*stdin_w);
    *stdin_w = NULL;

    WaitForSingleObject(pi.hProcess, INFINITE);
    WaitForMultipleObjects(2, threads, TRUE, INFINITE);

    DWORD exit_code;
    GetExitCodeProcess(pi.hProcess, &exit_code);

    if (io) {
        if (!out_args.use_callback) {
            io->out = outbuf ? (outbuf[out_len] = '\0', outbuf)
                             : (outbuf = xmalloc(1), outbuf[0] = '\0', outbuf);
            io->out_len = out_len;
        } else {
            io->out     = NULL;
            io->out_len = 0;
        }
        if (!err_args.use_callback) {
            io->err = errbuf ? (errbuf[err_len] = '\0', errbuf)
                             : (errbuf = xmalloc(1), errbuf[0] = '\0', errbuf);
            io->err_len = err_len;
        } else {
            io->err     = NULL;
            io->err_len = 0;
        }
    }

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    cleanup_handles(pipes, 6);
    CloseHandle(threads[0]);
    CloseHandle(threads[1]);

    return (int)exit_code;
}

#else
/* POSIX implementation */

static int set_nonblocking(int fd) {
    int flags = fcntl(fd, F_GETFL, 0);
    return flags == -1 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static void close_pipe_pair(int pipe_fds[2]) {
    if (pipe_fds[0] != -1) {
        close(pipe_fds[0]);
        pipe_fds[0] = -1;
    }
    if (pipe_fds[1] != -1) {
        close(pipe_fds[1]);
        pipe_fds[1] = -1;
    }
}

static int write_input_to_child(int write_fd, const char *input,
                                size_t input_len) {
    size_t written = 0;
    while (written < input_len) {
        ssize_t result = write(write_fd, input + written, input_len - written);
        if (result == -1) {
            if (errno == EAGAIN || errno == EINTR)
                continue;
            return -1;
        }
        written += result;
    }
    return 0;
}

static int ensure_buffer_capacity(char **buffer, size_t *capacity,
                                  size_t current_len, size_t needed) {
    if (*capacity - current_len >= needed)
        return 0;
    size_t new_capacity = (*capacity == 0) ? 4096 : *capacity * 2;
    while (new_capacity - current_len < needed)
        new_capacity *= 2;
    *buffer   = xrealloc(*buffer, new_capacity + 1);
    *capacity = new_capacity;
    return 0;
}

static int posix_shell_with_io(const char *cmd, shell_io *io, shell_ctx *ctx) {
    int  pipes[6] = {-1, -1, -1, -1, -1, -1}; /* in[2], out[2], err[2] */
    int *inpipe = &pipes[0], *outpipe = &pipes[2], *errpipe = &pipes[4];

    if (pipe(inpipe) == -1 || pipe(outpipe) == -1 || pipe(errpipe) == -1) {
        for (int i = 0; i < 6; i += 2)
            close_pipe_pair(&pipes[i]);
        return SHELL_ERR_PIPE;
    }

    pid_t pid = fork();
    if (pid == -1) {
        for (int i = 0; i < 6; i += 2)
            close_pipe_pair(&pipes[i]);
        return SHELL_ERR_FORK;
    }

    if (pid == 0) {
        /* Child process */
        close(inpipe[1]);
        close(outpipe[0]);
        close(errpipe[0]);

        if (dup2(inpipe[0], STDIN_FILENO) == -1 ||
            dup2(outpipe[1], STDOUT_FILENO) == -1 ||
            dup2(errpipe[1], STDERR_FILENO) == -1) {
            die("dup2");
        }
        close(inpipe[0]);
        close(outpipe[1]);
        close(errpipe[1]);

        /* Tokenize and Parse */
        char *cmd_copy = strdup(cmd);
        if (!cmd_copy)
            die("strdup");

        shell_lexer_t lexer;
        shell_lexer_with_ctx(&lexer, (unsigned char *)cmd_copy, ctx);
        shell_token_array_t tokens = shell_parse(&lexer);

        if (!tokens.data || lexer.error) {
            /* #1311: this used to _exit() with no diagnostic at all -- the
             * only visible symptom was "build: custom step 'x' failed (exit
             * 253)" with no hint of what was malformed, even though the
             * lexer already records the reason and offset. stderr is
             * already dup2'd above, so this reaches the parent's captured
             * output. */
            if (lexer.error)
                fprintf(stderr, "shell: %s (at offset %zu)\n", lexer.error,
                        lexer.error_pos);
            else
                fprintf(stderr, "shell: tokenize error\n");
            shell_token_array_free(&tokens);
            free(cmd_copy);
            _exit(SHELL_ERR_TOKENIZE);
        }

        shell_ast_t *ast = shell_eval_parser(tokens);
        if (!ast) {
            fprintf(stderr, "shell: syntax error\n");
            shell_token_array_free(&tokens);
            free(cmd_copy);
            _exit(SHELL_ERR_EVAL);
        }

        /* EXECUTE with Context; CCCC patch: propagate real exit code */
        int result = ast_exec(ctx, ast);

        shell_token_array_free(&tokens);
        free_ast(ast);
        free(cmd_copy);
        /* #1325: a builtin (cd/pwd/exit) that ran in-process here wrote to
         * this child's fully-buffered stdout; _exit() would discard it, so
         * `$(pwd)` and `pwd`-into-a-capture came back empty. */
        fflush(NULL);
        _exit(result >= 0 ? result : 1);
    }

    /* Parent process */
    close(inpipe[0]);
    close(outpipe[1]);
    close(errpipe[1]);

    set_nonblocking(inpipe[1]);
    set_nonblocking(outpipe[0]);
    set_nonblocking(errpipe[0]);

    if (io && io->in && io->in_len > 0)
        write_input_to_child(inpipe[1], io->in, io->in_len);
    close(inpipe[1]);

    char         *outbuf = NULL, *errbuf = NULL;
    size_t        out_len = 0, out_cap = 0, err_len = 0, err_cap = 0;
    int           use_out_cb = io && io->out_cb != NULL;
    int           use_err_cb = io && io->err_cb != NULL;

    struct pollfd fds[2]     = {{.fd = outpipe[0], .events = POLLIN | POLLHUP},
                                {.fd = errpipe[0], .events = POLLIN | POLLHUP}};
    int           active_fds = 2;

    while (active_fds > 0) {
        int poll_result = poll(fds, 2, -1);
        if (poll_result == -1) {
            if (errno == EINTR)
                continue;
            break;
        }

        for (int i = 0; i < 2; i++) {
            if (!(fds[i].revents & (POLLIN | POLLHUP)) || fds[i].fd == -1)
                continue;
            char    buffer[4096];
            ssize_t bytes_read;
            while ((bytes_read = read(fds[i].fd, buffer, sizeof(buffer))) > 0) {
                if (i == 0) {
                    if (use_out_cb)
                        io->out_cb(buffer, bytes_read, io->userdata);
                    else {
                        ensure_buffer_capacity(&outbuf, &out_cap, out_len,
                                               bytes_read);
                        memcpy(outbuf + out_len, buffer, bytes_read);
                        out_len += bytes_read;
                    }
                } else {
                    if (use_err_cb)
                        io->err_cb(buffer, bytes_read, io->userdata);
                    else {
                        ensure_buffer_capacity(&errbuf, &err_cap, err_len,
                                               bytes_read);
                        memcpy(errbuf + err_len, buffer, bytes_read);
                        err_len += bytes_read;
                    }
                }
            }
            if (bytes_read == 0 ||
                (bytes_read == -1 && errno != EAGAIN && errno != EINTR)) {
                fds[i].fd = -1;
                active_fds--;
            }
        }
    }

    if (io) {
        if (!use_out_cb) {
            io->out = outbuf ? (outbuf[out_len] = '\0', outbuf)
                             : (outbuf = xmalloc(1), outbuf[0] = '\0', outbuf);
            io->out_len = out_len;
        } else {
            io->out     = NULL;
            io->out_len = 0;
        }
        if (!use_err_cb) {
            io->err = errbuf ? (errbuf[err_len] = '\0', errbuf)
                             : (errbuf = xmalloc(1), errbuf[0] = '\0', errbuf);
            io->err_len = err_len;
        } else {
            io->err     = NULL;
            io->err_len = 0;
        }
    }

    close(outpipe[0]);
    close(errpipe[0]);
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
    shell_lexer_with_ctx(&lexer, (unsigned char *)cmd_copy, ctx);

    shell_token_array_t tokens = shell_parse(&lexer);
    if (!tokens.data || lexer.error) {
        if (lexer.error) {
            printf("error: '%s'\n", lexer.error);
        }
        shell_token_array_free(&tokens);
        free(cmd_copy);
        return -1;
    }

    shell_ast_t *ast = shell_eval_parser(tokens);
    if (!ast) {
        /* #1311: was silent here too. */
        printf("error: syntax error\n");
        shell_token_array_free(&tokens);
        free(cmd_copy);
        return -1;
    }

    /* CCCC patch: ast_exec now returns the real exit code */
    int result = ast_exec(ctx, ast);
    shell_token_array_free(&tokens);
    free_ast(ast);
    free(cmd_copy);
    return result;
}

#endif /* _WIN32 */

int shell_with_ctx(const char *cmd, shell_io *io, shell_ctx *ctx) {
    if (!cmd)
        return SHELL_ERR_GENERIC;

#if defined(_WIN32) || defined(_WIN64)
    return win_shell_with_io(cmd, io, ctx);
#else
    return io ? posix_shell_with_io(cmd, io, ctx)
              : posix_shell_inline(cmd, ctx);
#endif
}

int shell(const char *cmd, shell_io *io) {
    if (g_default_ctx) {
        return shell_with_ctx(cmd, io, g_default_ctx);
    } else {
        /* Temporary context */
        shell_ctx *tmp = shell_ctx_create();
        int        res = shell_with_ctx(cmd, io, tmp);
        shell_ctx_destroy(tmp);
        return res;
    }
}

int shell_fmt(shell_io *io, const char *fmt, ...) {
    if (!fmt)
        return SHELL_ERR_GENERIC;
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
