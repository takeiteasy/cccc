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
static char *read_include_filename(JCC *vm, Token **rest, Token *tok,
                                   bool *is_dquote, int *out_len);
static long eval_const_expr(JCC *vm, Token **rest, Token *tok);

static bool is_hash(Token *tok) { return tok->at_bol && equal(tok, "#"); }

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(JCC *vm, Token *tok) {
    if (tok->at_bol)
        return tok;
    warn_tok(vm, tok, "extra token");
    while (tok->at_bol)
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

// Extract a #pragma macro function definition and store it
// Returns the token after the function definition (or original token if
// extraction failed)
static Token *extract_pragma_macro(JCC *vm, Token *tok) {
    // Expected format: <return_type> <function_name>(<params>) { <body> }
    // tok should be the first token after "macro" on the next line

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
        error_tok(vm, start, "#pragma macro: expected function definition");
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
        error_tok(vm, start, "#pragma macro: expected function body");
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

    // Create PragmaMacro entry
    PragmaMacro *pm =
        arena_alloc(&vm->compiler.parser_arena, sizeof(PragmaMacro));
    memset(pm, 0, sizeof(PragmaMacro));
    pm->name = name;
    pm->body_tokens = head.next;
    pm->compiled_fn = NULL;
    pm->is_compiled = false;
    pm->next = vm->compiler.pragma_macros;
    vm->compiler.pragma_macros = pm;

    if (vm->debug_vm)
        printf("Captured pragma macro '%s'\n", name);

    // Return token after the function
    return body_end ? body_end : tok;
}

static Hideset *new_hideset(JCC *vm, char *name) {
    Hideset *hs = arena_alloc(&vm->compiler.parser_arena, sizeof(Hideset));
    memset(hs, 0, sizeof(Hideset));
    hs->name = name;
    return hs;
}

static Hideset *hideset_union(JCC *vm, Hideset *hs1, Hideset *hs2) {
    Hideset head = {};
    Hideset *cur = &head;

    for (; hs1; hs1 = hs1->next)
        cur = cur->next = new_hideset(vm, hs1->name);
    cur->next = hs2;
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
    char *buf = format("%d\n", val);
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
                char *relative_path = format(
                    "%s/%s", dirname(strdup(start->file->name)), filename);
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
        error_tok(vm, rest2, "extra token");
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
            *va_args_name = strndup(tok->loc, tok->len);
            *rest = skip(vm, tok->next->next, ")");
            return head.next;
        }

        MacroParam *m =
            arena_alloc(&vm->compiler.parser_arena, sizeof(MacroParam));
        memset(m, 0, sizeof(MacroParam));
        m->name = strndup(tok->loc, tok->len);
        cur = cur->next = m;
        tok = tok->next;
    }

    *rest = tok->next;
    return head.next;
}

static void read_macro_definition(JCC *vm, Token **rest, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "macro name must be an identifier");
    char *name = strndup(tok->loc, tok->len);
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
            error_tok(vm, tok, "premature end of input");

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
        error_tok(vm, start, "too many arguments");
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
    char *buf = format("%.*s%.*s", lhs->len, lhs->loc, rhs->len, rhs->loc);

    // Tokenize the resulting string.
    Token *tok =
        tokenize(vm, new_file(vm, lhs->file->name, lhs->file->file_no, buf));
    if (tok->next->kind != TK_EOF)
        error_tok(vm, lhs, "pasting forms '%s', an invalid token", buf);
    return tok;
}

static bool has_varargs(MacroArg *args) {
    for (MacroArg *ap = args; ap; ap = ap->next)
        if (!strcmp(ap->name, "__VA_ARGS__"))
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

static bool is_standard_header(const char *filename) {
    static const char *std_headers[] = {
        "assert.h", "ctype.h",        "errno.h",     "float.h",  "inttypes.h",
        "limits.h", "math.h",         "setjmp.h",    "stdarg.h", "stdbool.h",
        "stddef.h", "stdint.h",       "stdio.h",     "stdlib.h", "string.h",
        "time.h",   "Availability.h", "sys/cdefs.h", NULL};

    for (int i = 0; std_headers[i]; i++) {
        if (strcmp(filename, std_headers[i]) == 0)
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
        if (!file_exists(path))
            continue;
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
        char *path = format(
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
        char *path = format(
            "%s/%s", vm->compiler.system_include_paths.data[sys_idx], filename);
        if (file_exists(path))
            return path;
    }
    return NULL;
}

// Read an #include argument.
static char *read_include_filename(JCC *vm, Token **rest, Token *tok,
                                   bool *is_dquote, int *out_len) {
    // Pattern 1: #include "foo.h"
    if (tok->kind == TK_STR) {
        // A double-quoted filename for #include is a special kind of
        // token, and we don't want to interpret any escape sequences in it.
        // For example, "\f" in "C:\foo" is not a formfeed character but
        // just two non-control characters, backslash and f.
        // So we don't want to use token->str.
        *is_dquote = true;
        *rest = skip_line(vm, tok->next);
        if (out_len)
            *out_len = tok->len - 2;
        return strndup(tok->loc + 1, tok->len - 2);
    }

    // Pattern 2: #include <foo.h>
    if (equal(tok, "<")) {
        // Reconstruct a filename from a sequence of tokens between
        // "<" and ">".
        Token *start = tok;

        // Find closing ">".
        for (; !equal(tok, ">"); tok = tok->next)
            if (tok->at_bol || tok->kind == TK_EOF)
                error_tok(vm, tok, "expected '>'");

        *is_dquote = false;
        *rest = skip_line(vm, tok->next);
        return join_tokens(vm, start->next, tok, out_len);
    }

    // Pattern 3: #include FOO
    // In this case FOO must be macro-expanded to either
    // a single string token or a sequence of "<" ... ">".
    if (tok->kind == TK_IDENT) {
        Token *tok2 = preprocess2(vm, copy_line(vm, rest, tok));
        return read_include_filename(vm, &tok2, tok2, is_dquote, out_len);
    }

    error_tok(vm, tok, "expected a filename");
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

    char *macro = strndup(tok->loc, tok->len);
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
    if (strcmp(header_name, "ctype.h") == 0) {
        register_ctype_functions(vm);
    } else if (strcmp(header_name, "math.h") == 0) {
        register_math_functions(vm);
    } else if (strcmp(header_name, "stdio.h") == 0) {
        register_stdio_functions(vm);
    } else if (strcmp(header_name, "stdlib.h") == 0) {
        register_stdlib_functions(vm);
    } else if (strcmp(header_name, "string.h") == 0) {
        register_string_functions(vm);
    } else if (strcmp(header_name, "time.h") == 0) {
        register_time_functions(vm);
    }
    // Note: Other headers (like stddef.h, stdbool.h, etc.) don't have runtime
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
    static HashMap include_guards;
    char *guard_name = hashmap_get(&include_guards, path);
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
        hashmap_put(&include_guards, path, guard_name);

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
            error_tok(vm, tok, "premature end of input");

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
        error_tok(vm, rest, "extra token");
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
        filename = strndup(tok->loc + 1, tok->len - 2);
        tok = tok->next;
    } else if (equal(tok, "<")) {
        // Pattern: #embed <foo.bin>
        tok = tok->next;

        // Find closing ">"
        Token *end = tok;
        while (!equal(end, ">")) {
            if (end->at_bol || end->kind == TK_EOF)
                error_tok(vm, end, "expected '>'");
            end = end->next;
        }

        is_dquote = false;
        filename = join_tokens(vm, tok, end, &filename_len);
        tok = end->next;
    } else {
        error_tok(vm, tok, "expected a filename");
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
        char *relative_path = format(
            "%s/%s", dirname(strdup(directive_start->file->name)), filename);
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
    if (has_limit && (long)file_size > limit) {
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
            warn_tok(vm, directive_start,
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
            warn_tok(vm, directive_start,
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
            tok->line_delta = tok->file->line_delta;
            tok->filename = tok->file->display_name;
            cur = cur->next = tok;
            tok = tok->next;
            continue;
        }

        Token *start = tok;
        tok = tok->next;

        if (equal(tok, "include")) {
            bool is_dquote;
            int filename_len;
            char *filename = read_include_filename(vm, &tok, tok->next,
                                                   &is_dquote, &filename_len);

            // Check for URL includes (supported with both <...> and "...")
            if (is_url(filename)) {
#ifdef JCC_HAS_CURL
                char *cache_path = fetch_url_to_cache(vm, filename);
                if (!cache_path) {
                    error_tok(vm, start->next, "failed to fetch URL: %s",
                              filename);
                }
                // Track URL -> cache path mapping for error reporting
                hashmap_put(&vm->compiler.url_to_path, cache_path,
                            (void *)filename);
                tok = include_file(vm, tok, cache_path, start->next->next);
                continue;
#else
                error_tok(
                    vm, start->next,
                    "URL includes require JCC to be built with JCC_HAS_CURL=1");
#endif
            }

            if (filename[0] != '/' && is_dquote) {
                char *path = format("%s/%s", dirname(strdup(start->file->name)),
                                    filename);
                if (file_exists(path)) {
                    tok = include_file(vm, tok, path, start->next->next);
                    continue;
                }
            }

            // Search include paths (for quoted includes) or system paths (for
            // angle bracket)
            char *path =
                search_include_paths(vm, filename, filename_len, !is_dquote);

            // For quoted includes, if not found in include_paths, also try
            // system_include_paths This is needed for system headers that use
            // quoted includes for internal files
            if (!path && is_dquote) {
                path = search_include_paths(vm, filename, filename_len, true);
            }

            tok = include_file(vm, tok, path ? path : filename,
                               start->next->next);
            continue;
        }

        if (equal(tok, "include_next")) {
            bool ignore;
            int filename_len;
            char *filename = read_include_filename(vm, &tok, tok->next, &ignore,
                                                   &filename_len);
            char *path = search_include_next(vm, filename);
            tok = include_file(vm, tok, path ? path : filename,
                               start->next->next);
            continue;
        }

        if (equal(tok, "define")) {
            read_macro_definition(vm, &tok, tok->next);
            continue;
        }

        if (equal(tok, "undef")) {
            tok = tok->next;
            if (tok->kind != TK_IDENT)
                error_tok(vm, tok, "macro name must be an identifier");
            undef_macro(vm, strndup(tok->loc, tok->len));
            tok = skip_line(vm, tok->next);
            continue;
        }

        if (equal(tok, "if")) {
            long val = eval_const_expr(vm, &tok, tok);
            push_cond_incl(vm, start, val);
            if (!val)
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "ifdef")) {
            bool defined = find_macro(vm, tok->next);
            push_cond_incl(vm, tok, defined);
            tok = skip_line(vm, tok->next->next);
            if (!defined)
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "ifndef")) {
            bool defined = find_macro(vm, tok->next);
            push_cond_incl(vm, tok, !defined);
            tok = skip_line(vm, tok->next->next);
            if (defined)
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "elif")) {
            if (!vm->compiler.cond_incl ||
                vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #elif");
            vm->compiler.cond_incl->ctx = IN_ELIF;

            if (!vm->compiler.cond_incl->included &&
                eval_const_expr(vm, &tok, tok))
                vm->compiler.cond_incl->included = true;
            else
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "elifdef")) {
            if (!vm->compiler.cond_incl ||
                vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #elifdef");
            vm->compiler.cond_incl->ctx = IN_ELIF;

            bool defined = find_macro(vm, tok->next);
            tok = skip_line(vm, tok->next->next);
            if (!vm->compiler.cond_incl->included && defined)
                vm->compiler.cond_incl->included = true;
            else
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "elifndef")) {
            if (!vm->compiler.cond_incl ||
                vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #elifndef");
            vm->compiler.cond_incl->ctx = IN_ELIF;

            bool defined = find_macro(vm, tok->next);
            tok = skip_line(vm, tok->next->next);
            if (!vm->compiler.cond_incl->included && !defined)
                vm->compiler.cond_incl->included = true;
            else
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "else")) {
            if (!vm->compiler.cond_incl ||
                vm->compiler.cond_incl->ctx == IN_ELSE)
                error_tok(vm, start, "stray #else");
            vm->compiler.cond_incl->ctx = IN_ELSE;
            tok = skip_line(vm, tok->next);

            if (vm->compiler.cond_incl->included)
                tok = skip_cond_incl(vm, tok);
            continue;
        }

        if (equal(tok, "endif")) {
            if (!vm->compiler.cond_incl)
                error_tok(vm, start, "stray #endif");
            vm->compiler.cond_incl = vm->compiler.cond_incl->next;
            tok = skip_line(vm, tok->next);
            continue;
        }

        if (equal(tok, "line")) {
            read_line_marker(vm, &tok, tok->next);
            continue;
        }

        if (tok->kind == TK_PP_NUM) {
            read_line_marker(vm, &tok, tok);
            continue;
        }

        if (equal(tok, "pragma") && equal(tok->next, "once")) {
            hashmap_put(&vm->compiler.pragma_once, tok->file->name, (void *)1);
            tok = skip_line(vm, tok->next->next);
            continue;
        }

        if (equal(tok, "pragma") && equal(tok->next, "macro")) {
            // Skip to end of #pragma macro line
            Token *macro_tok = tok->next->next; // Skip "pragma" and "macro"
            while (macro_tok && !macro_tok->at_bol && macro_tok->kind != TK_EOF)
                macro_tok = macro_tok->next;
            // Now macro_tok points to the first token of the function
            // definition
            tok = extract_pragma_macro(vm, macro_tok);
            continue;
        }

        if (equal(tok, "pragma")) {
            do {
                tok = tok->next;
            } while (!tok->at_bol);
            continue;
        }

        if (equal(tok, "embed")) {
            tok = handle_embed_directive(vm, tok->next, start);
            continue;
        }

        if (equal(tok, "error"))
            error_tok(vm, tok, "error");

        if (equal(tok, "warning")) {
            warn_tok(vm, tok, "warning");
            tok = skip_line(vm, tok->next);
            continue;
        }

        // `#`-only line is legal. It's called a null directive.
        if (tok->at_bol)
            continue;

        error_tok(vm, tok, "invalid preprocessor directive");
    }

    cur->next = tok;
    return head.next;
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

    char *result = format("\"%s %2d %d\"", mon[tm->tm_mon], tm->tm_mday,
                          tm->tm_year + 1900);
    strarray_push(&vm->compiler.file_buffers, result);
    return result;
}

// __TIME__ is expanded to the current time, e.g. "13:34:03".
static char *format_time(JCC *vm, struct tm *tm) {
    char *result =
        format("\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min, tm->tm_sec);
    strarray_push(&vm->compiler.file_buffers, result);
    return result;
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
    define_macro(vm, "__STDC_VERSION__", "201112L");
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
    if (!strcmp(tok->loc, "u8"))
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
                  "unterminated conditional directive");
    convert_pp_tokens(vm, tok);
    join_adjacent_string_literals(vm, tok);

    for (Token *t = tok; t; t = t->next)
        t->line_no += t->line_delta;
    return tok;
}
