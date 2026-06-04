/*
 JCC: JIT C Compiler

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

 This file was original part of chibicc by Rui Ueyama (MIT)
 https://github.com/rui314/chibicc
*/

// This file implements the C preprocessor.
//
// The preprocessor takes a list of tokens as an input and returns a
// new list of tokens as an output.
//
// The preprocessing language is designed in such a way that that's
// guaranteed to stop even if there is a recursive macro.
// Informally speaking, a macro is applied only once for each token.
// That is, if a macro token T appears in a result of direct or
// indirect macro expansion of T, T won't be expanded any further.
// For example, if T is defined as U, and U is defined as T, then
// token T is expanded to U and then to T and the macro expansion
// stops at that point.
//
// To achieve the above behavior, we attach for each token a set of
// macro names from which the token is expanded. The set is called
// "hideset". Hideset is initially empty, and every time we expand a
// macro, the macro name is added to the resulting tokens' hidesets.
//
// The above macro expansion algorithm is explained in this document
// written by Dave Prossor, which is used as a basis for the
// standard's wording:
// https://github.com/rui314/chibicc/wiki/cpp.algo.pdf

#include "./internal.h"
#include "jcc.h"

#define MAX_PP_NESTING 1000

typedef struct MacroParam MacroParam;
struct MacroParam {
    MacroParam *next;
    char *name;
};

typedef struct MacroArg MacroArg;
struct MacroArg {
    MacroArg *next;
    char *name;
    bool is_va_args;
    Token *tok;
};

typedef Token *macro_handler_fn(JCC *, Token *);

typedef struct Macro Macro;
struct Macro {
    char *name;
    bool is_objlike; // Object-like or function-like
    MacroParam *params;
    char *va_args_name;
    Token *body;
    macro_handler_fn *handler;
};

static Token *preprocess2(JCC *vm, Token *tok);
static Macro *find_macro(JCC *vm, Token *tok);
static bool file_exists(char *path);
static char *format_relative_path(JCC *vm, char *base_file, char *filename);
static char *read_include_filename(JCC *vm, Token **rest, Token *tok,
                                   bool *is_dquote, int *out_len);
static long eval_const_expr(JCC *vm, Token **rest, Token *tok);

static bool is_hash(Token *tok) { return tok->at_bol && equal(tok, "#"); }

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(JCC *vm, Token *tok) {
    if (tok->at_bol)
        return tok;
    warn_tok(vm, tok, JCC_WARN_EXTRA_TOKENS, "extra tokens after directive");
    while (!tok->at_bol)
        tok = tok->next;
    return tok;
}

static Token *copy_token(JCC *vm, Token *tok) {
    Token *t = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *t = *tok;
    t->next = NULL;
    return t;
}

static Token *new_eof(JCC *vm, Token *tok) {
    Token *t = copy_token(vm, tok);
    t->kind = TK_EOF;
    t->len = 0;
    return t;
}

// Extract a [[jcc::macro]] / __attribute__((macro)) or comptime function
// definition and store it. Returns the token after the function definition
// (or original token if extraction failed).
static Token *extract_macro_function(JCC *vm, Token *tok,
                                     bool is_macro_entry,
                                     bool is_inline) {
    // Expected format: <return_type> <function_name>(<params>) { <body> }
    // tok should be the first token of the function definition

    Token *start = tok;
    Token *func_name_tok = NULL;

    // Skip to the function name: find identifier followed by '('
    while (tok && tok->kind != TK_EOF) {
        if (tok->kind == TK_IDENT && tok->next && equal(tok->next, "(")) {
            func_name_tok = tok;
            break;
        }
        tok = tok->next;
    }

    if (!func_name_tok) {
        error_tok(vm, start,
                  "__attribute__((macro)): expected function definition");
        return start;
    }

    // Extract function name
    char *name =
        arena_alloc(&vm->compiler.parser_arena, func_name_tok->len + 1);
    memcpy(name, func_name_tok->loc, func_name_tok->len);
    name[func_name_tok->len] = '\0';

    // Now find the opening brace of the function body
    int paren_depth = 0;
    tok = func_name_tok->next; // Start at '('

    // Skip parameter list
    while (tok && tok->kind != TK_EOF) {
        if (equal(tok, "("))
            paren_depth++;
        else if (equal(tok, ")")) {
            paren_depth--;
            if (paren_depth == 0) {
                tok = tok->next;
                break;
            }
        }
        tok = tok->next;
    }

    // Now find the opening brace
    while (tok && tok->kind != TK_EOF && !equal(tok, "{"))
        tok = tok->next;

    if (!equal(tok, "{")) {
        error_tok(vm, start,
                  "__attribute__((macro)): expected function body");
        return start;
    }

    Token *body_start = start;

    // Find the closing brace (matching the opening brace)
    int brace_depth = 0;
    Token *body_end = tok;
    while (tok && tok->kind != TK_EOF) {
        if (equal(tok, "{"))
            brace_depth++;
        else if (equal(tok, "}")) {
            brace_depth--;
            if (brace_depth == 0) {
                body_end = tok->next;
                break;
            }
        }
        tok = tok->next;
    }

    // Copy tokens from start to body_end
    Token head = {};
    Token *cur = &head;
    for (Token *t = body_start; t != body_end && t->kind != TK_EOF;
         t = t->next) {
        cur = cur->next = copy_token(vm, t);
    }
    cur->next = new_eof(vm, body_end ? body_end : tok);

    // Convert preprocessor tokens to parser tokens (TK_PP_NUM -> TK_NUM, etc.)
    convert_pp_tokens(vm, head.next);

    // Detect void return type: check whether the return-type token is 'void'
    // without a following '*' (which would be a void* pointer, not void return).
    bool is_void_macro = false;
    {
        Token *t = start;
        if (t && t->kind == TK_IDENT && t->len == 4 &&
            strncmp(t->loc, "void", 4) == 0) {
            Token *after = t->next;
            if (after && !equal(after, "*"))
                is_void_macro = true;
        }
    }

    // Create MacroFn entry
    MacroFn *pm =
        arena_alloc(&vm->compiler.parser_arena, sizeof(MacroFn));
    memset(pm, 0, sizeof(MacroFn));
    pm->name = name;
    pm->body_tokens = head.next;
    pm->compiled_fn = NULL;
    pm->is_compiled = false;
    pm->is_macro_entry = is_macro_entry;
    pm->is_inline = is_inline;
    pm->is_void_macro = is_void_macro;
    pm->next = vm->compiler.macro_fns;
    vm->compiler.macro_fns = pm;

    if (vm->debug_vm)
        printf("Captured %s macro function '%s'\n",
               is_macro_entry ? "macro" : "comptime", name);

    // Return token after the function
    return body_end ? body_end : tok;
}

// Extract a [[jcc::comptime]] variable declaration (not a function).
// Extracts tokens up to and including the terminating ';', creates a
// ComptimeVar entry, and returns the token after the ';'.
static Token *extract_comptime_var(JCC *vm, Token *tok) {
    Token *start = tok;

    // Find the variable name: the last identifier before '=' or ';' at depth 0.
    char *name = NULL;
    {
        Token *probe = tok;
        Token *last_ident = NULL;
        int brace_depth = 0, bracket_depth = 0;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, "{")) brace_depth++;
            else if (equal(probe, "}")) brace_depth--;
            else if (equal(probe, "[")) bracket_depth++;
            else if (equal(probe, "]")) bracket_depth--;
            else if (brace_depth == 0 && bracket_depth == 0) {
                if (equal(probe, "=") || equal(probe, ";"))
                    break;
                if (probe->kind == TK_IDENT)
                    last_ident = probe;
            }
            probe = probe->next;
        }
        if (!last_ident) {
            error_tok(vm, start,
                      "__attribute__((comptime)): expected variable name");
            return start;
        }
        name = arena_alloc(&vm->compiler.parser_arena, last_ident->len + 1);
        memcpy(name, last_ident->loc, last_ident->len);
        name[last_ident->len] = '\0';
    }

    // Reject pointer/string comptime vars: these create relocations which
    // the macro program's data segment does not support (ticket #188 scope).
    // Check for '*' at depth 0 before the variable name.
    {
        Token *probe = tok;
        int depth = 0;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, "{")) depth++;
            else if (equal(probe, "}")) depth--;
            else if (depth == 0) {
                if (equal(probe, "=") || equal(probe, ";")) break;
                if (equal(probe, "*")) {
                    error_tok(vm, probe,
                              "__attribute__((comptime)): pointer/string variables "
                              "are not supported yet (ticket #188 scope: "
                              "int/float/struct only)");
                    return start;
                }
            }
            probe = probe->next;
        }
    }

    // Extract tokens up to and including the terminating ';'.
    Token head = {};
    Token *cur = &head;
    Token *body_end = NULL;
    {
        int brace_depth = 0;
        for (Token *t = start; t && t->kind != TK_EOF; t = t->next) {
            if (equal(t, "{")) brace_depth++;
            else if (equal(t, "}")) brace_depth--;
            cur = cur->next = copy_token(vm, t);
            if (brace_depth == 0 && equal(t, ";")) {
                body_end = t->next;
                break;
            }
        }
    }
    if (!body_end) {
        error_tok(vm, start,
                  "__attribute__((comptime)): variable declaration not terminated");
        return start;
    }
    cur->next = new_eof(vm, body_end);
    convert_pp_tokens(vm, head.next);

    ComptimeVar *cv =
        arena_alloc(&vm->compiler.parser_arena, sizeof(ComptimeVar));
    memset(cv, 0, sizeof(ComptimeVar));
    cv->name = name;
    cv->decl_tokens = head.next;
    cv->next = vm->compiler.comptime_vars;
    vm->compiler.comptime_vars = cv;

    if (vm->debug_vm)
        printf("Captured comptime var '%s'\n", name);

    return body_end;
}

static Hideset *new_hideset(JCC *vm, char *name) {
    Hideset *hs = arena_alloc(&vm->compiler.parser_arena, sizeof(Hideset));
    memset(hs, 0, sizeof(Hideset));
    hs->name = name;
    return hs;
}

static bool hideset_contains(Hideset *hs, char *s, int len);

static Hideset *hideset_union(JCC *vm, Hideset *hs1, Hideset *hs2) {
    Hideset head = {};
    Hideset *cur = &head;

    for (; hs1; hs1 = hs1->next)
        if (!hideset_contains(head.next, hs1->name, strlen(hs1->name)))
            cur = cur->next = new_hideset(vm, hs1->name);
    for (; hs2; hs2 = hs2->next)
        if (!hideset_contains(head.next, hs2->name, strlen(hs2->name)))
            cur = cur->next = new_hideset(vm, hs2->name);
    return head.next;
}

static bool hideset_contains(Hideset *hs, char *s, int len) {
    for (; hs; hs = hs->next)
        if (strlen(hs->name) == len && !strncmp(hs->name, s, len))
            return true;
    return false;
}

static Hideset *hideset_intersection(JCC *vm, Hideset *hs1, Hideset *hs2) {
    Hideset head = {};
    Hideset *cur = &head;

    for (; hs1; hs1 = hs1->next)
        if (hideset_contains(hs2, hs1->name, strlen(hs1->name)))
            cur = cur->next = new_hideset(vm, hs1->name);
    return head.next;
}

static Token *add_hideset(JCC *vm, Token *tok, Hideset *hs) {
    Token head = {};
    Token *cur = &head;

    for (; tok; tok = tok->next) {
        Token *t = copy_token(vm, tok);
        t->hideset = hideset_union(vm, t->hideset, hs);
        cur = cur->next = t;
    }
    return head.next;
}

// Append tok2 to the end of tok1.
static Token *append(JCC *vm, Token *tok1, Token *tok2) {
    if (tok1->kind == TK_EOF)
        return tok2;

    Token head = {};
    Token *cur = &head;

    for (; tok1->kind != TK_EOF; tok1 = tok1->next)
        cur = cur->next = copy_token(vm, tok1);
    cur->next = tok2;
    return head.next;
}

static Token *skip_cond_incl2(JCC *vm, Token *tok, int depth) {
    if (depth > MAX_PP_NESTING)
        error_tok(vm, tok, "too many nested conditional includes");

    while (tok->kind != TK_EOF) {
        if (is_hash(tok) &&
            (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
             equal(tok->next, "ifndef"))) {
            tok = skip_cond_incl2(vm, tok->next->next, depth + 1);
            continue;
        }
        if (is_hash(tok) && equal(tok->next, "endif"))
            return tok->next->next;
        tok = tok->next;
    }
    return tok;
}

// Skip until next `#else`, `#elif` or `#endif`.
// Nested `#if` and `#endif` are skipped.
static Token *skip_cond_incl(JCC *vm, Token *tok) {
    while (tok->kind != TK_EOF) {
        if (is_hash(tok) &&
            (equal(tok->next, "if") || equal(tok->next, "ifdef") ||
             equal(tok->next, "ifndef"))) {
            tok = skip_cond_incl2(vm, tok->next->next, 0);
            continue;
        }

        if (is_hash(tok) &&
            (equal(tok->next, "elif") || equal(tok->next, "elifdef") ||
             equal(tok->next, "elifndef") || equal(tok->next, "else") ||
             equal(tok->next, "endif")))
            break;
        tok = tok->next;
    }
    return tok;
}

// Double-quote a given string and returns it.
static char *quote_string(JCC *vm, char *str) {
    int bufsize = 3;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            bufsize++;
        bufsize++;
    }

    char *buf = arena_alloc(&vm->compiler.parser_arena, bufsize);
    memset(buf, 0, bufsize);
    char *p = buf;
    *p++ = '"';
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            *p++ = '\\';
        *p++ = str[i];
    }
    *p++ = '"';
    *p++ = '\0';
    return buf;
}

static Token *new_str_token(JCC *vm, char *str, Token *tmpl) {
    char *buf = quote_string(vm, str);
    return tokenize(vm,
                    new_file(vm, tmpl->file->name, tmpl->file->file_no, buf));
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(JCC *vm, Token **rest, Token *tok) {
    Token head = {};
    Token *cur = &head;

    for (; !tok->at_bol; tok = tok->next)
        cur = cur->next = copy_token(vm, tok);

    cur->next = new_eof(vm, tok);
    *rest = tok;
    return head.next;
}

static Token *new_num_token(JCC *vm, int val, Token *tmpl) {
    char *buf = arena_format(vm, "%d\n", val);
    return tokenize(vm,
                    new_file(vm, tmpl->file->name, tmpl->file->file_no, buf));
}

// Generate comma-separated token sequence from binary data
static Token *generate_embed_tokens(JCC *vm, unsigned char *data, size_t size,
                                    Token *tmpl) {
    if (size == 0)
        return NULL;

    Token head = {};
    Token *cur = &head;

    for (size_t i = 0; i < size; i++) {
        // Create numeric token for this byte
        Token *num_stream = new_num_token(vm, data[i], tmpl);
        // Only take the first token (the number), not EOF
        Token *num = copy_token(vm, num_stream);
        num->next = NULL;
        cur = cur->next = num;

        // Add comma separator (except after last byte)
        if (i < size - 1) {
            Token *comma = copy_token(vm, tmpl);
            comma->kind = TK_PUNCT;
            comma->len = 1;
            comma->loc = ",";
            cur = cur->next = comma;
        }
    }

    return head.next;
}

// Helper: Check if token list ends with a comma
static bool ends_with_comma(Token *tok) {
    if (!tok)
        return false;

    // Find last token
    Token *last = tok;
    while (last->next)
        last = last->next;

    return last->kind == TK_PUNCT && last->len == 1 && last->loc[0] == ',';
}

// Helper: Check if token list starts with a comma
static bool starts_with_comma(Token *tok) {
    if (!tok)
        return false;

    return tok->kind == TK_PUNCT && tok->len == 1 && tok->loc[0] == ',';
}

// Helper: Create a comma token
static Token *make_comma_token(JCC *vm, Token *tmpl) {
    Token *comma = copy_token(vm, tmpl);
    comma->kind = TK_PUNCT;
    comma->len = 1;
    comma->loc = ",";
    comma->next = NULL;
    return comma;
}

// Helper: Append tokens to current position, updating file/line info
static Token *append_tokens(JCC *vm, Token *cur, Token *tokens, Token *tmpl) {
    for (Token *t = tokens; t; t = t->next) {
        Token *copy = copy_token(vm, t);
        if (tmpl) {
            copy->file = tmpl->file;
            copy->line_no = tmpl->line_no;
        }
        copy->next = NULL;
        cur = cur->next = copy;
    }
    return cur;
}

// Helper: Copy entire token list with updated source location
static Token *copy_token_list(JCC *vm, Token *tokens, Token *tmpl) {
    if (!tokens)
        return NULL;

    Token head = {};
    Token *cur = &head;

    cur = append_tokens(vm, cur, tokens, tmpl);

    return head.next;
}

// Generate #embed tokens with prefix, suffix, and if_empty support
static Token *
generate_embed_tokens_with_params(JCC *vm, unsigned char *data, size_t size,
                                  Token *prefix_tokens, Token *suffix_tokens,
                                  Token *if_empty_tokens, Token *tmpl) {
    // If empty, use if_empty tokens (ignore prefix/suffix)
    if (size == 0) {
        return if_empty_tokens ? copy_token_list(vm, if_empty_tokens, tmpl)
                               : NULL;
    }

    // Non-empty: assemble prefix + bytes + suffix
    Token head = {};
    Token *cur = &head;

    // Add prefix tokens
    if (prefix_tokens) {
        cur = append_tokens(vm, cur, prefix_tokens, tmpl);
        // Add comma if prefix doesn't end with one
        if (!ends_with_comma(prefix_tokens)) {
            cur = cur->next = make_comma_token(vm, tmpl);
        }
    }

    // Add byte tokens
    Token *byte_tokens = generate_embed_tokens(vm, data, size, tmpl);
    if (byte_tokens) {
        for (Token *t = byte_tokens; t; t = t->next) {
            cur = cur->next = t;
        }
    }

    // Add suffix tokens
    if (suffix_tokens) {
        // Add comma if suffix doesn't start with one and bytes don't end with
        // one
        if (!starts_with_comma(suffix_tokens) && byte_tokens &&
            !ends_with_comma(byte_tokens)) {
            cur = cur->next = make_comma_token(vm, tmpl);
        }
        cur = append_tokens(vm, cur, suffix_tokens, tmpl);
    }

    return head.next;
}

static Token *read_const_expr(JCC *vm, Token **rest, Token *tok) {
    tok = copy_line(vm, rest, tok);

    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // "defined(foo)" or "defined foo" becomes "1" if macro "foo"
        // is defined. Otherwise "0".
        if (equal(tok, "defined")) {
            Token *start = tok;
            bool has_paren = consume(vm, &tok, tok->next, "(");

            if (tok->kind != TK_IDENT)
                error_tok(vm, start, "macro name must be an identifier");
            Macro *m = find_macro(vm, tok);
            tok = tok->next;

            if (has_paren)
                tok = skip(vm, tok, ")");

            cur = cur->next = new_num_token(vm, m ? 1 : 0, start);
            continue;
        }

        // "__has_embed(filename)" returns 0 (not found), 1 (non-empty), or 2
        // (empty)
        if (equal(tok, "__has_embed")) {
            Token *start = tok;
            tok = skip(vm, tok->next, "(");

            // Parse filename
            bool is_dquote;
            int filename_len;
            char *filename =
                read_include_filename(vm, &tok, tok, &is_dquote, &filename_len);

            tok = skip(vm, tok, ")");

            // Resolve file path
            char *path = NULL;

            if (filename[0] == '/') {
                path = filename;
            } else if (is_dquote) {
                char *relative_path =
                    format_relative_path(vm, start->file->name, filename);
                if (file_exists(relative_path)) {
                    path = relative_path;
                }
            }

            if (!path) {
                path = search_include_paths(vm, filename, filename_len,
                                            !is_dquote);
            }

            // Determine result: 0 = not found, 1 = non-empty, 2 = empty
            int result = 0;
            if (path && file_exists(path)) {
                size_t file_size;
                unsigned char *data = read_binary_file(vm, path, &file_size);
                if (data) {
                    result = (file_size == 0) ? 2 : 1;
                }
            }

            cur = cur->next = new_num_token(vm, result, start);
            continue;
        }

        cur = cur->next = tok;
        tok = tok->next;
    }

    cur->next = tok;
    return head.next;
}

// Read and evaluate a constant expression.
static long eval_const_expr(JCC *vm, Token **rest, Token *tok) {
    Token *start = tok;
    Token *expr = read_const_expr(vm, rest, tok->next);
    expr = preprocess2(vm, expr);

    if (expr->kind == TK_EOF)
        error_tok(vm, start, "no expression");

    // [https://www.sigbus.info/n1570#6.10.1p4] The standard requires
    // we replace remaining non-macro identifiers with "0" before
    // evaluating a constant expression. For example, `#if foo` is
    // equivalent to `#if 0` if foo is not defined.
    for (Token *t = expr; t->kind != TK_EOF; t = t->next) {
        if (t->kind == TK_IDENT) {
            Token *next = t->next;
            *t = *new_num_token(vm, 0, t);
            t->next = next;
        }
    }

    // Convert pp-numbers to regular numbers
    convert_pp_tokens(vm, expr);

    Token *rest2;
    long val = const_expr(vm, &rest2, expr);
    if (rest2->kind != TK_EOF)
        error_tok(vm, rest2, "extra tokens after #if expression");
    return val;
}

static CondIncl *push_cond_incl(JCC *vm, Token *tok, bool included) {
    CondIncl *ci = arena_alloc(&vm->compiler.parser_arena, sizeof(CondIncl));
    memset(ci, 0, sizeof(CondIncl));
    ci->next = vm->compiler.cond_incl;
    ci->ctx = IN_THEN;
    ci->tok = tok;
    ci->included = included;
    vm->compiler.cond_incl = ci;
    return ci;
}

static Macro *find_macro(JCC *vm, Token *tok) {
    if (tok->kind != TK_IDENT)
        return NULL;
    return hashmap_get2(&vm->compiler.macros, tok->loc, tok->len);
}

static Macro *add_macro(JCC *vm, char *name, int name_len, bool is_objlike,
                        Token *body) {
    Macro *m = arena_alloc(&vm->compiler.parser_arena, sizeof(Macro));
    memset(m, 0, sizeof(Macro));
    m->name = name;
    m->is_objlike = is_objlike;
    m->body = body;
    hashmap_put2(&vm->compiler.macros, name, name_len, m);
    return m;
}

static MacroParam *read_macro_params(JCC *vm, Token **rest, Token *tok,
                                     char **va_args_name) {
    MacroParam head = {};
    MacroParam *cur = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        if (equal(tok, "...")) {
            *va_args_name = "__VA_ARGS__";
            *rest = skip(vm, tok->next, ")");
            return head.next;
        }

        if (tok->kind != TK_IDENT)
            error_tok(vm, tok, "expected an identifier");

        if (equal(tok->next, "...")) {
            *va_args_name = arena_strndup(vm, tok->loc, tok->len);
            *rest = skip(vm, tok->next->next, ")");
            return head.next;
        }

        MacroParam *m =
            arena_alloc(&vm->compiler.parser_arena, sizeof(MacroParam));
        memset(m, 0, sizeof(MacroParam));
        m->name = arena_strndup(vm, tok->loc, tok->len);
        cur = cur->next = m;
        tok = tok->next;
    }

    *rest = tok->next;
    return head.next;
}

static void read_macro_definition(JCC *vm, Token **rest, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "macro name must be an identifier");
    char *name = arena_strndup(vm, tok->loc, tok->len);
    int name_len = tok->len; // Save name length before moving tok
    tok = tok->next;

    if (!tok->has_space && equal(tok, "(")) {
        // Function-like macro
        char *va_args_name = NULL;
        MacroParam *params =
            read_macro_params(vm, &tok, tok->next, &va_args_name);

        Macro *m =
            add_macro(vm, name, name_len, false, copy_line(vm, rest, tok));
        m->params = params;
        m->va_args_name = va_args_name;
    } else {
        // Object-like macro
        add_macro(vm, name, name_len, true, copy_line(vm, rest, tok));
    }
}

static MacroArg *read_macro_arg_one(JCC *vm, Token **rest, Token *tok,
                                    bool read_rest) {
    Token head = {};
    Token *cur = &head;
    int level = 0;

    for (;;) {
        if (level == 0 && equal(tok, ")"))
            break;
        if (level == 0 && !read_rest && equal(tok, ","))
            break;

        if (tok->kind == TK_EOF)
            error_tok(vm, tok, "premature end of input in macro argument list");

        if (equal(tok, "("))
            level++;
        else if (equal(tok, ")"))
            level--;

        cur = cur->next = copy_token(vm, tok);
        tok = tok->next;
    }

    cur->next = new_eof(vm, tok);

    MacroArg *arg = arena_alloc(&vm->compiler.parser_arena, sizeof(MacroArg));
    memset(arg, 0, sizeof(MacroArg));
    arg->tok = head.next;
    *rest = tok;
    return arg;
}

static MacroArg *read_macro_args(JCC *vm, Token **rest, Token *tok,
                                 MacroParam *params, char *va_args_name) {
    Token *start = tok;
    tok = tok->next->next;

    MacroArg head = {};
    MacroArg *cur = &head;

    MacroParam *pp = params;
    for (; pp; pp = pp->next) {
        if (cur != &head)
            tok = skip(vm, tok, ",");
        cur = cur->next = read_macro_arg_one(vm, &tok, tok, false);
        cur->name = pp->name;
    }

    if (va_args_name) {
        MacroArg *arg;
        if (equal(tok, ")")) {
            arg = arena_alloc(&vm->compiler.parser_arena, sizeof(MacroArg));
            memset(arg, 0, sizeof(MacroArg));
            arg->tok = new_eof(vm, tok);
        } else {
            if (pp != params)
                tok = skip(vm, tok, ",");
            arg = read_macro_arg_one(vm, &tok, tok, true);
        }
        arg->name = va_args_name;
        ;
        arg->is_va_args = true;
        cur = cur->next = arg;
    } else if (pp) {
        error_tok(vm, start, "too many arguments to macro '%.*s'",
                  start->len, start->loc);
    }

    skip(vm, tok, ")");
    *rest = tok;
    return head.next;
}

static MacroArg *find_arg(MacroArg *args, Token *tok) {
    for (MacroArg *ap = args; ap; ap = ap->next)
        if (tok->len == strlen(ap->name) &&
            !strncmp(tok->loc, ap->name, tok->len))
            return ap;
    return NULL;
}

// Concatenates all tokens in `tok` and returns a new string.
static char *join_tokens(JCC *vm, Token *tok, Token *end, int *out_len) {
    // Compute the length of the resulting token.
    int len = 1;
    for (Token *t = tok; t != end && t->kind != TK_EOF; t = t->next) {
        if (t != tok && t->has_space)
            len++;
        len += t->len;
    }

    char *buf = arena_alloc(&vm->compiler.parser_arena, len);
    memset(buf, 0, len);

    // Copy token texts.
    int pos = 0;
    for (Token *t = tok; t != end && t->kind != TK_EOF; t = t->next) {
        if (t != tok && t->has_space)
            buf[pos++] = ' ';
        strncpy(buf + pos, t->loc, t->len);
        pos += t->len;
    }
    buf[pos] = '\0';
    if (out_len)
        *out_len = pos;
    return buf;
}

// Concatenates all tokens in `arg` and returns a new string token.
// This function is used for the stringizing operator (#).
static Token *stringize(JCC *vm, Token *hash, Token *arg) {
    // Create a new string token. We need to set some value to its
    // source location for error reporting function, so we use a macro
    // name token as a template.
    char *s = join_tokens(vm, arg, NULL, NULL);
    return new_str_token(vm, s, hash);
}

// Concatenate two tokens to create a new token.
static Token *paste(JCC *vm, Token *lhs, Token *rhs) {
    // Paste the two tokens.
    char *buf =
        arena_format(vm, "%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);

    // Tokenize the resulting string.
    Token *tok =
        tokenize(vm, new_file(vm, lhs->file->name, lhs->file->file_no, buf));
    if (tok->next->kind != TK_EOF)
        error_tok(vm, lhs, "pasting forms '%s', an invalid token", buf);
    return tok;
}

static bool has_varargs(MacroArg *args) {
    for (MacroArg *ap = args; ap; ap = ap->next)
        if (strncmp(ap->name, "__VA_ARGS__", sizeof("__VA_ARGS__")) == 0)
            return ap->tok->kind != TK_EOF;
    return false;
}

// Replace func-like macro parameters with given arguments.
static Token *subst(JCC *vm, Token *tok, MacroArg *args) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // "#" followed by a parameter is replaced with stringized actuals.
        if (equal(tok, "#")) {
            MacroArg *arg = find_arg(args, tok->next);
            if (!arg)
                error_tok(vm, tok->next,
                          "'#' is not followed by a macro parameter");
            cur = cur->next = stringize(vm, tok, arg->tok);
            tok = tok->next->next;
            continue;
        }

        // [GNU] If __VA_ARG__ is empty, `,##__VA_ARGS__` is expanded
        // to the empty token list. Otherwise, its expaned to `,` and
        // __VA_ARGS__.
        if (equal(tok, ",") && equal(tok->next, "##")) {
            MacroArg *arg = find_arg(args, tok->next->next);
            if (arg && arg->is_va_args) {
                if (arg->tok->kind == TK_EOF) {
                    tok = tok->next->next->next;
                } else {
                    cur = cur->next = copy_token(vm, tok);
                    tok = tok->next->next;
                }
                continue;
            }
        }

        if (equal(tok, "##")) {
            if (cur == &head)
                error_tok(vm, tok,
                          "'##' cannot appear at start of macro expansion");

            if (tok->next->kind == TK_EOF)
                error_tok(vm, tok,
                          "'##' cannot appear at end of macro expansion");

            MacroArg *arg = find_arg(args, tok->next);
            if (arg) {
                if (arg->tok->kind != TK_EOF) {
                    *cur = *paste(vm, cur, arg->tok);
                    for (Token *t = arg->tok->next; t->kind != TK_EOF;
                         t = t->next)
                        cur = cur->next = copy_token(vm, t);
                }
                tok = tok->next->next;
                continue;
            }

            *cur = *paste(vm, cur, tok->next);
            tok = tok->next->next;
            continue;
        }

        MacroArg *arg = find_arg(args, tok);

        if (arg && equal(tok->next, "##")) {
            Token *rhs = tok->next->next;

            if (arg->tok->kind == TK_EOF) {
                MacroArg *arg2 = find_arg(args, rhs);
                if (arg2) {
                    for (Token *t = arg2->tok; t->kind != TK_EOF; t = t->next)
                        cur = cur->next = copy_token(vm, t);
                } else {
                    cur = cur->next = copy_token(vm, rhs);
                }
                tok = rhs->next;
                continue;
            }

            for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next)
                cur = cur->next = copy_token(vm, t);
            tok = tok->next;
            continue;
        }

        // If __VA_ARG__ is empty, __VA_OPT__(x) is expanded to the
        // empty token list. Otherwise, __VA_OPT__(x) is expanded to x.
        if (equal(tok, "__VA_OPT__") && equal(tok->next, "(")) {
            MacroArg *arg = read_macro_arg_one(vm, &tok, tok->next->next, true);
            if (has_varargs(args)) {
                // Manually substitute parameters in __VA_OPT__ content
                for (Token *t = arg->tok; t->kind != TK_EOF; t = t->next) {
                    MacroArg *a = find_arg(args, t);
                    if (a) {
                        // Expand and copy the parameter's tokens
                        Token *expanded = preprocess2(vm, a->tok);
                        for (Token *e = expanded; e->kind != TK_EOF;
                             e = e->next)
                            cur = cur->next = copy_token(vm, e);
                    } else {
                        // Not a parameter, just copy the token
                        cur = cur->next = copy_token(vm, t);
                    }
                }
            }
            tok = skip(vm, tok, ")");
            continue;
        }

        // Handle a macro token. Macro arguments are completely macro-expanded
        // before they are substituted into a macro body.
        if (arg) {
            Token *t = preprocess2(vm, arg->tok);
            t->at_bol = tok->at_bol;
            t->has_space = tok->has_space;
            for (; t->kind != TK_EOF; t = t->next)
                cur = cur->next = copy_token(vm, t);
            tok = tok->next;
            continue;
        }

        // Handle a non-macro token.
        cur = cur->next = copy_token(vm, tok);
        tok = tok->next;
        continue;
    }

    cur->next = tok;
    return head.next;
}

// If tok is a macro, expand it and return true.
// Otherwise, do nothing and return false.
static bool expand_macro(JCC *vm, Token **rest, Token *tok) {
    if (hideset_contains(tok->hideset, tok->loc, tok->len))
        return false;

    Macro *m = find_macro(vm, tok);
    if (!m)
        return false;

    // Built-in dynamic macro application such as __LINE__
    if (m->handler) {
        *rest = m->handler(vm, tok);
        (*rest)->next = tok->next;
        return true;
    }

    // Object-like macro application
    if (m->is_objlike) {
        Hideset *hs = hideset_union(vm, tok->hideset, new_hideset(vm, m->name));
        Token *body = add_hideset(vm, m->body, hs);
        for (Token *t = body; t->kind != TK_EOF; t = t->next)
            t->origin = tok;
        *rest = append(vm, body, tok->next);
        (*rest)->at_bol = tok->at_bol;
        (*rest)->has_space = tok->has_space;
        return true;
    }

    // If a funclike macro token is not followed by an argument list,
    // treat it as a normal identifier.
    if (!equal(tok->next, "("))
        return false;

    // Function-like macro application
    Token *macro_token = tok;
    MacroArg *args = read_macro_args(vm, &tok, tok, m->params, m->va_args_name);
    Token *rparen = tok;

    // Tokens that consist a func-like macro invocation may have different
    // hidesets, and if that's the case, it's not clear what the hideset
    // for the new tokens should be. We take the interesection of the
    // macro token and the closing parenthesis and use it as a new hideset
    // as explained in the Dave Prossor's algorithm.
    Hideset *hs =
        hideset_intersection(vm, macro_token->hideset, rparen->hideset);
    hs = hideset_union(vm, hs, new_hideset(vm, m->name));

    Token *body = subst(vm, m->body, args);
    body = add_hideset(vm, body, hs);
    for (Token *t = body; t->kind != TK_EOF; t = t->next)
        t->origin = macro_token;
    *rest = append(vm, body, tok->next);
    (*rest)->at_bol = macro_token->at_bol;
    (*rest)->has_space = macro_token->has_space;
    return true;
}

static bool file_exists(char *path) {
    struct stat st;
    return !stat(path, &st);
}

static char *format_relative_path(JCC *vm, char *base_file, char *filename) {
    char *slash = strrchr(base_file, '/');
    if (!slash)
        return arena_format(vm, "./%s", filename);
    return arena_format(vm, "%.*s/%s", (int)(slash - base_file), base_file,
                        filename);
}

static bool is_standard_header(const char *filename) {
    static const char *std_headers[] = {
        "assert.h", "complex.h",      "ctype.h",     "errno.h",  "float.h",  "inttypes.h",
        "limits.h", "math.h",         "setjmp.h",    "stdarg.h", "stdbool.h",
        "stddef.h", "stdint.h",       "stdio.h",     "stdlib.h", "string.h",
        "time.h",   "Availability.h", "sys/cdefs.h", NULL};

    for (int i = 0; std_headers[i]; i++) {
        if (strlen(filename) == strlen(std_headers[i]) &&
            strncmp(filename, std_headers[i], strlen(std_headers[i])) == 0)
            return true;
    }
    return false;
}

char *search_include_paths(JCC *vm, char *filename, int filename_len,
                           bool is_system) {
    if (filename[0] == '/')
        return filename;

    char *cached =
        hashmap_get2(&vm->compiler.include_cache, filename, filename_len);
    if (cached)
        return cached;

    // - If use_system_headers is enabled: only force for VM-required headers
    // (stdarg.h, setjmp.h)
    // - Otherwise: force for all standard C library headers
    bool force_jcc_headers = is_standard_header(filename);

    // For <...> includes:
    //   - Standard headers: search include_paths (JCC's headers)
    //   - Other headers: search system_include_paths (requires --isystem)
    // For "..." includes: search include_paths
    StringArray *paths;
    if (force_jcc_headers || !is_system) {
        paths = &vm->compiler.include_paths;
    } else {
        paths = &vm->compiler.system_include_paths;
    }

    // Search a file from the include paths.
    for (int i = 0; i < paths->len; i++) {
        char *path = format("%s/%s", paths->data[i], filename);
        if (!file_exists(path)) {
            free(path);
            continue;
        }
        hashmap_put2(&vm->compiler.include_cache, filename, filename_len, path);
        vm->compiler.include_next_idx = i + 1;
        return path;
    }
    return NULL;
}

static char *search_include_next(JCC *vm, char *filename) {
    // First search include_paths
    for (; vm->compiler.include_next_idx < vm->compiler.include_paths.len;
         vm->compiler.include_next_idx++) {
        char *path = arena_format(
            vm,
            "%s/%s",
            vm->compiler.include_paths.data[vm->compiler.include_next_idx],
            filename);
        if (file_exists(path))
            return path;
    }
    // Then search system_include_paths (needed for #include_next from JCC
    // wrapper headers)
    int sys_idx =
        vm->compiler.include_next_idx - vm->compiler.include_paths.len;
    for (; sys_idx < vm->compiler.system_include_paths.len; sys_idx++) {
        char *path = arena_format(
            vm, "%s/%s", vm->compiler.system_include_paths.data[sys_idx],
            filename);
        if (file_exists(path))
            return path;
    }
    return NULL;
}

// Read an #include argument.
static char *read_include_filename(JCC *vm, Token **rest, Token *tok,
                                   bool *is_dquote, int *out_len) {
    // Pattern 1: #include "foo.h" or __has_embed("foo")
    if (tok->kind == TK_STR) {
        // A double-quoted filename for #include is a special kind of
        // token, and we don't want to interpret any escape sequences in it.
        // For example, "\f" in "C:\foo" is not a formfeed character but
        // just two non-control characters, backslash and f.
        // So we don't want to use token->str.
        *is_dquote = true;
        *rest = tok->next;
        if (out_len)
            *out_len = tok->len - 2;
        return arena_strndup(vm, tok->loc + 1, tok->len - 2);
    }

    // Pattern 2: #include <foo.h> or __has_embed(<foo>)
    if (equal(tok, "<")) {
        // Reconstruct a filename from a sequence of tokens between
        // "<" and ">".
        Token *start = tok;

        // Find closing ">".
        for (; !equal(tok, ">"); tok = tok->next)
            if (tok->at_bol || tok->kind == TK_EOF)
                error_tok(vm, tok, "expected '>' after include filename");

        *is_dquote = false;
        *rest = tok->next;
        return join_tokens(vm, start->next, tok, out_len);
    }

    // Pattern 3: #include FOO
    // In this case FOO must be macro-expanded to either
    // a single string token or a sequence of "<" ... ">".
    if (tok->kind == TK_IDENT) {
        Token *tok2 = preprocess2(vm, copy_line(vm, rest, tok));
        return read_include_filename(vm, &tok2, tok2, is_dquote, out_len);
    }

    error_tok(vm, tok, "expected a filename for include directive");
    return NULL;
}

// Detect the following "include guard" pattern.
//
//   #ifndef FOO_H
//   #define FOO_H
//   ...
//   #endif
static char *detect_include_guard(JCC *vm, Token *tok) {
    // Detect the first two lines.
    if (!is_hash(tok) || !equal(tok->next, "ifndef"))
        return NULL;
    tok = tok->next->next;

    if (tok->kind != TK_IDENT)
        return NULL;

    char *macro = arena_strndup(vm, tok->loc, tok->len);
    tok = tok->next;

    if (!is_hash(tok) || !equal(tok->next, "define") ||
        !equal(tok->next->next, macro))
        return NULL;

    // Read until the end of the file.
    while (tok->kind != TK_EOF) {
        if (!is_hash(tok)) {
            tok = tok->next;
            continue;
        }

        if (equal(tok->next, "endif") && tok->next->next->kind == TK_EOF)
            return macro;

        if (equal(tok, "if") || equal(tok, "ifdef") || equal(tok, "ifndef"))
            tok = skip_cond_incl(vm, tok->next);
        else
            tok = tok->next;
    }
    return NULL;
}

// Register stdlib functions for a specific header
// Called automatically when a standard header is #include'd
static void register_stdlib_for_header(JCC *vm, const char *header_name) {
    // Check if already registered
    if (hashmap_get(&vm->compiler.included_headers, header_name)) {
        return; // Already registered
    }

    // Mark as registered
    hashmap_put(&vm->compiler.included_headers, header_name, (void *)1);

    // Dispatch to appropriate registration function
    if (strncmp(header_name, "ctype.h", sizeof("ctype.h")) == 0) {
        register_ctype_functions(vm);
    } else if (strncmp(header_name, "dlfcn.h", sizeof("dlfcn.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "fcntl.h", sizeof("fcntl.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "fenv.h", sizeof("fenv.h")) == 0) {
        register_fenv_functions(vm);
    } else if (strncmp(header_name, "locale.h", sizeof("locale.h")) == 0) {
        register_locale_functions(vm);
    } else if (strncmp(header_name, "math.h", sizeof("math.h")) == 0) {
        register_math_functions(vm);
    } else if (strncmp(header_name, "signal.h", sizeof("signal.h")) == 0) {
        register_signal_functions(vm);
    } else if (strncmp(header_name, "stdio.h", sizeof("stdio.h")) == 0) {
        register_stdio_functions(vm);
    } else if (strncmp(header_name, "stdlib.h", sizeof("stdlib.h")) == 0) {
        register_stdlib_functions(vm);
    } else if (strncmp(header_name, "string.h", sizeof("string.h")) == 0) {
        register_string_functions(vm);
    } else if (strncmp(header_name, "time.h", sizeof("time.h")) == 0) {
        register_time_functions(vm);
    } else if (strncmp(header_name, "uchar.h", sizeof("uchar.h")) == 0) {
        register_wide_functions(vm);
    } else if (strncmp(header_name, "unistd.h", sizeof("unistd.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "wchar.h", sizeof("wchar.h")) == 0) {
        register_wide_functions(vm);
    } else if (strncmp(header_name, "wctype.h", sizeof("wctype.h")) == 0) {
        register_wide_functions(vm);
    } else if (strncmp(header_name, "strings.h", sizeof("strings.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "libgen.h", sizeof("libgen.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "fnmatch.h", sizeof("fnmatch.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "getopt.h", sizeof("getopt.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "poll.h", sizeof("poll.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "sys/wait.h", sizeof("sys/wait.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "sys/time.h", sizeof("sys/time.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "sys/mman.h", sizeof("sys/mman.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "sys/stat.h", sizeof("sys/stat.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "utime.h", sizeof("utime.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "arpa/inet.h", sizeof("arpa/inet.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "dirent.h", sizeof("dirent.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "glob.h", sizeof("glob.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "grp.h", sizeof("grp.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "netdb.h", sizeof("netdb.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "netinet/in.h", sizeof("netinet/in.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "in.h", sizeof("in.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "pwd.h", sizeof("pwd.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "regex.h", sizeof("regex.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "sys/socket.h", sizeof("sys/socket.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "socket.h", sizeof("socket.h")) == 0) {
        register_posix_functions(vm);
    } else if (strncmp(header_name, "termios.h", sizeof("termios.h")) == 0) {
        register_posix_functions(vm);
    }
    // Note: Other headers (like stddef.h, stdbool.h, sys/types.h, etc.) don't have runtime
    // functions
}

static Token *include_file(JCC *vm, Token *tok, char *path,
                           Token *filename_tok) {
    // Check for "#pragma once"
    if (hashmap_get(&vm->compiler.pragma_once, path))
        return tok;

    // If we read the same file before, and if the file was guarded
    // by the usual #ifndef ... #endif pattern, we may be able to
    // skip the file without opening it.
    char *guard_name = hashmap_get(&vm->compiler.include_guards, path);
    if (guard_name && hashmap_get(&vm->compiler.macros, guard_name))
        return tok;

    Token *tok2 = tokenize_file(vm, path);
    if (!tok2)
        error_tok(vm, filename_tok, "%s: cannot open file: %s", path,
                  strerror(errno));

    // Register stdlib functions for standard headers (header-based lazy
    // loading)
    char *basename = strrchr(path, '/');
    basename = basename ? basename + 1 : path;
    register_stdlib_for_header(vm, basename);

    guard_name = detect_include_guard(vm, tok2);
    if (guard_name)
        hashmap_put(&vm->compiler.include_guards, path, guard_name);

    return append(vm, tok2, tok);
}

// Read #line arguments
static void read_line_marker(JCC *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok = preprocess(vm, copy_line(vm, rest, tok));

    if (tok->kind != TK_NUM || tok->ty->kind != TY_INT)
        error_tok(vm, tok, "invalid line marker");
    start->file->line_delta = tok->val - start->line_no;

    tok = tok->next;
    if (tok->kind == TK_EOF)
        return;

    if (tok->kind != TK_STR)
        error_tok(vm, tok, "filename expected");
    start->file->display_name = tok->str;
}

// Read a token sequence for #embed parameters (prefix, suffix, if_empty)
// Similar to read_macro_arg_one but simplified for #embed use case
static Token *read_embed_parameter(JCC *vm, Token **rest, Token *tok) {
    Token head = {};
    Token *cur = &head;
    int level = 0;

    for (;;) {
        if (level == 0 && equal(tok, ")"))
            break;

        if (tok->kind == TK_EOF)
            error_tok(vm, tok, "premature end of input in #embed parameter list");

        if (equal(tok, "("))
            level++;
        else if (equal(tok, ")"))
            level--;

        cur = cur->next = copy_token(vm, tok);
        tok = tok->next;
    }

    *rest = tok;
    return head.next; // NULL if empty parameter
}

static long eval_embed_limit_expr(JCC *vm, Token *start, Token *expr,
                                  Token *end) {
    if (!expr)
        error_tok(vm, start, "no expression");

    Token head = {};
    Token *cur = &head;

    for (Token *t = expr; t; t = t->next)
        cur = cur->next = copy_token(vm, t);
    cur->next = new_eof(vm, end);

    expr = preprocess2(vm, head.next);
    if (expr->kind == TK_EOF)
        error_tok(vm, start, "no expression");

    convert_pp_tokens(vm, expr);

    Token *rest;
    long val = const_expr(vm, &rest, expr);
    if (rest->kind != TK_EOF)
        error_tok(vm, rest, "extra tokens after #if expression");
    return val;
}

// Main #embed directive handler
static Token *handle_embed_directive(JCC *vm, Token *tok,
                                     Token *directive_start) {
    // Parse filename (quoted string or <angle brackets>)
    bool is_dquote;
    int filename_len;
    char *filename;

    if (tok->kind == TK_STR) {
        // Pattern: #embed "foo.bin"
        is_dquote = true;
        filename_len = tok->len - 2;
        filename = arena_strndup(vm, tok->loc + 1, tok->len - 2);
        tok = tok->next;
    } else if (equal(tok, "<")) {
        // Pattern: #embed <foo.bin>
        tok = tok->next;

        // Find closing ">"
        Token *end = tok;
        while (!equal(end, ">")) {
            if (end->at_bol || end->kind == TK_EOF)
                error_tok(vm, end, "expected '>' after #embed filename");
            end = end->next;
        }

        is_dquote = false;
        filename = join_tokens(vm, tok, end, &filename_len);
        tok = end->next;
    } else {
        error_tok(vm, tok, "expected a filename for #embed");
        return tok;
    }

    // Parse optional parameters
    long limit = -1; // -1 means no limit
    bool has_limit = false;
    Token *prefix_tokens = NULL;
    Token *suffix_tokens = NULL;
    Token *if_empty_tokens = NULL;

    // Parse parameters in any order
    while (equal(tok, "limit") || equal(tok, "__limit__") ||
           equal(tok, "prefix") || equal(tok, "__prefix__") ||
           equal(tok, "suffix") || equal(tok, "__suffix__") ||
           equal(tok, "if_empty") || equal(tok, "__if_empty__")) {

        if (equal(tok, "limit") || equal(tok, "__limit__")) {
            has_limit = true;
            Token *start = tok;
            tok = skip(vm, tok->next, "(");
            Token *expr = read_embed_parameter(vm, &tok, tok);
            limit = eval_embed_limit_expr(vm, start, expr, tok);
            tok = skip(vm, tok, ")");

            if (limit < 0)
                error_tok(vm, start, "limit must be non-negative");
        } else if (equal(tok, "prefix") || equal(tok, "__prefix__")) {
            tok = skip(vm, tok->next, "(");
            prefix_tokens = read_embed_parameter(vm, &tok, tok);
            tok = skip(vm, tok, ")");
        } else if (equal(tok, "suffix") || equal(tok, "__suffix__")) {
            tok = skip(vm, tok->next, "(");
            suffix_tokens = read_embed_parameter(vm, &tok, tok);
            tok = skip(vm, tok, ")");
        } else if (equal(tok, "if_empty") || equal(tok, "__if_empty__")) {
            tok = skip(vm, tok->next, "(");
            if_empty_tokens = read_embed_parameter(vm, &tok, tok);
            tok = skip(vm, tok, ")");
        }
    }

    // Skip to next line (check for extraneous tokens)
    tok = skip_line(vm, tok);

    // Resolve file path
    char *path = NULL;

    if (filename[0] == '/') {
        // Absolute path
        path = filename;
    } else if (is_dquote) {
        // Try relative to current file first
        char *relative_path =
            format_relative_path(vm, directive_start->file->name, filename);
        if (file_exists(relative_path)) {
            path = relative_path;
        }
    }

    // Search include paths if not found
    if (!path) {
        path = search_include_paths(vm, filename, filename_len, !is_dquote);
    }

    if (!path || !file_exists(path)) {
        error_tok(vm, directive_start, "file not found: %s", filename);
    }

    // Read binary file
    size_t file_size;
    unsigned char *data = read_binary_file(vm, path, &file_size);

    if (!data) {
        error_tok(vm, directive_start, "failed to read file: %s", path);
    }

    // Apply limit parameter
    size_t embed_size = file_size;
    if (has_limit && file_size > (size_t)limit) {
        embed_size = (size_t)limit;
    }

    // Check against configured limits
    if (embed_size >= vm->compiler.embed_limit) {
        if (vm->compiler.embed_hard_error) {
            error_tok(vm, directive_start,
                      "embedding large file exceeds limit: %s (%zu bytes, "
                      "limit: %zu bytes)",
                      path, embed_size, vm->compiler.embed_limit);
        } else {
            warn_tok(vm, directive_start, JCC_WARN_LARGE_FILE_EMBED,
                     "embedding large file: %s (%zu bytes)", path, embed_size);
        }
    }
    if (embed_size >= vm->compiler.embed_hard_limit) {
        if (vm->compiler.embed_hard_error) {
            error_tok(vm, directive_start,
                      "embedding very large file exceeds limit: %s (%zu bytes, "
                      "limit: %zu bytes)",
                      path, embed_size, vm->compiler.embed_hard_limit);
        } else {
            warn_tok(vm, directive_start, JCC_WARN_LARGE_FILE_EMBED,
                     "embedding very large file: %s (%zu bytes)", path,
                     embed_size);
        }
    }

    // Generate token sequence with parameter support
    Token *embed_tokens = generate_embed_tokens_with_params(
        vm, data, embed_size, prefix_tokens, suffix_tokens, if_empty_tokens,
        directive_start);

    // Link to rest of token stream
    if (embed_tokens) {
        Token *last = embed_tokens;
        while (last->next)
            last = last->next;
        last->next = tok;
        return embed_tokens;
    }

    return tok;
}

typedef enum {
    PP_NONE = 0,
    PP_IF, PP_IFDEF, PP_IFNDEF,
    PP_ELIF, PP_ELIFDEF, PP_ELIFNDEF,
    PP_ELSE, PP_ENDIF,
    PP_INCLUDE, PP_INCLUDE_NEXT,
    PP_DEFINE, PP_UNDEF,
    PP_LINE, PP_PRAGMA, PP_EMBED,
    PP_ERROR, PP_WARNING,
} PPDir;

static PPDir pp_directive(Token *tok) {
    const char *s = tok->loc;
    switch (tok->len) {
    case 2:
        if (s[0]=='i' && s[1]=='f') return PP_IF;
        break;
    case 4:
        switch (s[0]) {
        case 'e':
            if (s[1]=='l') {
                if (s[2]=='i' && s[3]=='f') return PP_ELIF;
                if (s[2]=='s' && s[3]=='e') return PP_ELSE;
            }
            break;
        case 'l': if (s[1]=='i' && s[2]=='n' && s[3]=='e') return PP_LINE; break;
        }
        break;
    case 5:
        switch (s[0]) {
        case 'e':
            switch (s[1]) {
            case 'm': if (memcmp(s+2,"bed",3)==0) return PP_EMBED;  break;
            case 'n': if (memcmp(s+2,"dif",3)==0) return PP_ENDIF;  break;
            case 'r': if (memcmp(s+2,"ror",3)==0) return PP_ERROR;  break;
            }
            break;
        case 'i': if (memcmp(s+1,"fdef",4)==0) return PP_IFDEF;  break;
        case 'u': if (memcmp(s+1,"ndef",4)==0) return PP_UNDEF;  break;
        }
        break;
    case 6:
        switch (s[0]) {
        case 'd': if (memcmp(s+1,"efine",5)==0) return PP_DEFINE;  break;
        case 'i': if (memcmp(s+1,"fndef",5)==0) return PP_IFNDEF;  break;
        case 'p': if (memcmp(s+1,"ragma",5)==0) return PP_PRAGMA;  break;
        }
        break;
    case 7:
        switch (s[0]) {
        case 'e': if (memcmp(s+1,"lifdef",6)==0) return PP_ELIFDEF;  break;
        case 'i': if (memcmp(s+1,"nclude",6)==0) return PP_INCLUDE;  break;
        case 'w': if (memcmp(s+1,"arning",6)==0) return PP_WARNING;  break;
        }
        break;
    case 8:
        if (memcmp(s,"elifndef",8)==0) return PP_ELIFNDEF;
        break;
    case 12:
        if (memcmp(s,"include_next",12)==0) return PP_INCLUDE_NEXT;
        break;
    }
    return PP_NONE;
}

// Scan the attribute argument list of a GNU __attribute__(( ... )) or C23
// [[ ... ]] block for [[jcc::macro]], [[jcc::comptime]], __attribute__((macro)),
// __attribute__((comptime)), and the inline modifier. If a macro/comptime marker
// is found, extract the following function or variable definition from the token
// stream, register it as a MacroFn or ComptimeVar, update *tok_ptr to the token
// after the extracted definition, and return true.
//
// If the attribute block contains no macro/comptime marker (e.g. [[nodiscard]],
// __attribute__((unused))), *tok_ptr is left unchanged and the function returns
// false so the token flows to the parser as normal.
static bool try_extract_attr_macro(JCC *vm, Token **tok_ptr) {
    Token *tok = *tok_ptr;
    bool is_gnu_attr = false;
    bool is_c23_attr = false;

    if (equal(tok, "__attribute__") &&
        tok->next && equal(tok->next, "(") &&
        tok->next->next && equal(tok->next->next, "("))
        is_gnu_attr = true;
    else if (equal(tok, "[") && tok->next && equal(tok->next, "["))
        is_c23_attr = true;

    if (!is_gnu_attr && !is_c23_attr)
        return false;

    // Scan inside the attribute argument list for macro/comptime/inline markers.
    bool is_macro_kind    = false;
    bool is_comptime_kind = false;
    bool is_inline        = false;
    Token *attr_end       = NULL;

    Token *scan = is_gnu_attr ? tok->next->next->next  // skip __attribute__ ( (
                              : tok->next->next;        // skip [ [
    int depth = 0;

    for (Token *t = scan; t && t->kind != TK_EOF; t = t->next) {
        if (is_gnu_attr) {
            if (equal(t, "(")) { depth++; continue; }
            if (equal(t, ")")) {
                if (depth == 0) {
                    if (t->next && equal(t->next, ")"))
                        attr_end = t->next->next;
                    break;
                }
                depth--;
                continue;
            }
        } else {
            if (equal(t, "[")) { depth++; continue; }
            if (equal(t, "]")) {
                if (depth == 0) {
                    if (t->next && equal(t->next, "]"))
                        attr_end = t->next->next;
                    break;
                }
                depth--;
                continue;
            }
        }

        if (depth != 0) continue;
        if (equal(t, ",")) continue;

        if (equal(t, "macro")) {
            is_macro_kind = true;
            // macro(inline) — inline is an argument, not a separate attribute
            if (t->next && equal(t->next, "(") &&
                t->next->next && equal(t->next->next, "inline") &&
                t->next->next->next && equal(t->next->next->next, ")"))
                is_inline = true;
        } else if (equal(t, "comptime")) {
            is_comptime_kind = true;
        } else if (equal(t, "jcc") &&
                   t->next && equal(t->next, ":") &&
                   t->next->next && equal(t->next->next, ":") &&
                   t->next->next->next) {
            Token *after_scope = t->next->next->next;
            if (equal(after_scope, "macro")) {
                is_macro_kind = true;
                // jcc::macro(inline)
                if (after_scope->next && equal(after_scope->next, "(") &&
                    after_scope->next->next && equal(after_scope->next->next, "inline") &&
                    after_scope->next->next->next &&
                    equal(after_scope->next->next->next, ")"))
                    is_inline = true;
            } else if (equal(after_scope, "comptime")) {
                is_comptime_kind = true;
            }
        }
    }

    // Only act on a positive macro/comptime match.
    if ((!is_macro_kind && !is_comptime_kind) || !attr_end)
        return false;

    bool is_macro_entry = is_macro_kind; // macro=true, comptime=false

    // Probe what follows attr_end: function or variable?
    // Heuristic: scan (respecting brace depth) for "ident (" before ";" or "=".
    bool looks_like_function = false;
    {
        Token *probe = attr_end;
        int brace_depth = 0;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, "{"))       brace_depth++;
            else if (equal(probe, "}")) brace_depth--;
            else if (brace_depth == 0) {
                if (equal(probe, ";") || equal(probe, "=")) break;
                if (probe->kind == TK_IDENT && probe->next &&
                    equal(probe->next, "(")) {
                    looks_like_function = true;
                    break;
                }
            }
            probe = probe->next;
        }
    }

    if (is_macro_entry || looks_like_function) {
        *tok_ptr = extract_macro_function(vm, attr_end,
                                          is_macro_entry, is_inline);
    } else {
        *tok_ptr = extract_comptime_var(vm, attr_end);
    }
    return true;
}

// Handle #pragma GCC diagnostic <action> ["-Wname"]
// Returns the token after the consumed pragma line.
static Token *handle_gcc_diagnostic(JCC *vm, Token *tok) {
    if (equal(tok, "push")) {
        // Grow the stack if needed
        if (vm->compiler.diag_stack_depth >= vm->compiler.diag_stack_cap) {
            int new_cap = vm->compiler.diag_stack_cap ? vm->compiler.diag_stack_cap * 2 : 4;
            vm->compiler.diag_stack_warnings =
                realloc(vm->compiler.diag_stack_warnings, sizeof(uint64_t) * new_cap);
            vm->compiler.diag_stack_werror =
                realloc(vm->compiler.diag_stack_werror, sizeof(uint64_t) * new_cap);
            vm->compiler.diag_stack_cap = new_cap;
        }
        int d = vm->compiler.diag_stack_depth++;
        vm->compiler.diag_stack_warnings[d] = vm->compiler.warnings;
        vm->compiler.diag_stack_werror[d]   = vm->compiler.warning_errors;
        return skip_line(vm, tok->next);
    }

    if (equal(tok, "pop")) {
        if (vm->compiler.diag_stack_depth <= 0) {
            warn_tok(vm, tok, JCC_WARN_CPP,
                     "#pragma GCC diagnostic pop with no matching push");
        } else {
            int d = --vm->compiler.diag_stack_depth;
            vm->compiler.warnings       = vm->compiler.diag_stack_warnings[d];
            vm->compiler.warning_errors = vm->compiler.diag_stack_werror[d];
        }
        return skip_line(vm, tok->next);
    }

    // ignore / warning / error — next token must be a string literal "-Wname"
    bool do_ignore  = equal(tok, "ignore") || equal(tok, "ignored");
    bool do_warning = equal(tok, "warning");
    bool do_error   = equal(tok, "error");

    if (do_ignore || do_warning || do_error) {
        Token *action_tok = tok;
        tok = tok->next;
        // Expect a string token like "-Wunused"
        if (!tok || tok->kind != TK_STR || tok->at_bol) {
            warn_tok(vm, action_tok, JCC_WARN_CPP,
                     "#pragma GCC diagnostic: expected warning option string");
            return skip_line(vm, tok ? tok : action_tok);
        }
        // tok->str holds the unescaped string contents (no quotes).
        const char *s = tok->str;

        // Must start with "-W"
        if (!s || s[0] != '-' || s[1] != 'W') {
            warn_tok(vm, tok, JCC_WARN_CPP,
                     "#pragma GCC diagnostic: option must begin with '-W'");
            return skip_line(vm, tok->next);
        }

        // Extract the warning name (strip leading "-W")
        char name[256];
        int namelen = (int)strlen(s) - 2;
        if (namelen <= 0 || namelen >= (int)sizeof(name)) {
            warn_tok(vm, tok, JCC_WARN_CPP,
                     "#pragma GCC diagnostic: malformed warning option '%s'", s);
            return skip_line(vm, tok->next);
        }
        memcpy(name, s + 2, namelen);
        name[namelen] = '\0';

        uint64_t mask = jcc_warning_mask_for_name(name);
        if (!mask) {
            warn_tok(vm, tok, JCC_WARN_CPP,
                     "#pragma GCC diagnostic: unknown warning option '-W%s'", name);
            return skip_line(vm, tok->next);
        }

        if (do_ignore) {
            vm->compiler.warnings       &= ~mask;
            vm->compiler.warning_errors &= ~mask;
        } else if (do_warning) {
            vm->compiler.warnings       |=  mask;
            vm->compiler.warning_errors &= ~mask;
        } else { // do_error
            vm->compiler.warnings       |=  mask;
            vm->compiler.warning_errors |=  mask;
        }
        return skip_line(vm, tok->next);
    }

    warn_tok(vm, tok, JCC_WARN_CPP,
             "#pragma GCC diagnostic: unknown action '%.*s'",
             tok->len, tok->loc);
    return skip_line(vm, tok->next);
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(JCC *vm, Token *tok) {
    Token head = {};
    Token *cur = &head;

    while (tok->kind != TK_EOF) {
        // If it is a macro, expand it.
        if (expand_macro(vm, &tok, tok))
            continue;

        // Pass through if it is not a "#".
        if (!is_hash(tok)) {
            // Intercept [[jcc::macro]] / __attribute__((macro)) and comptime
            // attribute blocks before they reach the parser. On a match,
            // the definition is extracted into the MacroFn/ComptimeVar list
            // and tok is advanced past it; nothing is added to the output.
            if (try_extract_attr_macro(vm, &tok))
                continue;
            tok->line_delta = tok->file->line_delta;
            tok->filename = tok->file->display_name;
            // Stamp the effective diagnostic state so warn_tok can use it.
            tok->diag_warnings = (1ULL << 63) | vm->compiler.warnings;
            tok->diag_werror   = (1ULL << 63) | vm->compiler.warning_errors;
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        Token *start = tok;
        tok = tok->next;

        if (tok->kind == TK_PP_NUM) {
            read_line_marker(vm, &tok, tok);
            continue;
        }

        switch (pp_directive(tok)) {
        case PP_INCLUDE: {
            bool is_dquote;
            int filename_len;
            char *filename = read_include_filename(vm, &tok, tok->next,
                                                   &is_dquote, &filename_len);
            // Gate standard headers that require a minimum C version.
            {
                static const struct { const char *name; CStdVersion min; } gates[] = {
                    // C99 headers
                    {"complex.h",    JCC_STD_C99},
                    {"fenv.h",       JCC_STD_C99},
                    {"inttypes.h",   JCC_STD_C99},
                    {"iso646.h",     JCC_STD_C99},
                    {"stdbool.h",    JCC_STD_C99},
                    {"stdint.h",     JCC_STD_C99},
                    {"tgmath.h",     JCC_STD_C99},
                    {"wchar.h",      JCC_STD_C99},
                    {"wctype.h",     JCC_STD_C99},
                    // C11 headers
                    {"stdalign.h",   JCC_STD_C11},
                    {"stdatomic.h",  JCC_STD_C11},
                    {"stdnoreturn.h",JCC_STD_C11},
                    {"uchar.h",      JCC_STD_C11},
                    {NULL, 0}
                };
                for (int gi = 0; gates[gi].name; gi++) {
                    if (strcmp(filename, gates[gi].name) == 0 &&
                        vm->compiler.c_std < gates[gi].min) {
                        const char *req = gates[gi].min == JCC_STD_C11 ? "C11" : "C99";
                        error_tok(vm, start->next,
                                  "<%s> is not available before %s", filename, req);
                        break;
                    }
                }
            }
            tok = skip_line(vm, tok);

            if (filename[0] != '/' && is_dquote) {
                char *path =
                    format_relative_path(vm, start->file->name, filename);
                if (file_exists(path)) {
                    tok = include_file(vm, tok, path, start->next->next);
                    break;
                }
            }

            // Search include paths (for quoted includes) or system paths (for
            // angle bracket)
            char *path =
                search_include_paths(vm, filename, filename_len, !is_dquote);

            // For quoted includes, if not found in include_paths, also try
            // system_include_paths This is needed for system headers that use
            // quoted includes for internal files
            if (!path && is_dquote)
                path = search_include_paths(vm, filename, filename_len, true);

            tok = include_file(vm, tok, path ? path : filename,
                               start->next->next);
            break;
        }
        case PP_INCLUDE_NEXT: {
            bool ignore;
            int filename_len;
            char *filename = read_include_filename(vm, &tok, tok->next, &ignore,
                                                   &filename_len);
            tok = skip_line(vm, tok);
            char *path = search_include_next(vm, filename);
            tok = include_file(vm, tok, path ? path : filename,
                               start->next->next);
            break;
        }
        case PP_DEFINE:
            read_macro_definition(vm, &tok, tok->next);
            break;
        case PP_UNDEF:
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_tok(vm, tok, "macro name must be an identifier");
            undef_macro(vm, arena_strndup(vm, tok->loc, tok->len));
            tok = skip_line(vm, tok->next);
            break;
        case PP_IF: {
            long val = eval_const_expr(vm, &tok, tok);
            push_cond_incl(vm, start, val);
            if (!val)
                tok = skip_cond_incl(vm, tok);
            break;
        }
        case PP_IFDEF: {
            bool defined = find_macro(vm, tok->next);
            push_cond_incl(vm, tok, defined);
            tok = skip_line(vm, tok->next->next);
            if (!defined)
                tok = skip_cond_incl(vm, tok);
            break;
        }
        case PP_IFNDEF: {
            bool defined = find_macro(vm, tok->next);
            push_cond_incl(vm, tok, !defined);
            tok = skip_line(vm, tok->next->next);
            if (defined)
                tok = skip_cond_incl(vm, tok);
            break;
        }
        case PP_ELIF:
            if (!vm->compiler.cond_incl)
                error_tok(vm, start, "stray #elif without matching #if");
            if (vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #elif after #else");
            vm->compiler.cond_incl->ctx = IN_ELIF;
            if (!vm->compiler.cond_incl->included &&
                eval_const_expr(vm, &tok, tok))
                vm->compiler.cond_incl->included = true;
            else
                tok = skip_cond_incl(vm, tok);
            break;
        case PP_ELIFDEF: {
            if (!vm->compiler.cond_incl)
                error_tok(vm, start, "stray #elifdef without matching #if");
            if (vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #elifdef after #else");
            vm->compiler.cond_incl->ctx = IN_ELIF;
            bool defined = find_macro(vm, tok->next);
            tok = skip_line(vm, tok->next->next);
            if (!vm->compiler.cond_incl->included && defined)
                vm->compiler.cond_incl->included = true;
            else
                tok = skip_cond_incl(vm, tok);
            break;
        }
        case PP_ELIFNDEF: {
            if (!vm->compiler.cond_incl)
                error_tok(vm, start, "stray #elifndef without matching #if");
            if (vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #elifndef after #else");
            vm->compiler.cond_incl->ctx = IN_ELIF;
            bool defined = find_macro(vm, tok->next);
            tok = skip_line(vm, tok->next->next);
            if (!vm->compiler.cond_incl->included && !defined)
                vm->compiler.cond_incl->included = true;
            else
                tok = skip_cond_incl(vm, tok);
            break;
        }
        case PP_ELSE:
            if (!vm->compiler.cond_incl)
                error_tok(vm, start, "stray #else without matching #if");
            if (vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #else after previous #else");
            vm->compiler.cond_incl->ctx = IN_ELSE;
            tok = skip_line(vm, tok->next);
            if (vm->compiler.cond_incl->included)
                tok = skip_cond_incl(vm, tok);
            break;
        case PP_ENDIF:
            if (!vm->compiler.cond_incl)
                error_tok(vm, start, "stray #endif without matching #if");
            vm->compiler.cond_incl = vm->compiler.cond_incl->next;
            tok = skip_line(vm, tok->next);
            break;
        case PP_LINE:
            read_line_marker(vm, &tok, tok->next);
            break;
        case PP_PRAGMA:
            if (equal(tok->next, "once")) {
                hashmap_put(&vm->compiler.pragma_once, tok->file->name, (void *)1);
                tok = skip_line(vm, tok->next->next);
            } else if (equal(tok->next, "macro")) {
                error_tok(vm, tok->next,
                          "#pragma macro is no longer supported; use "
                          "[[jcc::macro]] or __attribute__((macro))");
            } else if (equal(tok->next, "comptime")) {
                error_tok(vm, tok->next,
                          "#pragma comptime is no longer supported; use "
                          "[[jcc::comptime]] or __attribute__((comptime))");
            } else if ((equal(tok->next, "GCC") ||
                        equal(tok->next, "clang") ||
                        equal(tok->next, "JCC")) &&
                       equal(tok->next->next, "diagnostic")) {
                tok = handle_gcc_diagnostic(vm, tok->next->next->next);
            } else {
                do { tok = tok->next; } while (!tok->at_bol);
            }
            break;
        case PP_EMBED:
            if (vm->compiler.c_std < JCC_STD_C23)
                error_tok(vm, tok, "'#embed' is not available before C23");
            tok = handle_embed_directive(vm, tok->next, start);
            break;
        case PP_ERROR: {
            Token *msg_end;
            Token *msg = copy_line(vm, &msg_end, tok->next);
            char *text = join_tokens(vm, msg, NULL, NULL);
            if (text && text[0])
                error_tok(vm, tok, "%s", text);
            else
                error_tok(vm, tok, "#error directive");
            break;
        }
        case PP_WARNING: {
            Token *msg_end;
            Token *msg = copy_line(vm, &msg_end, tok->next);
            char *text = join_tokens(vm, msg, NULL, NULL);
            if (text && text[0])
                warn_tok(vm, tok, JCC_WARN_CPP, "%s", text);
            else
                warn_tok(vm, tok, JCC_WARN_CPP, "#warning directive");
            tok = msg_end;
            break;
        }
        default:
            // `#`-only line is legal (null directive).
            if (tok->at_bol) continue;
            error_tok(vm, tok, "invalid preprocessor directive '%.*s'",
                      tok->len, tok->loc);
        }
    }

    cur->next = tok;
    return head.next;
}

// (Re)define standard-dependent predefined macros from vm->compiler.c_std.
// This function is authoritative and idempotent — it can be called more than
// once (e.g. first with the default inside cc_init, then again after the user's
// -std= flag is parsed) and always produces the complete correct state.
void define_std_macros(JCC *vm) {
    const char *v;
    switch (vm->compiler.c_std) {
    case JCC_STD_C89:
        undef_macro(vm, "__STDC_VERSION__");
        return;
    case JCC_STD_C99: v = "199901L"; break;
    case JCC_STD_C11: v = "201112L"; break;
    case JCC_STD_C23: v = "202311L"; break;
    case JCC_STD_C17: default: v = "201710L"; break;
    }
    define_macro(vm, "__STDC_VERSION__", (char *)v);
}

void define_macro(JCC *vm, char *name, char *buf) {
    Token *tok = tokenize(vm, new_file(vm, "<built-in>", 1, buf));
    add_macro(vm, name, strlen(name), true, tok);
}

void undef_macro(JCC *vm, char *name) {
    hashmap_delete(&vm->compiler.macros, name);
}

static Macro *add_builtin(JCC *vm, char *name, macro_handler_fn *fn) {
    Macro *m = add_macro(vm, name, strlen(name), true, NULL);
    m->handler = fn;
    return m;
}

static Token *file_macro(JCC *vm, Token *tmpl) {
    while (tmpl->origin)
        tmpl = tmpl->origin;
    return new_str_token(vm, tmpl->file->display_name, tmpl);
}

static Token *line_macro(JCC *vm, Token *tmpl) {
    while (tmpl->origin)
        tmpl = tmpl->origin;
    int i = tmpl->line_no + tmpl->file->line_delta;
    return new_num_token(vm, i, tmpl);
}

// __COUNTER__ is expanded to serial values starting from 0.
static Token *counter_macro(JCC *vm, Token *tmpl) {
    return new_num_token(vm, vm->compiler.counter_macro_value++, tmpl);
}

// __TIMESTAMP__ is expanded to a string describing the last
// modification time of the current file. E.g.
// "Fri Jul 24 01:32:50 2020"
static Token *timestamp_macro(JCC *vm, Token *tmpl) {
    struct stat st;
    if (stat(tmpl->file->name, &st) != 0)
        return new_str_token(vm, "??? ??? ?? ??:??:?? ????", tmpl);

    char buf[30];
    ctime_r(&st.st_mtime, buf);
    buf[24] = '\0';
    return new_str_token(vm, buf, tmpl);
}

// __DATE__ is expanded to the current date, e.g. "May 17 2020".
static char *format_date(JCC *vm, struct tm *tm) {
    static char mon[][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    return arena_format(vm, "\"%s %2d %d\"", mon[tm->tm_mon], tm->tm_mday,
                        tm->tm_year + 1900);
}

// __TIME__ is expanded to the current time, e.g. "13:34:03".
static char *format_time(JCC *vm, struct tm *tm) {
    return arena_format(vm, "\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min,
                        tm->tm_sec);
}

void init_macros(JCC *vm) {
    // Define predefined macros
    define_macro(vm, "__C99_MACRO_WITH_VA_ARGS", "1");
    define_macro(vm, "__SIZEOF_DOUBLE__", "8");
    define_macro(vm, "__SIZEOF_FLOAT__", "4");
    define_macro(vm, "__SIZEOF_INT__", "4");
    define_macro(vm, "__SIZEOF_LONG_DOUBLE__", "8");
    define_macro(vm, "__SIZEOF_LONG_LONG__", "8");
    define_macro(vm, "__SIZEOF_LONG__", "8");
    define_macro(vm, "__SIZEOF_POINTER__", "8");
    define_macro(vm, "__SIZEOF_PTRDIFF_T__", "8");
    define_macro(vm, "__SIZEOF_SHORT__", "2");
    define_macro(vm, "__SIZEOF_SIZE_T__", "8");
    define_macro(vm, "__SIZE_TYPE__", "unsigned long");
    define_macro(vm, "__STDC_HOSTED__", "1");
    define_macro(vm, "__STDC_NO_COMPLEX__", "1");
    define_macro(vm, "__STDC_UTF_16__", "1");
    define_macro(vm, "__STDC_UTF_32__", "1");
    define_std_macros(vm);
    define_macro(vm, "__STDC__", "1");
    define_macro(vm, "__USER_LABEL_PREFIX__", "");
    define_macro(vm, "__alignof__", "_Alignof");
    define_macro(vm, "__const__", "const");
    define_macro(vm, "__inline__", "inline");
    define_macro(vm, "__signed__", "signed");
    define_macro(vm, "__typeof__", "typeof");
    define_macro(vm, "__volatile__", "volatile");
    define_macro(vm, "__JCC__", "1");

    // GCC compatibility macros for system headers
    // Claim GCC 4.2.1 compatibility (minimum version for modern headers)
    define_macro(vm, "__GNUC__", "4");
    define_macro(vm, "__GNUC_MINOR__", "2");
    define_macro(vm, "__GNUC_PATCHLEVEL__", "1");

    // __builtin_va_list - system headers use this for va_list typedef
    // Define as char* for compatibility (macOS system headers expect a pointer
    // type)
    define_macro(vm, "__builtin_va_list", "char*");
    define_macro(vm, "__gnuc_va_list", "char*");

    // Strip __attribute__ specifications from system headers since JCC parser
    // doesn't handle all attribute positions. Attributes are used for
    // optimization hints and documentation, not required for correct
    // compilation.
    define_macro(vm, "__attribute__(x)", "");

    // Architecture macros - pass through from host compiler
#if defined(__x86_64__) || defined(__x86_64) || defined(__amd64__) ||          \
    defined(__amd64)
    define_macro(vm, "__x86_64__", "1");
    define_macro(vm, "__x86_64", "1");
    define_macro(vm, "__amd64__", "1");
    define_macro(vm, "__amd64", "1");
    define_macro(vm, "__LP64__", "1");
#endif
#if defined(__aarch64__) || defined(__arm64__)
    define_macro(vm, "__aarch64__", "1");
    define_macro(vm, "__arm64__", "1");
    define_macro(vm, "__LP64__", "1");
#endif
#if defined(__i386__) || defined(__i386)
    define_macro(vm, "__i386__", "1");
    define_macro(vm, "__i386", "1");
#endif

#ifdef _MSC_VER
#if defined(_M_AMD64)
    define_macro(vm, "ARCH_X64", "1");
#elif defined(_M_IX86)
    define_macro(vm, "ARCH_X86", "1");
#elif defined(_M_ARM64)
    define_macro(vm, "ARCH_ARM64", "1");
#elif defined(_M_ARM)
    define_macro(vm, "ARCH_ARM32", "1");
#elif defined(_M_IA64)
    define_macro(vm, "ARCH_IA64", "1");
#endif
#endif

#ifdef __clang__
#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||           \
    defined(__x86_64)
    define_macro(vm, "ARCH_X64", "1");
#elif defined(i386) || defined(__i386) || defined(__i386__)
    define_macro(vm, "ARCH_X86", "1");
#elif defined(__aarch64__)
    define_macro(vm, "ARCH_ARM64", "1");
#elif defined(__arm__)
    define_macro(vm, "ARCH_ARM32", "1");
#elif defined(__ia64__)
    define_macro(vm, "ARCH_IA64", "1");
#endif
#endif

#if defined(__GNUC__) || defined(__GNUG__)
#if defined(__amd64__) || defined(__amd64) || defined(__x86_64__) ||           \
    defined(__x86_64)
    define_macro(vm, "ARCH_X64", "1");
#elif defined(i386) || defined(__i386) || defined(__i386__)
    define_macro(vm, "ARCH_X86", "1");
#elif defined(__aarch64__)
    define_macro(vm, "ARCH_ARM64", "1");
#elif defined(__arm__)
    define_macro(vm, "ARCH_ARM32", "1");
#elif defined(__ia64__)
    define_macro(vm, "ARCH_IA64", "1");
#endif
#endif

#ifdef _WIN32
    define_macro(vm, "_WIN32", "1");
#endif
#ifdef _WIN64
    define_macro(vm, "_WIN64", "1");
#endif

#ifdef __linux__
    define_macro(vm, "__linux__", "1");
    define_macro(vm, "PLATFORM_LINUX", "1");
#endif
#ifdef __APPLE__
    define_macro(vm, "__APPLE__", "1");
    // Darwin feature test macros for system header compatibility
    define_macro(vm, "_DARWIN_C_SOURCE", "1");
    define_macro(vm, "__DARWIN_64_BIT_INO_T", "1");
#endif
#ifdef __FreeBSD__
    define_macro(vm, "__FreeBSD__", "1");
    define_macro(vm, "PLATFORM_FREEBSD", "1");
#endif
#ifdef __NetBSD__
    define_macro(vm, "__NetBSD__", "1");
    define_macro(vm, "PLATFORM_NETBSD", "1");
#endif
#ifdef __OpenBSD__
    define_macro(vm, "__OpenBSD__", "1");
    define_macro(vm, "PLATFORM_OPENBSD", "1");
#endif
#ifdef __sun
    define_macro(vm, "__sun", "1");
    define_macro(vm, "PLATFORM_SOLARIS", "1");
#endif
#ifdef __unix__
    define_macro(vm, "__unix__", "1");
    define_macro(vm, "PLATFORM_UNIX", "1");
#endif

    add_builtin(vm, "__FILE__", file_macro);
    add_builtin(vm, "__LINE__", line_macro);
    add_builtin(vm, "__COUNTER__", counter_macro);
    add_builtin(vm, "__TIMESTAMP__", timestamp_macro);

    time_t now = time(NULL);
    struct tm *tm = localtime(&now);
    define_macro(vm, "__DATE__", format_date(vm, tm));
    define_macro(vm, "__TIME__", format_time(vm, tm));
}

typedef enum {
    STR_NONE,
    STR_UTF8,
    STR_UTF16,
    STR_UTF32,
    STR_WIDE,
} StringKind;

static StringKind getStringKind(Token *tok) {
    if (tok->len == 2 && strncmp(tok->loc, "u8", 2) == 0)
        return STR_UTF8;

    switch (tok->loc[0]) {
    case '"':
        return STR_NONE;
    case 'u':
        return STR_UTF16;
    case 'U':
        return STR_UTF32;
    case 'L':
        return STR_WIDE;
    }
    unreachable();
    return -1;
}

// Concatenate adjacent string literals into a single string literal
// as per the C spec.
static void join_adjacent_string_literals(JCC *vm, Token *tok) {
    // First pass: If regular string literals are adjacent to wide
    // string literals, regular string literals are converted to a wide
    // type before concatenation. In this pass, we do the conversion.
    for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
        if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
            tok1 = tok1->next;
            continue;
        }

        StringKind kind = getStringKind(tok1);
        Type *basety = tok1->ty->base;

        for (Token *t = tok1->next; t->kind == TK_STR; t = t->next) {
            StringKind k = getStringKind(t);
            if (kind == STR_NONE) {
                kind = k;
                basety = t->ty->base;
            } else if (k != STR_NONE && kind != k) {
                error_tok(vm, t,
                          "unsupported non-standard concatenation of string "
                          "literals");
            }
        }

        if (basety->size > 1)
            for (Token *t = tok1; t->kind == TK_STR; t = t->next)
                if (t->ty->base->size == 1)
                    *t = *tokenize_string_literal(vm, t, basety);

        while (tok1->kind == TK_STR)
            tok1 = tok1->next;
    }

    // Second pass: concatenate adjacent string literals.
    for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
        if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
            tok1 = tok1->next;
            continue;
        }

        Token *tok2 = tok1->next;
        while (tok2->kind == TK_STR)
            tok2 = tok2->next;

        int len = tok1->ty->array_len;
        for (Token *t = tok1->next; t != tok2; t = t->next)
            len = len + t->ty->array_len - 1;

        char *buf =
            arena_alloc(&vm->compiler.parser_arena, tok1->ty->base->size * len);
        memset(buf, 0, tok1->ty->base->size * len);

        int i = 0;
        for (Token *t = tok1; t != tok2; t = t->next) {
            memcpy(buf + i, t->str, t->ty->size);
            i = i + t->ty->size - t->ty->base->size;
        }

        *tok1 = *copy_token(vm, tok1);
        tok1->ty = array_of(vm, tok1->ty->base, len);
        tok1->str = buf;
        tok1->next = tok2;
        tok1 = tok2;
    }
}

// Entry point function of the preprocessor.
Token *preprocess(JCC *vm, Token *tok) {
    tok = preprocess2(vm, tok);
    if (vm->compiler.cond_incl)
        error_tok(vm, vm->compiler.cond_incl->tok,
                  "unterminated conditional directive (started with #%.*s)",
                  vm->compiler.cond_incl->tok->len,
                  vm->compiler.cond_incl->tok->loc);
    convert_pp_tokens(vm, tok);
    join_adjacent_string_literals(vm, tok);

    for (Token *t = tok; t; t = t->next)
        t->line_no += t->line_delta;
    return tok;
}
