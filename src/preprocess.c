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
#include <fenv.h>  // host <fenv.h>, for init_fenv_macros() (#771)
#include <errno.h> // host <errno.h>, for init_errno_macros() (#813)
#if !defined(_WIN32) && !defined(_WIN64)
#include <dlfcn.h> // host <dlfcn.h>, for init_dlfcn_macros() (#1152)
#endif

#define MAX_PP_NESTING 1000

typedef struct MacroParam MacroParam;
struct MacroParam {
    MacroParam *next;
    char       *name;
};

typedef struct MacroArg MacroArg;
struct MacroArg {
    MacroArg *next;
    char     *name;
    bool      is_va_args;
    Token    *tok;
};

typedef Token *macro_handler_fn(VirtualMachine *, Token *);

typedef struct Macro Macro;
struct Macro {
    char             *name;
    bool              is_objlike; // Object-like or function-like
    MacroParam       *params;
    char             *va_args_name;
    Token            *body;
    macro_handler_fn *handler;
    int               use_count; // number of times this macro has been expanded
    Token *define_tok; // token at the #define site (the macro name token)
    bool   is_shared;  // #888: #define @shared NAME -- survives
                       // isolate_comptime_macros
};

static Token *preprocess2(VirtualMachine *vm, Token *tok);
static Macro *find_macro(VirtualMachine *vm, Token *tok);
static bool probe_function_definition(Token *tok);
static bool file_exists(char *path);
static char *format_relative_path(VirtualMachine *vm, char *base_file,
                                  char *filename);
static char *resolve_embedded_relative_header(VirtualMachine *vm,
                                              char *base_file, char *filename);
static char *read_include_filename(VirtualMachine *vm, Token **rest, Token *tok,
                                   bool *is_dquote, int *out_len);
char *search_include_paths(VirtualMachine *vm, char *filename, int filename_len,
                           bool is_system);
static long eval_const_expr(VirtualMachine *vm, Token **rest, Token *tok);

static bool is_hash(Token *tok) {
    return tok->at_bol && equal(tok, "#");
}

static ComptimeCtxEntry *ctx_top(VirtualMachine *vm) {
    return vm->compiler.ctx_stack_len
               ? &vm->compiler.ctx_stack[vm->compiler.ctx_stack_len - 1]
               : NULL;
}

static void ctx_push(VirtualMachine *vm, ComptimeCtxType type, bool needs_end,
                     File *file, Token *open_tok) {
    if (vm->compiler.ctx_stack_len == vm->compiler.ctx_stack_cap) {
        vm->compiler.ctx_stack_cap =
            vm->compiler.ctx_stack_cap ? vm->compiler.ctx_stack_cap * 2 : 4;
        vm->compiler.ctx_stack =
            realloc(vm->compiler.ctx_stack,
                    vm->compiler.ctx_stack_cap * sizeof(ComptimeCtxEntry));
    }
    vm->compiler.ctx_stack[vm->compiler.ctx_stack_len++] =
        (ComptimeCtxEntry){type, needs_end, file, open_tok};
}

static void ctx_pop(VirtualMachine *vm) {
    vm->compiler.ctx_stack_len--;
}

// Push a new suite component onto the hierarchical suite path.
// Composites current_suite + "/" + name and saves the previous strlen so
// suite_pop can truncate back without re-joining.  open_tok is saved to
// produce a useful "unclosed suite begin" error pointing at the pragma line.
static void suite_push(VirtualMachine *vm, const char *name, Token *open_tok) {
    size_t prev =
        vm->compiler.current_suite ? strlen(vm->compiler.current_suite) : 0;
    // Grow the entry-stack if needed.
    if (vm->compiler.suite_stack_len == vm->compiler.suite_stack_cap) {
        vm->compiler.suite_stack_cap =
            vm->compiler.suite_stack_cap ? vm->compiler.suite_stack_cap * 2 : 4;
        vm->compiler.suite_len_stack =
            realloc(vm->compiler.suite_len_stack,
                    vm->compiler.suite_stack_cap *
                        sizeof(*vm->compiler.suite_len_stack));
    }
    vm->compiler.suite_len_stack[vm->compiler.suite_stack_len++] =
        (struct SuiteLenEntry){prev, open_tok};
    // Build new composite path: prev + "/" + name (or just name at depth 0).
    size_t namelen = strlen(name);
    size_t sep     = prev ? 1 : 0; // '/' separator needed when joining
    size_t need    = prev + sep + namelen + 1;
    char  *buf     = realloc(vm->compiler.current_suite, need);
    if (sep)
        buf[prev] = '/';
    memcpy(buf + prev + sep, name, namelen + 1);
    vm->compiler.current_suite = buf;
}

// Pop the innermost suite level, restoring current_suite to its previous path.
// Must only be called when suite_stack_len > 0.
static void suite_pop(VirtualMachine *vm) {
    size_t prev =
        vm->compiler.suite_len_stack[--vm->compiler.suite_stack_len].prev_len;
    if (prev == 0) {
        // Popped back to top-level: no active suite.
        free(vm->compiler.current_suite);
        vm->compiler.current_suite = NULL;
    } else {
        // Truncate the composite path at the saved length.
        vm->compiler.current_suite[prev] = '\0';
    }
}

// Stack of vm->compiler.macros snapshots used to isolate #define/#undef
// directives inside individual [[cccc::comptime]] function bodies from each
// other (#283). Pushed/popped by TK_MACRO_SCOPE_PUSH/POP marker tokens
// synthesized in build_combined_macro_tokens.
static void macro_scope_push(VirtualMachine *vm, HashMap snap) {
    if (vm->compiler.macro_scope_stack_len ==
        vm->compiler.macro_scope_stack_cap) {
        vm->compiler.macro_scope_stack_cap =
            vm->compiler.macro_scope_stack_cap
                ? vm->compiler.macro_scope_stack_cap * 2
                : 4;
        vm->compiler.macro_scope_stack =
            realloc(vm->compiler.macro_scope_stack,
                    vm->compiler.macro_scope_stack_cap * sizeof(HashMap));
    }
    vm->compiler.macro_scope_stack[vm->compiler.macro_scope_stack_len++] = snap;
}

static HashMap macro_scope_pop(VirtualMachine *vm) {
    // Underflow means a TK_MACRO_SCOPE_PUSH/POP pair went unbalanced
    // upstream -- e.g. #884's extraction-swallow bug jumped a later
    // extraction pass over a PUSH marker. That bug is fixed at the source,
    // but this guard turns any future imbalance into a diagnostic instead
    // of a NULL-based read.
    if (vm->compiler.macro_scope_stack_len == 0)
        error("internal error: unbalanced compile-time macro scope stack");
    return vm->compiler.macro_scope_stack[--vm->compiler.macro_scope_stack_len];
}

typedef enum {
    INCLUDE_ROUTE_NORMAL,
    INCLUDE_ROUTE_COMPTIME,
    INCLUDE_ROUTE_EMIT,
    INCLUDE_ROUTE_SHARED,
    INCLUDE_ROUTE_BUILD,
    INCLUDE_ROUTE_TEST,
} IncludeRoute;

// Some preprocessor directives such as #include allow extraneous
// tokens before newline. This function skips such tokens.
static Token *skip_line(VirtualMachine *vm, Token *tok) {
    if (tok->at_bol)
        return tok;
    warn_tok(vm, tok, CCCC_WARN_EXTRA_TOKENS, "extra tokens after directive");
    // Defence in depth: every token list this walks is expected to end in a
    // TK_EOF with at_bol set (the tokenizer sets it on the real EOF; any
    // synthesized token list must too -- see copy_macro_token_bol() in
    // src/macros.c). Stop at TK_EOF rather than walking a chunk boundary
    // that lost at_bol and dereferencing past the end of the list -- callers
    // all assume the returned token is dereferenceable, so fall back to the
    // last TK_EOF seen (or tok itself if the walk never finds one) instead
    // of NULL.
    Token *last = tok;
    while (tok && tok->kind != TK_EOF && !tok->at_bol) {
        last = tok;
        tok  = tok->next;
    }
    return tok ? tok : last;
}

// #1155: a captured `#include` line's operand can carry incidental internal
// whitespace CCCC's own tokenizer tolerates (`#include < glob.h>` compiles
// fine under CCCC -- read_include_filename()/join_tokens() skip the
// separator before the *first* filename token, so the resolved path is
// still exactly "glob.h") but a real host cc's preprocessor does not: it
// treats the `<...>`/`"..."` span as one opaque header-name token, so a
// leading space makes it look for a file literally named " glob.h" and
// fail ("' glob.h' file not found, did you mean 'glob.h'?"). Strip
// whitespace immediately inside the two delimiters, in place -- a header
// name can never legally contain a space itself (POSIX/C), so this can't
// be over-eager. Applied to the fully-assembled line so it covers both
// copiers below (a routed line's route-qualifier tokens, e.g. `@shared`,
// never themselves contain `<`/`"`, so the first delimiter found here is
// always the operand's own).
static void normalize_include_operand_spacing(char *line) {
    if (!line || line[0] != '#')
        return;
    char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    if (strncmp(p, "include", 7) != 0)
        return;
    p          += 7;
    char *open  = strpbrk(p, "<\"");
    if (!open)
        return;
    char  close_ch = (*open == '<') ? '>' : '"';
    char *start    = open + 1;
    char *end      = strchr(start, close_ch);
    if (!end)
        return;
    char *content = start;
    while (content < end && (*content == ' ' || *content == '\t'))
        content++;
    char *content_end = end;
    while (content_end > content &&
           (content_end[-1] == ' ' || content_end[-1] == '\t'))
        content_end--;
    if (content == start && content_end == end)
        return;                                         // nothing to strip
    size_t content_len = (size_t)(content_end - content);
    memmove(start, content, content_len);
    memmove(start + content_len, end, strlen(end) + 1); // +1 for NUL
}

static char *copy_raw_directive_line(VirtualMachine *vm, Token *start) {
    char *end = start->loc;
    while (*end && *end != '\n')
        end++;
    if (end > start->loc && end[-1] == '\r')
        end--;
    char *line = arena_strndup(vm, start->loc, end - start->loc);
    normalize_include_operand_spacing(line);
    return line;
}

static char *copy_routed_directive_line(VirtualMachine *vm, Token *hash,
                                        Token *route_start, Token *route_end) {
    (void)route_start;
    Token *directive = hash->next;
    size_t cap       = 64;
    size_t len       = 0;
    char  *line      = arena_alloc(&vm->compiler.parser_arena, cap);
#define APPEND_BYTES(ptr, n)                                                   \
    do {                                                                       \
        size_t need = len + (size_t)(n) + 1;                                   \
        if (need > cap) {                                                      \
            char *next = arena_alloc(&vm->compiler.parser_arena, need * 2);    \
            memcpy(next, line, len);                                           \
            line = next;                                                       \
            cap  = need * 2;                                                   \
        }                                                                      \
        memcpy(line + len, (ptr), (size_t)(n));                                \
        len       += (size_t)(n);                                              \
        line[len]  = '\0';                                                     \
    } while (0)
    APPEND_BYTES("#", 1);
    APPEND_BYTES(directive->loc, directive->len);
    bool wrote_space = false;
    for (Token *t = route_end; t && t->kind != TK_EOF && !t->at_bol;
         t        = t->next) {
        if (!wrote_space || t->has_space)
            APPEND_BYTES(" ", 1);
        APPEND_BYTES(t->loc, t->len);
        wrote_space = true;
    }
#undef APPEND_BYTES
    normalize_include_operand_spacing(line);
    return line;
}

static void push_emit_directive(VirtualMachine *vm, char *line, bool dedup) {
    if (!line)
        return;
    StringArray *arr = &vm->compiler.emit_directives;
    if (dedup) {
        for (int i = 0; i < arr->len; i++)
            if (strcmp(arr->data[i], line) == 0)
                return;
    }
    strarray_push(arr, strdup(line));
}

static bool is_c23_route_attr(Token *tok, IncludeRoute *route, Token **rest) {
    if (!equal(tok, "[") || !tok->next || !equal(tok->next, "["))
        return false;
    Token *p = tok->next->next;
    if (!equal(p, "cccc") || !p->next || !equal(p->next, ":") ||
        !p->next->next || !equal(p->next->next, ":") || !p->next->next->next)
        return false;
    p = p->next->next->next;
    IncludeRoute r;
    if (equal(p, "comptime"))
        r = INCLUDE_ROUTE_COMPTIME;
    else if (equal(p, "emit"))
        r = INCLUDE_ROUTE_EMIT;
    else if (equal(p, "shared"))
        r = INCLUDE_ROUTE_SHARED;
    else if (equal(p, "build"))
        r = INCLUDE_ROUTE_BUILD;
    else if (equal(p, "test"))
        r = INCLUDE_ROUTE_TEST;
    else
        return false;
    if (!p->next || !equal(p->next, "]") || !p->next->next ||
        !equal(p->next->next, "]"))
        return false;
    *route = r;
    *rest  = p->next->next->next;
    return true;
}

static bool is_at_route_attr(Token *tok, IncludeRoute *route, Token **rest) {
    if (!equal(tok, "@") || !tok->next)
        return false;
    if (equal(tok->next, "comptime"))
        *route = INCLUDE_ROUTE_COMPTIME;
    else if (equal(tok->next, "emit"))
        *route = INCLUDE_ROUTE_EMIT;
    else if (equal(tok->next, "shared"))
        *route = INCLUDE_ROUTE_SHARED;
    else if (equal(tok->next, "build"))
        *route = INCLUDE_ROUTE_BUILD;
    else if (equal(tok->next, "test"))
        *route = INCLUDE_ROUTE_TEST;
    else
        return false;
    *rest = tok->next->next;
    return true;
}

static bool is_gnu_route_attr(Token *tok, IncludeRoute *route, Token **rest) {
    if (!equal(tok, "__attribute__") || !tok->next || !equal(tok->next, "(") ||
        !tok->next->next || !equal(tok->next->next, "(") ||
        !tok->next->next->next)
        return false;
    Token       *p = tok->next->next->next;
    IncludeRoute r;
    if (equal(p, "comptime"))
        r = INCLUDE_ROUTE_COMPTIME;
    else if (equal(p, "emit"))
        r = INCLUDE_ROUTE_EMIT;
    else if (equal(p, "shared"))
        r = INCLUDE_ROUTE_SHARED;
    else if (equal(p, "build"))
        r = INCLUDE_ROUTE_BUILD;
    else if (equal(p, "test"))
        r = INCLUDE_ROUTE_TEST;
    else
        return false;
    if (!p->next || !equal(p->next, ")") || !p->next->next ||
        !equal(p->next->next, ")"))
        return false;
    *route = r;
    *rest  = p->next->next->next;
    return true;
}

// #1048: true for one of tokenize_private_header()'s own synthetic file
// tags (reflection.h/testing.h/building.h, injected under
// "<implicit-reflection.h>"/"<testing.h>"/"<building.h>" -- see
// src/macros.c, src/testing.c, src/build.c) or reflect.c's own "<quote>"
// pseudo-file (__builtin_quote's template tokenization). Neither is ever
// reached through the ordinary #include auto-capture path
// (emit_include_paths), so marking them cccc-only would never actually
// suppress a real replayed #include -- but cc_file_is_cccc_only() also
// gates record_type_name()'s from_include check (parse_core.c), where
// marking would wrongly flip an internal reflection-API type (Obj, Node,
// ...) to "must be re-derived", producing output that references
// CCCC-internal comptime-only constructs with no host equivalent (the
// exact #1034/#892 regression this exact exclusion list was written to
// prevent -- ticket #1034's own investigation found a broader "<...>"
// prefix match caught these too). Exact match, matching that precedent,
// not a prefix.
static bool is_private_header_tag(const char *filename) {
    if (!filename)
        return false;
    static const char *tags[] = {
        "<implicit-reflection.h>",
        "<building.h>",
        "<testing.h>",
        "<quote>",
        NULL,
    };
    for (int i = 0; tags[i]; i++)
        if (!strcmp(filename, tags[i]))
            return true;
    return false;
}

// #896: mark `filename` as containing cccc-only preprocessor routing syntax
// (@comptime/@shared/@emit/@build/@test, or the [[cccc::...]]/
// __attribute__((...)) spellings) -- never valid to hand to a downstream
// system compiler as raw text. See run_native_backend's re-emission filter
// (main.c) and cc_file_is_cccc_only below.
static void mark_cccc_only_file(VirtualMachine *vm, const char *filename) {
    if (!filename)
        return;
    hashmap_put(&vm->compiler.cccc_only_files, filename, (void *)1);
}

// #896: record that `parent` contains a plain #include resolving to
// `child_path`. Used to find a file that, when opened directly by a
// downstream compiler (because cccc auto-re-emitted a raw #include of it),
// would itself open a file containing cccc-only routing syntax -- the
// routing site may be several #include levels deeper than the file cccc
// actually re-emits.
static void record_include_edge(VirtualMachine *vm, const char *parent,
                                const char *child_path) {
    if (!parent || !child_path)
        return;
    StringArray *children = hashmap_get(&vm->compiler.include_children, parent);
    if (!children) {
        children = arena_alloc(&vm->compiler.parser_arena, sizeof(StringArray));
        memset(children, 0, sizeof(StringArray));
        hashmap_put(&vm->compiler.include_children, parent, children);
    }
    arena_strarray_push(vm, children, arena_strdup(vm, child_path));
}

// #896: true if `filename` itself uses cccc-only routing, or plain-
// #includes (directly or transitively) a file that does. `visited` guards
// against pathological include cycles -- ordinary #include guards normally
// prevent an edge from ever being recorded for a cycle, but this is cheap
// insurance against a false-positive infinite recursion.
static bool file_is_cccc_only_closure(VirtualMachine *vm, const char *filename,
                                      HashMap *visited) {
    if (!filename || hashmap_get(visited, filename))
        return false;
    hashmap_put(visited, filename, (void *)1);
    if (hashmap_get(&vm->compiler.cccc_only_files, filename))
        return true;
    StringArray *children =
        hashmap_get(&vm->compiler.include_children, filename);
    if (!children)
        return false;
    for (int i = 0; i < children->len; i++)
        if (file_is_cccc_only_closure(vm, children->data[i], visited))
            return true;
    return false;
}

bool cc_file_is_cccc_only(VirtualMachine *vm, const char *filename) {
    HashMap visited = {0};
    bool    result  = file_is_cccc_only_closure(vm, filename, &visited);
    hashmap_deinit(&visited);
    return result;
}

// #1096: mark `filename` (an embedded-header key or an on-disk path) as
// resolving to one of CCCC's own bundled headers -- see
// Compiler.cccc_bundled_files' own comment (src/cccc.h) for why this is a
// distinct question from cc_file_is_cccc_only() above. No transitive
// closure needed here (unlike cccc-only routing, "declared in a bundled
// header" doesn't need to chase through further #includes) -- a straight
// hashmap lookup keyed by the exact File.name the declaration's own token
// carries is sufficient.
static void mark_cccc_bundled_file(VirtualMachine *vm, const char *filename) {
    if (!filename)
        return;
    hashmap_put(&vm->compiler.cccc_bundled_files, filename, (void *)1);
}

bool cc_file_is_cccc_bundled(VirtualMachine *vm, const char *filename) {
    return filename &&
           hashmap_get(&vm->compiler.cccc_bundled_files, filename) != NULL;
}

// #1143: mark `dir` (one of vm->compiler.include_paths/system_include_paths'
// own stored strings -- cc_include()/cc_system_include(), src/vm.c) as an
// entry that resolved one of CCCC's own bundled std headers
// (search_include_paths()'s is_std hit, below). See
// Compiler.cccc_bundled_include_dirs' own comment (src/cccc.h) for why
// run_native_backend() (main.c) needs this.
static void mark_cccc_bundled_include_dir(VirtualMachine *vm, const char *dir) {
    if (!dir)
        return;
    hashmap_put(&vm->compiler.cccc_bundled_include_dirs, dir, (void *)1);
}

bool cc_include_dir_is_cccc_bundled(VirtualMachine *vm, const char *dir) {
    return dir &&
           hashmap_get(&vm->compiler.cccc_bundled_include_dirs, dir) != NULL;
}

// #1143 regression: run_native_backend()'s directory-wide -idirafter
// demotion (main.c) is correct for the hand-off headers #1143 was fixing
// (pthread.h/errno.h/fenv.h/etc, each with its own #ifdef __CCCC__ / #else
// #include_next body) and for sched.h/locale.h (deliberately no body of
// their own, relying entirely on directory order to reach the real host
// copy) -- but it demotes the *whole* -I/-isystem entry the instant it
// resolves ANY std header from it, sweeping in headers that were never
// meant to hand off at all (math.h/float.h: zero #include_next in either
// file, documented in man/HEADERS.md as complete, self-contained
// polyfills). Once demoted, a replayed `#include <math.h>` reaches the
// real host's math.h instead, which doesn't declare the C23 IEEE family
// (fmaximum/setpayload/etc) the way CCCC's bundled copy unconditionally
// does -- "undeclared identifier" on both platforms (a regression from the
// documented pre-#1143 behaviour of failing only at *link* time on macOS,
// per #1037).
//
// Fix: for a header that must never be handed off, find the specific
// bundled directory (there's normally exactly one; cc_include_dir_is_cccc_
// bundled() already tracks which -I/-isystem entries qualify) and hand
// back its on-disk path to `basename` so serialize_program.c's
// include-replay loop can substitute an absolute-path #include, bypassing
// directory-search order entirely -- no new flag, no directory-layout
// change, same "force CCCC's own copy" outcome #1143 relied on setjmp.h's
// own dedicated suppression for (serialize_synth_setjmp_decls). Returns
// NULL when no directory was ever marked bundled (the demotion never fired
// in the first place, so the bare replay already resolves correctly) or
// `basename` isn't actually present there.
// A relative -I entry (e.g. the test harness's `-I./include`) resolves
// against *this process's* CWD, but the host cc compiling the generated C
// runs from a different directory (run_native_backend's own temp file) --
// canonicalize to an absolute path so the emitted #include stays valid
// there too. `path` is always freed; returns a freshly strdup'd absolute
// path on success, or NULL if realpath() itself fails (a dangling/
// unreadable entry, treated as "not found" by the caller).
static char *canonicalize_and_free(char *path) {
    char  resolved[PATH_MAX];
    char *abs = realpath(path, resolved);
    free(path);
    return abs ? strdup(abs) : NULL;
}

char *find_cccc_bundled_header_path(VirtualMachine *vm, const char *basename) {
    if (!basename)
        return NULL;
    for (int i = 0; i < vm->compiler.include_paths.len; i++) {
        char *dir = vm->compiler.include_paths.data[i];
        if (!cc_include_dir_is_cccc_bundled(vm, dir))
            continue;
        char *path = format("%s/%s", dir, basename);
        if (file_exists(path))
            return canonicalize_and_free(path);
        free(path);
    }
    for (int i = 0; i < vm->compiler.system_include_paths.len; i++) {
        char *dir = vm->compiler.system_include_paths.data[i];
        if (!cc_include_dir_is_cccc_bundled(vm, dir))
            continue;
        char *path = format("%s/%s", dir, basename);
        if (file_exists(path))
            return canonicalize_and_free(path);
        free(path);
    }
    return NULL;
}

// #1006 (investigation, filed as part of #1005/#1006's fix): true when
// `name` is the exact path of one of the files the user listed on the
// command line, as opposed to a header any of them #included. This used to
// be serialize_program.c's file_is_command_line_input() (added by
// #1002's investigation); promoted to a shared, exported helper so
// record_type_name() (parse.c) and the preprocessor's auto-capture gate
// (below in this file) can use the exact same test serialize_program.c's
// function passes already use, instead of each comparing against
// vm->compiler.primary_file (pinned to input_files[0] forever,
// cc_preprocess/linker.c) the way they did before -- that mismatch is what
// #1006 was about: a type or #include written in input_files[1..N] was
// treated as "not written by the user" and dropped from -c=native/-m
// output. Keyed by File.name, which new_file() (tokenize.c) sets to the
// exact string main.c passed to cc_preprocess(), so a straight lookup is
// sufficient -- no path canonicalization is attempted, matching
// cc_file_is_cccc_only() above.
bool cc_file_is_command_line_input(VirtualMachine *vm, const char *name) {
    return name && hashmap_get(&vm->compiler.command_line_inputs, name) != NULL;
}

// #1001: reset every bit of per-TU preprocessor state before preprocessing
// the *next* command-line input file (main.c's per-TU loop calls this
// between iterations, never before the first). Without this, #define/
// #undef and #pragma once/include-guard bookkeeping from one .c input
// leaked into every later one sharing this cccc invocation's single
// VirtualMachine -- non-conforming (each translation unit's preprocessing
// is independent per the standard) and the direct cause of #1001's own
// repro (a #define in one file, with no #include at all, silently visible
// in another).
//
// The macro table is reset by re-taking a *fresh* deep copy of
// vm->compiler.cli_macro_snapshot (the -D/-U baseline captured once, right
// after CLI processing, in main.c) via hashmap_snapshot -- deliberately
// not a bare hashmap_restore(&vm->compiler.macros,
// vm->compiler.cli_macro_snapshot), which would hand cli_macro_snapshot's
// own bucket array over to vm->compiler.macros (hashmap_restore's contract
// is "install this snapshot", not "copy it") -- corrupting the baseline
// the instant either map is mutated or freed again, and leaving nothing
// for the *third* TU to restore from. hashmap_snapshot deep-copies, so the
// baseline itself is untouched and can be re-taken for every remaining TU.
//
// pragma_once/include_guards/included_headers/guard_macros all own their
// key copies (hashmap_put, not _borrowed -- mirrors the identical ownership
// comment on compile_macro_program's own save/restore of the first two,
// above) so need hashmap_deinit, not _borrowed. cond_incl is a plain
// arena-allocated linked list (push_cond_incl) -- preprocess() itself
// already errors if it's non-NULL at a file's EOF, so a well-formed file
// leaves it NULL anyway; zeroed here defensively in case a recoverable
// per-file error (error_tok_recover) left it non-NULL.
void cc_reset_preprocessor_state_for_next_tu(VirtualMachine *vm) {
    if (vm->compiler.has_cli_macro_snapshot) {
        HashMap fresh = hashmap_snapshot(&vm->compiler.cli_macro_snapshot);
        hashmap_restore(&vm->compiler.macros, fresh);
    } else {
        hashmap_deinit(&vm->compiler.macros);
        memset(&vm->compiler.macros, 0, sizeof(vm->compiler.macros));
    }

    hashmap_deinit(&vm->compiler.pragma_once);
    memset(&vm->compiler.pragma_once, 0, sizeof(vm->compiler.pragma_once));
    hashmap_deinit(&vm->compiler.include_guards);
    memset(&vm->compiler.include_guards, 0,
           sizeof(vm->compiler.include_guards));
    hashmap_deinit(&vm->compiler.included_headers);
    memset(&vm->compiler.included_headers, 0,
           sizeof(vm->compiler.included_headers));
    hashmap_deinit(&vm->compiler.guard_macros);
    memset(&vm->compiler.guard_macros, 0, sizeof(vm->compiler.guard_macros));

    vm->compiler.cond_incl = NULL;
}

static IncludeRoute read_include_route(Token **tok_ptr) {
    IncludeRoute route = INCLUDE_ROUTE_NORMAL;
    Token       *rest  = NULL;
    Token       *tok   = *tok_ptr;
    // Route attributes must appear on the same line as the directive. A token
    // with at_bol=true starts a new line and cannot be part of the current
    // directive — so it is never a route attribute. Without this guard,
    // [[cccc::comptime]] at the start of the next line after a #endif would
    // be misidentified as a routing attribute, leaving cond_incl unpopped.
    if (tok->at_bol)
        return INCLUDE_ROUTE_NORMAL;
    if (is_c23_route_attr(tok, &route, &rest) ||
        is_at_route_attr(tok, &route, &rest) ||
        is_gnu_route_attr(tok, &route, &rest))
        *tok_ptr = rest;
    return route;
}

// Returns true for any #pragma cccc directive. Used to route context-control
// pragmas through handle_pragma_body even when inside an emit block.
static bool is_pragma_cccc(Token *hash) {
    Token *tok = hash->next;
    return tok && equal(tok, "pragma") && equal(tok->next, "cccc");
}

// Returns true for `#pragma pack(...)`. Used to exclude it from the
// auto-capture-and-replay path (#1173) once CCCC honours it directly --
// replaying the raw line would apply it a second time on top of an already
// pack(N)-correct emitted layout, and the auto-capture hoists directives to
// the top of the file, ahead of the structs they were meant to scope.
static bool is_pragma_pack(Token *hash) {
    Token *tok = hash->next;
    return tok && equal(tok, "pragma") && equal(tok->next, "pack");
}

static Token *copy_token(VirtualMachine *vm, Token *tok) {
    Token *t = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *t       = *tok;
    t->next  = NULL;
    return t;
}

static Token *new_eof(VirtualMachine *vm, Token *tok) {
    Token *t = copy_token(vm, tok);
    t->kind  = TK_EOF;
    t->len   = 0;
    return t;
}

// Extract a [[cccc::comptime]] / __attribute__((comptime)) function definition
// and store it. Returns the token after the function definition (or original
// token on failure).
static Token *extract_macro_function(VirtualMachine *vm, Token *tok,
                                     bool  is_macro_entry,
                                     char *attribute_name) {
    // Expected format: <return_type> <function_name>(<params>) { <body> }
    // tok should be the first token of the function definition

    Token *start         = tok;
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
                  "[[cccc::comptime]]: expected function definition");
        return start;
    }

    bool is_variadic       = false;
    int  fixed_param_count = 0;
    {
        Token *param_start = func_name_tok->next->next;
        Token *param_end   = func_name_tok->next;
        int    depth       = 1;
        while (param_end && param_end->kind != TK_EOF) {
            param_end = param_end->next;
            if (equal(param_end, "("))
                depth++;
            else if (equal(param_end, ")")) {
                depth--;
                if (depth == 0)
                    break;
            }
        }

        if (!param_end || param_end->kind == TK_EOF) {
            error_tok(vm, func_name_tok,
                      "[[cccc::comptime]]: unterminated parameter list");
            return start;
        }

        if (param_start != param_end) {
            bool only_void = param_start->kind == TK_IDENT &&
                             param_start->len == 4 &&
                             strncmp(param_start->loc, "void", 4) == 0 &&
                             param_start->next == param_end;
            if (!only_void) {
                bool saw_segment_token = false;
                int  paren             = 0;
                int  bracket           = 0;
                for (Token *t = param_start; t && t != param_end; t = t->next) {
                    if (equal(t, "("))
                        paren++;
                    else if (equal(t, ")") && paren > 0)
                        paren--;
                    else if (equal(t, "["))
                        bracket++;
                    else if (equal(t, "]") && bracket > 0)
                        bracket--;

                    if (paren == 0 && bracket == 0 && equal(t, "...")) {
                        is_variadic = true;
                        break;
                    }

                    if (paren == 0 && bracket == 0 && equal(t, ",")) {
                        if (saw_segment_token)
                            fixed_param_count++;
                        saw_segment_token = false;
                        continue;
                    }

                    if (paren == 0 && bracket == 0)
                        saw_segment_token = true;
                }
                if (saw_segment_token && !is_variadic)
                    fixed_param_count++;
                else if (saw_segment_token && is_variadic)
                    fixed_param_count++;
            }
        }
    }

    // Extract function name
    char *name =
        arena_alloc(&vm->compiler.parser_arena, func_name_tok->len + 1);
    memcpy(name, func_name_tok->loc, func_name_tok->len);
    name[func_name_tok->len] = '\0';

    // Now find the opening brace of the function body
    int paren_depth = 0;
    tok             = func_name_tok->next; // Start at '('

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

    // Now find the opening brace. Stop at a bodyless declaration's ";"
    // instead of walking past it into whatever follows (#884) -- without
    // this, a forward declaration silently swallows the next top-level
    // construct into this function's captured body.
    while (tok && tok->kind != TK_EOF && !equal(tok, "{") && !equal(tok, ";"))
        tok = tok->next;

    if (!equal(tok, "{")) {
        error_tok(vm, start, "[[cccc::comptime]]: expected function body");
        return start;
    }

    Token *body_start = start;

    // Find the closing brace (matching the opening brace)
    int    brace_depth = 0;
    Token *body_end    = tok;
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
    Token  head = {};
    Token *cur  = &head;
    for (Token *t = body_start; t != body_end && t->kind != TK_EOF;
         t        = t->next) {
        cur = cur->next = copy_token(vm, t);
    }
    cur->next = new_eof(vm, body_end ? body_end : tok);

    // Convert preprocessor tokens to parser tokens (TK_PP_NUM -> TK_NUM, etc.)
    convert_pp_tokens(vm, head.next);

    // Detect void return type: check whether the return-type token is 'void'
    // without a following '*' (which would be a void* pointer, not void
    // return).
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

    // Ticket #235: if another macro function has already claimed this
    // attribute name (e.g. a user-defined handler registered earlier in the
    // main translation unit), skip registering this one. This gives
    // user-defined attribute handlers precedence over built-in reflection.h
    // handlers shipped under the same attribute name, and prevents duplicate
    // registrations when reflection.h's handlers are (re-)preprocessed.
    if (attribute_name) {
        for (MacroFn *existing = vm->compiler.macro_fns; existing;
             existing          = existing->next) {
            if (existing->is_attribute_handler && existing->attribute_name &&
                strcmp(existing->attribute_name, attribute_name) == 0) {
                if (vm->debug_vm)
                    printf("Skipping comptime function '%s' for attribute '%s' "
                           "(already claimed by '%s')\n",
                           name, attribute_name, existing->name);
                return body_end ? body_end : tok;
            }
        }
    }

    // Create MacroFn entry
    MacroFn *pm = arena_alloc(&vm->compiler.parser_arena, sizeof(MacroFn));
    memset(pm, 0, sizeof(MacroFn));
    pm->name                 = name;
    pm->body_tokens          = head.next;
    pm->compiled_fn          = NULL;
    pm->is_compiled          = false;
    pm->is_macro_entry       = is_macro_entry;
    pm->is_void_macro        = is_void_macro;
    pm->is_variadic          = is_variadic;
    pm->is_attribute_handler = attribute_name != NULL;
    pm->attribute_name       = attribute_name;
    pm->fixed_param_count    = fixed_param_count;
    pm->next                 = vm->compiler.macro_fns;
    vm->compiler.macro_fns   = pm;

    if (vm->debug_vm) {
        if (pm->is_attribute_handler)
            printf("Captured comptime function '%s' for attribute '%s'\n", name,
                   pm->attribute_name);
        else
            printf("Captured comptime function '%s'\n", name);
    }

    // Return token after the function
    return body_end ? body_end : tok;
}

// Extract a [[cccc::comptime]] variable declaration (not a function).
// Extracts tokens up to and including the terminating ';', creates a
// ComptimeVar entry, and returns the token after the ';'.
static Token *extract_comptime_var(VirtualMachine *vm, Token *tok) {
    Token *start = tok;

    // Find the variable name: the last identifier before '=' or ';' at depth 0.
    char *name = NULL;
    {
        Token *probe       = tok;
        Token *last_ident  = NULL;
        int    brace_depth = 0, bracket_depth = 0;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, "{"))
                brace_depth++;
            else if (equal(probe, "}"))
                brace_depth--;
            else if (equal(probe, "["))
                bracket_depth++;
            else if (equal(probe, "]"))
                bracket_depth--;
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
        int    depth = 0;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, "{"))
                depth++;
            else if (equal(probe, "}"))
                depth--;
            else if (depth == 0) {
                if (equal(probe, "=") || equal(probe, ";"))
                    break;
                if (equal(probe, "*")) {
                    error_tok(
                        vm, probe,
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
    Token  head     = {};
    Token *cur      = &head;
    Token *body_end = NULL;
    {
        int brace_depth = 0;
        for (Token *t = start; t && t->kind != TK_EOF; t = t->next) {
            if (equal(t, "{"))
                brace_depth++;
            else if (equal(t, "}"))
                brace_depth--;
            cur = cur->next = copy_token(vm, t);
            if (brace_depth == 0 && equal(t, ";")) {
                body_end = t->next;
                break;
            }
        }
    }
    if (!body_end) {
        error_tok(
            vm, start,
            "__attribute__((comptime)): variable declaration not terminated");
        return start;
    }
    cur->next = new_eof(vm, body_end);
    convert_pp_tokens(vm, head.next);

    ComptimeVar *cv =
        arena_alloc(&vm->compiler.parser_arena, sizeof(ComptimeVar));
    memset(cv, 0, sizeof(ComptimeVar));
    cv->name                   = name;
    cv->decl_tokens            = head.next;
    cv->next                   = vm->compiler.comptime_vars;
    vm->compiler.comptime_vars = cv;

    if (vm->debug_vm)
        printf("Captured comptime var '%s'\n", name);

    return body_end;
}

static Hideset *new_hideset(VirtualMachine *vm, char *name) {
    Hideset *hs = arena_alloc(&vm->compiler.parser_arena, sizeof(Hideset));
    memset(hs, 0, sizeof(Hideset));
    hs->name = name;
    return hs;
}

static bool hideset_contains(Hideset *hs, char *s, int len);

static Hideset *hideset_union(VirtualMachine *vm, Hideset *hs1, Hideset *hs2) {
    Hideset  head = {};
    Hideset *cur  = &head;

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

static Hideset *hideset_intersection(VirtualMachine *vm, Hideset *hs1,
                                     Hideset *hs2) {
    Hideset  head = {};
    Hideset *cur  = &head;

    for (; hs1; hs1 = hs1->next)
        if (hideset_contains(hs2, hs1->name, strlen(hs1->name)))
            cur = cur->next = new_hideset(vm, hs1->name);
    return head.next;
}

static Token *add_hideset(VirtualMachine *vm, Token *tok, Hideset *hs) {
    Token  head = {};
    Token *cur  = &head;

    for (; tok; tok = tok->next) {
        Token *t   = copy_token(vm, tok);
        t->hideset = hideset_union(vm, t->hideset, hs);
        cur = cur->next = t;
    }
    return head.next;
}

// Append tok2 to the end of tok1.
static Token *append(VirtualMachine *vm, Token *tok1, Token *tok2) {
    if (tok1->kind == TK_EOF)
        return tok2;

    Token  head = {};
    Token *cur  = &head;

    for (; tok1->kind != TK_EOF; tok1 = tok1->next)
        cur = cur->next = copy_token(vm, tok1);
    cur->next = tok2;
    return head.next;
}

static Token *skip_cond_incl2(VirtualMachine *vm, Token *tok, int depth) {
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
static Token *skip_cond_incl(VirtualMachine *vm, Token *tok) {
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
static char *quote_string(VirtualMachine *vm, char *str) {
    int bufsize = 3;
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            bufsize++;
        bufsize++;
    }

    char *buf = arena_alloc(&vm->compiler.parser_arena, bufsize);
    memset(buf, 0, bufsize);
    char *p = buf;
    *p++    = '"';
    for (int i = 0; str[i]; i++) {
        if (str[i] == '\\' || str[i] == '"')
            *p++ = '\\';
        *p++ = str[i];
    }
    *p++ = '"';
    *p++ = '\0';
    return buf;
}

static Token *new_str_token(VirtualMachine *vm, char *str, Token *tmpl) {
    char *buf = quote_string(vm, str);
    return tokenize(vm,
                    new_file(vm, tmpl->file->name, tmpl->file->file_no, buf));
}

// Copy all tokens until the next newline, terminate them with
// an EOF token and then returns them. This function is used to
// create a new list of tokens for `#if` arguments.
static Token *copy_line(VirtualMachine *vm, Token **rest, Token *tok) {
    Token  head = {};
    Token *cur  = &head;

    // Defence in depth: stop at TK_EOF too, not just at_bol -- see the
    // matching comment on skip_line() above. A well-formed token list always
    // has at_bol set on its terminating TK_EOF, so this is normally
    // unreachable; it exists so a future chunk-boundary bug fails safe
    // instead of walking off the end of the list.
    for (; tok->kind != TK_EOF && !tok->at_bol; tok = tok->next)
        cur = cur->next = copy_token(vm, tok);

    cur->next = new_eof(vm, tok);
    *rest     = tok;
    return head.next;
}

static Token *new_num_token(VirtualMachine *vm, int val, Token *tmpl) {
    char *buf = arena_format(vm, "%d\n", val);
    return tokenize(vm,
                    new_file(vm, tmpl->file->name, tmpl->file->file_no, buf));
}

// Generate comma-separated token sequence from binary data
static Token *generate_embed_tokens(VirtualMachine *vm, unsigned char *data,
                                    size_t size, Token *tmpl) {
    if (size == 0)
        return NULL;

    Token  head = {};
    Token *cur  = &head;

    for (size_t i = 0; i < size; i++) {
        // Create numeric token for this byte
        Token *num_stream = new_num_token(vm, data[i], tmpl);
        // Only take the first token (the number), not EOF
        Token *num = copy_token(vm, num_stream);
        num->next  = NULL;
        cur = cur->next = num;

        // Add comma separator (except after last byte)
        if (i < size - 1) {
            Token *comma = copy_token(vm, tmpl);
            comma->kind  = TK_PUNCT;
            comma->len   = 1;
            comma->loc   = ",";
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
static Token *make_comma_token(VirtualMachine *vm, Token *tmpl) {
    Token *comma = copy_token(vm, tmpl);
    comma->kind  = TK_PUNCT;
    comma->len   = 1;
    comma->loc   = ",";
    comma->next  = NULL;
    return comma;
}

// Helper: Append tokens to current position, updating file/line info
static Token *append_tokens(VirtualMachine *vm, Token *cur, Token *tokens,
                            Token *tmpl) {
    for (Token *t = tokens; t; t = t->next) {
        Token *copy = copy_token(vm, t);
        if (tmpl) {
            copy->file    = tmpl->file;
            copy->line_no = tmpl->line_no;
        }
        copy->next = NULL;
        cur = cur->next = copy;
    }
    return cur;
}

static char *escape_c_string(VirtualMachine *vm, const char *s) {
    size_t len = 0;
    for (const char *p = s; *p; p++)
        len += (*p == '\\' || *p == '"') ? 2 : 1;
    char *out = arena_alloc(&vm->compiler.parser_arena, len + 1);
    char *q   = out;
    for (const char *p = s; *p; p++) {
        if (*p == '\\' || *p == '"')
            *q++ = '\\';
        *q++ = *p;
    }
    *q = '\0';
    return out;
}

static Token *append_emit_marker_tokens(VirtualMachine *vm, Token *cur,
                                        Token *tmpl, char *line) {
    char  *escaped = escape_c_string(vm, line);
    char  *src = arena_format(vm, "__builtin_emit_line__(\"%s\");\n", escaped);
    Token *tokens = tokenize_string(vm, "<cccc-emit>", src);
    for (Token *t = tokens; t && t->kind != TK_EOF; t = t->next) {
        Token *copy   = copy_token(vm, t);
        copy->file    = tmpl->file;
        copy->line_no = tmpl->line_no;
        copy->next    = NULL;
        cur = cur->next = copy;
    }
    return cur;
}

// Helper: Copy entire token list with updated source location
static Token *copy_token_list(VirtualMachine *vm, Token *tokens, Token *tmpl) {
    if (!tokens)
        return NULL;

    Token  head = {};
    Token *cur  = &head;

    cur         = append_tokens(vm, cur, tokens, tmpl);

    return head.next;
}

// Generate #embed tokens with prefix, suffix, and if_empty support
static Token *generate_embed_tokens_with_params(
    VirtualMachine *vm, unsigned char *data, size_t size, Token *prefix_tokens,
    Token *suffix_tokens, Token *if_empty_tokens, Token *tmpl) {
    // If empty, use if_empty tokens (ignore prefix/suffix)
    if (size == 0) {
        return if_empty_tokens ? copy_token_list(vm, if_empty_tokens, tmpl)
                               : NULL;
    }

    // Non-empty: assemble prefix + bytes + suffix
    Token  head = {};
    Token *cur  = &head;

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

static bool consume_pp_name(VirtualMachine *vm, Token **rest, Token *tok,
                            char **vendor, char **name) {
    if (tok->kind != TK_IDENT)
        return false;

    Token *first = tok;
    tok          = tok->next;

    if (equal(tok, "::") || (equal(tok, ":") && equal(tok->next, ":"))) {
        tok = equal(tok, "::") ? tok->next : tok->next->next;
        if (tok->kind != TK_IDENT)
            error_tok(vm, tok, "expected identifier after '::'");
        *vendor = arena_strndup(vm, first->loc, first->len);
        *name   = arena_strndup(vm, tok->loc, tok->len);
        *rest   = tok->next;
        return true;
    }

    *vendor = NULL;
    *name   = arena_strndup(vm, first->loc, first->len);
    *rest   = tok;
    return true;
}

static char *resolve_include_probe(VirtualMachine *vm, Token *start,
                                   char *filename, int filename_len,
                                   bool is_dquote) {
    if (filename[0] == '/')
        return filename;

    if (is_dquote) {
        char *relative_path =
            format_relative_path(vm, start->file->name, filename);
        if (file_exists(relative_path))
            return relative_path;
    }

    char *path = search_include_paths(vm, filename, filename_len, !is_dquote);

    if (!path && is_dquote)
        path = search_include_paths(vm, filename, filename_len, true);

    return path;
}

static int eval_has_include(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok          = skip(vm, tok->next, "(");

    bool  is_dquote;
    int   filename_len;
    char *filename =
        read_include_filename(vm, &tok, tok, &is_dquote, &filename_len);
    tok   = skip(vm, tok, ")");
    *rest = tok;

    // URL probes fetch into the shared cache (cache-first), so a probe
    // answers exactly what a following `#include` of the same URL would
    // do -- including reusing an already-cached copy without network I/O.
    if (is_url(filename)) {
#ifdef CCCC_HAS_CURL
        return fetch_url_to_cache(vm, filename) != NULL;
#else
        return 0;
#endif
    }

    char *path =
        resolve_include_probe(vm, start, filename, filename_len, is_dquote);
    if (path && file_exists(path))
        return 1;
    // #1194: resolve_include_probe's own current-file-relative branch never
    // resolves a quoted include written inside an embedded header (its
    // base path is the synthetic "<embedded>/..." key, never real on
    // disk) -- without this, __has_include("../time.h") from inside
    // sys/stat.h silently answers false instead of true.
    if (is_dquote) {
        char *embedded = resolve_embedded_relative_header(
            vm, start->file ? start->file->name : NULL, filename);
        if (embedded)
            return 1;
    }
    return 0;
}

static bool is_has_feature_supported(VirtualMachine *vm, char *name) {
    if (!strcmp(name, "c99"))
        return vm->compiler.c_std >= CCCC_STD_C99;
    if (!strcmp(name, "c11"))
        return vm->compiler.c_std >= CCCC_STD_C11;
    if (!strcmp(name, "c23"))
        return vm->compiler.c_std >= CCCC_STD_C23;

    if (!strcmp(name, "c_alignas") || !strcmp(name, "c_alignof") ||
        !strcmp(name, "c_generic_selections") ||
        !strcmp(name, "c_static_assert"))
        return vm->compiler.c_std >= CCCC_STD_C11;

    return false;
}

typedef enum { ATTR_CCCC, ATTR_STD, ATTR_GNU } AttrCategory;
typedef struct {
    const char  *name;
    AttrCategory cat;
    bool         has_attr;
    long         date;
} AttrInfo;

static const AttrInfo known_attrs[] = {
    // CCCC-specific (cccc:: scoped) — vendor attrs report 1, not a date
    {"comptime", ATTR_CCCC, true, 1},
    {"emit", ATTR_CCCC, true, 1},
    {"optimize", ATTR_CCCC, true, 1},
    {"test", ATTR_CCCC, true, 1},
    {"test_setup", ATTR_CCCC, true, 1},
    {"test_teardown", ATTR_CCCC, true, 1},
    // Checked C-style checked-pointer attributes (#770/#482-484). Only
    // meaningful in post-'*' qualifier position (see pointers() in parse.c);
    // listed here so @single/@array/... route to [[cccc::...]] instead of
    // __attribute__((...)) and __has_c_attribute(cccc::array) etc. work.
    {"single", ATTR_CCCC, true, 1},
    {"array", ATTR_CCCC, true, 1},
    {"ntarray", ATTR_CCCC, true, 1},
    {"count", ATTR_CCCC, true, 1},
    {"byte_count", ATTR_CCCC, true, 1},
    {"bounds", ATTR_CCCC, true, 1},
    // Macro standard library attribute handlers (ticket #235)
    {"serialize", ATTR_CCCC, true, 1},
    {"deserialize", ATTR_CCCC, true, 1},
    {"enum_to_string", ATTR_CCCC, true, 1},
    {"enum_from_string", ATTR_CCCC, true, 1},
    {"generate_getters", ATTR_CCCC, true, 1},
    {"generate_setters", ATTR_CCCC, true, 1},
    {"generate_constructor", ATTR_CCCC, true, 1},
    // Standard C23 ([[name]]) — return C23 version date per N3220 §6.10.10.2
    {"maybe_unused", ATTR_STD, false, 202311L},
    {"deprecated", ATTR_STD, true, 202311L},
    {"noreturn", ATTR_STD, true, 202311L},
    {"nodiscard", ATTR_STD, false, 202311L},
    // fallthrough has a GNU __attribute__ form with real compiler semantics
    // (matches GCC/Clang __has_attribute), so has_attr=true here even though
    // it's listed under the C23 std table for __has_c_attribute's date value.
    {"fallthrough", ATTR_STD, true, 202311L},
    {"no_unique_address", ATTR_STD, false, 202311L},
    // GNU-only (__attribute__((name)))
    {"aligned", ATTR_GNU, true, 0},
    {"packed", ATTR_GNU, true, 0},
    {"unused", ATTR_GNU, true, 0},
    {"__unused__", ATTR_GNU, true, 0},
    {"__deprecated__", ATTR_GNU, true, 0},
    {"format", ATTR_GNU, true, 0},
    // Genuinely-implemented GNU attributes with real compiler semantics
    // (ticket #681) — these were missing from this table despite being
    // fully handled in src/parse.c, so __has_attribute wrongly reported 0.
    {"cleanup", ATTR_GNU, true, 0},
    {"error", ATTR_GNU, true, 0},
    {"warning", ATTR_GNU, true, 0},
    {"nonnull", ATTR_GNU, true, 0},
    {"returns_nonnull", ATTR_GNU, true, 0},
    {"pure", ATTR_GNU, true, 0},
    {"const", ATTR_GNU, true, 0},
    {"warn_unused_result", ATTR_GNU, true, 0},
    // Recognized but architecturally inert (ticket #657): no ELF/Mach-O
    // output, no linker, no per-function ISA codegen, no inliner, no
    // branch-temperature layout, no symbol-level DCE, no strict-aliasing
    // optimizer, no machine-mode type system. Parsed and ignored via the
    // generic attribute fallback (src/parse.c), but reported as
    // recognized so __has_attribute matches real GCC/Clang.
    {"visibility", ATTR_GNU, true, 0},
    {"section", ATTR_GNU, true, 0},
    {"weak", ATTR_GNU, true, 0},
    {"alias", ATTR_GNU, true, 0},
    {"target", ATTR_GNU, true, 0},
    {"always_inline", ATTR_GNU, true, 0},
    {"noinline", ATTR_GNU, true, 0},
    {"flatten", ATTR_GNU, true, 0},
    {"cold", ATTR_GNU, true, 0},
    {"hot", ATTR_GNU, true, 0},
    {"used", ATTR_GNU, true, 0},
    {"may_alias", ATTR_GNU, true, 0},
    {"mode", ATTR_GNU, true, 0},
    {"transparent_union", ATTR_GNU, true, 0},
    {"constructor", ATTR_GNU, true, 0},
    {"destructor", ATTR_GNU, true, 0},
    {NULL, 0, false, 0},
};

static const AttrInfo *find_attr_info(char *name) {
    for (int i = 0; known_attrs[i].name; i++)
        if (!strcmp(name, known_attrs[i].name))
            return &known_attrs[i];

    // GNU __x__ alternate spelling (e.g. __pure__, __cleanup__): strip the
    // leading/trailing "__" and retry, matching real GCC/Clang behavior
    // (ticket #681). The explicit __unused__/__deprecated__ entries above
    // still win via exact match first, so this is purely additive.
    size_t len = strlen(name);
    if (len >= 5 && len < 64 && name[0] == '_' && name[1] == '_' &&
        name[len - 1] == '_' && name[len - 2] == '_') {
        char   inner[64];
        size_t inner_len = len - 4;
        memcpy(inner, name + 2, inner_len);
        inner[inner_len] = '\0';
        for (int i = 0; known_attrs[i].name; i++)
            if (!strcmp(inner, known_attrs[i].name))
                return &known_attrs[i];
    }
    return NULL;
}

static bool is_has_attribute_supported(char *name) {
    const AttrInfo *a = find_attr_info(name);
    return a && a->has_attr;
}

static bool is_has_builtin_supported(char *name) {
    static const char *builtins[] = {
        "__builtin_types_compatible_p",
        "__builtin_classify_type",
        "__builtin_reg_class",
        "__builtin_compare_and_swap",
        "__builtin_atomic_exchange",
        "__builtin_atomic_load",
        "__builtin_atomic_store",
        "__builtin_frame_address",
        "__builtin_huge_val",
        "__builtin_huge_valf",
        "__builtin_huge_vall",
        "__builtin_inf",
        "__builtin_inff",
        "__builtin_infl",
        "__builtin_nan",
        "__builtin_nanf",
        "__builtin_nanl",
        "__builtin_strlen",
        "__builtin_strcmp",
        // #1154: __builtin_memset/memcpy/memmove/memcmp were added
        // alongside strlen/strcmp above (#1144, parse_decl.c) but never
        // added here, so __has_builtin(__builtin_memcpy) returned false
        // while the builtin itself worked.
        "__builtin_memset",
        "__builtin_memcpy",
        "__builtin_memmove",
        "__builtin_memcmp",
        "__builtin_isnan",
        "__builtin_isinf",
        "__builtin_isfinite",
        "__builtin_signbit",
        "__builtin_expect",
        "__builtin_constant_p",
        "__builtin_alloca",
        "__builtin_alloca_with_align",
        "__builtin_unreachable",
        "__builtin_trap",
        "__builtin_debugtrap",
        "__builtin_clz",
        "__builtin_clzll",
        "__builtin_ctz",
        "__builtin_ctzll",
        "__builtin_popcount",
        "__builtin_popcountll",
        "__builtin_parity",
        "__builtin_parityll",
        "__builtin_ffs",
        "__builtin_ffsll",
        "__builtin_bswap16",
        "__builtin_bswap32",
        "__builtin_bswap64",
        "__builtin_add_overflow",
        "__builtin_sub_overflow",
        "__builtin_mul_overflow",
        // Parser special forms with dedicated codegen (ticket #682) — these
        // were missing despite being fully implemented in src/parse.c.
        "__builtin_choose_expr",
        "__builtin_return_address",
        "__builtin_object_size",
        "__builtin_dynamic_object_size",
        "__builtin_expect_with_probability",
        "__builtin_prefetch",
        "__builtin_assume",
        "__builtin_pc_function_name",
        "__builtin_pc_source_location",
        NULL,
    };

    for (int i = 0; builtins[i]; i++)
        if (!strcmp(name, builtins[i]))
            return true;
    return false;
}

static long is_has_c_attribute_supported(char *vendor, char *name) {
    const AttrInfo *a = find_attr_info(name);
    if (!a)
        return 0;
    if (!vendor)
        return (a->cat == ATTR_STD) ? a->date : 0;
    if (!strcmp(vendor, "cccc"))
        return (a->cat == ATTR_CCCC) ? a->date : 0;
    return 0;
}

static int eval_has_name(VirtualMachine *vm, Token **rest, Token *tok,
                         char *kind) {
    tok = skip(vm, tok->next, "(");

    char *vendor;
    char *name;
    if (!consume_pp_name(vm, &tok, tok, &vendor, &name))
        error_tok(vm, tok, "expected identifier");

    if (equal(tok, ",")) {
        tok = tok->next;
        if (vendor)
            error_tok(vm, tok, "expected a single vendor qualifier");
        if (tok->kind != TK_IDENT)
            error_tok(vm, tok, "expected vendor identifier");
        vendor = arena_strndup(vm, tok->loc, tok->len);
        tok    = tok->next;
    }

    tok   = skip(vm, tok, ")");
    *rest = tok;

    if (!strcmp(kind, "__has_feature") || !strcmp(kind, "__has_extension"))
        return is_has_feature_supported(vm, name);
    if (!strcmp(kind, "__has_attribute"))
        return is_has_attribute_supported(name);
    if (!strcmp(kind, "__has_builtin"))
        return is_has_builtin_supported(name);
    if (!strcmp(kind, "__has_c_attribute"))
        return is_has_c_attribute_supported(vendor, name);
    return 0;
}

static Token *read_const_expr(VirtualMachine *vm, Token **rest, Token *tok) {
    tok         = copy_line(vm, rest, tok);

    Token  head = {};
    Token *cur  = &head;

    while (tok->kind != TK_EOF) {
        // "defined(foo)" or "defined foo" becomes "1" if macro "foo"
        // is defined. Otherwise "0".
        if (equal(tok, "defined")) {
            Token *start     = tok;
            bool   has_paren = consume(vm, &tok, tok->next, "(");

            if (tok->kind != TK_IDENT)
                error_tok(vm, start, "macro name must be an identifier");
            Macro *m = find_macro(vm, tok);
            if (m)
                m->use_count++;
            tok = tok->next;

            if (has_paren)
                tok = skip(vm, tok, ")");

            cur = cur->next = new_num_token(vm, m ? 1 : 0, start);
            continue;
        }

        if (equal(tok, "__has_include")) {
            Token *start  = tok;
            int    result = eval_has_include(vm, &tok, tok);
            cur = cur->next = new_num_token(vm, result, start);
            continue;
        }

        if (equal(tok, "__has_feature") || equal(tok, "__has_extension") ||
            equal(tok, "__has_attribute") || equal(tok, "__has_builtin") ||
            equal(tok, "__has_c_attribute") ||
            equal(tok, "__has_cpp_attribute")) {
            Token *start  = tok;
            char  *kind   = arena_strndup(vm, tok->loc, tok->len);
            int    result = eval_has_name(vm, &tok, tok, kind);
            cur = cur->next = new_num_token(vm, result, start);
            continue;
        }

        // "__has_embed(filename)" returns 0 (not found), 1 (non-empty), or 2
        // (empty)
        if (equal(tok, "__has_embed")) {
            Token *start = tok;
            tok          = skip(vm, tok->next, "(");

            // Parse filename
            bool  is_dquote;
            int   filename_len;
            char *filename =
                read_include_filename(vm, &tok, tok, &is_dquote, &filename_len);

            tok = skip(vm, tok, ")");

            // Determine result: 0 = not found, 1 = non-empty, 2 = empty
            int result = 0;

            if (is_url(filename)) {
                // Same URL policy as __has_include above: fetch into the
                // shared cache and judge the cached copy, so both probes
                // agree with each other and with a real `#embed`.
#ifdef CCCC_HAS_CURL
                char *cache_path = fetch_url_to_cache(vm, filename);
                if (cache_path) {
                    struct stat st;
                    if (!stat(cache_path, &st))
                        result = (st.st_size == 0) ? 2 : 1;
                }
#endif
            } else {
                char *path = resolve_include_probe(vm, start, filename,
                                                   filename_len, is_dquote);

                if (path && file_exists(path)) {
                    size_t         file_size;
                    unsigned char *data =
                        read_binary_file(vm, path, &file_size);
                    if (data) {
                        result = (file_size == 0) ? 2 : 1;
                    }
                }
            }

            cur = cur->next = new_num_token(vm, result, start);
            continue;
        }

        cur = cur->next = tok;
        tok             = tok->next;
    }

    cur->next = tok;
    return head.next;
}

// Read and evaluate a constant expression.
static long eval_const_expr(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    Token *expr  = read_const_expr(vm, rest, tok->next);
    // #889: defined(...) / __has_include(...) inside expr are rewritten by
    // read_const_expr into synthetic tokens minted via new_num_token(), which
    // allocates a fresh File*. The recursive preprocess2() below must not let
    // comptime/emit block interception see those tokens as "the block's file
    // changed" and auto-close an open block -- bump the depth counter so
    // those interception sites stand down for the duration of this call.
    int saved_depth                  = vm->compiler.pp_const_expr_depth;
    vm->compiler.pp_const_expr_depth = saved_depth + 1;
    expr                             = preprocess2(vm, expr);
    vm->compiler.pp_const_expr_depth = saved_depth;

    if (expr->kind == TK_EOF)
        error_tok(vm, start, "no expression");

    // [https://www.sigbus.info/n1570#6.10.1p4] The standard requires
    // we replace remaining non-macro identifiers with "0" before
    // evaluating a constant expression. For example, `#if foo` is
    // equivalent to `#if 0` if foo is not defined.
    //
    // C23 §6.10.1 additionally specifies that `true` evaluates to 1
    // and `false` evaluates to 0 in preprocessor constant expressions.
    // In C23 mode these tokens are TK_KEYWORD (not TK_IDENT), so the
    // generic ident→0 pass below would leave them unhandled — producing
    // wrong results (e.g. `#if true` evaluating to 0). Handle them
    // explicitly first, before the generic TK_IDENT fallback.
    for (Token *t = expr; t->kind != TK_EOF; t = t->next) {
        if (equal(t, "true")) {
            // C23 §6.10.1: true → pp-number 1
            Token *next = t->next;
            *t          = *new_num_token(vm, 1, t);
            t->next     = next;
        } else if (equal(t, "false")) {
            // C23 §6.10.1: false → pp-number 0
            Token *next = t->next;
            *t          = *new_num_token(vm, 0, t);
            t->next     = next;
        } else if (t->kind == TK_IDENT) {
            Token *next = t->next;
            *t          = *new_num_token(vm, 0, t);
            t->next     = next;
        }
    }

    // Convert pp-numbers to regular numbers
    convert_pp_tokens(vm, expr);

    Token *rest2;
    long   val = const_expr(vm, &rest2, expr);
    if (rest2->kind != TK_EOF)
        error_tok(vm, rest2, "extra tokens after #if expression");
    return val;
}

static CondIncl *push_cond_incl(VirtualMachine *vm, Token *tok, bool included) {
    CondIncl *ci = arena_alloc(&vm->compiler.parser_arena, sizeof(CondIncl));
    memset(ci, 0, sizeof(CondIncl));
    ci->next               = vm->compiler.cond_incl;
    ci->ctx                = IN_THEN;
    ci->tok                = tok;
    ci->included           = included;
    vm->compiler.cond_incl = ci;
    return ci;
}

static Macro *find_macro(VirtualMachine *vm, Token *tok) {
    if (tok->kind != TK_IDENT)
        return NULL;
    return hashmap_get2(&vm->compiler.macros, tok->loc, tok->len);
}

// #887: used by parse.c's undefined-variable diagnostic (in_macro_mode only)
// to tell a genuinely undefined identifier apart from one that's only
// invisible because isolate_comptime_macros stripped it before the comptime
// preprocess/parse (source-file #defines are never forwarded into comptime
// bodies -- see man/MACROS.md). Looks the name up in the pre-isolation
// snapshot taken by compile_macro_program (macro_snapshot_backup), which
// stays populated for the lifetime of that compile. define_tok is non-NULL
// only for a #define with a real source site (NULL for CCCC builtins and
// -D command-line defines, which the comptime pass forwards anyway).
bool cc_is_source_define_name(VirtualMachine *vm, const char *name, int len) {
    if (!vm->compiler.has_macro_snapshot)
        return false;
    Macro *m = hashmap_get2(&vm->compiler.macro_snapshot_backup, name, len);
    return m && m->define_tok != NULL;
}

static Macro *add_macro(VirtualMachine *vm, char *name, int name_len,
                        bool is_objlike, Token *body, Token *define_tok) {
    Macro *m = arena_alloc(&vm->compiler.parser_arena, sizeof(Macro));
    memset(m, 0, sizeof(Macro));
    // #1097: pre-existing bug found (not introduced) while verifying #1044
    // against tests/test_minilua.c's own `-DLUA_USE_JUMPTABLE=0`
    // CCCC_FLAGS: every other caller passes a `name` backed by arena/token
    // storage that outlives this Macro, but define_macro() (below) is
    // reachable from main.c's parse_define(), which heap-allocates `name`
    // for a `-DFOO=bar` command-line define and frees it right after
    // cc_define() returns -- storing that raw pointer here left `m->name`
    // (read by, among others, hideset_union()) dangling the instant the
    // caller freed it. Confirmed via AddressSanitizer (heap-use-after-free,
    // preprocess.c:hideset_union) against unmodified trunk, not just this
    // branch -- a nondeterministic, heap-layout-dependent bug, silent until
    // something else's allocation pattern (here, #1044's own new serializer
    // arrays) shifted enough to land on freed memory. A copy here is safe
    // for every caller (the arena never frees), so always duplicate rather
    // than special-case the one caller that doesn't already own long-lived
    // storage.
    m->name       = arena_strndup(vm, name, name_len);
    m->is_objlike = is_objlike;
    m->body       = body;
    m->define_tok = define_tok;
    hashmap_put2(&vm->compiler.macros, name, name_len, m);
    return m;
}

static MacroParam *read_macro_params(VirtualMachine *vm, Token **rest,
                                     Token *tok, char **va_args_name) {
    MacroParam  head = {};
    MacroParam *cur  = &head;

    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");

        if (equal(tok, "...")) {
            *va_args_name = "__VA_ARGS__";
            *rest         = skip(vm, tok->next, ")");
            return head.next;
        }

        if (tok->kind != TK_IDENT)
            error_tok(vm, tok, "expected an identifier");

        if (equal(tok->next, "...")) {
            *va_args_name = arena_strndup(vm, tok->loc, tok->len);
            *rest         = skip(vm, tok->next->next, ")");
            return head.next;
        }

        MacroParam *m =
            arena_alloc(&vm->compiler.parser_arena, sizeof(MacroParam));
        memset(m, 0, sizeof(MacroParam));
        m->name = arena_strndup(vm, tok->loc, tok->len);
        cur = cur->next = m;
        tok             = tok->next;
    }

    *rest = tok->next;
    return head.next;
}

static void read_macro_definition(VirtualMachine *vm, Token **rest,
                                  Token *tok) {
    // #888: #define @shared NAME ... opts this one macro into visibility
    // during the isolated comptime preprocessing pass (isolate_comptime_macros
    // keeps entries with is_shared set even though define_tok != NULL). Any
    // other route attribute on #define is rejected by the caller before this
    // function is reached, so the only route possible here is SHARED or NORMAL.
    bool         is_shared  = false;
    Token       *route_rest = tok;
    IncludeRoute route      = read_include_route(&route_rest);
    if (route == INCLUDE_ROUTE_SHARED) {
        is_shared = true;
        tok       = route_rest;
    }

    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "macro name must be an identifier");
    char  *name     = arena_strndup(vm, tok->loc, tok->len);
    int    name_len = tok->len; // Save name length before moving tok
    Token *name_tok = tok;      // Save for define_tok
    tok             = tok->next;

    Macro *m;
    if (!tok->has_space && equal(tok, "(")) {
        // Function-like macro
        char       *va_args_name = NULL;
        MacroParam *params =
            read_macro_params(vm, &tok, tok->next, &va_args_name);

        m = add_macro(vm, name, name_len, false, copy_line(vm, rest, tok),
                      name_tok);
        m->params       = params;
        m->va_args_name = va_args_name;
    } else {
        // Object-like macro
        m = add_macro(vm, name, name_len, true, copy_line(vm, rest, tok),
                      name_tok);
    }
    m->is_shared = is_shared;
}

static MacroArg *read_macro_arg_one(VirtualMachine *vm, Token **rest,
                                    Token *tok, bool read_rest) {
    Token  head  = {};
    Token *cur   = &head;
    int    level = 0;

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
        tok             = tok->next;
    }

    cur->next     = new_eof(vm, tok);

    MacroArg *arg = arena_alloc(&vm->compiler.parser_arena, sizeof(MacroArg));
    memset(arg, 0, sizeof(MacroArg));
    arg->tok = head.next;
    *rest    = tok;
    return arg;
}

static MacroArg *read_macro_args(VirtualMachine *vm, Token **rest, Token *tok,
                                 MacroParam *params, char *va_args_name) {
    Token *start     = tok;
    tok              = tok->next->next;

    MacroArg    head = {};
    MacroArg   *cur  = &head;

    MacroParam *pp   = params;
    for (; pp; pp = pp->next) {
        if (cur != &head)
            tok = skip(vm, tok, ",");
        cur = cur->next = read_macro_arg_one(vm, &tok, tok, false);
        cur->name       = pp->name;
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
        error_tok(vm, start, "too many arguments to macro '%.*s'", start->len,
                  start->loc);
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
static char *join_tokens(VirtualMachine *vm, Token *tok, Token *end,
                         int *out_len) {
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
static Token *stringize(VirtualMachine *vm, Token *hash, Token *arg) {
    // Create a new string token. We need to set some value to its
    // source location for error reporting function, so we use a macro
    // name token as a template.
    char *s = join_tokens(vm, arg, NULL, NULL);
    return new_str_token(vm, s, hash);
}

// Concatenate two tokens to create a new token.
static Token *paste(VirtualMachine *vm, Token *lhs, Token *rhs) {
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
static Token *subst(VirtualMachine *vm, Token *tok, MacroArg *args) {
    Token  head = {};
    Token *cur  = &head;

    while (tok->kind != TK_EOF) {
        // "#" followed by a parameter is replaced with stringized actuals.
        if (equal(tok, "#")) {
            MacroArg *arg = find_arg(args, tok->next);
            if (!arg)
                error_tok(vm, tok->next,
                          "'#' is not followed by a macro parameter");
            cur = cur->next = stringize(vm, tok, arg->tok);
            tok             = tok->next->next;
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
                    tok             = tok->next->next;
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
                         t        = t->next)
                        cur = cur->next = copy_token(vm, t);
                }
                tok = tok->next->next;
                continue;
            }

            *cur = *paste(vm, cur, tok->next);
            tok  = tok->next->next;
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
                             e        = e->next)
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
            Token *t     = preprocess2(vm, arg->tok);
            t->at_bol    = tok->at_bol;
            t->has_space = tok->has_space;
            for (; t->kind != TK_EOF; t = t->next) {
                Token *c   = copy_token(vm, t);
                c->hideset = NULL;
                cur = cur->next = c;
            }
            tok = tok->next;
            continue;
        }

        // Handle a non-macro token.
        cur = cur->next = copy_token(vm, tok);
        tok             = tok->next;
        continue;
    }

    cur->next = tok;
    return head.next;
}

// If tok is a macro, expand it and return true.
// Otherwise, do nothing and return false.
static bool expand_macro(VirtualMachine *vm, Token **rest, Token *tok) {
    if (hideset_contains(tok->hideset, tok->loc, tok->len))
        return false;

    Macro *m = find_macro(vm, tok);
    if (!m)
        return false;

    m->use_count++;

    // Built-in dynamic macro application such as __LINE__
    if (m->handler) {
        *rest         = m->handler(vm, tok);
        (*rest)->next = tok->next;
        return true;
    }

    // Object-like macro application
    if (m->is_objlike) {
        Hideset *hs = hideset_union(vm, tok->hideset, new_hideset(vm, m->name));
        Token   *body = add_hideset(vm, m->body, hs);
        for (Token *t = body; t->kind != TK_EOF; t = t->next)
            t->origin = tok;
        *rest              = append(vm, body, tok->next);
        (*rest)->at_bol    = tok->at_bol;
        (*rest)->has_space = tok->has_space;
        return true;
    }

    // If a funclike macro token is not followed by an argument list,
    // treat it as a normal identifier.
    if (!equal(tok->next, "("))
        return false;

    // Function-like macro application
    Token    *macro_token = tok;
    MacroArg *args = read_macro_args(vm, &tok, tok, m->params, m->va_args_name);
    Token    *rparen = tok;

    // Tokens that consist a func-like macro invocation may have different
    // hidesets, and if that's the case, it's not clear what the hideset
    // for the new tokens should be. We take the interesection of the
    // macro token and the closing parenthesis and use it as a new hideset
    // as explained in the Dave Prossor's algorithm.
    Hideset *hs =
        hideset_intersection(vm, macro_token->hideset, rparen->hideset);
    hs          = hideset_union(vm, hs, new_hideset(vm, m->name));

    Token *body = subst(vm, m->body, args);
    body        = add_hideset(vm, body, hs);
    for (Token *t = body; t->kind != TK_EOF; t = t->next)
        t->origin = macro_token;
    *rest              = append(vm, body, tok->next);
    (*rest)->at_bol    = macro_token->at_bol;
    (*rest)->has_space = macro_token->has_space;
    return true;
}

static bool file_exists(char *path) {
    struct stat st;
    return !stat(path, &st);
}

static char *format_relative_path(VirtualMachine *vm, char *base_file,
                                  char *filename) {
    char *slash = strrchr(base_file, '/');
    if (!slash)
        return arena_format(vm, "./%s", filename);
    return arena_format(vm, "%.*s/%s", (int)(slash - base_file), base_file,
                        filename);
}

// #1194: lexically join `dir` with `filename`, collapsing "." and ".."
// components as plain string operations -- no filesystem access, since an
// embedded header's synthetic "<embedded>/..." path never exists on disk
// for realpath()/stat() to resolve. Used only to chase a quoted #include's
// "../" spelling written inside an embedded header back down to the
// embedded-table key it should resolve to.
static char *lexical_path_join(VirtualMachine *vm, const char *dir,
                               const char *filename) {
    char *joined  = arena_format(vm, "%s/%s", dir, filename);
    char *scratch = arena_strdup(vm, joined);
    char *parts[64];
    int   n    = 0;
    char *save = NULL;
    for (char *part = strtok_r(scratch, "/", &save); part;
         part       = strtok_r(NULL, "/", &save)) {
        if (part[0] == '\0' || strcmp(part, ".") == 0)
            continue;
        if (strcmp(part, "..") == 0) {
            if (n > 0 && strcmp(parts[n - 1], "..") != 0)
                n--;               // pop a real component
            else if (n < 64)
                parts[n++] = part; // leading ".." (out of the joined root)
            continue;
        }
        if (n < 64)
            parts[n++] = part;
    }
    if (n == 0)
        return arena_strdup(vm, ".");
    char *out = arena_strdup(vm, parts[0]);
    for (int i = 1; i < n; i++)
        out = arena_format(vm, "%s/%s", out, parts[i]);
    return out;
}

// #1194: resolve a quoted #include written inside an EMBEDDED header
// (base_file spelled "<embedded>/some/path.h", the synthetic key
// embedded_header_key() gives such a header -- never a real path on disk).
// The ordinary current-file-relative branch always misses for these (the
// "<embedded>/..." prefix never exists as a real directory), which
// degrades every such include to a literal embedded-table lookup by its
// own spelling -- fine for "sys/types.h" (a table key as written), but
// "../time.h" can never be one. Lexically resolve the include against the
// embedded header's own virtual directory instead, strip the "<embedded>/"
// prefix back off, and hand the remainder to get_std_header() the same way
// try_embedded_std_header() does. Returns NULL if base_file isn't an
// embedded header, or the normalized spelling isn't a known embedded
// header.
static char *resolve_embedded_relative_header(VirtualMachine *vm,
                                              char *base_file, char *filename) {
    static const char PREFIX[]   = "<embedded>/";
    size_t            prefix_len = sizeof(PREFIX) - 1;
    if (!base_file || strncmp(base_file, PREFIX, prefix_len) != 0)
        return NULL;
    char *embedded_path = base_file + prefix_len;
    char *slash         = strrchr(embedded_path, '/');
    char *dir =
        slash ? arena_strndup(vm, embedded_path, (int)(slash - embedded_path))
              : arena_strdup(vm, "");
    char *joined = lexical_path_join(vm, dir, filename);
    if (get_std_header(joined))
        return joined;
    return NULL;
}

// Headers that must always resolve to CCCC's own copies because they are
// tightly coupled to the VM ABI or the compiler's type system.
//
// - stdarg.h / setjmp.h: va_list and jmp_buf have CCCC-VM-specific layouts;
//   the SDK copies use compiler builtins that do not match.
// - stdbool.h / stddef.h / stdint.h / inttypes.h: CCCC's versions are
//   authoritative for the built-in boolean and integer types it exposes.
// - complex.h: creal/cimag/CMPLX etc. lower to CCCC-specific __cccc_* builtins
//   rather than the real complex-argument-passing ABI a genuine SDK copy
//   expects, so a real SDK complex.h compiles but silently miscodegens.
// - stdatomic.h / stdckdint.h: atomic_load/atomic_store/ckd_add etc. lower to
//   CCCC-specific __builtin_atomic_*/__builtin_*_overflow VM builtins, not a
//   real hosted implementation, so a real SDK copy would silently miscodegen
//   the same way.
//
// These are never overridden even when --use-system-headers is active.
//
// #1031: not static -- src/serialize_type.c's type_layout_is_host_owned() also
// needs this list, to exclude stdarg.h's va_list/setjmp.h's jmp_buf from
// -c=native's general sizeof/_Alignof re-materialization. Those two
// deliberately use the opposite strategy from an ordinary from_include
// type (widen CCCC's own layout to cover every supported host's real one,
// so the *guest-folded* constant stays a safe upper bound on purpose --
// see their own man/NATIVE.md entries) -- re-materializing the operator
// for them would defeat that, replacing the safe padded literal with
// whatever the real host's own (possibly smaller, via the header's own
// #include_next hand-off) va_list/jmp_buf size happens to be.
bool is_compiler_owned_header(const char *name) {
    static const char *owned[] = {
        "stdarg.h",   "setjmp.h",  "stdbool.h",   "stddef.h",    "stdint.h",
        "inttypes.h", "complex.h", "stdatomic.h", "stdckdint.h", NULL,
    };
    for (int i = 0; owned[i]; i++)
        if (!strcmp(name, owned[i]))
            return true;
    return false;
}

// #1003: standard headers whose CCCC copy is not merely *preferred* (like
// is_compiler_owned_header's ABI-coupled list above) but is the **only**
// implementation likely to exist at all on a typical host -- a C23/C11
// header some platforms haven't shipped yet (stdbit.h, stdckdint.h,
// threads.h, uchar.h), an Apple-only compatibility shim whose CCCC copy is
// a deliberate stub (Availability.h), or a CCCC-specific extension header
// with no standard name to collide with in the first place (decimal_math.h,
// gated on CCCC_HAS_DECIMAL and declaring VM-only __cccc_dec_* symbols).
// `-c=native`/`-m`/`-c=generated` auto-capture a plain #include and replay
// it verbatim into the generated C (see man/HEADERS.md) -- correct for a
// header the host is expected to have, but for one of these it produces an
// unresolvable "file not found" from the downstream compiler even though
// CCCC itself compiled the program fine. The PP_INCLUDE handler below marks
// such a header cccc-only (mark_cccc_only_file) the moment it resolves,
// reusing the #896/#999 machinery wholesale: the replay is suppressed
// (cc_file_is_cccc_only, serialize_program.c's #include loop) and the header's
// own content is re-derived into the output instead (record_type_name's
// from_include check, parse.c; function_is_header_supplied,
// serialize_program.c) -- the same two mechanisms a @comptime-routed include
// already gets. This is distinct from "owned": is_compiler_owned_header is
// neither necessary nor sufficient here (stdckdint.h is owned and header-only,
// so suppressing its replay alone is enough; stdbit.h is not owned but still
// needs this).
static bool is_cccc_supplied_only_header(const char *name) {
    static const char *cccc_only[] = {
        "stdbit.h",       "stdckdint.h",    "threads.h", "uchar.h",
        "Availability.h", "decimal_math.h", NULL,
    };
    for (int i = 0; cccc_only[i]; i++)
        if (!strcmp(name, cccc_only[i]))
            return true;
    return false;
}

// Whether CCCC's own copy of a standard header — the embedded src/std.c
// table (tried by the PP_INCLUDE handler before ever calling this function;
// see man/HEADERS.md) or the on-disk builtin_include_dir fallback below —
// should be considered at all for this header name: always for owned
// headers (no valid SDK substitute exists for them), and for other known-std
// headers unless --no-builtin-includes was passed (which asks to fail
// outright, rather than silently fall back, once the SDK copy is missing).
static bool wants_builtin_header(VirtualMachine *vm, bool is_std, bool owned) {
    return owned || (is_std && !vm->compiler.no_builtin_includes);
}

char *search_include_paths(VirtualMachine *vm, char *filename, int filename_len,
                           bool is_system) {
    if (filename[0] == '/')
        return filename;

    char *cached =
        hashmap_get2(&vm->compiler.include_cache, filename, filename_len);
    if (cached)
        return cached;

    // Determine how aggressively we force CCCC's own headers.
    //
    // Default mode (use_system_headers=false):
    //   All headers known to CCCC (get_std_header != NULL) are forced to
    //   resolve from CCCC's own copies rather than any system directory.
    //
    // --use-system-headers mode:
    //   Headers in is_compiler_owned_header() are still forced to CCCC.
    //   All other std headers prefer system_include_paths first, then fall
    //   back to CCCC's own copy (unless --no-builtin-includes is also set).
    //
    // --no-builtin-includes (requires --use-system-headers):
    //   Non-owned std headers do NOT fall back to CCCC's own copy; if
    //   missing from system paths the include fails with "cannot open file".
    bool is_std = get_std_header(filename) != NULL;
    // `owned` must NOT be gated on is_std (#842): is_std reflects whatever
    // subset of headers happens to be in the embedded get_std_header table
    // (e.g. a near-empty stage0 seed table), but is_compiler_owned_header()
    // is a fixed, VM-ABI-driven list (stdarg.h, stdint.h, ...) that must
    // stay force-resolved to CCCC's own copies regardless of what the table
    // currently knows about — otherwise a reduced table silently
    // de-protects them under --use-system-headers.
    bool owned       = is_compiler_owned_header(filename);
    bool force_cccc  = owned || (!vm->compiler.use_system_headers && is_std);
    bool try_builtin = wants_builtin_header(vm, is_std, owned);

    if (vm->compiler.use_system_headers && is_std && !owned && is_system) {
        // In system-header mode, try SDK directories before CCCC's own copy.
        // CCCC's own builtin include dir is never registered in
        // system_include_paths (see cc_init), so no skip is needed here.
        for (int i = 0; i < vm->compiler.system_include_paths.len; i++) {
            const char *dir  = vm->compiler.system_include_paths.data[i];
            char       *path = format("%s/%s", dir, filename);
            if (file_exists(path)) {
                hashmap_put2(&vm->compiler.include_cache, filename,
                             filename_len, path);
                vm->compiler.include_next_idx =
                    vm->compiler.include_paths.len + i + 1;
                return path;
            }
            free(path);
        }
        // SDK copy not found. If --no-builtin-includes, do not fall back.
        if (!try_builtin)
            return NULL;
    }

    // For <...> includes, search -I paths first and then --isystem paths.
    // For "..." includes, the caller handles current-file-relative lookup and
    // this helper searches -I paths.
    for (int i = 0; i < vm->compiler.include_paths.len; i++) {
        char *path =
            format("%s/%s", vm->compiler.include_paths.data[i], filename);
        if (file_exists(path)) {
            hashmap_put2(&vm->compiler.include_cache, filename, filename_len,
                         path);
            vm->compiler.include_next_idx = i + 1;
            // #1143: this -I entry just resolved one of CCCC's own bundled
            // std headers (e.g. a test harness's `-I./include`) -- record
            // it so run_native_backend() (main.c) forwards it demoted
            // (`-idirafter`) rather than as a plain `-I` that would shadow
            // the real host headers -c=native needs.
            if (is_std)
                mark_cccc_bundled_include_dir(
                    vm, vm->compiler.include_paths.data[i]);
            return path;
        }
        free(path);
    }

    // CCCC's own bundled headers, on disk. This is a fallback only — the
    // PP_INCLUDE handler tries the embedded src/std.c table first, which
    // covers standard headers without touching the filesystem at all (see
    // man/HEADERS.md). This path still matters for: a stage0 build linked
    // against src/std_stub.c (embeds nothing, so the embedded lookup always
    // misses); the three private headers (reflection.h/testing.h/building.h,
    // via tokenize_private_header) on a stage0 build; and as a safety net if
    // a build's embedded table is stale or partial.
    if (try_builtin && vm->compiler.builtin_include_dir) {
        char *path =
            format("%s/%s", vm->compiler.builtin_include_dir, filename);
        if (file_exists(path)) {
            hashmap_put2(&vm->compiler.include_cache, filename, filename_len,
                         path);
            return path;
        }
        free(path);
    }

    if (force_cccc || !is_system)
        return NULL;

    for (int i = 0; i < vm->compiler.system_include_paths.len; i++) {
        char *path = format("%s/%s", vm->compiler.system_include_paths.data[i],
                            filename);
        if (file_exists(path)) {
            hashmap_put2(&vm->compiler.include_cache, filename, filename_len,
                         path);
            vm->compiler.include_next_idx =
                vm->compiler.include_paths.len + i + 1;
            // #1143: same reasoning as the include_paths loop above, for a
            // user `-isystem` entry that happens to also hold one of
            // CCCC's own bundled std headers.
            if (is_std)
                mark_cccc_bundled_include_dir(
                    vm, vm->compiler.system_include_paths.data[i]);
            return path;
        }
        free(path);
    }

    return NULL;
}

// Tokenize one of CCCC's private headers (include/cccc/reflection.h,
// testing.h, building.h) for internal injection (implicit_reflection_tokens,
// cc_inject_test_header, cc_inject_build_header). Prefers the embedded std
// table; falls back to resolving "cccc/<name>" on disk via the normal -I
// include search path, so a stage0 compiler linked against src/std_stub.c
// (which embeds nothing) can still find these via `-I./include`.
Token *tokenize_private_header(VirtualMachine *vm, char *name, char *tag) {
    char *src = get_std_header(name);
    if (src)
        return tokenize_string(vm, tag, src);

    char *rel  = format("cccc/%s", name);
    char *path = search_include_paths(vm, rel, (int)strlen(rel), false);
    if (!path)
        error("cannot find cccc/%s -- run `make bootstrap` (or pass "
              "-I<repo>/include) to make it resolvable",
              name);

    Token *toks = tokenize_file(vm, path, false);
    if (!toks)
        error("cannot open %s", path);
    return toks;
}

static char *search_include_next(VirtualMachine *vm, char *filename) {
    // First search include_paths
    for (; vm->compiler.include_next_idx < vm->compiler.include_paths.len;
         vm->compiler.include_next_idx++) {
        char *path = arena_format(
            vm, "%s/%s",
            vm->compiler.include_paths.data[vm->compiler.include_next_idx],
            filename);
        if (file_exists(path))
            return path;
    }
    // Then search system_include_paths (needed for #include_next from CCCC
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
static char *read_include_filename(VirtualMachine *vm, Token **rest, Token *tok,
                                   bool *is_dquote, int *out_len) {
    // Pattern 1: #include "foo.h" or __has_embed("foo")
    if (tok->kind == TK_STR) {
        // A double-quoted filename for #include is a special kind of
        // token, and we don't want to interpret any escape sequences in it.
        // For example, "\f" in "C:\foo" is not a formfeed character but
        // just two non-control characters, backslash and f.
        // So we don't want to use token->str.
        *is_dquote = true;
        *rest      = tok->next;
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
        *rest      = tok->next;
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
static char *detect_include_guard(VirtualMachine *vm, Token *tok) {
    // Detect the first two lines.
    if (!is_hash(tok) || !equal(tok->next, "ifndef"))
        return NULL;
    tok = tok->next->next;

    if (tok->kind != TK_IDENT)
        return NULL;

    char *macro = arena_strndup(vm, tok->loc, tok->len);
    tok         = tok->next;

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
static void register_stdlib_for_header(VirtualMachine *vm,
                                       const char     *header_name) {
    if (hashmap_get(&vm->compiler.included_headers, header_name))
        return;
    hashmap_put(&vm->compiler.included_headers, header_name, (void *)1);

    const char *fn_name = get_stdlib_reg_fn_name(header_name);
    if (!fn_name)
        return;

    static const struct {
        const char *name;
        void (*fn)(VirtualMachine *);
    } fns[] = {
        {"register_ctype_functions", register_ctype_functions},
        {"register_decimal_math_functions", register_decimal_math_functions},
        {"register_fenv_functions", register_fenv_functions},
        {"register_locale_functions", register_locale_functions},
        {"register_math_functions", register_math_functions},
        {"register_posix_functions", register_posix_functions},
        {"register_posix_aio_functions", register_posix_aio_functions},
        {"register_posix_dir_functions", register_posix_dir_functions},
        {"register_posix_io_functions", register_posix_io_functions},
        {"register_posix_ipc_functions", register_posix_ipc_functions},
        {"register_posix_lang_functions", register_posix_lang_functions},
        {"register_posix_mqueue_functions", register_posix_mqueue_functions},
        {"register_posix_ndbm_functions", register_posix_ndbm_functions},
        {"register_posix_net_functions", register_posix_net_functions},
        {"register_posix_poll_functions", register_posix_poll_functions},
        {"register_posix_sched_functions", register_posix_sched_functions},
        {"register_posix_search_functions", register_posix_search_functions},
        {"register_posix_spawn_functions", register_posix_spawn_functions},
        {"register_posix_statfs_functions", register_posix_statfs_functions},
        {"register_posix_wait_functions", register_posix_wait_functions},
        {"register_posix_wordexp_functions", register_posix_wordexp_functions},
        {"register_pthread_functions", register_pthread_functions},
        {"register_threads_functions", register_threads_functions},
        {"register_signal_functions", register_signal_functions},
        {"register_stdio_functions", register_stdio_functions},
        {"register_stdlib_functions", register_stdlib_functions},
        {"register_string_functions", register_string_functions},
        {"register_time_functions", register_time_functions},
        {"register_wide_functions", register_wide_functions},
    };
    for (int i = 0; i < (int)(sizeof(fns) / sizeof(fns[0])); i++) {
        if (strcmp(fn_name, fns[i].name) == 0) {
            fns[i].fn(vm);
            return;
        }
    }
}

static Token *include_file(VirtualMachine *vm, Token *tok, char *path,
                           Token *filename_tok, const char *include_name,
                           bool is_system) {
    // Check for "#pragma once"
    if (hashmap_get(&vm->compiler.pragma_once, path))
        return tok;

    // If we read the same file before, and if the file was guarded
    // by the usual #ifndef ... #endif pattern, we may be able to
    // skip the file without opening it.
    char *guard_name = hashmap_get(&vm->compiler.include_guards, path);
    if (guard_name && hashmap_get(&vm->compiler.macros, guard_name))
        return tok;

    Token *tok2 = tokenize_file(vm, path, false);
    if (!tok2)
        error_tok(vm, filename_tok, "%s: cannot open file: %s", path,
                  strerror(errno));
    if (is_system && tok2->file)
        tok2->file->is_system_header = true;

    // Register stdlib functions for standard headers (header-based lazy
    // loading)
    register_stdlib_for_header(vm, include_name);

    guard_name = detect_include_guard(vm, tok2);
    if (guard_name) {
        hashmap_put(&vm->compiler.include_guards, path, guard_name);
        hashmap_put(&vm->compiler.guard_macros, guard_name, (void *)1);
    }

    return append(vm, tok2, tok);
}

// The embedded table also carries CCCC's three *private* headers under
// their bare names ("reflection.h", "testing.h", "building.h") -- see
// tools/generate_stdlib.c -- because that's the exact name
// tokenize_private_header() looks them up by for internal injection
// (implicit_reflection_tokens et al). They are deliberately not
// user-includable: `#include <cccc/reflection.h>` is the public spelling
// (search_include_paths, disk-only). An ordinary `#include <reflection.h>`
// must keep failing (see test_builtin_reflection_header_unavailable.c).
static bool is_private_embedded_header(const char *name) {
    return !strcmp(name, "reflection.h") || !strcmp(name, "testing.h") ||
           !strcmp(name, "building.h");
}

// #891: the embedded src/std.c table's fallback for a standard header not
// found on disk (foreign CWD with no ./include alongside the cccc binary,
// or a copied binary with no repo at all). Only tried once
// search_include_paths() has already come up empty (both the -I search and the
// on-disk builtin_include_dir fallback), so an on-disk copy always wins when
// one is reachable. Returns NULL for anything not a known *public* standard
// header, or when wants_builtin_header() says CCCC's own copy shouldn't be
// considered here (--no-builtin-includes on a non-owned header).
static char *try_embedded_std_header(VirtualMachine *vm, char *filename) {
    if (is_private_embedded_header(filename))
        return NULL;
    char *src = get_std_header(filename);
    if (!src)
        return NULL;
    bool owned = is_compiler_owned_header(filename);
    if (!wants_builtin_header(vm, /*is_std=*/true, owned))
        return NULL;
    return src;
}

// #998: the synthetic path an embedded standard header (served from the
// src/std.c table, no real file on disk) is keyed and recorded under --
// shared between include_embedded_header()'s own #pragma once/include-guard
// bookkeeping and the PP_INCLUDE handler's emit_include_paths registration,
// so the two can't drift apart. Also what TypeName.file_path ends up
// holding for a type declared in such a header (record_type_name, parse.c),
// confirmed empirically against this exact string.
static char *embedded_header_key(VirtualMachine *vm, const char *filename) {
    return arena_format(vm, "<embedded>/%s", filename);
}

// Splice an embedded standard header's tokens into the include stream.
// Mirrors include_file()'s #pragma once / include-guard bookkeeping, keyed
// by a synthetic "<embedded>/name" string since there is no real path on
// disk to key on.
static Token *include_embedded_header(VirtualMachine *vm, Token *tok,
                                      char *filename, char *src,
                                      Token *filename_tok) {
    char *key = embedded_header_key(vm, filename);

    if (hashmap_get(&vm->compiler.pragma_once, key))
        return tok;

    char *guard_name = hashmap_get(&vm->compiler.include_guards, key);
    if (guard_name && hashmap_get(&vm->compiler.macros, guard_name))
        return tok;

    Token *tok2 = tokenize_string(vm, key, src);
    if (!tok2)
        error_tok(vm, filename_tok, "%s: cannot tokenize embedded header",
                  filename);
    if (tok2->file)
        tok2->file->is_system_header = true;

    register_stdlib_for_header(vm, filename);

    guard_name = detect_include_guard(vm, tok2);
    if (guard_name) {
        hashmap_put(&vm->compiler.include_guards, key, guard_name);
        hashmap_put(&vm->compiler.guard_macros, guard_name, (void *)1);
    }

    return append(vm, tok2, tok);
}

// Read #line arguments
static void read_line_marker(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok          = preprocess(vm, copy_line(vm, rest, tok));

    if (tok->kind != TK_NUM || tok->ty->kind != TY_INT)
        error_tok(vm, tok, "invalid line marker");
    start->file->line_delta = tok->val - start->line_no;

    tok                     = tok->next;
    if (tok->kind == TK_EOF)
        return;

    if (tok->kind != TK_STR)
        error_tok(vm, tok, "filename expected");
    start->file->display_name = tok->str;
}

// Read a token sequence for #embed parameters (prefix, suffix, if_empty)
// Similar to read_macro_arg_one but simplified for #embed use case
static Token *read_embed_parameter(VirtualMachine *vm, Token **rest,
                                   Token *tok) {
    Token  head  = {};
    Token *cur   = &head;
    int    level = 0;

    for (;;) {
        if (level == 0 && equal(tok, ")"))
            break;

        if (tok->kind == TK_EOF)
            error_tok(vm, tok,
                      "premature end of input in #embed parameter list");

        if (equal(tok, "("))
            level++;
        else if (equal(tok, ")"))
            level--;

        cur = cur->next = copy_token(vm, tok);
        tok             = tok->next;
    }

    *rest = tok;
    return head.next; // NULL if empty parameter
}

static long eval_embed_limit_expr(VirtualMachine *vm, Token *start, Token *expr,
                                  Token *end) {
    if (!expr)
        error_tok(vm, start, "no expression");

    Token  head = {};
    Token *cur  = &head;

    for (Token *t = expr; t; t = t->next)
        cur = cur->next = copy_token(vm, t);
    cur->next = new_eof(vm, end);

    expr      = preprocess2(vm, head.next);
    if (expr->kind == TK_EOF)
        error_tok(vm, start, "no expression");

    convert_pp_tokens(vm, expr);

    Token *rest;
    long   val = const_expr(vm, &rest, expr);
    if (rest->kind != TK_EOF)
        error_tok(vm, rest, "extra tokens after #if expression");
    return val;
}

// Main #embed directive handler
static Token *handle_embed_directive(VirtualMachine *vm, Token *tok,
                                     Token *directive_start, bool is_inline) {
    // Parse filename (quoted string or <angle brackets>)
    bool  is_dquote;
    int   filename_len;
    char *filename;

    if (tok->kind == TK_STR) {
        // Pattern: #embed "foo.bin"
        is_dquote    = true;
        filename_len = tok->len - 2;
        filename     = arena_strndup(vm, tok->loc + 1, tok->len - 2);
        tok          = tok->next;
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
        filename  = join_tokens(vm, tok, end, &filename_len);
        tok       = end->next;
    } else {
        error_tok(vm, tok, "expected a filename for #embed");
        return tok;
    }

    // Parse optional parameters
    long   limit           = -1; // -1 means no limit
    bool   has_limit       = false;
    Token *prefix_tokens   = NULL;
    Token *suffix_tokens   = NULL;
    Token *if_empty_tokens = NULL;

    // Parse parameters in any order
    while (equal(tok, "limit") || equal(tok, "__limit__") ||
           equal(tok, "prefix") || equal(tok, "__prefix__") ||
           equal(tok, "suffix") || equal(tok, "__suffix__") ||
           equal(tok, "if_empty") || equal(tok, "__if_empty__")) {

        if (equal(tok, "limit") || equal(tok, "__limit__")) {
            has_limit    = true;
            Token *start = tok;
            tok          = skip(vm, tok->next, "(");
            Token *expr  = read_embed_parameter(vm, &tok, tok);
            limit        = eval_embed_limit_expr(vm, start, expr, tok);
            tok          = skip(vm, tok, ")");

            if (limit < 0)
                error_tok(vm, start, "limit must be non-negative");
        } else if (equal(tok, "prefix") || equal(tok, "__prefix__")) {
            tok           = skip(vm, tok->next, "(");
            prefix_tokens = read_embed_parameter(vm, &tok, tok);
            tok           = skip(vm, tok, ")");
        } else if (equal(tok, "suffix") || equal(tok, "__suffix__")) {
            tok           = skip(vm, tok->next, "(");
            suffix_tokens = read_embed_parameter(vm, &tok, tok);
            tok           = skip(vm, tok, ")");
        } else if (equal(tok, "if_empty") || equal(tok, "__if_empty__")) {
            tok             = skip(vm, tok->next, "(");
            if_empty_tokens = read_embed_parameter(vm, &tok, tok);
            tok             = skip(vm, tok, ")");
        }
    }

    // Skip to next line (check for extraneous tokens)
    if (!is_inline)
        tok = skip_line(vm, tok);

    // Resolve file path
    char *path = NULL;

    if (is_url(filename)) {
        // URL embeds fetch into the same cache URL #include uses, then
        // embed from the cached copy; limit/prefix/suffix/if_empty apply
        // to the fetched bytes exactly as they do for local files.
#ifdef CCCC_HAS_CURL
        char *cache_path = fetch_url_to_cache(vm, filename);
        if (!cache_path) {
            error_tok(vm, directive_start, "failed to fetch URL: %s", filename);
        }
        // Track URL -> cache path mapping for error reporting
        hashmap_put(&vm->compiler.url_to_path, cache_path, (void *)filename);
        path = cache_path;
#else
        error_tok(vm, directive_start,
                  "URL embeds require CCCC to be built with CCCC_HAS_CURL=1");
#endif
    } else if (filename[0] == '/') {
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
    size_t         file_size;
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
            warn_tok(vm, directive_start, CCCC_WARN_LARGE_FILE_EMBED,
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
            warn_tok(vm, directive_start, CCCC_WARN_LARGE_FILE_EMBED,
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
    PP_IF,
    PP_IFDEF,
    PP_IFNDEF,
    PP_ELIF,
    PP_ELIFDEF,
    PP_ELIFNDEF,
    PP_ELSE,
    PP_ENDIF,
    PP_INCLUDE,
    PP_INCLUDE_NEXT,
    PP_DEFINE,
    PP_UNDEF,
    PP_LINE,
    PP_PRAGMA,
    PP_EMBED,
    PP_ERROR,
    PP_WARNING,
} PPDir;

static PPDir pp_directive(Token *tok) {
    const char *s = tok->loc;
    switch (tok->len) {
        case 2:
            if (s[0] == 'i' && s[1] == 'f')
                return PP_IF;
            break;
        case 4:
            switch (s[0]) {
                case 'e':
                    if (s[1] == 'l') {
                        if (s[2] == 'i' && s[3] == 'f')
                            return PP_ELIF;
                        if (s[2] == 's' && s[3] == 'e')
                            return PP_ELSE;
                    }
                    break;
                case 'l':
                    if (s[1] == 'i' && s[2] == 'n' && s[3] == 'e')
                        return PP_LINE;
                    break;
            }
            break;
        case 5:
            switch (s[0]) {
                case 'e':
                    switch (s[1]) {
                        case 'm':
                            if (memcmp(s + 2, "bed", 3) == 0)
                                return PP_EMBED;
                            break;
                        case 'n':
                            if (memcmp(s + 2, "dif", 3) == 0)
                                return PP_ENDIF;
                            break;
                        case 'r':
                            if (memcmp(s + 2, "ror", 3) == 0)
                                return PP_ERROR;
                            break;
                    }
                    break;
                case 'i':
                    if (memcmp(s + 1, "fdef", 4) == 0)
                        return PP_IFDEF;
                    break;
                case 'u':
                    if (memcmp(s + 1, "ndef", 4) == 0)
                        return PP_UNDEF;
                    break;
            }
            break;
        case 6:
            switch (s[0]) {
                case 'd':
                    if (memcmp(s + 1, "efine", 5) == 0)
                        return PP_DEFINE;
                    break;
                case 'i':
                    if (memcmp(s + 1, "fndef", 5) == 0)
                        return PP_IFNDEF;
                    break;
                case 'p':
                    if (memcmp(s + 1, "ragma", 5) == 0)
                        return PP_PRAGMA;
                    break;
            }
            break;
        case 7:
            switch (s[0]) {
                case 'e':
                    if (memcmp(s + 1, "lifdef", 6) == 0)
                        return PP_ELIFDEF;
                    break;
                case 'i':
                    if (memcmp(s + 1, "nclude", 6) == 0)
                        return PP_INCLUDE;
                    break;
                case 'w':
                    if (memcmp(s + 1, "arning", 6) == 0)
                        return PP_WARNING;
                    break;
            }
            break;
        case 8:
            if (memcmp(s, "elifndef", 8) == 0)
                return PP_ELIFNDEF;
            break;
        case 12:
            if (memcmp(s, "include_next", 12) == 0)
                return PP_INCLUDE_NEXT;
            break;
    }
    return PP_NONE;
}

static bool is_pp_directive_kw(Token *tok) {
    static const char *kws[] = {
        "define", "undef",   "if",     "ifdef", "ifndef",  "elif", "else",
        "endif",  "include", "pragma", "error", "warning", "line", NULL,
    };
    for (int i = 0; kws[i]; i++)
        if (equal(tok, (char *)kws[i]))
            return true;
    return false;
}

// Rewrite @<ppkeyword> rest-of-line to #<ppkeyword> @<opposite_route>
// rest-of-line. Opposite route: CTX_COMPTIME -> "emit"; all other contexts ->
// "comptime". Returns false (tok_ptr unchanged) if the current token is not
// @<ppkeyword>. Must be called before try_rewrite_at_attr to prevent @define
// etc. from being mangled into __attribute__((define)).
static bool try_rewrite_at_directive(VirtualMachine *vm, Token **tok_ptr) {
    Token *tok = *tok_ptr;
    if (!equal(tok, "@") || !tok->next || tok->next->kind != TK_IDENT)
        return false;
    Token *kw = tok->next;
    if (!is_pp_directive_kw(kw))
        return false;
    ComptimeCtxEntry *top = ctx_top(vm);
    const char       *route =
        (top && top->type == CTX_COMPTIME) ? "emit" : "comptime";
    char  *src = arena_format(vm, "#%.*s @%s ", (int)kw->len, kw->loc, route);
    Token *new_toks =
        tokenize(vm, new_file(vm, tok->file->name, tok->file->file_no, src));
    Token *last = new_toks;
    while (last->next && last->next->kind != TK_EOF)
        last = last->next;
    last->next = kw->next;
    *tok_ptr   = new_toks;
    return true;
}

// Rewrite @identifier or @identifier(args) into the equivalent attribute form
// before try_extract_attr_macro runs. The target form depends on the attribute
// registry: ATTR_CCCC -> [[cccc::name(args)]], ATTR_STD -> [[name(args)]],
// ATTR_GNU / unknown -> __attribute__((name(args))). Returns false (tok
// unchanged) if tok is not "@" or is not followed by an identifier.
static bool try_rewrite_at_attr(VirtualMachine *vm, Token **tok_ptr) {
    Token *tok = *tok_ptr;
    if (!equal(tok, "@") || !tok->next || tok->next->kind != TK_IDENT)
        return false;

    Token *name_tok = tok->next;
    char  *name     = arena_strndup(vm, name_tok->loc, name_tok->len);
    Token *after    = name_tok->next;

    // Build the args text "(arg1, arg2, ...)" by re-emitting raw source text
    // for each token inside the parens, separated by spaces.
    char *args_text = "";
    if (after && equal(after, "(")) {
        int    depth = 0;
        char  *buf   = arena_format(vm, "(");
        Token *p     = after->next; // first token inside "("
        while (p && p->kind != TK_EOF) {
            if (equal(p, "(")) {
                depth++;
                buf = arena_format(vm, "%s(", buf);
                p   = p->next;
                continue;
            }
            if (equal(p, ")")) {
                if (depth == 0) {
                    after = p->next; // advance past closing ")"
                    break;
                }
                depth--;
                buf = arena_format(vm, "%s)", buf);
                p   = p->next;
                continue;
            }
            buf = arena_format(vm, "%s%.*s", buf, (int)p->len, p->loc);
            p   = p->next;
            if (p && !equal(p, ")") && !equal(p, "("))
                buf = arena_format(vm, "%s ", buf);
        }
        if (!p || p->kind == TK_EOF)
            error_tok(vm, tok, "unterminated '@%s(' attribute", name);
        args_text = arena_format(vm, "%s)", buf);
    }

    const AttrInfo *info = find_attr_info(name);
    AttrCategory    cat  = info ? info->cat : ATTR_GNU;

    char           *src;
    switch (cat) {
        case ATTR_CCCC:
            src = arena_format(vm, "[[cccc::%s%s]]\n", name, args_text);
            break;
        case ATTR_STD:
            src = arena_format(vm, "[[%s%s]]\n", name, args_text);
            break;
        default:
            src = arena_format(vm, "__attribute__((%s%s))\n", name, args_text);
            break;
    }

    Token *new_toks =
        tokenize(vm, new_file(vm, tok->file->name, tok->file->file_no, src));
    // Strip trailing TK_EOF and splice new tokens before `after`.
    Token *last = new_toks;
    while (last->next && last->next->kind != TK_EOF)
        last = last->next;
    last->next = after;

    *tok_ptr   = new_toks;
    return true;
}

static const char *cccc_keyword_alias_name(Token *tok) {
    if (equal(tok, "__comptime") || equal(tok, "__comptime__"))
        return "comptime";
    if (equal(tok, "__test") || equal(tok, "__test__"))
        return "test";
    if (equal(tok, "__test_setup") || equal(tok, "__test_setup__"))
        return "test_setup";
    if (equal(tok, "__test_teardown") || equal(tok, "__test_teardown__"))
        return "test_teardown";
    return NULL;
}

static bool try_rewrite_cccc_keyword_attr(VirtualMachine *vm, Token **tok_ptr) {
    Token      *tok  = *tok_ptr;
    const char *name = cccc_keyword_alias_name(tok);
    if (!name)
        return false;

    Token *after     = tok->next;
    char  *args_text = "";
    if (after && equal(after, "(")) {
        int    depth = 0;
        char  *buf   = arena_format(vm, "(");
        Token *p     = after->next;
        while (p && p->kind != TK_EOF) {
            if (equal(p, "(")) {
                depth++;
                buf = arena_format(vm, "%s(", buf);
                p   = p->next;
                continue;
            }
            if (equal(p, ")")) {
                if (depth == 0) {
                    after = p->next;
                    break;
                }
                depth--;
                buf = arena_format(vm, "%s)", buf);
                p   = p->next;
                continue;
            }
            buf = arena_format(vm, "%s%.*s", buf, (int)p->len, p->loc);
            p   = p->next;
            if (p && !equal(p, ")") && !equal(p, "("))
                buf = arena_format(vm, "%s ", buf);
        }
        if (!p || p->kind == TK_EOF)
            error_tok(vm, tok, "unterminated '%s(' attribute", name);
        args_text = arena_format(vm, "%s)", buf);
    }

    char  *src = arena_format(vm, "[[cccc::%s%s]]\n", name, args_text);
    Token *new_toks =
        tokenize(vm, new_file(vm, tok->file->name, tok->file->file_no, src));
    Token *last = new_toks;
    while (last->next && last->next->kind != TK_EOF)
        last = last->next;
    last->next = after;

    *tok_ptr   = new_toks;
    return true;
}

// Parsed arguments from [[cccc::test(...)]] / __attribute__((test(...)))
typedef struct {
    const char *suite_name;
    const char *error_pat;
    bool        error_pat_negate;
    const char *test_name;
    long        timeout_ms;
    int         error_count;
    CmpOp       error_count_op;
    RetKind     ret_kind;
    CmpOp       ret_op;
    int64_t     ret_int_val;
    double      ret_float_val;
    const char *ret_str_val;
    double      ret_epsilon_val;
    int         exit_code_val; // -1 = not set
    bool
        expect_compile_error; // true = any compile error passes the test (#615)
    const char *flags; // flags = "..." per-test CLI-flag string; NULL if unset
    // Per-test output assertions (#614)
    const char *expect_stderr;
    const char *reject_stderr;
    const char *expect_stdout;
    const char *reject_stdout;
    // RET_STRUCT: compound-literal field list and raw source span text
    TestRetField
         *ret_fields; // heap-alloc'd; ownership transferred to TestFnRecord
    char *ret_struct_text; // heap-alloc'd strndup of the literal source span
} TestArgs;

// Parsed arguments from [[cccc::test_setup(...)]] /
// __attribute__((test_setup(...)))
typedef struct {
    const char *name_pat;
    const char *suite;
    bool        once;
    bool        inherit;
} TestSetupArgs;

// Classify one scalar token (or a leading "-" then NUM) from the token stream
// into a RetKind and value.  Advances *p_ptr past the consumed tokens.
// Returns true on success; leaves *p_ptr unchanged on failure.
// For RET_STR the caller must strdup val->s (it points into token string data).
static bool parse_scalar_operand(Token **p_ptr, RetKind *out_kind,
                                 int64_t *out_i, double *out_f,
                                 const char **out_s) {
    Token *p = *p_ptr;
    if (!p)
        return false;
    if (p->kind == TK_STR) {
        *out_kind = RET_STR;
        *out_s    = p->str;
        *p_ptr    = p->next;
        return true;
    }
    if (p->kind == TK_NUM || p->kind == TK_PP_NUM) {
        bool is_float = false;
        for (int _i = 0; _i < (int)p->len; _i++) {
            char _c = p->loc[_i];
            if (_c == '.' || _c == 'e' || _c == 'E') {
                is_float = true;
                break;
            }
        }
        if (!is_float && p->len > 0 &&
            (p->loc[p->len - 1] == 'f' || p->loc[p->len - 1] == 'F'))
            is_float = true;
        char _buf[64];
        int  _n = p->len < 63 ? (int)p->len : 63;
        memcpy(_buf, p->loc, _n);
        _buf[_n] = '\0';
        if (is_float) {
            *out_kind = RET_FLOAT;
            *out_f    = strtod(_buf, NULL);
        } else {
            *out_kind = RET_INT;
            *out_i    = (int64_t)strtoll(_buf, NULL, 0);
        }
        *p_ptr = p->next;
        return true;
    }
    // Negative number: "-" followed by NUM/PP_NUM
    if (equal(p, "-") && p->next &&
        (p->next->kind == TK_NUM || p->next->kind == TK_PP_NUM)) {
        char _buf[64];
        int  _n = p->next->len < 63 ? (int)p->next->len : 63;
        memcpy(_buf, p->next->loc, _n);
        _buf[_n]  = '\0';
        *out_kind = RET_INT;
        *out_i    = -(int64_t)strtoll(_buf, NULL, 0);
        *p_ptr    = p->next->next;
        return true;
    }
    return false;
}

// Free a TestRetField linked list (heap-allocated by parse_test_args),
// including nested RET_STRUCT children. Declared in cccc.h -- also called
// from TestFnRecord teardown (src/vm.c) so there is exactly one
// implementation.
void cc_free_ret_fields(TestRetField *f) {
    while (f) {
        TestRetField *next = f->next;
        free(f->name);
        if (f->kind == RET_STR)
            free(f->val.s);
        else if (f->kind == RET_STRUCT)
            cc_free_ret_fields(f->val.sub);
        free(f);
        f = next;
    }
}

// Skip tokens until the '}' that closes the currently-open brace list,
// tracking nested '{'/'}' pairs. *p_ptr must point somewhere inside an
// already-opened '{' (i.e. at brace depth 1 relative to that '{'). Leaves
// *p_ptr just past the matching top-level '}' (or at TK_EOF if unbalanced).
static void skip_balanced_braces(Token **p_ptr) {
    Token *p     = *p_ptr;
    int    depth = 1;
    while (p && p->kind != TK_EOF && depth > 0) {
        if (equal(p, "{"))
            depth++;
        else if (equal(p, "}"))
            depth--;
        p = p->next;
    }
    *p_ptr = p;
}

// Max nesting depth accepted for a compound-literal return= assertion
// (struct-in-struct, array-of-struct, etc). Guards against runaway
// recursion on a malformed/adversarial attribute; deeper literals produce a
// -Wattributes warning and the assertion is skipped.
#define CCCC_RET_FIELD_MAX_DEPTH 8

// Recursively parse a brace-delimited initializer list for a return=
// compound-literal assertion: `{ [.name =] value, ... }`. *p_ptr must point
// to the first token *after* the opening '{'; on success it is left just
// past the matching '}' and *out holds the parsed field list (caller owns,
// free with cc_free_ret_fields). A `value` may itself be a nested
// initializer -- `(struct|union TAG){...}` or a bare `{...}` -- which
// recurses with depth+1, producing a RET_STRUCT field whose `val.sub` is the
// child list. Entries without a `.name =` designator are positional (used
// for array-element lists) and get `name == NULL`.
//
// On failure (malformed syntax or depth exceeded), warns via warn_tok,
// frees any partially-built list, advances *p_ptr past the matching '}' via
// skip_balanced_braces (so the caller's token stream stays in sync even
// with nested braces), and returns false. If out_close is non-NULL, *out_close
// receives the matching '}' token on success (used by the top-level caller
// to compute the source span for diagnostics); it is left NULL on failure.
static bool parse_ret_init_list(VirtualMachine *vm, Token **p_ptr,
                                TestRetField **out, Token **out_close,
                                int depth) {
    Token *p = *p_ptr;
    *out     = NULL;
    if (out_close)
        *out_close = NULL;

    if (depth > CCCC_RET_FIELD_MAX_DEPTH) {
        warn_tok(vm, p, CCCC_WARN_ATTRIBUTES,
                 "compound-literal return= assertion nested too deeply "
                 "(max %d levels); assertion skipped",
                 CCCC_RET_FIELD_MAX_DEPTH);
        skip_balanced_braces(&p);
        *p_ptr = p;
        return false;
    }

    TestRetField *fields = NULL, **ftail = &fields;
    bool          parse_ok = true;
    bool warned = false; // true once a specific diagnostic has been emitted,
                         // so the generic "malformed compound literal"
                         // fallback below doesn't double-warn
    Token *close_brace = NULL;

    while (p && !equal(p, "}") && p->kind != TK_EOF) {
        char *fname = NULL;
        if (equal(p, ".")) {
            p = p->next;
            if (!p || p->kind != TK_IDENT) {
                parse_ok = false;
                break;
            }
            fname = strndup(p->loc, p->len);
            p     = p->next;
            if (!p || !equal(p, "=")) {
                free(fname);
                parse_ok = false;
                break;
            }
            p = p->next;
        }
        // else: no designator -- positional entry (array-element list)

        bool is_nested_typed =
            p && equal(p, "(") && p->next &&
            (equal(p->next, "struct") || equal(p->next, "union")) &&
            p->next->next && p->next->next->kind == TK_IDENT &&
            p->next->next->next && equal(p->next->next->next, ")") &&
            p->next->next->next->next && equal(p->next->next->next->next, "{");
        bool          is_nested_bare = p && equal(p, "{");

        RetKind       fkind          = RET_NONE;
        int64_t       ival           = 0;
        double        fval           = 0.0;
        const char   *sval           = NULL;
        TestRetField *sub            = NULL;

        if (is_nested_typed || is_nested_bare) {
            p = is_nested_typed ? p->next->next->next->next
                                      ->next // past ( struct|union TAG ) {
                                : p->next;   // past {
            TestRetField *children = NULL;
            if (!parse_ret_init_list(vm, &p, &children, NULL, depth + 1)) {
                // Inner call already warned and skipped past its own '}'.
                free(fname);
                parse_ok = false;
                warned   = true;
                break;
            }
            fkind = RET_STRUCT;
            sub   = children;
        } else if (!parse_scalar_operand(&p, &fkind, &ival, &fval, &sval)) {
            if (fname)
                warn_tok(
                    vm, p, CCCC_WARN_ATTRIBUTES,
                    "unrecognized value for field '%s' in compound-literal "
                    "return= assertion; skipping",
                    fname);
            else
                warn_tok(vm, p, CCCC_WARN_ATTRIBUTES,
                         "unrecognized value in compound-literal return= "
                         "assertion; skipping");
            free(fname);
            parse_ok = false;
            warned   = true;
            break;
        }

        TestRetField *f = calloc(1, sizeof(TestRetField));
        f->name         = fname;
        f->kind         = fkind;
        if (fkind == RET_INT)
            f->val.i = ival;
        else if (fkind == RET_FLOAT)
            f->val.f = fval;
        else if (fkind == RET_STR)
            f->val.s = sval ? strdup(sval) : NULL;
        else if (fkind == RET_STRUCT)
            f->val.sub = sub;
        *ftail = f;
        ftail  = &f->next;

        if (p && equal(p, ","))
            p = p->next;
    }

    if (p && equal(p, "}")) {
        close_brace = p;
        p           = p->next;
    }

    if (!parse_ok || !close_brace) {
        cc_free_ret_fields(fields);
        if (!warned) {
            warn_tok(vm, p ? p : close_brace, CCCC_WARN_ATTRIBUTES,
                     "malformed compound literal in return= assertion; "
                     "skipping");
        }
        skip_balanced_braces(&p);
        *p_ptr = p;
        return false;
    }

    *out = fields;
    if (out_close)
        *out_close = close_brace;
    *p_ptr = p;
    return true;
}

// Parse test(...) argument list. *p_ptr must point to the first token inside
// the opening "("; on return it points to the closing ")".
static void parse_test_args(VirtualMachine *vm, Token **p_ptr, TestArgs *out) {
    Token *p = *p_ptr;
    while (p && !equal(p, ")") && p->kind != TK_EOF) {
        if (equal(p, "suite") && p->next && equal(p->next, "=") &&
            p->next->next && p->next->next->kind == TK_STR) {
            out->suite_name = p->next->next->str;
            p               = p->next->next->next;
        } else if (equal(p, "error")) {
            p        = p->next;
            bool neg = p && equal(p, "!=");
            if (neg || (p && equal(p, "=")))
                p = p->next;
            if (p && p->kind == TK_STR) {
                out->error_pat        = p->str;
                out->error_pat_negate = neg;
                p                     = p->next;
            }
        } else if (equal(p, "expect_compile_error")) {
            p = p->next;
            if (p && equal(p, "="))
                p = p->next;
            if (p && equal(p, "true")) {
                out->expect_compile_error = true;
                p                         = p->next;
            } else if (p && equal(p, "false")) {
                out->expect_compile_error = false;
                p                         = p->next;
            } else {
                out->expect_compile_error = true;
            }
        } else if (equal(p, "name") && p->next && equal(p->next, "=") &&
                   p->next->next && p->next->next->kind == TK_STR) {
            out->test_name = p->next->next->str;
            p              = p->next->next->next;
        } else if (equal(p, "timeout") && p->next && equal(p->next, "=") &&
                   p->next->next &&
                   (p->next->next->kind == TK_NUM ||
                    p->next->next->kind == TK_PP_NUM)) {
            Token *vt = p->next->next;
            char   _buf[64];
            int    _n = vt->len < 63 ? (int)vt->len : 63;
            memcpy(_buf, vt->loc, _n);
            _buf[_n]        = '\0';
            out->timeout_ms = strtoll(_buf, NULL, 0);
            p               = p->next->next->next;
        } else if (equal(p, "error_count")) {
            p        = p->next;
            CmpOp op = CMP_EQ;
            if (p && (equal(p, "=") || equal(p, "==") || equal(p, "!=") ||
                      equal(p, "<") || equal(p, "<=") || equal(p, ">") ||
                      equal(p, ">="))) {
                if (equal(p, "!="))
                    op = CMP_NE;
                else if (equal(p, "<="))
                    op = CMP_LE;
                else if (equal(p, "<"))
                    op = CMP_LT;
                else if (equal(p, ">="))
                    op = CMP_GE;
                else if (equal(p, ">"))
                    op = CMP_GT;
                p = p->next;
            }
            if (p && p->kind == TK_NUM) {
                out->error_count    = (int)p->val;
                out->error_count_op = op;
                p                   = p->next;
            }
        } else if (equal(p, "return")) {
            p        = p->next;
            CmpOp op = CMP_EQ;
            if (p && (equal(p, "=") || equal(p, "==") || equal(p, "!=") ||
                      equal(p, "<") || equal(p, "<=") || equal(p, ">") ||
                      equal(p, ">="))) {
                if (equal(p, "!="))
                    op = CMP_NE;
                else if (equal(p, "<="))
                    op = CMP_LE;
                else if (equal(p, "<"))
                    op = CMP_LT;
                else if (equal(p, ">="))
                    op = CMP_GE;
                else if (equal(p, ">"))
                    op = CMP_GT;
                p = p->next;
            }
            // Compound literal: (struct|union TAG){...}
            if (p && equal(p, "(") && p->next &&
                (equal(p->next, "struct") || equal(p->next, "union")) &&
                p->next->next && p->next->next->kind == TK_IDENT &&
                p->next->next->next && equal(p->next->next->next, ")") &&
                p->next->next->next->next &&
                equal(p->next->next->next->next, "{")) {

                if (op != CMP_EQ && op != CMP_NE) {
                    warn_tok(
                        vm, p, CCCC_WARN_ATTRIBUTES,
                        "struct return= assertion only supports '=' or '!='; "
                        "ordered comparisons are not meaningful for structs; "
                        "assertion skipped");
                    // Skip to closing brace (depth-aware: the literal may
                    // itself contain nested compound literals).
                    p = p->next->next->next->next
                            ->next; // past ( struct|union TAG ) {
                    skip_balanced_braces(&p);
                } else {
                    const char *span_start = p->loc; // points to '('
                    // Advance past: ( struct|union TAG ) {
                    p                         = p->next->next->next->next->next;

                    TestRetField *fields      = NULL;
                    Token        *close_brace = NULL;
                    if (parse_ret_init_list(vm, &p, &fields, &close_brace, 1)) {
                        size_t span_len =
                            (close_brace->loc + close_brace->len) - span_start;
                        out->ret_struct_text = strndup(span_start, span_len);
                        out->ret_fields      = fields;
                        out->ret_kind        = RET_STRUCT;
                        out->ret_op          = op;
                    }
                    // On failure, parse_ret_init_list already warned, freed
                    // the partial list, and left p past the matching '}'.
                }
            } else {
                // Scalar operands: use helper (handles STR / NUM / PP_NUM /
                // negative int)
                int64_t     ival = 0;
                double      fval = 0.0;
                const char *sval = NULL;
                RetKind     sk   = RET_NONE;
                if (parse_scalar_operand(&p, &sk, &ival, &fval, &sval)) {
                    out->ret_kind = sk;
                    out->ret_op   = op;
                    if (sk == RET_INT)
                        out->ret_int_val = ival;
                    else if (sk == RET_FLOAT)
                        out->ret_float_val = fval;
                    else if (sk == RET_STR)
                        out->ret_str_val = sval;
                } else if (p && p->kind != TK_EOF && !equal(p, ")") &&
                           !equal(p, ",")) {
                    warn_tok(vm, p, CCCC_WARN_ATTRIBUTES,
                             "unrecognized return= operand '%.*s';"
                             " assertion skipped (enum names not"
                             " supported, use the integer value)",
                             (int)p->len, p->loc);
                    p = p->next;
                }
            }
        } else if (equal(p, "exit_code") && p->next && equal(p->next, "=") &&
                   p->next->next &&
                   (p->next->next->kind == TK_NUM ||
                    p->next->next->kind == TK_PP_NUM)) {
            Token *vt = p->next->next;
            char   _buf[64];
            int    _n = vt->len < 63 ? (int)vt->len : 63;
            memcpy(_buf, vt->loc, _n);
            _buf[_n]           = '\0';
            out->exit_code_val = (int)strtoll(_buf, NULL, 0);
            p                  = p->next->next->next;
        } else if (equal(p, "return_epsilon") && p->next &&
                   equal(p->next, "=") && p->next->next &&
                   (p->next->next->kind == TK_NUM ||
                    p->next->next->kind == TK_PP_NUM)) {
            Token *vt = p->next->next;
            char   _buf[64];
            int    _n = vt->len < 63 ? (int)vt->len : 63;
            memcpy(_buf, vt->loc, _n);
            _buf[_n]             = '\0';
            out->ret_epsilon_val = strtod(_buf, NULL);
            p                    = p->next->next->next;
        } else if (equal(p, "flags") && p->next && equal(p->next, "=") &&
                   p->next->next && p->next->next->kind == TK_STR) {
            out->flags = p->next->next->str;
            p          = p->next->next->next;
        } else if (equal(p, "expect_stderr") && p->next &&
                   equal(p->next, "=") && p->next->next &&
                   p->next->next->kind == TK_STR) {
            out->expect_stderr = p->next->next->str;
            p                  = p->next->next->next;
        } else if (equal(p, "reject_stderr") && p->next &&
                   equal(p->next, "=") && p->next->next &&
                   p->next->next->kind == TK_STR) {
            out->reject_stderr = p->next->next->str;
            p                  = p->next->next->next;
        } else if (equal(p, "expect_stdout") && p->next &&
                   equal(p->next, "=") && p->next->next &&
                   p->next->next->kind == TK_STR) {
            out->expect_stdout = p->next->next->str;
            p                  = p->next->next->next;
        } else if (equal(p, "reject_stdout") && p->next &&
                   equal(p->next, "=") && p->next->next &&
                   p->next->next->kind == TK_STR) {
            out->reject_stdout = p->next->next->str;
            p                  = p->next->next->next;
        } else {
            p = p->next;
        }
        if (p && equal(p, ","))
            p = p->next;
    }
    *p_ptr = p;
}

// Parse test_setup/teardown(...) argument list. *p_ptr must point to the first
// token inside the opening "("; on return it points to the closing ")".
static void parse_test_setup_args(Token **p_ptr, TestSetupArgs *out) {
    Token *p = *p_ptr;
    while (p && !equal(p, ")") && p->kind != TK_EOF) {
        if (equal(p, "name") && p->next && equal(p->next, "=") &&
            p->next->next && p->next->next->kind == TK_STR) {
            out->name_pat = p->next->next->str;
            p             = p->next->next->next;
        } else if (equal(p, "suite") && p->next && equal(p->next, "=") &&
                   p->next->next && p->next->next->kind == TK_STR) {
            out->suite = p->next->next->str;
            p          = p->next->next->next;
        } else if (equal(p, "once")) {
            out->once = true;
            p         = p->next;
        } else if (equal(p, "inherit")) {
            out->inherit = true;
            p            = p->next;
        } else {
            p = p->next;
        }
        if (p && equal(p, ","))
            p = p->next;
    }
    *p_ptr = p;
}

// Scan the attribute argument list of a GNU __attribute__(( ... )) or C23
// [[ ... ]] block for [[cccc::comptime]], __attribute__((comptime)), and the
// inline modifier. The old [[cccc::macro]] / __attribute__((macro)) spellings
// (and the @macro shorthand, which rewrites to __attribute__((macro))) are
// rejected with a deprecation error directing the user to [[cccc::comptime]].
// If a comptime marker is found, extract
// the following function or variable definition from the token stream, register
// it as a MacroFn or ComptimeVar, update *tok_ptr to the token
// after the extracted definition, and return true.
//
// If the attribute block contains no macro/comptime marker (e.g. [[nodiscard]],
// __attribute__((unused))), *tok_ptr is left unchanged and the function returns
// false so the token flows to the parser as normal.
// #886: a declaration inside comptime-executed code (whether marked with
// [[cccc::comptime]] or written inside a `#pragma cccc comptime begin/end`
// region) that starts with `typedef` is a type declaration, never a comptime
// function or a comptime variable -- it declares no object, so neither the
// function/variable dispatch heuristics below nor the ticket #188
// pointer/string-variable check apply to it. Keywords are still TK_IDENT at
// this point in preprocessing (convert_pp_tokens hasn't run yet), so this is
// a plain identifier-text comparison.
static bool starts_with_typedef(Token *tok) {
    return tok && tok->kind == TK_IDENT && equal(tok, "typedef");
}

static void read_macro_attr_options(VirtualMachine *vm, Token *macro_tok,
                                    char **attribute_name) {
    if (!macro_tok || !macro_tok->next || !equal(macro_tok->next, "("))
        return;

    Token *p = macro_tok->next->next;
    while (p && p->kind != TK_EOF && !equal(p, ")")) {
        if (equal(p, ",")) {
            p = p->next;
            continue;
        }
        if (equal(p, "inline")) {
            error_tok(vm, p,
                      "[[cccc::comptime(inline)]] is no longer supported; "
                      "use [[cccc::comptime]] — all comptime functions are "
                      "callable in expression position");
            p = p->next;
            continue;
        }
        if (equal(p, "attribute")) {
            p = skip(vm, p->next, "(");
            if (p->kind != TK_STR)
                error_tok(vm, p,
                          "comptime(attribute(...)) expects a string literal "
                          "attribute name");
            // Adjacent string literals ("a" "b" -> "ab") are a plain
            // expression-context concern normally handled once, late, by
            // join_adjacent_string_literals() (translation phase 6) -- but
            // this runs during preprocessing, well before that pass, so an
            // attribute name split across adjacent literals (e.g. by a
            // formatter's line-length wrapping of a long single string, the
            // shape reflection.h's own @generate_constructor hit) still
            // arrives here as separate TK_STR tokens. Concatenate any run of
            // them the same way, narrow-string only (an attribute name has
            // no legitimate use for a wide/unicode literal, rejected below).
            // Length comes from each token's own ty->array_len (the same
            // source join_adjacent_string_literals() itself trusts), not
            // strlen(str) -- str is the unescaped content and may legally
            // contain an embedded NUL, which strlen would silently
            // truncate at.
            if (p->ty->base->size != 1)
                error_tok(vm, p,
                          "comptime(attribute(...)) attribute name must be a "
                          "plain (narrow) string literal");
            size_t len  = (size_t)p->ty->array_len - 1;
            Token *last = p;
            for (Token *t = p->next; t->kind == TK_STR; t = t->next) {
                if (t->ty->base->size != 1)
                    error_tok(vm, t,
                              "comptime(attribute(...)) attribute name must "
                              "be a plain (narrow) string literal");
                len  += (size_t)t->ty->array_len - 1;
                last  = t;
            }
            char  *name = arena_alloc(&vm->compiler.parser_arena, len + 1);
            size_t off  = 0;
            for (Token *t = p;; t = t->next) {
                size_t piece = (size_t)t->ty->array_len - 1;
                memcpy(name + off, t->str, piece);
                off += piece;
                if (t == last)
                    break;
            }
            name[off]       = '\0';
            *attribute_name = name;
            p               = skip(vm, last->next, ")");
            continue;
        }
        error_tok(vm, p, "unknown comptime attribute option '%.*s'", p->len,
                  p->loc);
    }
}

bool try_extract_attr_macro(VirtualMachine *vm, Token **tok_ptr,
                            bool emit_scan) {
    Token *tok         = *tok_ptr;
    bool   is_gnu_attr = false;
    bool   is_c23_attr = false;

    if (equal(tok, "__attribute__") && tok->next && equal(tok->next, "(") &&
        tok->next->next && equal(tok->next->next, "("))
        is_gnu_attr = true;
    else if (equal(tok, "[") && tok->next && equal(tok->next, "["))
        is_c23_attr = true;

    if (!is_gnu_attr && !is_c23_attr)
        return false;

    // Scan inside the attribute argument list for macro/comptime/test/inline
    // markers.
    bool  is_comptime_kind     = false;
    bool  is_test_kind         = false;
    bool  is_setup_kind        = false;
    bool  is_teardown_kind     = false;
    bool  is_build_kind        = false;
    bool  is_build_target_kind = false;
    char *build_target_kind =
        NULL; // "native" (default) when is_build_target_kind
    char    *attribute_name = NULL;
    Token   *attr_end       = NULL;
    TestArgs ta             = {0};
    ta.error_count_op       = CMP_NONE;
    ta.ret_op               = CMP_EQ;
    ta.exit_code_val        = -1;
    TestSetupArgs tsa       = {0};

    Token *scan  = is_gnu_attr ? tok->next->next->next // skip __attribute__ ( (
                               : tok->next->next;      // skip [ [
    int    depth = 0;

    for (Token *t = scan; t && t->kind != TK_EOF; t = t->next) {
        if (is_gnu_attr) {
            if (equal(t, "(")) {
                depth++;
                continue;
            }
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
            if (equal(t, "[")) {
                depth++;
                continue;
            }
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

        if (depth != 0)
            continue;
        if (equal(t, ","))
            continue;

        if (equal(t, "macro")) {
            error_tok(vm, t,
                      "[[cccc::macro]] is deprecated; use [[cccc::comptime]]");
        } else if (equal(t, "comptime")) {
            is_comptime_kind = true;
            read_macro_attr_options(vm, t, &attribute_name);
        } else if (equal(t, "cccc") && t->next && equal(t->next, ":") &&
                   t->next->next && equal(t->next->next, ":") &&
                   t->next->next->next) {
            Token *after_scope = t->next->next->next;
            if (equal(after_scope, "macro")) {
                error_tok(
                    vm, after_scope,
                    "[[cccc::macro]] is deprecated; use [[cccc::comptime]]");
            } else if (equal(after_scope, "comptime")) {
                is_comptime_kind = true;
                read_macro_attr_options(vm, after_scope, &attribute_name);
            } else if (equal(after_scope, "test")) {
                is_test_kind = true;
                if (after_scope->next && equal(after_scope->next, "(")) {
                    Token *p = after_scope->next->next;
                    parse_test_args(vm, &p, &ta);
                }
            } else if (equal(after_scope, "test_setup") ||
                       equal(after_scope, "test_teardown")) {
                if (equal(after_scope, "test_teardown"))
                    is_teardown_kind = true;
                else
                    is_setup_kind = true;
                if (after_scope->next && equal(after_scope->next, "(")) {
                    Token *p = after_scope->next->next;
                    parse_test_setup_args(&p, &tsa);
                }
            } else if (equal(after_scope, "build")) {
                is_build_kind = true;
            } else if (equal(after_scope, "build_target")) {
                is_build_target_kind = true;
                // parse optional (kind=NAME); validate kind value now (with
                // token)
                if (after_scope->next && equal(after_scope->next, "(")) {
                    Token *p = after_scope->next->next;
                    while (p && !equal(p, ")") && p->kind != TK_EOF) {
                        if (equal(p, "kind") && p->next &&
                            equal(p->next, "=") && p->next->next &&
                            p->next->next->kind == TK_IDENT) {
                            Token *kind_tok = p->next->next;
                            if (!equal(kind_tok, "native"))
                                error_tok(vm, kind_tok,
                                          "[[cccc::build_target(kind=%.*s)]] "
                                          "is not supported — "
                                          "the only valid value is "
                                          "kind=native",
                                          kind_tok->len, kind_tok->loc);
                            build_target_kind =
                                strndup(kind_tok->loc, kind_tok->len);
                            p = kind_tok->next;
                        } else if (equal(p, ",")) {
                            p = p->next;
                        } else {
                            error_tok(
                                vm, p,
                                "unknown [[cccc::build_target]] option '%.*s'",
                                p->len, p->loc);
                        }
                    }
                }
            }
            // Advance past the attribute name token so the bare-name branches
            // below don't re-process it. The for loop's t = t->next will skip
            // to after_scope->next (e.g. '(' for args, ']' for closing
            // bracket).
            t = after_scope;
        } else if (equal(t, "build_target")) {
            // bare: __attribute__((build_target)) or
            // __attribute__((build_target(...)))
            is_build_target_kind = true;
            if (t->next && equal(t->next, "(")) {
                Token *p = t->next->next;
                while (p && !equal(p, ")") && p->kind != TK_EOF) {
                    if (equal(p, "kind") && p->next && equal(p->next, "=") &&
                        p->next->next && p->next->next->kind == TK_IDENT) {
                        Token *kind_tok = p->next->next;
                        if (!equal(kind_tok, "native"))
                            error_tok(
                                vm, kind_tok,
                                "build_target(kind=%.*s) is not supported — "
                                "the only valid value is kind=native",
                                kind_tok->len, kind_tok->loc);
                        build_target_kind =
                            strndup(kind_tok->loc, kind_tok->len);
                        p = kind_tok->next;
                    } else if (equal(p, ",")) {
                        p = p->next;
                    } else {
                        error_tok(
                            vm, p,
                            "unknown build_target attribute option '%.*s'",
                            p->len, p->loc);
                    }
                }
            }
        } else if (equal(t, "build")) {
            // bare: __attribute__((build))
            is_build_kind = true;
        } else if (equal(t, "test")) {
            // bare: __attribute__((test)) or __attribute__((test(...)))
            is_test_kind = true;
            if (t->next && equal(t->next, "(")) {
                Token *p = t->next->next;
                parse_test_args(vm, &p, &ta);
            }
        } else if (equal(t, "test_setup") || equal(t, "test_teardown")) {
            // bare: __attribute__((test_setup)) or
            // __attribute__((test_teardown))
            if (equal(t, "test_teardown"))
                is_teardown_kind = true;
            else
                is_setup_kind = true;
            if (t->next && equal(t->next, "(")) {
                Token *p = t->next->next;
                parse_test_setup_args(&p, &tsa);
            }
        }
    }

    // Only act on a positive comptime/test/setup/teardown/build match.
    if ((!is_comptime_kind && !is_test_kind && !is_setup_kind &&
         !is_teardown_kind && !is_build_kind && !is_build_target_kind) ||
        !attr_end)
        return false;

    // #1048: a `[[cccc::comptime]]`/`__attribute__((comptime))` declaration
    // (function or variable, body or bodyless) is cccc-only syntax -- its
    // extraction below removes it from the normal token stream entirely,
    // and any comptime *body* can reference reflection-API constructs
    // (Obj/MakeFunction/GetType/...) with no meaning to a host compiler.
    // Unlike #896's directive-level routing (`#include @comptime "x.h"`,
    // marked where the *directive* is written), there was previously no
    // marking at all for this attribute form, so a header reached only via
    // a plain `#include` -- never routed -- but containing its own
    // `[[cccc::comptime]]` declarations replayed verbatim into -c=native
    // output, and the host compiler choked on the comptime-only body text
    // past the (harmlessly ignored) unknown-attribute warning. `[[cccc::
    // test]]`/`build`/etc are deliberately not marked here -- those leave
    // an ordinary, portable function definition in the token stream (only
    // the attribute itself is stripped elsewhere), so replaying their
    // header verbatim is safe. tok->file, not attr_end->file: the
    // attribute's own opening token is where this declaration was
    // *written*, same granularity #896 already uses.
    if (is_comptime_kind && tok->file &&
        !is_private_header_tag(tok->file->name))
        mark_cccc_only_file(vm, tok->file->name);

    // #886: [[cccc::comptime]] typedef ...; -- a typedef is neither a
    // function nor a variable. Drop the attribute and let the typedef
    // itself flow through unchanged; it needs no comptime handling at all.
    // Must run before the function/variable probe below: that probe matches
    // on bare "ident (" token shape, which a function-pointer typedef's
    // declarator (e.g. `typedef int (*fn_t)(int);`) satisfies by accident
    // (on the leading `int (`), misrouting it into extract_macro_function
    // or extract_comptime_var.
    if (is_comptime_kind && starts_with_typedef(attr_end)) {
        *tok_ptr = attr_end;
        return true;
    }

    // Probe what follows attr_end: function or variable?
    // Heuristic: scan (respecting brace depth) for "ident (" before ";" or "=".
    bool   looks_like_function    = false;
    Token *comptime_decl_name_tok = NULL;
    {
        Token *probe       = attr_end;
        int    brace_depth = 0;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, "{"))
                brace_depth++;
            else if (equal(probe, "}"))
                brace_depth--;
            else if (brace_depth == 0) {
                if (equal(probe, ";") || equal(probe, "="))
                    break;
                if (probe->kind == TK_IDENT && probe->next &&
                    equal(probe->next, "(")) {
                    looks_like_function    = true;
                    comptime_decl_name_tok = probe;
                    break;
                }
            }
            probe = probe->next;
        }
    }
    // #884: a bodyless declaration (`ident (...)` reaches ";" with no "{")
    // must not be routed into extract_macro_function -- that scans forward
    // past the ";" hunting for a "{", swallowing whatever follows and
    // corrupting downstream prototype extraction (the original crash). It's
    // a no-op: compile_all_macros already emits prototypes for every
    // captured comptime function before any definition, so a forward
    // declaration is never needed for comptime-to-comptime calls.
    bool is_bodyless_comptime_decl = is_comptime_kind && looks_like_function &&
                                     !probe_function_definition(attr_end);

    // [[cccc::test]] / __attribute__((test)): record the function name, strip
    // the attribute, keep the function definition in the normal compilation
    // stream.
    if (is_test_kind) {
        Token *probe = attr_end;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, ";") || equal(probe, "="))
                break;
            if (probe->kind == TK_IDENT && probe->next &&
                equal(probe->next, "(")) {
                TestFnRecord *rec = calloc(1, sizeof(TestFnRecord));
                rec->name         = strndup(probe->loc, probe->len);
                rec->display_name = ta.test_name ? strdup(ta.test_name) : NULL;
                // Suite: explicit attribute arg takes priority, then active
                // pragma suite
                const char *s =
                    ta.suite_name ? ta.suite_name : vm->compiler.current_suite;
                rec->suite     = s ? strdup(s) : NULL;
                rec->error_pat = ta.error_pat ? strdup(ta.error_pat) : NULL;
                rec->error_pat_negate = ta.error_pat_negate;
                rec->expect_compile_error =
                    ta.expect_compile_error && !ta.error_pat;
                if (ta.expect_compile_error && ta.error_pat)
                    warn_tok(vm, probe, CCCC_WARN_ATTRIBUTES,
                             "expect_compile_error= is redundant when error= "
                             "is also set; ignored");
                rec->timeout_ms = ta.timeout_ms;
                if (ta.error_pat && ta.error_count_op != CMP_NONE) {
                    rec->expect_errors  = ta.error_count;
                    rec->error_count_op = ta.error_count_op;
                } else if (ta.error_pat && ta.error_count > 0) {
                    rec->expect_errors  = ta.error_count;
                    rec->error_count_op = CMP_EQ;
                }
                rec->ret_kind = ta.ret_kind;
                rec->ret_op   = (ta.ret_kind != RET_NONE) ? ta.ret_op : CMP_EQ;
                rec->ret_epsilon = ta.ret_epsilon_val;
                if (ta.ret_kind == RET_INT)
                    rec->ret_expect.ret_int = ta.ret_int_val;
                else if (ta.ret_kind == RET_FLOAT)
                    rec->ret_expect.ret_float = ta.ret_float_val;
                else if (ta.ret_kind == RET_STR)
                    rec->ret_expect.ret_str =
                        ta.ret_str_val ? strdup(ta.ret_str_val) : NULL;
                else if (ta.ret_kind == RET_STRUCT) {
                    // Ownership of the heap-allocated fields/text is
                    // transferred here.
                    rec->ret_expect.ret_fields = ta.ret_fields;
                    rec->ret_struct_text       = ta.ret_struct_text;
                }
                rec->expect_exit_code = ta.exit_code_val;
                // Link into the list before cc_parse_test_flags so that
                // the VM cleanup loop frees the record if error_tok longjmps.
                rec->next             = vm->compiler.test_fns;
                vm->compiler.test_fns = rec;
                if (ta.flags) {
                    rec->test_flags         = strdup(ta.flags);
                    CcTestFlagsDelta _delta = {0};
                    cc_parse_test_flags(vm, probe, ta.flags, rec->name,
                                        &_delta);
                    rec->test_flags_or           = _delta.or_bits;
                    rec->test_flags_mask         = _delta.set_mask;
                    rec->test_warn_or            = _delta.warn_or;
                    rec->test_warn_mask          = _delta.warn_mask;
                    rec->test_warn_errors_or     = _delta.warn_errors_or;
                    rec->test_warn_errors_mask   = _delta.warn_errors_mask;
                    rec->test_warn_as_errors     = _delta.warn_as_errors;
                    rec->test_warn_as_errors_set = _delta.warn_as_errors_set;
                    rec->test_ffi_allow          = _delta.ffi_allow;
                    rec->test_ffi_allow_count    = _delta.ffi_allow_count;
                    _delta.ffi_allow             = NULL;
                    _delta.ffi_allow_count       = 0;
                }
                rec->expect_stderr =
                    ta.expect_stderr ? strdup(ta.expect_stderr) : NULL;
                rec->reject_stderr =
                    ta.reject_stderr ? strdup(ta.reject_stderr) : NULL;
                rec->expect_stdout =
                    ta.expect_stdout ? strdup(ta.expect_stdout) : NULL;
                rec->reject_stdout =
                    ta.reject_stdout ? strdup(ta.reject_stdout) : NULL;
                if (ta.exit_code_val >= 0 && ta.error_pat) {
                    warn_tok(vm, probe, CCCC_WARN_ATTRIBUTES,
                             "exit_code= and error= are mutually exclusive; "
                             "error= ignored");
                    rec->error_pat = NULL;
                }
                if (ta.exit_code_val >= 0 && ta.ret_kind != RET_NONE) {
                    warn_tok(vm, probe, CCCC_WARN_ATTRIBUTES,
                             "exit_code= and return= are mutually exclusive; "
                             "return= ignored");
                    rec->ret_kind = RET_NONE;
                }
                break;
            }
            probe = probe->next;
        }
        *tok_ptr = attr_end;
        return true;
    }

    // [[cccc::test_setup]] / [[cccc::test_teardown]] / __attribute__ bare
    // forms: record the hook, keep the function in the normal compilation token
    // stream.
    if (is_setup_kind || is_teardown_kind) {
        Token *probe = attr_end;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, ";") || equal(probe, "="))
                break;
            if (probe->kind == TK_IDENT && probe->next &&
                equal(probe->next, "(")) {
                TestSetupRecord *rec = calloc(1, sizeof(TestSetupRecord));
                rec->fn_name         = strndup(probe->loc, probe->len);
                rec->name_pat    = tsa.name_pat ? strdup(tsa.name_pat) : NULL;
                rec->suite       = tsa.suite ? strdup(tsa.suite) : NULL;
                rec->once        = tsa.once;
                rec->is_teardown = is_teardown_kind;
                rec->inherit     = tsa.inherit;
                rec->next        = vm->compiler.test_setups;
                vm->compiler.test_setups = rec;
                break;
            }
            probe = probe->next;
        }
        *tok_ptr = attr_end;
        return true;
    }

    // [[cccc::build]] / __attribute__((build)): record the build-entry function
    // name, strip the attribute, keep the function in the normal compilation
    // stream (the runner finds and invokes it by name in --build mode).
    if (is_build_kind) {
        Token *probe = attr_end;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, ";") || equal(probe, "="))
                break;
            if (probe->kind == TK_IDENT && probe->next &&
                equal(probe->next, "(")) {
                BuildFnRecord *rec     = calloc(1, sizeof(BuildFnRecord));
                rec->name              = strndup(probe->loc, probe->len);
                rec->next              = vm->compiler.build_fns;
                vm->compiler.build_fns = rec;
                break;
            }
            probe = probe->next;
        }
        *tok_ptr = attr_end;
        return true;
    }

    // [[cccc::build_target]] / [[cccc::build_target(kind=native)]]: record
    // the factory function name and kind.  "native" is the only supported
    // (and default) kind.  The attribute is stripped; the function stays in
    // the normal compilation stream so the runner can find and invoke it by
    // address.
    if (is_build_target_kind) {
        const char *kind  = build_target_kind ? build_target_kind : "native";
        Token      *probe = attr_end;
        while (probe && probe->kind != TK_EOF) {
            if (equal(probe, ";") || equal(probe, "="))
                break;
            if (probe->kind == TK_IDENT && probe->next &&
                equal(probe->next, "(")) {
                BuildTargetFnRecord *rec =
                    calloc(1, sizeof(BuildTargetFnRecord));
                rec->name                     = strndup(probe->loc, probe->len);
                rec->kind                     = strdup(kind);
                rec->next                     = vm->compiler.build_target_fns;
                vm->compiler.build_target_fns = rec;
                break;
            }
            probe = probe->next;
        }
        free(build_target_kind);
        *tok_ptr = attr_end;
        return true;
    }

    // Route to function or variable extraction.
    if (emit_scan)
        return false; // comptime/macro extraction invalid inside emit blocks

    if (is_bodyless_comptime_decl) {
        // Record the name (for the never-defined check in compile_all_macros)
        // and drop the ENTIRE declaration, attribute included -- comptime
        // functions are never real Objs/symbols, so a leftover plain
        // prototype serves no purpose in the token stream. Worse, leaving
        // it in place is actively unsafe: scan_and_execute_global_calls'
        // "file-scope macro call" heuristic matches purely on token shape
        // (IDENT "(" ... ")" ";") and cannot tell a parameter list from call
        // arguments, so `int is_odd(int n);` would be misread as a
        // zero-context call to the comptime function `is_odd` and executed.
        if (comptime_decl_name_tok) {
            ComptimeDeclRecord *rec     = calloc(1, sizeof(ComptimeDeclRecord));
            rec->name                   = strndup(comptime_decl_name_tok->loc,
                                                  comptime_decl_name_tok->len);
            rec->tok                    = comptime_decl_name_tok;
            rec->next                   = vm->compiler.comptime_decls;
            vm->compiler.comptime_decls = rec;
        }
        Token *decl_end = attr_end;
        while (decl_end && decl_end->kind != TK_EOF && !equal(decl_end, ";"))
            decl_end = decl_end->next;
        *tok_ptr =
            decl_end && decl_end->kind != TK_EOF ? decl_end->next : decl_end;
        return true;
    }

    if (looks_like_function) {
        *tok_ptr = extract_macro_function(vm, attr_end, true, attribute_name);
    } else {
        *tok_ptr = extract_comptime_var(vm, attr_end);
    }
    return true;
}

// Returns true if tok starts a function definition: finds ident( ... ) {
// This is stricter than the look-ahead in try_extract_attr_macro — it requires
// the opening brace to be present, so bare call-statements like foo(); are not
// mistaken for definitions.
static bool probe_function_definition(Token *tok) {
    Token *probe = tok;
    while (probe && probe->kind != TK_EOF) {
        if (equal(probe, ";"))
            return false;
        if (probe->kind == TK_IDENT && probe->next && equal(probe->next, "(")) {
            // Scan past the parameter list
            Token *p     = probe->next;
            int    depth = 0;
            while (p && p->kind != TK_EOF) {
                if (equal(p, "("))
                    depth++;
                else if (equal(p, ")")) {
                    depth--;
                    if (depth == 0) {
                        p = p->next;
                        break;
                    }
                }
                p = p->next;
            }
            // Skip optional attribute/qualifier tokens then look for {
            while (p && p->kind != TK_EOF && !equal(p, "{") && !equal(p, ";"))
                p = p->next;
            return p && equal(p, "{");
        }
        probe = probe->next;
    }
    return false;
}

// Returns true if tok starts a plain variable/struct declaration (has an
// identifier before = or ; and no function-body brace at depth 0).
static bool probe_var_declaration(Token *tok) {
    // A file-scope call like foo(); starts with IDENT immediately followed by
    // '('. That is an expression statement, not a declaration — reject it.
    if (tok && tok->kind == TK_IDENT && tok->next && equal(tok->next, "("))
        return false;
    Token *probe       = tok;
    bool   found_ident = false;
    int    depth       = 0;
    while (probe && probe->kind != TK_EOF) {
        if (equal(probe, "{"))
            depth++;
        else if (equal(probe, "}"))
            depth--;
        else if (depth == 0) {
            if (equal(probe, "=") || equal(probe, ";"))
                return found_ident;
            if (probe->kind == TK_IDENT)
                found_ident = true;
        }
        probe = probe->next;
    }
    return false;
}

// If tok starts a struct/union/enum type definition (no variable after '}'),
// return the token AFTER the closing ';'.  Return NULL otherwise.
// Used to bulk-pass struct type definitions through the comptime block
// handler so individual body tokens never trigger false extractions.
static Token *probe_struct_type_def_end(Token *tok) {
    if (!equal(tok, "struct") && !equal(tok, "union") && !equal(tok, "enum"))
        return NULL;
    Token *p = tok->next;
    if (p && p->kind == TK_IDENT)
        p = p->next; // optional tag name
    if (!p || !equal(p, "{"))
        return NULL;
    int d = 0;
    while (p && p->kind != TK_EOF) {
        if (equal(p, "{"))
            d++;
        else if (equal(p, "}")) {
            if (--d == 0) {
                p = p->next;
                break;
            }
        }
        p = p->next;
    }
    if (!p || !equal(p, ";"))
        return NULL; // must end with ';' (no var name)
    return p->next;  // token after ';'
}

// #886: if tok starts a `typedef ...;` declaration, return the token AFTER
// the terminating ';'. Return NULL otherwise. Used to bulk-pass typedefs
// through the comptime block handler -- same rationale as
// probe_struct_type_def_end just above: reprocessing a typedef's individual
// body tokens one at a time (rather than as one block) lets a later token
// (e.g. the parameter-list identifier in a function-pointer declarator) get
// mistaken for the start of its own variable declaration by
// probe_var_declaration, which is paren-blind. Depth tracks '(' / '[' / '{'
// so a ';' inside e.g. an array-size expression or a nested declarator
// doesn't end the scan early.
static Token *probe_typedef_end(Token *tok) {
    if (!starts_with_typedef(tok))
        return NULL;
    Token *p     = tok->next;
    int    paren = 0, bracket = 0, brace = 0;
    while (p && p->kind != TK_EOF) {
        if (equal(p, "("))
            paren++;
        else if (equal(p, ")"))
            paren--;
        else if (equal(p, "["))
            bracket++;
        else if (equal(p, "]"))
            bracket--;
        else if (equal(p, "{"))
            brace++;
        else if (equal(p, "}"))
            brace--;
        else if (paren == 0 && bracket == 0 && brace == 0 && equal(p, ";"))
            return p->next;
        p = p->next;
    }
    return NULL;
}

// Inside a #pragma cccc comptime begin...end block, try to intercept an
// unannotated function definition or variable declaration and extract it
// as an implicit [[cccc::comptime]] entity.  Called from preprocess2 AFTER
// the _Pragma check so _Pragma tokens are never mis-routed.
// Returns true and advances *tok_ptr past the extracted definition on match.
// NOTE: struct/union/enum type definitions are handled by the caller via
// probe_struct_type_def_end; this function will never see them.
static bool try_extract_comptime_block_decl(VirtualMachine *vm,
                                            Token         **tok_ptr) {
    Token *tok = *tok_ptr;

    // #886: typedefs are handled by the caller in preprocess2 via
    // probe_typedef_end, passed through en bloc before this function is
    // ever reached -- same treatment as struct/union/enum type definitions
    // via probe_struct_type_def_end. This function should never see one.

    if (probe_function_definition(tok)) {
        *tok_ptr = extract_macro_function(vm, tok, true, NULL);
        return true;
    }
    if (probe_var_declaration(tok)) {
        *tok_ptr = extract_comptime_var(vm, tok);
        return true;
    }
    return false;
}

// Handle #pragma GCC diagnostic <action> ["-Wname"]
// Returns the token after the consumed pragma line.
static Token *handle_gcc_diagnostic(VirtualMachine *vm, Token *tok) {
    if (equal(tok, "push")) {
        // Grow the stack if needed
        if (vm->compiler.diag_stack_depth >= vm->compiler.diag_stack_cap) {
            int new_cap = vm->compiler.diag_stack_cap
                              ? vm->compiler.diag_stack_cap * 2
                              : 4;
            vm->compiler.diag_stack_warnings = realloc(
                vm->compiler.diag_stack_warnings, sizeof(uint64_t) * new_cap);
            vm->compiler.diag_stack_werror = realloc(
                vm->compiler.diag_stack_werror, sizeof(uint64_t) * new_cap);
            vm->compiler.diag_stack_cap = new_cap;
        }
        int d                               = vm->compiler.diag_stack_depth++;
        vm->compiler.diag_stack_warnings[d] = vm->compiler.warnings;
        vm->compiler.diag_stack_werror[d]   = vm->compiler.warning_errors;
        return skip_line(vm, tok->next);
    }

    if (equal(tok, "pop")) {
        if (vm->compiler.diag_stack_depth <= 0) {
            warn_tok(vm, tok, CCCC_WARN_CPP,
                     "#pragma GCC diagnostic pop with no matching push");
        } else {
            int d                       = --vm->compiler.diag_stack_depth;
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
        tok               = tok->next;
        // Expect a string token like "-Wunused"
        if (!tok || tok->kind != TK_STR || tok->at_bol) {
            warn_tok(vm, action_tok, CCCC_WARN_CPP,
                     "#pragma GCC diagnostic: expected warning option string");
            return skip_line(vm, tok ? tok : action_tok);
        }
        // tok->str holds the unescaped string contents (no quotes).
        const char *s = tok->str;

        // Must start with "-W"
        if (!s || s[0] != '-' || s[1] != 'W') {
            warn_tok(vm, tok, CCCC_WARN_CPP,
                     "#pragma GCC diagnostic: option must begin with '-W'");
            return skip_line(vm, tok->next);
        }

        // Extract the warning name (strip leading "-W")
        char name[256];
        int  namelen = (int)strlen(s) - 2;
        if (namelen <= 0 || namelen >= (int)sizeof(name)) {
            warn_tok(vm, tok, CCCC_WARN_CPP,
                     "#pragma GCC diagnostic: malformed warning option '%s'",
                     s);
            return skip_line(vm, tok->next);
        }
        memcpy(name, s + 2, namelen);
        name[namelen] = '\0';

        uint64_t mask = cccc_warning_mask_for_name(name);
        if (!mask) {
            // Suppress the "unknown warning" diagnostic when compiling a system
            // header or when --use-system-headers is active: SDK headers
            // routinely suppress Clang-specific warnings that CCCC does not
            // recognise, and these would just be noise.
            bool in_sys = (tok->file && tok->file->is_system_header) ||
                          vm->compiler.use_system_headers;
            if (!in_sys)
                warn_tok(
                    vm, tok, CCCC_WARN_CPP,
                    "#pragma GCC diagnostic: unknown warning option '-W%s'",
                    name);
            return skip_line(vm, tok->next);
        }

        if (do_ignore) {
            vm->compiler.warnings       &= ~mask;
            vm->compiler.warning_errors &= ~mask;
        } else if (do_warning) {
            vm->compiler.warnings       |= mask;
            vm->compiler.warning_errors &= ~mask;
        } else { // do_error
            vm->compiler.warnings       |= mask;
            vm->compiler.warning_errors |= mask;
        }
        return skip_line(vm, tok->next);
    }

    warn_tok(vm, tok, CCCC_WARN_CPP,
             "#pragma GCC diagnostic: unknown action '%.*s'", tok->len,
             tok->loc);
    return skip_line(vm, tok->next);
}

// Boolean-valued options accepted by #pragma cccc config(...) (#357). A bare
// key (no "= value") is shorthand for "= true", mirroring the no-argument
// individual pragmas these keys would otherwise correspond to.
typedef struct {
    const char *name;
    uint32_t    bit;
} PragmaConfigFlag;

static const PragmaConfigFlag pragma_config_flags[] = {
    {"bounds_checks", CCCC_BOUNDS_CHECKS},
    {"uaf_detection", CCCC_UAF_DETECTION},
    {"type_checks", CCCC_TYPE_CHECKS},
    {"overflow_checks", CCCC_OVERFLOW_CHECKS},
    {"stack_canaries", CCCC_STACK_CANARIES},
    {"heap_canaries", CCCC_HEAP_CANARIES},
    {"memory_leak_detection", CCCC_MEMORY_LEAK_DETECT},
    {"pointer_sanitizer", CCCC_POINTER_SANITIZER},
    {"memory_tagging", CCCC_MEMORY_TAGGING},
    {"checked_pointers", CCCC_CHECKED_BOUNDS}, // #770/#482-484
};

// Reads an integer literal token's text into *out. Returns false if tok is
// not a numeric token or contains non-digit characters.
static bool pragma_config_read_int(Token *tok, long *out) {
    if (!tok || (tok->kind != TK_PP_NUM && tok->kind != TK_NUM))
        return false;
    char   buf[32];
    size_t n = tok->len < sizeof(buf) - 1 ? tok->len : sizeof(buf) - 1;
    memcpy(buf, tok->loc, n);
    buf[n] = '\0';
    char *end;
    long  v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0')
        return false;
    *out = v;
    return true;
}

// Reads a boolean value: `true`/`false` identifiers or `1`/`0` literals.
static bool pragma_config_read_bool(Token *tok, bool *out) {
    if (!tok)
        return false;
    if (tok->kind == TK_IDENT) {
        if (equal(tok, "true")) {
            *out = true;
            return true;
        }
        if (equal(tok, "false")) {
            *out = false;
            return true;
        }
        return false;
    }
    long v;
    if (pragma_config_read_int(tok, &v) && (v == 0 || v == 1)) {
        *out = (v != 0);
        return true;
    }
    return false;
}

// Sets or clears a single CCCCFlags bit for `#pragma cccc config(...)`,
// respecting CLI precedence (#357: CLI-set bits always win) and skipping
// the change entirely in native mode (config() flags only affect VM codegen).
static void pragma_config_set_flag(VirtualMachine *vm, uint32_t bit,
                                   bool enable) {
    if (vm->compiler.native_mode)
        return;
    if (vm->compiler.cli_flags_mask & bit)
        return;
    if (enable)
        vm->flags |= bit;
    else
        vm->flags &= ~bit;
}

// Applies `safety = N` (0..3): clears/sets exactly the bits the corresponding
// -0/-1/-2/-3 preset would touch, except bits the CLI already pinned.
static void pragma_config_set_safety(VirtualMachine *vm, int level) {
    if (vm->compiler.native_mode)
        return;
    uint32_t preset    = level == 0   ? 0u
                         : level == 1 ? (uint32_t)CCCC_SAFETY_BASIC
                         : level == 2 ? (uint32_t)CCCC_SAFETY_STANDARD
                                      : (uint32_t)CCCC_SAFETY_MAX;
    uint32_t touchable = CCCC_SAFETY_PRESET_BITS & ~vm->compiler.cli_flags_mask;
    vm->flags          = (vm->flags & ~touchable) | (preset & touchable);
}

// Applies a single `key [= value]` pair from #pragma cccc config(...).
// `value` is NULL for a bare key. Unknown keys and invalid values are hard
// errors, matching existing #pragma cccc diagnostic style.
static void pragma_config_apply(VirtualMachine *vm, Token *key, Token *value) {
    // #924: every key this function handles (safety/individual flag) only ever
    // touches VM bytecode generation or runtime behavior --
    // pragma_config_set_flag/_safety both early-return under native_mode
    // already (see their own comments), so the pragma is silently a no-op
    // there. Surface that instead of
    // staying quiet about it, matching -c=native/-m/-c=generated's CLI-flag
    // warning for the same reason (main.c's warn_ignored_vm_flags). Fires
    // before key validation below: even a malformed value is still a no-op
    // here.
    if (vm->compiler.native_mode)
        warn_tok(
            vm, key, CCCC_WARN_IGNORED_FEATURES,
            "#pragma cccc config(%.*s) has no effect in -c=native mode "
            "-- it configures VM bytecode generation/runtime behavior only",
            (int)key->len, key->loc);
    if (equal(key, "safety")) {
        long level = 0;
        if (!value || !pragma_config_read_int(value, &level) || level < 0 ||
            level > 3)
            error_tok(
                vm, value ? value : key,
                "#pragma cccc config: 'safety' requires an integer value 0..3");
        pragma_config_set_safety(vm, (int)level);
        return;
    }
    for (size_t i = 0;
         i < sizeof(pragma_config_flags) / sizeof(pragma_config_flags[0]);
         i++) {
        if (!equal(key, (char *)pragma_config_flags[i].name))
            continue;
        bool enable = true; // bare key == "= true"
        if (value && !pragma_config_read_bool(value, &enable))
            error_tok(vm, value,
                      "#pragma cccc config: '%.*s' requires a boolean value "
                      "(true/false)",
                      (int)key->len, key->loc);
        pragma_config_set_flag(vm, pragma_config_flags[i].bit, enable);
        return;
    }
    error_tok(vm, key, "#pragma cccc config: unknown option '%.*s'",
              (int)key->len, key->loc);
}

// Parses `config ( key [= value] (, key [= value])* )` and applies each
// option via pragma_config_apply(). `sub` is the "config" token itself.
static Token *handle_pragma_config(VirtualMachine *vm, Token *sub) {
    Token *p = sub->next;
    if (!p || p->at_bol || !equal(p, "("))
        error_tok(vm, p && !p->at_bol && p->kind != TK_EOF ? p : sub,
                  "expected '(' after '#pragma cccc config'");
    p = p->next;
    if (equal(p, ")"))
        error_tok(vm, p, "#pragma cccc config requires at least one option");
    for (;;) {
        if (!p || p->kind != TK_IDENT)
            error_tok(vm, p && p->kind != TK_EOF ? p : sub,
                      "expected an option name in '#pragma cccc config'");
        Token *key   = p;
        p            = p->next;
        Token *value = NULL;
        if (equal(p, "=")) {
            value = p->next;
            p     = value ? value->next : NULL;
        }
        pragma_config_apply(vm, key, value);
        if (equal(p, ",")) {
            p = p->next;
            continue;
        }
        if (equal(p, ")")) {
            p = p->next;
            break;
        }
        error_tok(vm, p && p->kind != TK_EOF ? p : key,
                  "expected ',' or ')' in '#pragma cccc config'");
    }
    return skip_line(vm, p);
}

// Parses `link ( "name" (, "name")* )` and queues each string for FFI
// library resolution alongside -l/--library (#357). `sub` is the "link" token.
static Token *handle_pragma_link(VirtualMachine *vm, Token *sub) {
    Token *p = sub->next;
    if (!p || p->at_bol || !equal(p, "("))
        error_tok(vm, p && !p->at_bol && p->kind != TK_EOF ? p : sub,
                  "expected '(' after '#pragma cccc link'");
    p = p->next;
    if (equal(p, ")"))
        error_tok(vm, p,
                  "#pragma cccc link requires at least one library name");
    for (;;) {
        if (!p || p->kind != TK_STR)
            error_tok(vm, p && p->kind != TK_EOF ? p : sub,
                      "expected a string literal library name in '#pragma cccc "
                      "link'");
        strarray_push(&vm->compiler.pragma_link_libs, strdup(p->str));
        p = p->next;
        if (equal(p, ",")) {
            p = p->next;
            continue;
        }
        if (equal(p, ")")) {
            p = p->next;
            break;
        }
        error_tok(vm, p && p->kind != TK_EOF ? p : sub,
                  "expected ',' or ')' in '#pragma cccc link'");
    }
    return skip_line(vm, p);
}

// Copies a token's raw text into a malloc'd, NUL-terminated buffer -- unlike
// arena_strndup, this is individually freeable, matching pack_stack_names'
// own malloc/free lifetime (freed alongside pack_stack in src/vm.c, or
// individually on pack(pop, ident)/pack(pop)).
static char *dup_tok_text(Token *tok) {
    char *s = malloc(tok->len + 1);
    memcpy(s, tok->loc, tok->len);
    s[tok->len] = '\0';
    return s;
}

// Reads and validates a `#pragma pack` alignment argument: an integer power
// of two in [1,16], matching GCC/MSVC's own accepted range. error_tok's on
// anything else -- silently clamping or ignoring an out-of-range value would
// recreate the exact "accepted, not honoured" bug class #1173 exists to fix.
static int read_pack_align(VirtualMachine *vm, Token *tok) {
    long v;
    if (!pragma_config_read_int(tok, &v) || v <= 0 || v > 16 ||
        (v & (v - 1)) != 0)
        error_tok(vm, tok,
                  "#pragma pack: alignment must be a power of two between 1 "
                  "and 16");
    return (int)v;
}

// Parses `pack(N)`, `pack()`, `pack(push[, ident][, N])`, and
// `pack(pop[, ident])` -- the GCC/MSVC-compatible subset (#1173). `tok` is
// the "pack" token itself. Unlike the previous behavior (accepted, parsed,
// silently never honoured -- NATIVE.md's own prior claim), any other form
// is a hard error rather than a silent fallthrough to "unknown pragma
// ignored", which would just recreate the same bug under a narrower name.
static Token *handle_pragma_pack(VirtualMachine *vm, Token *tok) {
    Token *p = tok->next;
    if (!p || p->at_bol || !equal(p, "("))
        error_tok(vm, p && !p->at_bol && p->kind != TK_EOF ? p : tok,
                  "expected '(' after '#pragma pack'");
    p = p->next;

    if (equal(p, ")")) {
        vm->compiler.pack_cur = 0;
        return skip_line(vm, p->next);
    }

    if (equal(p, "push") || equal(p, "pop")) {
        bool is_push     = equal(p, "push");
        p                = p->next;
        Token *name_tok  = NULL;
        Token *align_tok = NULL;
        if (equal(p, ",")) {
            p = p->next;
            if (p && p->kind == TK_IDENT) {
                name_tok = p;
                p        = p->next;
                if (is_push && equal(p, ",")) {
                    align_tok = p->next;
                    p         = align_tok ? align_tok->next : NULL;
                }
            } else if (is_push) {
                align_tok = p;
                p         = p ? p->next : NULL;
            } else {
                error_tok(vm, p && p->kind != TK_EOF ? p : tok,
                          "expected an identifier after '#pragma pack(pop,'");
            }
        }
        if (!equal(p, ")"))
            error_tok(vm, p && p->kind != TK_EOF ? p : tok,
                      "expected ')' in '#pragma pack'");

        if (is_push) {
            if (vm->compiler.pack_stack_depth >= vm->compiler.pack_stack_cap) {
                int new_cap = vm->compiler.pack_stack_cap
                                  ? vm->compiler.pack_stack_cap * 2
                                  : 4;
                vm->compiler.pack_stack =
                    realloc(vm->compiler.pack_stack, sizeof(int) * new_cap);
                vm->compiler.pack_stack_names = realloc(
                    vm->compiler.pack_stack_names, sizeof(char *) * new_cap);
                vm->compiler.pack_stack_cap = new_cap;
            }
            int d                      = vm->compiler.pack_stack_depth++;
            vm->compiler.pack_stack[d] = vm->compiler.pack_cur;
            vm->compiler.pack_stack_names[d] =
                name_tok ? dup_tok_text(name_tok) : NULL;
            if (align_tok)
                vm->compiler.pack_cur = read_pack_align(vm, align_tok);
        } else { // pop
            if (vm->compiler.pack_stack_depth <= 0) {
                error_tok(vm, tok, "#pragma pack(pop) with no matching push");
            } else if (name_tok) {
                int idx = -1;
                for (int i = vm->compiler.pack_stack_depth - 1; i >= 0; i--) {
                    char *n = vm->compiler.pack_stack_names[i];
                    if (n && (int)strlen(n) == name_tok->len &&
                        strncmp(n, name_tok->loc, name_tok->len) == 0) {
                        idx = i;
                        break;
                    }
                }
                if (idx < 0)
                    error_tok(vm, name_tok,
                              "#pragma pack(pop, ...): no matching "
                              "'#pragma pack(push, ...)' with this name");
                vm->compiler.pack_cur = vm->compiler.pack_stack[idx];
                for (int i = vm->compiler.pack_stack_depth - 1; i >= idx; i--)
                    free(vm->compiler.pack_stack_names[i]);
                vm->compiler.pack_stack_depth = idx;
            } else {
                int d                 = --vm->compiler.pack_stack_depth;
                vm->compiler.pack_cur = vm->compiler.pack_stack[d];
                free(vm->compiler.pack_stack_names[d]);
            }
        }
        return skip_line(vm, p->next);
    }

    // pack(N)
    int n = read_pack_align(vm, p);
    p     = p->next;
    if (!equal(p, ")"))
        error_tok(vm, p && p->kind != TK_EOF ? p : tok,
                  "expected ')' in '#pragma pack'");
    vm->compiler.pack_cur = n;
    return skip_line(vm, p->next);
}

// Dispatch the body of a #pragma directive or a _Pragma() operator.
// tok is the first content token (after "#pragma" / after the destringized
// string).
static Token *handle_pragma_body(VirtualMachine *vm, Token *tok) {
    if (equal(tok, "once")) {
        hashmap_put(&vm->compiler.pragma_once, tok->file->name, (void *)1);
        return skip_line(vm, tok->next);
    } else if (equal(tok, "macro")) {
        error_tok(vm, tok,
                  "#pragma macro is no longer supported; use "
                  "[[cccc::comptime]] or __attribute__((comptime))");
    } else if (equal(tok, "comptime")) {
        error_tok(vm, tok,
                  "#pragma comptime is no longer supported; use "
                  "[[cccc::comptime]] or __attribute__((comptime))");
    } else if (equal(tok, "cccc")) {
        Token *sub = tok->next;
        if (equal(sub, "comptime")) {
            Token *after = sub->next;
            if (after && equal(after, "end")) {
                ComptimeCtxEntry *top = ctx_top(vm);
                if (!top || top->type != CTX_COMPTIME || !top->needs_end)
                    error_tok(vm, tok,
                              "stray #pragma cccc comptime end without "
                              "matching begin");
                ctx_pop(vm);
                return skip_line(vm, after->next);
            }
            bool              is_begin = after && equal(after, "begin");
            ComptimeCtxEntry *top      = ctx_top(vm);
            if (top && top->type == CTX_COMPTIME)
                error_tok(vm, tok,
                          "#pragma cccc comptime: blocks cannot be nested");
            ctx_push(vm, CTX_COMPTIME, is_begin, tok->file, tok);
            return skip_line(vm, is_begin ? after->next : after);
        } else if (equal(sub, "emit")) {
            Token *after = sub->next;
            if (after && equal(after, "end")) {
                ComptimeCtxEntry *top = ctx_top(vm);
                if (!top || top->type != CTX_EMIT)
                    error_tok(
                        vm, tok,
                        "stray #pragma cccc emit end without matching begin");
                ctx_pop(vm);
                return skip_line(vm, after->next);
            }
            if (!after || after->kind == TK_EOF || after->at_bol ||
                !equal(after, "begin"))
                error_tok(
                    vm,
                    after && !after->at_bol && after->kind != TK_EOF ? after
                                                                     : tok,
                    "expected 'begin' or 'end' after '#pragma cccc emit'");
            ComptimeCtxEntry *top = ctx_top(vm);
            if (top && top->type == CTX_EMIT)
                error_tok(vm, tok,
                          "#pragma cccc emit: blocks cannot be nested");
            if (!top || top->type != CTX_COMPTIME)
                error_tok(
                    vm, tok,
                    "#pragma cccc emit requires an active comptime context");
            ctx_push(vm, CTX_EMIT, true, tok->file, tok);
            return skip_line(vm, after->next);
        } else if (equal(sub, "end")) {
            ComptimeCtxEntry *top = ctx_top(vm);
            if (top && top->type == CTX_COMPTIME && top->needs_end) {
                ctx_pop(vm);
            } else if (vm->compiler.suite_stack_len > 0) {
                suite_pop(vm);
            } else {
                error_tok(vm, tok,
                          "stray #pragma cccc end without matching begin");
            }
            return skip_line(vm, sub->next);
        } else if (equal(sub, "suite")) {
            Token *after = sub->next;
            if (equal(after, "begin")) {
                Token *name_tok = after->next;
                if (!name_tok || name_tok->kind != TK_STR || name_tok->at_bol)
                    error_tok(
                        vm, after,
                        "#pragma cccc suite begin requires a string name");
                if (name_tok->str[0] == '\0')
                    error_tok(
                        vm, name_tok,
                        "#pragma cccc suite begin requires a non-empty name");
                suite_push(vm, name_tok->str, tok);
                return skip_line(vm, name_tok->next);
            } else if (equal(after, "end")) {
                if (vm->compiler.suite_stack_len == 0)
                    error_tok(
                        vm, tok,
                        "stray #pragma cccc suite end without matching begin");
                suite_pop(vm);
                return skip_line(vm, after->next);
            } else {
                error_tok(
                    vm, after && after->kind != TK_EOF ? after : sub,
                    "expected 'begin' or 'end' after '#pragma cccc suite'");
            }
        } else if (equal(sub, "config")) {
            return handle_pragma_config(vm, sub);
        } else if (equal(sub, "link")) {
            return handle_pragma_link(vm, sub);
        } else if (equal(sub, "diagnostic")) {
            return handle_gcc_diagnostic(vm, sub->next);
        } else {
            error_tok(vm, sub && sub->kind != TK_EOF ? sub : tok,
                      "unknown #pragma cccc directive");
        }
    } else if ((equal(tok, "GCC") || equal(tok, "clang")) &&
               equal(tok->next, "diagnostic")) {
        return handle_gcc_diagnostic(vm, tok->next->next);
    } else if (equal(tok, "pack")) {
        return handle_pragma_pack(vm, tok);
    } else {
        // Suppress "unknown pragma" noise from system headers. Real SDK
        // headers use #pragma GCC system_header, #pragma clang
        // assume_nonnull, and various other pragmas that CCCC does not
        // implement. These are informational hints to the SDK compiler and
        // not relevant to CCCC's VM execution.
        bool in_sys = (tok->file && tok->file->is_system_header) ||
                      vm->compiler.use_system_headers;
        if (!in_sys)
            warn_tok(vm, tok, CCCC_WARN_CPP, "unknown pragma ignored");
        do {
            tok = tok->next;
        } while (!tok->at_bol && tok->kind != TK_EOF);
    }
    return tok;
}

static void queue_comptime_include(VirtualMachine *vm, const char *filename,
                                   bool is_dquote) {
    char *line = arena_format(
        vm, is_dquote ? "#include \"%s\"" : "#include <%s>", filename);
    strarray_push(&vm->compiler.comptime_pending_includes, line);
}

// Resolve a quoted relative include (e.g. "local.h") against the including
// file's directory (and the include search paths) before it gets replayed
// under the synthetic <comptime-include> filename during the comptime pass
// (build_combined_macro_tokens in src/macros.c) — that synthetic file has no
// real directory of its own, so an unresolved relative path can't be found
// from there. Returns NULL (caller falls back to the raw filename) for URLs
// or names that don't resolve to an existing path.
static char *resolve_comptime_include_path(VirtualMachine *vm, Token *start_tok,
                                           char *filename, int filename_len,
                                           bool is_dquote) {
    if (is_url(filename))
        return NULL;
    char *resolved = NULL;
    if (filename[0] != '/' && is_dquote) {
        char *rel = format_relative_path(vm, start_tok->file->name, filename);
        if (file_exists(rel))
            resolved = rel;
        // #1194: same embedded-header gap as the other two resolution
        // sites -- a quoted include written inside an embedded header
        // never matches the on-disk relative branch above. Returning the
        // normalized short name here (not the "<embedded>/..." key) is
        // enough: queue_comptime_include below requeues it as a fresh
        // `#include <name>` line, which the ordinary PP_INCLUDE handling
        // already resolves through try_embedded_std_header().
        if (!resolved) {
            char *embedded = resolve_embedded_relative_header(
                vm, start_tok->file ? start_tok->file->name : NULL, filename);
            if (embedded)
                resolved = embedded;
        }
    }
    if (!resolved)
        resolved = search_include_paths(vm, filename, filename_len, !is_dquote);
    if (!resolved && is_dquote)
        resolved = search_include_paths(vm, filename, filename_len, true);
    return resolved;
}

static void queue_comptime_directive(VirtualMachine *vm, char *line) {
    if (line && *line)
        strarray_push(&vm->compiler.comptime_pending_includes, line);
}

// Visit all tokens in `tok` while evaluating preprocessing
// macros and directives.
static Token *preprocess2(VirtualMachine *vm, Token *tok) {
    Token  head = {};
    Token *cur  = &head;

    while (tok->kind != TK_EOF) {
        if (tok->kind == TK_MACRO_SCOPE_PUSH) {
            macro_scope_push(vm, hashmap_snapshot(&vm->compiler.macros));
            tok = tok->next;
            continue;
        }
        if (tok->kind == TK_MACRO_SCOPE_POP) {
            hashmap_restore(&vm->compiler.macros, macro_scope_pop(vm));
            tok = tok->next;
            continue;
        }

        if (ctx_top(vm) && ctx_top(vm)->type == CTX_EMIT) {
            Token *start = tok;
            if (try_rewrite_at_directive(vm, &tok))
                continue;
            if (is_hash(start)) {
                if (is_pragma_cccc(start)) {
                    tok = handle_pragma_body(vm, tok->next->next);
                    continue;
                }
                {
                    Token *route_start = start->next->next;
                    Token *route_after = route_start;
                    if (route_start && read_include_route(&route_after) ==
                                           INCLUDE_ROUTE_COMPTIME) {
                        char *line = copy_routed_directive_line(
                            vm, start, route_start, route_after);
                        queue_comptime_directive(vm, line);
                        tok = skip_line(vm, route_after);
                        continue;
                    }
                }
                char *line = copy_raw_directive_line(vm, start);
                push_emit_directive(vm, line, false);
                cur = append_emit_marker_tokens(vm, cur, start, line);
                tok = skip_line(vm, tok->next);
                continue;
            }
            // Scan for [[cccc::test]]/[[cccc::build]]/[[cccc::build_target]] in
            // emitted tokens so comptime emit blocks can generate
            // mode-attributed functions. Use a local pointer so the emit loop's
            // tok is unaffected.
            {
                Token *scan = start;
                try_extract_attr_macro(vm, &scan, /*emit_scan=*/true);
            }
            cc_record_emit_source(vm, copy_raw_directive_line(vm, start));
            bool first_token = true;
            while (tok->kind != TK_EOF && (first_token || !tok->at_bol)) {
                first_token        = false;
                tok->line_delta    = tok->file->line_delta;
                tok->filename      = tok->file->display_name;
                tok->diag_warnings = (1ULL << 63) | vm->compiler.warnings;
                tok->diag_werror   = (1ULL << 63) | vm->compiler.warning_errors;
                tok->pack_align    = vm->compiler.pack_cur;
                cur = cur->next = tok;
                tok             = tok->next;
            }
            continue;
        }

        // If it is a macro, expand it.
        if (expand_macro(vm, &tok, tok))
            continue;

        // Pass through if it is not a "#".
        if (!is_hash(tok)) {
            // Intercept [[cccc::macro]] / __attribute__((macro)) and comptime
            // attribute blocks before they reach the parser. On a match,
            // the definition is extracted into the MacroFn/ComptimeVar list
            // and tok is advanced past it; nothing is added to the output.
            // @<ppkeyword> is rewritten to #<keyword> @<opposite_route> before
            // @name / @name(args) is rewritten to the canonical attribute form
            // so that try_extract_attr_macro sees [[cccc::name(...)]] as usual.
            if (try_rewrite_at_directive(vm, &tok))
                continue;
            if (try_rewrite_at_attr(vm, &tok))
                continue;
            if (try_rewrite_cccc_keyword_attr(vm, &tok))
                continue;
            if (try_extract_attr_macro(vm, &tok, false))
                continue;

            // _Pragma("string") — C99 §6.10.9: equivalent to #pragma string
            if (equal(tok, "_Pragma")) {
                tok = tok->next;
                tok = skip(vm, tok, "(");
                if (tok->kind != TK_STR)
                    error_tok(vm, tok, "_Pragma requires a string literal");
                char  *content = arena_format(vm, "%s\n", tok->str);
                Token *pragma_toks =
                    tokenize(vm, new_file(vm, tok->file->name,
                                          tok->file->file_no, content));
                handle_pragma_body(vm, pragma_toks);
                tok = tok->next;
                tok = skip(vm, tok, ")");
                continue;
            }

            // Extension: inline #embed — '#' not at beginning of line
            if (equal(tok, "#") && equal(tok->next, "embed")) {
                if (vm->compiler.c_std < CCCC_STD_C23)
                    error_tok(vm, tok, "'#embed' is not available before C23");
                tok = handle_embed_directive(vm, tok->next->next, tok, true);
                continue;
            }

            // Inside a #pragma cccc comptime begin...end block: intercept
            // unannotated function definitions and variable declarations.
            // #889: skip this entirely while evaluating a #if/#elif constant
            // expression (pp_const_expr_depth > 0) -- defined(...)/
            // __has_include(...) mint synthetic tokens under a fresh File*,
            // which must not be mistaken by the check below for "the file
            // that opened the block has ended".
            ComptimeCtxEntry *comptime_top =
                vm->compiler.pp_const_expr_depth ? NULL : ctx_top(vm);
            if (comptime_top && comptime_top->type == CTX_COMPTIME) {
                // Auto-close if the file that opened the block has ended.
                if (tok->file != comptime_top->file) {
                    if (comptime_top->needs_end)
                        warn_tok(vm, tok, CCCC_WARN_COMPTIME_BLOCK_LEAK,
                                 "unclosed #pragma cccc comptime begin in "
                                 "included file; "
                                 "block closed automatically");
                    ctx_pop(vm);
                } else {
                    // struct/union/enum type definitions and typedefs must
                    // pass through en-bloc so body tokens don't trigger
                    // false extractions.
                    Token *struct_end = probe_struct_type_def_end(tok);
                    Token *typedef_end =
                        struct_end ? NULL : probe_typedef_end(tok);
                    Token *passthrough_end =
                        struct_end ? struct_end : typedef_end;
                    if (passthrough_end) {
                        while (tok != passthrough_end) {
                            tok->line_delta = tok->file->line_delta;
                            tok->filename   = tok->file->display_name;
                            tok->diag_warnings =
                                (1ULL << 63) | vm->compiler.warnings;
                            tok->diag_werror =
                                (1ULL << 63) | vm->compiler.warning_errors;
                            tok->pack_align = vm->compiler.pack_cur;
                            cur = cur->next = tok;
                            tok             = tok->next;
                        }
                        continue;
                    }
                    if (try_extract_comptime_block_decl(vm, &tok))
                        continue;
                }
            }

            tok->line_delta = tok->file->line_delta;
            tok->filename   = tok->file->display_name;
            // Stamp the effective diagnostic state so warn_tok can use it.
            tok->diag_warnings = (1ULL << 63) | vm->compiler.warnings;
            tok->diag_werror   = (1ULL << 63) | vm->compiler.warning_errors;
            // Stamp the effective #pragma pack(N) alignment cap so
            // struct_union_decl can read it off the struct/union keyword
            // token later, at parse time (#1173).
            tok->pack_align = vm->compiler.pack_cur;
            cur = cur->next = tok;
            tok             = tok->next;
            continue;
        }

        Token *start = tok;
        tok          = tok->next;

        if (tok->kind == TK_PP_NUM) {
            read_line_marker(vm, &tok, tok);
            continue;
        }

        Token       *route_start     = tok->next;
        Token       *route_after     = route_start;
        IncludeRoute directive_route = read_include_route(&route_after);
        // #896: a directive using any cccc-only routing taints its own file
        // -- that syntax is never valid to a downstream system compiler, so
        // the file can't be re-emitted as a raw #include for -c=native. This
        // applies regardless of which file the directive is in, not just the
        // primary file (see run_native_backend's re-emission filter).
        if (directive_route != INCLUDE_ROUTE_NORMAL && start->file)
            mark_cccc_only_file(vm, start->file->name);
        if (directive_route == INCLUDE_ROUTE_EMIT) {
            char *line =
                copy_routed_directive_line(vm, start, route_start, route_after);
            push_emit_directive(vm, line, pp_directive(tok) == PP_INCLUDE);
            cur = append_emit_marker_tokens(vm, cur, start, line);
            tok = skip_line(vm, route_after);
            continue;
        }
        if (directive_route == INCLUDE_ROUTE_COMPTIME) {
            // #include @comptime "local.h" needs its relative path resolved
            // here — the generic copy_routed_directive_line text path below
            // would otherwise replay the raw quoted filename under the
            // synthetic <comptime-include> file, which has no directory of
            // its own to resolve against (ticket #684). Other directives
            // routed with @comptime (#define, #if, ...) don't reference a
            // filesystem path, so they keep the original text-copy path.
            if (pp_directive(tok) == PP_INCLUDE) {
                bool   is_dquote;
                int    filename_len;
                Token *rest     = route_after;
                char  *filename = read_include_filename(
                    vm, &rest, route_after, &is_dquote, &filename_len);
                char *resolved = resolve_comptime_include_path(
                    vm, start, filename, filename_len, is_dquote);
                queue_comptime_include(vm, resolved ? resolved : filename,
                                       resolved ? false : is_dquote);
                tok = skip_line(vm, rest);
                continue;
            }
            char *line =
                copy_routed_directive_line(vm, start, route_start, route_after);
            queue_comptime_directive(vm, line);
            tok = skip_line(vm, route_after);
            continue;
        }
        // @build / @test gate: mode-conditional directive routing.
        // When the mode is active the route token is stripped and the directive
        // falls through to the switch for normal processing.
        // When inactive:
        //   - if/ifdef/ifndef/elif/elifdef/elifndef → rewrite as #if 0 / #elif
        //   0
        //     so the conditional is always false while keeping nesting balanced
        //   - else/endif → strip route and process normally (must run to
        //   balance)
        //   - all other directives (define, undef, include, ...) → silently
        //   drop
        if (directive_route == INCLUDE_ROUTE_BUILD ||
            directive_route == INCLUDE_ROUTE_TEST) {
            bool  active = (directive_route == INCLUDE_ROUTE_BUILD)
                               ? vm->compiler.build_mode
                               : vm->compiler.testing_mode;
            PPDir d      = pp_directive(tok);
            if (active) {
                // Strip the route token; directive falls through to the switch.
                tok->next = route_after;
            } else {
                switch (d) {
                    case PP_IF:
                    case PP_IFDEF:
                    case PP_IFNDEF:
                    case PP_ELIF:
                    case PP_ELIFDEF:
                    case PP_ELIFNDEF: {
                        // Push a false conditional to maintain nesting balance.
                        const char *kw  = (d == PP_ELIF || d == PP_ELIFDEF ||
                                           d == PP_ELIFNDEF)
                                              ? "elif"
                                              : "if";
                        char       *src = arena_format(vm, "#%s 0\n", kw);
                        Token      *nt =
                            tokenize(vm, new_file(vm, start->file->name,
                                                  start->file->file_no, src));
                        Token *last = nt;
                        while (last->next && last->next->kind != TK_EOF)
                            last = last->next;
                        last->next = skip_line(vm, route_after);
                        tok        = nt;
                        continue;
                    }
                    case PP_ELSE:
                    case PP_ENDIF:
                        // Must run so the conditional stack stays balanced.
                        tok->next = route_after;
                        break;
                    default:
                        // Silently drop mode-inactive directives.
                        tok = skip_line(vm, route_after);
                        continue;
                }
            }
        }
        // @shared / [[cccc::shared]] is only meaningful on #include and
        // #define (#888 -- per-macro opt-in into the isolated comptime macro
        // table); reject it on any other directive before falling into the
        // switch.
        if (directive_route == INCLUDE_ROUTE_SHARED &&
            pp_directive(tok) != PP_INCLUDE && pp_directive(tok) != PP_DEFINE) {
            error_tok(vm, route_start,
                      "@shared is only valid on #include or #define");
        }

        // Auto-capture: when not using --emit-only, record directives from any
        // command-line input file (not just input_files[0]) that are outside
        // comptime blocks verbatim, so they appear in -c=generated output
        // without needing [[cccc::emit]] annotations, and get replayed into
        // -c=native/-m output for the host compiler.
        // Skip during the macro-compilation preprocessing pass (in_macro_mode)
        // since those token streams re-use primary_file pointers but are not
        // part of the user-visible source.
        // For @shared includes, emit a clean line (route stripped) so generated
        // output contains a plain #include rather than #include @shared.
        // #1006: was `vm->compiler.primary_file && start->file ==
        // vm->compiler.primary_file` -- primary_file only ever names
        // input_files[0] (cc_preprocess/linker.c pin it to the *first* input
        // file forever), so a non-primary translation unit's own #includes
        // (e.g. <stdlib.h>, <stdio.h>) were never captured, and so never
        // replayed -- the host compiler then rejected size_t/malloc/free/
        // puts as undeclared even though tu2.c alone compiled fine. Widened
        // to cc_file_is_command_line_input() (preprocess.c), the same test
        // #1002/#1006 already established for
        // serialize_program.c's/parse_types.c's own primary_file-keyed drops.
        // One residual: directives from more than one TU can now collide in the
        // replayed output (e.g. two TUs each #define-ing the same macro to
        // different values) -- harmless, since by the time this text is
        // replayed every TU has already been fully parsed into AST using its
        // own, now-per-TU (#1001) macro state; the replayed text exists only to
        // bring types/library declarations into scope for the host compiler, so
        // a colliding #define is at worst a host redefinition warning, not a
        // semantic change. See man/HEADERS.md.
        char *ac_include_line = NULL; // #896: set below when this directive
                                      // is a captured #include that got
                                      // auto-captured, so the PP_INCLUDE
                                      // case can pair it with its resolved
                                      // path once that's known
        {
            ComptimeCtxEntry *_ac = ctx_top(vm);
            // #1262: under -c=generated, an unrouted directive from a
            // *non-primary* command-line input is not replayed. Additional
            // inputs there are comptime-support modules (their bodies are
            // forwarded into the comptime program by #1243, on demand) --
            // none of their runtime code reaches the serialized output, so
            // their own #include/#define scaffolding has no business in it
            // either, and a downstream `cc` should not need the extra -I to
            // resolve a leaked quoted include. -c=native/-m are unaffected
            // (the whole program is emitted there, so #1006's widening still
            // applies); an @emit/@shared route still opts a directive in
            // from any file.
            bool _ac_generated_nonprimary =
                vm->compiler.emit_generated_only &&
                directive_route == INCLUDE_ROUTE_NORMAL && start->file &&
                start->file != vm->compiler.primary_file &&
                cc_file_is_command_line_input(vm, start->file->name) &&
                !cc_file_is_cccc_only(vm, start->file->name);
            // #1022 (found closing #1022's own pthread.h work): a
            // cccc-only header's (is_cccc_supplied_only_header --
            // stdbit.h/uchar.h/threads.h/Availability.h/decimal_math.h)
            // #include line is deliberately suppressed by serialize_program.c's
            // replay loop (cc_file_is_cccc_only), and its own type/function
            // definitions are re-derived to compensate (#896) -- but a
            // *plain, non-cccc-only* header that IT #includes (threads.h's
            // own `#include "pthread.h"`/`"time.h"`) was neither replayed
            // (this gate only fired for a command-line input file) nor
            // re-derived (re-derivation only covers types the parser itself
            // saw declarations for, not a header pulled in transitively) --
            // so a real host compiler reprocessing the re-derived text hit
            // "unknown type name 'pthread_key_t'"/"'struct timespec' will
            // not be visible", types nothing declared. mark_cccc_only_file
            // for the outer header runs before its own body is walked (see
            // the PP_INCLUDE case below), so cc_file_is_cccc_only(start->
            // file->name) is already true here for a directive nested
            // inside one -- widen the gate to also auto-capture from a
            // cccc-only includer, not just a command-line input file.
            if (!vm->compiler.emit_strict && !vm->compiler.in_macro_mode &&
                !_ac_generated_nonprimary && start->file &&
                (cc_file_is_command_line_input(vm, start->file->name) ||
                 cc_file_is_cccc_only(vm, start->file->name)) &&
                !(_ac && _ac->type == CTX_COMPTIME) && !is_pragma_cccc(start) &&
                !is_pragma_pack(start)) {
                char *_ac_line = (directive_route == INCLUDE_ROUTE_SHARED ||
                                  directive_route == INCLUDE_ROUTE_BUILD ||
                                  directive_route == INCLUDE_ROUTE_TEST)
                                     ? copy_routed_directive_line(
                                           vm, start, route_start, route_after)
                                     : copy_raw_directive_line(vm, start);
                push_emit_directive(vm, _ac_line,
                                    pp_directive(tok) == PP_INCLUDE);
                cc_record_emit_source(vm, _ac_line);
                if (pp_directive(tok) == PP_INCLUDE)
                    ac_include_line = _ac_line;
            }
        }

        switch (pp_directive(tok)) {
            case PP_INCLUDE: {
                bool         is_dquote;
                int          filename_len;
                Token       *filename_start = tok->next;
                IncludeRoute include_route =
                    read_include_route(&filename_start);
                char *filename = read_include_filename(
                    vm, &tok, filename_start, &is_dquote, &filename_len);
                // Comptime includes and ordinary includes inside a comptime
                // block are queued for the comptime pass only; they never reach
                // the runtime TU. Resolve the absolute path before queueing so
                // that quoted relative includes like #include @comptime
                // "local.h" can be found from the synthetic comptime
                // preprocessing context (ticket #684).
                if (include_route == INCLUDE_ROUTE_COMPTIME ||
                    (include_route == INCLUDE_ROUTE_NORMAL && ctx_top(vm) &&
                     ctx_top(vm)->type == CTX_COMPTIME)) {
                    tok            = skip_line(vm, tok);
                    char *resolved = resolve_comptime_include_path(
                        vm, start, filename, filename_len, is_dquote);
                    // Unresolved (e.g. a URL, or a name genuinely not found
                    // anywhere) falls back to the original filename/quoting
                    // unchanged.
                    queue_comptime_include(vm, resolved ? resolved : filename,
                                           resolved ? false : is_dquote);
                    break;
                }
                // Shared includes go to both contexts: queue for comptime, then
                // fall through to the normal runtime splice below. Resolve the
                // absolute path before queueing so that quoted relative
                // includes like #include @shared "local.h" can be found from
                // the synthetic comptime preprocessing context.
                if (include_route == INCLUDE_ROUTE_SHARED) {
                    char *shared_path = resolve_comptime_include_path(
                        vm, start, filename, filename_len, is_dquote);
                    // Queue as an angle-bracket include so the absolute path is
                    // used directly (no relative-path ambiguity in comptime
                    // context).
                    queue_comptime_include(
                        vm, shared_path ? shared_path : filename, false);
                }
                if (include_route == INCLUDE_ROUTE_EMIT) {
                    char *line = arena_format(
                        vm, is_dquote ? "#include \"%s\"" : "#include <%s>",
                        filename);
                    tok = skip_line(vm, tok);
                    push_emit_directive(vm, line, true);
                    break;
                }
                // Gate standard headers that require a minimum C version.
                {
                    static const struct {
                        const char *name;
                        CStdVersion min;
                    } gates[] = {// C99 headers
                                 {"complex.h", CCCC_STD_C99},
                                 {"fenv.h", CCCC_STD_C99},
                                 {"inttypes.h", CCCC_STD_C99},
                                 {"iso646.h", CCCC_STD_C99},
                                 {"stdbool.h", CCCC_STD_C99},
                                 {"stdint.h", CCCC_STD_C99},
                                 {"tgmath.h", CCCC_STD_C99},
                                 {"wchar.h", CCCC_STD_C99},
                                 {"wctype.h", CCCC_STD_C99},
                                 // C11 headers
                                 {"stdalign.h", CCCC_STD_C11},
                                 {"stdatomic.h", CCCC_STD_C11},
                                 {"stdnoreturn.h", CCCC_STD_C11},
                                 {"uchar.h", CCCC_STD_C11},
                                 {NULL, 0}};
                    for (int gi = 0; gates[gi].name; gi++) {
                        if (strcmp(filename, gates[gi].name) == 0 &&
                            vm->compiler.c_std < gates[gi].min) {
                            const char *req =
                                gates[gi].min == CCCC_STD_C11 ? "C11" : "C99";
                            error_tok(vm, start->next,
                                      "<%s> is not available before %s",
                                      filename, req);
                            break;
                        }
                    }
                }
                tok = skip_line(vm, tok);

                // Check for URL includes (supported with both <...> and "...")
                if (is_url(filename)) {
#ifdef CCCC_HAS_CURL
                    char *cache_path = fetch_url_to_cache(vm, filename);
                    if (!cache_path) {
                        error_tok(vm, start->next, "failed to fetch URL: %s",
                                  filename);
                    }
                    // Track URL -> cache path mapping for error reporting
                    hashmap_put(&vm->compiler.url_to_path, cache_path,
                                (void *)filename);
                    tok = include_file(vm, tok, cache_path, start->next->next,
                                       filename, false);
                    continue;
#else
                    error_tok(vm, start->next,
                              "URL includes require CCCC to be built with "
                              "CCCC_HAS_CURL=1");
#endif
                }

                if (filename[0] != '/' && is_dquote) {
                    char *path =
                        format_relative_path(vm, start->file->name, filename);
                    if (file_exists(path)) {
                        record_include_edge(
                            vm, start->file ? start->file->name : NULL,
                            path);           // #896
                        if (ac_include_line) // #896
                            hashmap_put(&vm->compiler.emit_include_paths,
                                        ac_include_line, path);
                        // #1096: a bundled header quote-#including another
                        // bundled header by relative path (fcntl.h's own
                        // `#include "unistd.h"`) takes this early branch,
                        // not the general search_include_paths() branch
                        // further down that also marks bundled headers --
                        // without this, `close()`'s Obj.tok would still
                        // point at unistd.h and the #901 gate below would
                        // still treat it as "supplied by the replayed
                        // #include" even though it isn't (real bug this
                        // ticket is about: `#include <fcntl.h>` replays to
                        // the *host's* fcntl.h, which never declares
                        // close()). get_std_header() identifies the target
                        // by name, same test the on-disk branch below uses.
                        if (get_std_header(filename))
                            mark_cccc_bundled_file(vm, path);
                        tok = include_file(vm, tok, path, start->next->next,
                                           filename,
                                           start->file->is_system_header);
                        break;
                    }
                }

                // #1194: a quoted include written INSIDE an embedded header
                // (e.g. bundled sys/stat.h's own `#include "../time.h"`) --
                // the on-disk relative branch just above always misses (the
                // synthetic "<embedded>/..." base path never exists on
                // disk), so without this it degrades straight to
                // search_include_paths()/try_embedded_std_header() below,
                // both of which key on the include's own literal spelling --
                // "../time.h" can never be a table key. Resolve it against
                // the embedded header's own virtual directory instead.
                if (is_dquote) {
                    char *resolved = resolve_embedded_relative_header(
                        vm, start->file ? start->file->name : NULL, filename);
                    if (resolved) {
                        char *embedded_src =
                            try_embedded_std_header(vm, resolved);
                        if (embedded_src) {
                            if (ac_include_line)
                                hashmap_put(&vm->compiler.emit_include_paths,
                                            ac_include_line,
                                            embedded_header_key(vm, resolved));
                            if (is_cccc_supplied_only_header(resolved))
                                mark_cccc_only_file(
                                    vm, embedded_header_key(vm, resolved));
                            mark_cccc_bundled_file(
                                vm, embedded_header_key(vm, resolved));
                            tok = include_embedded_header(vm, tok, resolved,
                                                          embedded_src,
                                                          start->next->next);
                            break;
                        }
                    }
                }

                // Search include paths (for quoted includes) or system paths
                // (for angle bracket)
                char *path = search_include_paths(vm, filename, filename_len,
                                                  !is_dquote);

                // For quoted includes, if not found in include_paths, also try
                // system_include_paths This is needed for system headers that
                // use quoted includes for internal files
                bool found_in_sys = false;
                if (!path && is_dquote) {
                    path =
                        search_include_paths(vm, filename, filename_len, true);
                    found_in_sys = (path != NULL);
                }

                if (!path) {
                    // Nothing on disk (no -I hit, no CCCC ./include fallback —
                    // e.g. cccc running from a CWD that isn't its own repo, or
                    // a copied binary with no include/ alongside it). Fall back
                    // to the header text embedded in the binary itself
                    // (src/std.c), the same table tokenize_private_header()
                    // already uses for reflection.h et al. This is what makes
                    // standard headers resolve with zero configuration
                    // regardless of process CWD
                    // (#891).
                    char *embedded_src = try_embedded_std_header(vm, filename);
                    if (embedded_src) {
                        // #998: register this include's provenance BEFORE the
                        // splice below -- the same header may already have been
                        // seen once (e.g. routed @comptime earlier in this same
                        // TU), in which case include_embedded_header() early-
                        // returns on its #pragma once/include-guard check
                        // without reaching any registration of its own. The
                        // #include line is still replayed verbatim into
                        // -c=generated output regardless (emit_directives,
                        // above), so path_is_captured() (serialize_type.c)
                        // needs to know this key is supplied by it -- otherwise
                        // serialize_type_defs_for_owner() re-derives the type's
                        // definition on top of the replayed #include, a
                        // redefinition the host compiler rejects (#998).
                        //
                        // Deliberately NOT paired with a record_include_edge()
                        // call the way the two on-disk branches above are:
                        // under -c=native/-c=generated the downstream compiler
                        // opens the real system header, not CCCC's embedded
                        // copy, so a cc_file_is_cccc_only() closure computed
                        // over the embedded copy would answer a question about
                        // the wrong file -- it could only ever cause a false
                        // suppression of a legitimate #include.
                        if (ac_include_line)
                            hashmap_put(&vm->compiler.emit_include_paths,
                                        ac_include_line,
                                        embedded_header_key(vm, filename));
                        // #1003: a header whose CCCC copy is the only
                        // implementation likely to exist on a typical host
                        // (see is_cccc_supplied_only_header's comment above)
                        // must never be replayed as a raw #include the host
                        // compiler can't resolve -- reuse the #896/#999
                        // cccc-only machinery wholesale by marking this exact
                        // key (the same one just registered above) cccc-only,
                        // so serialize_program.c's #include-replay loop
                        // suppresses it and this header's own content is
                        // re-derived instead of relied upon.
                        if (is_cccc_supplied_only_header(filename))
                            mark_cccc_only_file(
                                vm, embedded_header_key(vm, filename));
                        // #1096: this whole branch is CCCC's own embedded
                        // header table (try_embedded_std_header, above) --
                        // unconditionally mark it bundled so a bodiless
                        // declaration sourced from it (e.g. bundled
                        // fcntl.h's own `#include "unistd.h"` declaring
                        // close()) isn't mistaken for "supplied by the
                        // user's own replayed #include" by the #901
                        // prototype-suppression gate in serialize_program.c.
                        mark_cccc_bundled_file(
                            vm, embedded_header_key(vm, filename));
                        tok = include_embedded_header(
                            vm, tok, filename, embedded_src, start->next->next);
                        break;
                    }
                }

                record_include_edge(vm, start->file ? start->file->name : NULL,
                                    path ? path : filename); // #896
                if (ac_include_line)                         // #896
                    hashmap_put(&vm->compiler.emit_include_paths,
                                ac_include_line, path ? path : filename);
                // #1003: same reasoning as the embedded branch above -- this is
                // the branch a polyfill header resolves through when found on
                // disk (e.g. under tools/tests.py's -I./include), which #1003's
                // own investigation found is not merely the embedded-table
                // case the ticket described.
                if (is_cccc_supplied_only_header(filename))
                    mark_cccc_only_file(vm, path ? path : filename);
                // #1096: an on-disk hit (e.g. `-I./include`) for a name
                // CCCC also bundles (get_std_header() != NULL, independent
                // of wants_builtin_header()'s "prefer host" policy) is
                // CCCC's own copy, not a real host header -- mark it the
                // same way the embedded branch above does, so the #901
                // prototype-suppression gate can tell the two apart
                // regardless of which of the two resolution paths a given
                // invocation took (this on-disk path is what the test
                // harness's standard -I./include invocation always uses).
                if (get_std_header(filename))
                    mark_cccc_bundled_file(vm, path ? path : filename);
                tok = include_file(vm, tok, path ? path : filename,
                                   start->next->next, filename,
                                   !is_dquote || found_in_sys);
                break;
            }
            case PP_INCLUDE_NEXT: {
                bool  ignore;
                int   filename_len;
                char *filename = read_include_filename(vm, &tok, tok->next,
                                                       &ignore, &filename_len);
                tok            = skip_line(vm, tok);
                char *path     = search_include_next(vm, filename);
                record_include_edge(vm, start->file ? start->file->name : NULL,
                                    path ? path : filename); // #896
                tok = include_file(vm, tok, path ? path : filename,
                                   start->next->next, filename, !ignore);
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
                Macro *ifdef_m = find_macro(vm, tok->next);
                if (ifdef_m)
                    ifdef_m->use_count++;
                push_cond_incl(vm, tok, ifdef_m != NULL);
                tok = skip_line(vm, tok->next->next);
                if (!ifdef_m)
                    tok = skip_cond_incl(vm, tok);
                break;
            }
            case PP_IFNDEF: {
                Macro *ifndef_m = find_macro(vm, tok->next);
                if (ifndef_m)
                    ifndef_m->use_count++;
                push_cond_incl(vm, tok, ifndef_m == NULL);
                tok = skip_line(vm, tok->next->next);
                if (ifndef_m)
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
                Macro *elifdef_m            = find_macro(vm, tok->next);
                if (elifdef_m)
                    elifdef_m->use_count++;
                tok = skip_line(vm, tok->next->next);
                if (!vm->compiler.cond_incl->included && elifdef_m)
                    vm->compiler.cond_incl->included = true;
                else
                    tok = skip_cond_incl(vm, tok);
                break;
            }
            case PP_ELIFNDEF: {
                if (!vm->compiler.cond_incl)
                    error_tok(vm, start,
                              "stray #elifndef without matching #if");
                if (vm->compiler.cond_incl->ctx == IN_ELSE)
                    error_tok(vm, start, "stray #elifndef after #else");
                vm->compiler.cond_incl->ctx = IN_ELIF;
                Macro *elifndef_m           = find_macro(vm, tok->next);
                if (elifndef_m)
                    elifndef_m->use_count++;
                tok = skip_line(vm, tok->next->next);
                if (!vm->compiler.cond_incl->included && !elifndef_m)
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
                tok                         = skip_line(vm, tok->next);
                if (vm->compiler.cond_incl->included)
                    tok = skip_cond_incl(vm, tok);
                break;
            case PP_ENDIF:
                if (!vm->compiler.cond_incl)
                    error_tok(vm, start, "stray #endif without matching #if");
                vm->compiler.cond_incl = vm->compiler.cond_incl->next;
                tok                    = skip_line(vm, tok->next);
                break;
            case PP_LINE:
                read_line_marker(vm, &tok, tok->next);
                break;
            case PP_PRAGMA:
                tok = handle_pragma_body(vm, tok->next);
                break;
            case PP_EMBED:
                if (vm->compiler.c_std < CCCC_STD_C23)
                    error_tok(vm, tok, "'#embed' is not available before C23");
                tok = handle_embed_directive(vm, tok->next, start, false);
                break;
            case PP_ERROR: {
                Token *msg_end;
                Token *msg  = copy_line(vm, &msg_end, tok->next);
                char  *text = join_tokens(vm, msg, NULL, NULL);
                if (text && text[0])
                    error_tok(vm, tok, "%s", text);
                else
                    error_tok(vm, tok, "#error directive");
                break;
            }
            case PP_WARNING: {
                Token *msg_end;
                Token *msg  = copy_line(vm, &msg_end, tok->next);
                char  *text = join_tokens(vm, msg, NULL, NULL);
                if (text && text[0])
                    warn_tok(vm, tok, CCCC_WARN_CPP, "%s", text);
                else
                    warn_tok(vm, tok, CCCC_WARN_CPP, "#warning directive");
                tok = msg_end;
                break;
            }
            default:
                // `#`-only line is legal (null directive).
                if (tok->at_bol)
                    continue;
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
// -std= flag (long form: --std=) is parsed) and always produces the complete
// correct state.
void define_std_macros(VirtualMachine *vm) {
    const char *v;
    switch (vm->compiler.c_std) {
        case CCCC_STD_C89:
            undef_macro(vm, "__STDC_VERSION__");
            return;
        case CCCC_STD_C99:
            v = "199901L";
            break;
        case CCCC_STD_C11:
            v = "201112L";
            break;
        case CCCC_STD_C23:
            v = "202311L";
            break;
        case CCCC_STD_C17:
        default:
            v = "201710L";
            break;
    }
    define_macro(vm, "__STDC_VERSION__", (char *)v);
}

void define_macro(VirtualMachine *vm, char *name, char *buf) {
    Token *tok = tokenize(vm, new_file(vm, "<built-in>", 1, buf));
    add_macro(vm, name, strlen(name), true, tok, NULL);
}

void undef_macro(VirtualMachine *vm, char *name) {
    if (vm->compiler.warnings & CCCC_WARN_UNUSED_MACROS) {
        Macro *m = hashmap_get2(&vm->compiler.macros, name, strlen(name));
        if (m && !m->handler && m->use_count == 0 && m->define_tok &&
            m->define_tok->file && !m->define_tok->file->is_system_header &&
            !hashmap_get(&vm->compiler.guard_macros, m->name))
            warn_tok(vm, m->define_tok, CCCC_WARN_UNUSED_MACROS,
                     "macro '%s' defined but not used", m->name);
    }
    hashmap_delete(&vm->compiler.macros, name);
}

static Macro *add_builtin(VirtualMachine *vm, char *name,
                          macro_handler_fn *fn) {
    Macro *m   = add_macro(vm, name, strlen(name), true, NULL, NULL);
    m->handler = fn;
    return m;
}

static Token *file_macro(VirtualMachine *vm, Token *tmpl) {
    while (tmpl->origin)
        tmpl = tmpl->origin;
    return new_str_token(vm, tmpl->file->display_name, tmpl);
}

static Token *line_macro(VirtualMachine *vm, Token *tmpl) {
    while (tmpl->origin)
        tmpl = tmpl->origin;
    int i = tmpl->line_no + tmpl->file->line_delta;
    return new_num_token(vm, i, tmpl);
}

// __COUNTER__ is expanded to serial values starting from 0.
static Token *counter_macro(VirtualMachine *vm, Token *tmpl) {
    return new_num_token(vm, vm->compiler.counter_macro_value++, tmpl);
}

// __TIMESTAMP__ is expanded to a string describing the last
// modification time of the current file. E.g.
// "Fri Jul 24 01:32:50 2020"
static Token *timestamp_macro(VirtualMachine *vm, Token *tmpl) {
    struct stat st;
    if (stat(tmpl->file->name, &st) != 0)
        return new_str_token(vm, "??? ??? ?? ??:??:?? ????", tmpl);

    char buf[30];
    ctime_r(&st.st_mtime, buf);
    buf[24] = '\0';
    return new_str_token(vm, buf, tmpl);
}

// __DATE__ is expanded to the current date, e.g. "May 17 2020".
static char *format_date(VirtualMachine *vm, struct tm *tm) {
    static char mon[][4] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec",
    };

    return arena_format(vm, "\"%s %2d %d\"", mon[tm->tm_mon], tm->tm_mday,
                        tm->tm_year + 1900);
}

// __TIME__ is expanded to the current time, e.g. "13:34:03".
static char *format_time(VirtualMachine *vm, struct tm *tm) {
    return arena_format(vm, "\"%02d:%02d:%02d\"", tm->tm_hour, tm->tm_min,
                        tm->tm_sec);
}

// Inject the real host <fenv.h> constants as predefined macros so
// include/fenv.h (which guest programs see) can define FE_* in terms of
// these instead of hardcoding one platform's bit values. src/stdlib/fenv.c's
// wrap_fe*() functions pass FE_* straight through to the real host libc, so
// the guest header's constants MUST match whatever <fenv.h> this binary was
// actually compiled against on this machine -- they cannot be baked into
// src/std.c at stdlib-regen time (`make bootstrap`) on one dev's machine
// and expected to be correct on another platform (#771).
static void init_fenv_macros(VirtualMachine *vm) {
    define_macro(vm, "__CCCC_FE_INVALID__", arena_format(vm, "%d", FE_INVALID));
    define_macro(vm, "__CCCC_FE_DIVBYZERO__",
                 arena_format(vm, "%d", FE_DIVBYZERO));
    define_macro(vm, "__CCCC_FE_OVERFLOW__",
                 arena_format(vm, "%d", FE_OVERFLOW));
    define_macro(vm, "__CCCC_FE_UNDERFLOW__",
                 arena_format(vm, "%d", FE_UNDERFLOW));
    define_macro(vm, "__CCCC_FE_INEXACT__", arena_format(vm, "%d", FE_INEXACT));
    // Host FE_ALL_EXCEPT verbatim -- NOT the OR of the five named exceptions
    // above, since some platforms (x86) include additional bits (e.g.
    // FE_DENORMAL) that have no portable C name.
    define_macro(vm, "__CCCC_FE_ALL_EXCEPT__",
                 arena_format(vm, "%d", FE_ALL_EXCEPT));
    define_macro(vm, "__CCCC_FE_TONEAREST__",
                 arena_format(vm, "%d", FE_TONEAREST));
    define_macro(vm, "__CCCC_FE_DOWNWARD__",
                 arena_format(vm, "%d", FE_DOWNWARD));
    define_macro(vm, "__CCCC_FE_UPWARD__", arena_format(vm, "%d", FE_UPWARD));
    define_macro(vm, "__CCCC_FE_TOWARDZERO__",
                 arena_format(vm, "%d", FE_TOWARDZERO));
    // Sizes drive fexcept_t/fenv_t's guest typedefs -- fexcept_t is 2 bytes
    // on macOS/arm64 but the guest header previously hardcoded `unsigned
    // int` (4 bytes), leaving the top half of fegetexceptflag()'s output
    // object uninitialized.
    define_macro(vm, "__CCCC_SIZEOF_FEXCEPT_T__",
                 arena_format(vm, "%d", (int)sizeof(fexcept_t)));
    define_macro(vm, "__CCCC_SIZEOF_FENV_T__",
                 arena_format(vm, "%d", (int)sizeof(fenv_t)));
}

// Inject the real host <errno.h> values for the ~44 POSIX error codes that
// differ across platforms, so include/errno.h (which guest programs see)
// can define these in terms of __CCCC_E*__ instead of a hand-maintained
// #ifdef __APPLE__ / #else table. #779 was exactly this failure mode:
// EDEADLK/EAGAIN were hardcoded at one platform's values outside any
// conditional, so the guest macro didn't match what the host libc actually
// returned on the other platform. Deriving every value straight from this
// binary's own compile-time <errno.h> (same reasoning as init_fenv_macros
// above, #813) makes that class of bug structurally impossible: whichever
// platform builds cccc is the platform whose real errno numbers get baked
// in, with nothing to transcribe or keep in sync by hand.
static void init_errno_macros(VirtualMachine *vm) {
#define E(name)                                                                \
    define_macro(vm, "__CCCC_" #name "__", arena_format(vm, "%d", name))
    E(EAGAIN);
    E(EDEADLK);
    E(EWOULDBLOCK);
    E(EINPROGRESS);
    E(EALREADY);
    E(ENOTSOCK);
    E(EDESTADDRREQ);
    E(EMSGSIZE);
    E(EPROTOTYPE);
    E(ENOPROTOOPT);
    E(ENOTSUP);
    E(EAFNOSUPPORT);
    E(EADDRINUSE);
    E(EADDRNOTAVAIL);
    E(ENETDOWN);
    E(ENETUNREACH);
    E(ECONNABORTED);
    E(ECONNRESET);
    E(ENOBUFS);
    E(EISCONN);
    E(ENOTCONN);
    E(ETIMEDOUT);
    E(ECONNREFUSED);
    E(ELOOP);
    E(ENAMETOOLONG);
    E(EHOSTUNREACH);
    E(ENOTEMPTY);
    E(ENOSYS);
    E(ECANCELED);
    E(EIDRM);
    E(ENOMSG);
    E(EOVERFLOW);
    E(EBADMSG);
    E(EMULTIHOP);
    E(EILSEQ);
    E(ENOLINK);
    E(EPROTO);
    E(ENOLCK);
    E(EOPNOTSUPP);
    E(ENOTRECOVERABLE);
    E(EOWNERDEAD);
    E(ESTALE);
    E(EDQUOT);
    E(ETXTBSY);
    E(ENOTBLK);
#undef E
}

// Inject the real host <dlfcn.h> RTLD_* values, so include/dlfcn.h (which
// guest programs see) can define these in terms of __CCCC_RTLD_*__ instead
// of a hand-maintained #ifdef __APPLE__ / #else table. #1152 was exactly
// this failure mode: RTLD_LOCAL/RTLD_GLOBAL were hardcoded at glibc's values
// unconditionally, so on macOS a guest asking for RTLD_GLOBAL (glibc 0x100)
// actually passed RTLD_FIRST (Darwin's own 0x100) to the real host
// dlopen() -- a different flag entirely, not just a different number for
// the same flag. Same reasoning as init_errno_macros/init_fenv_macros
// above (#779/#813/#771): deriving every value from this binary's own
// compile-time <dlfcn.h> makes hand-transcription errors structurally
// impossible.
//
// Flags with no equivalent on the host libdl (RTLD_FIRST on Linux,
// RTLD_DEEPBIND/RTLD_BINDING_MASK on macOS) are simply never injected, so
// the guest macro stays undefined and using it is a compile error -- the
// #824 no-lossy-emulation policy, not a silently wrong integer.
//
// Deliberately NOT injected: the dlsym() pseudo-handles (RTLD_NEXT,
// RTLD_DEFAULT, RTLD_SELF, RTLD_MAIN_ONLY). cccc_rt_dlsym (src/vm.c)
// resolves its first argument through the VM's own dynamic-library
// registry token table, not a raw host handle, so a pseudo-handle would
// fail there ("invalid dynamic library handle") while working natively --
// a new VM/native divergence, exactly what #1105 removed for dlclose.
static void init_dlfcn_macros(VirtualMachine *vm) {
#if !defined(_WIN32) && !defined(_WIN64)
#define D(name)                                                                \
    define_macro(vm, "__CCCC_" #name "__", arena_format(vm, "%d", name))
    D(RTLD_LAZY);
    D(RTLD_NOW);
    D(RTLD_LOCAL);
    D(RTLD_GLOBAL);
#ifdef RTLD_NOLOAD
    D(RTLD_NOLOAD);
#endif
#ifdef RTLD_NODELETE
    D(RTLD_NODELETE);
#endif
#ifdef RTLD_FIRST        /* macOS only */
    D(RTLD_FIRST);
#endif
#ifdef RTLD_DEEPBIND     /* glibc only */
    D(RTLD_DEEPBIND);
#endif
#ifdef RTLD_BINDING_MASK /* glibc only */
    D(RTLD_BINDING_MASK);
#endif
#undef D
#endif
}

void init_macros(VirtualMachine *vm) {
    // Define predefined macros
    init_fenv_macros(vm);
    init_errno_macros(vm);
    init_dlfcn_macros(vm);
    define_macro(vm, "__C99_MACRO_WITH_VA_ARGS", "1");
    define_macro(vm, "__SIZEOF_DOUBLE__", "8");
    define_macro(vm, "__SIZEOF_FLOAT__", "4");
    define_macro(vm, "__SIZEOF_INT__", "4");
    define_macro(vm, "__SIZEOF_INT128__", "16");
    // #1174: this used to be hardcoded "8" unconditionally -- already
    // inconsistent with sizeof(long double) (then hardcoded 16 in
    // src/type.c's ty_ldouble) on every platform before that fix, found
    // incidentally while fixing it. Now mirrors ty_ldouble's own platform
    // split so a guest `#if __SIZEOF_LONG_DOUBLE__ == N` agrees with the
    // real `sizeof(long double)` it's predicting.
#if defined(__APPLE__) && defined(__aarch64__)
    define_macro(vm, "__SIZEOF_LONG_DOUBLE__", "8");
#else
    define_macro(vm, "__SIZEOF_LONG_DOUBLE__", "16");
#endif
    define_macro(vm, "__SIZEOF_LONG_LONG__", "8");
    define_macro(vm, "__SIZEOF_LONG__", "8");
    define_macro(vm, "__SIZEOF_POINTER__", "8");
    define_macro(vm, "__SIZEOF_PTRDIFF_T__", "8");
    define_macro(vm, "__SIZEOF_SHORT__", "2");
    define_macro(vm, "__SIZEOF_SIZE_T__", "8");
    define_macro(vm, "__SIZE_TYPE__", "unsigned long");
    define_macro(vm, "__STDC_HOSTED__", "1");
    define_macro(vm, "__STDC_NO_COMPLEX__", "1");
#ifdef CCCC_HAS_DECIMAL
    // #402: real IEEE-754-2008 decimal FP via Intel BID. This is the
    // predefine a guest program checks to tell the two build configurations
    // apart -- decimal literals/arithmetic are a compile error without it.
    // __STDC_DEC_FP__ is the older TS 18661-2 spelling some code still checks.
    define_macro(vm, "__STDC_IEC_60559_DFP__", "202311L");
    define_macro(vm, "__STDC_DEC_FP__", "202311L");
#endif
    define_macro(vm, "__STDC_UTF_16__", "1");
    define_macro(vm, "__STDC_UTF_32__", "1");
    define_std_macros(vm);
    define_macro(vm, "__STDC__", "1");
    define_macro(vm, "__USER_LABEL_PREFIX__", "");
    define_macro(vm, "__alignof__", "_Alignof");
    define_macro(vm, "__const__", "const");
    define_macro(vm, "__inline", "inline");
    define_macro(vm, "__inline__", "inline");
    define_macro(vm, "__signed__", "signed");
    define_macro(vm, "__typeof__", "typeof");
    define_macro(vm, "__volatile__", "volatile");
    define_macro(vm, "__CCCC__", "1");

    // Expose whether stack canaries are enabled so stdarg.h can correct the
    // va_start register-slot count for the reserved canary slot (#445).
    define_macro(vm, "__CCCC_STACK_CANARIES__",
                 (vm->flags & CCCC_STACK_CANARIES) ? "1" : "0");

    // Expose whether --posix-emulation is set so headers can guard the
    // declarations of POSIX functions that only exist on this host via a
    // lossy/approximate emulation (e.g. ppoll()/sched_setscheduler() family
    // on macOS) -- see poll.h and sched.h. Without the flag those functions
    // are simply undeclared on hosts lacking the real primitive, matching
    // what a native compiler on the same host would do (#824).
    if (vm->flags & CCCC_POSIX_EMULATION)
        define_macro(vm, "__CCCC_POSIX_EMULATION__", "1");

    // Republish the host-side CCCC_HAS_NDBM build knob into the guest macro
    // namespace so include/ndbm.h can guard itself on Linux (#871).
#ifdef CCCC_HAS_NDBM
    define_macro(vm, "__CCCC_HAS_NDBM__", "1");
#endif

    // Same republishing for the libcurl knob: lets guest code (and tests)
    // tell URL #include/#embed-capable builds apart.
#ifdef CCCC_HAS_CURL
    define_macro(vm, "__CCCC_HAS_CURL__", "1");
#endif

    define_macro(vm, "__has_include(x)", "0");
    define_macro(vm, "__has_feature(x)", "0");
    define_macro(vm, "__has_extension(x)", "0");
    define_macro(vm, "__has_attribute(x)", "0");
    define_macro(vm, "__has_builtin(x)", "0");
    define_macro(vm, "__has_c_attribute(x)", "0");
    define_macro(vm, "__has_cpp_attribute(x)", "0");

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

    // Strip __attribute__ specifications from system headers since CCCC parser
    // doesn't handle all attribute positions. Attributes are used for
    // optimization hints and documentation, not required for correct
    // compilation.
    define_macro(vm, "__attribute__(x)", "");

    // GCC extension keyword — expands to nothing; silences pedantic warnings
    // in GCC-targeting headers that use __extension__ to suppress diagnostics.
    define_macro(vm, "__extension__", "");

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

    // POSIX feature-test macros (#732). CCCC's POSIX headers are always-on
    // and ungated — these are predefined so third-party code that
    // feature-tests before including them (e.g. `#if !defined(_XOPEN_SOURCE)`)
    // sees the full POSIX.1-2008 / X/Open 7 surface CCCC actually exposes,
    // rather than always seeing nothing. define_macro()/add_macro() silently
    // overwrite on redefinition (no "redefined" diagnostic), so a user
    // `-D`/`#define` of any of these before/instead of this point always
    // wins — this is a default, not an override lock. Not defined on
    // Windows, where the POSIX headers themselves are unavailable.
#ifndef _WIN32
    define_macro(vm, "_POSIX_C_SOURCE", "200809L");
    define_macro(vm, "_POSIX_SOURCE", "1");
    define_macro(vm, "_XOPEN_SOURCE", "700");
    define_macro(vm, "_DEFAULT_SOURCE", "1");
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

    time_t     now = time(NULL);
    struct tm *tm  = localtime(&now);
    define_macro(vm, "__DATE__", format_date(vm, tm));
    define_macro(vm, "__TIME__", format_time(vm, tm));
}

// Called after vm->compiler.build_mode / testing_mode are set in main.
// Defines __CCCC_BUILD_MODE__, __CCCC_TEST_MODE__, or __CCCC_COMP_MODE__
// (only the active one) so user code can #ifdef on the current mode.
void init_mode_macros(VirtualMachine *vm) {
    if (vm->compiler.build_mode)
        define_macro(vm, "__CCCC_BUILD_MODE__", "1");
    if (vm->compiler.testing_mode)
        define_macro(vm, "__CCCC_TEST_MODE__", "1");
    if (!vm->compiler.build_mode && !vm->compiler.testing_mode)
        define_macro(vm, "__CCCC_COMP_MODE__", "1");
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
static void join_adjacent_string_literals(VirtualMachine *vm, Token *tok) {
    // First pass: If regular string literals are adjacent to wide
    // string literals, regular string literals are converted to a wide
    // type before concatenation. In this pass, we do the conversion.
    for (Token *tok1 = tok; tok1->kind != TK_EOF;) {
        if (tok1->kind != TK_STR || tok1->next->kind != TK_STR) {
            tok1 = tok1->next;
            continue;
        }

        StringKind kind   = getStringKind(tok1);
        Type      *basety = tok1->ty->base;

        for (Token *t = tok1->next; t->kind == TK_STR; t = t->next) {
            StringKind k = getStringKind(t);
            if (kind == STR_NONE) {
                kind   = k;
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

        *tok1      = *copy_token(vm, tok1);
        tok1->ty   = array_of(vm, tok1->ty->base, len);
        tok1->str  = buf;
        tok1->next = tok2;
        tok1       = tok2;
    }
}

// hashmap_foreach callback used by isolate_comptime_macros.
static int isolate_comptime_macro_iter(char *key, int keylen, void *val,
                                       void *user_data) {
    (void)key;
    (void)keylen;
    HashMap *macros = (HashMap *)user_data;
    Macro   *m      = (Macro *)val;
    // Keep only command-line and builtin macros (define_tok == NULL), plus
    // per-macro #define @shared opt-ins (#888).
    // All other source-file #defines — whether from the primary file or any
    // included header — are removed so they are not visible during the
    // comptime preprocessing pass. @shared / @comptime header macros are
    // removed here but re-added when their queued re-includes are
    // re-processed by the comptime pass. (tickets #552, #627)
    if (m->define_tok && !m->is_shared)
        hashmap_delete2(macros, key, keylen);
    return 0; // continue
}

// hashmap_foreach callback used by isolate_comptime_macros to re-apply a -D
// that a same-named source #define shadowed in the live macro table before
// the strip pass above ran. (#888)
static int reapply_cli_define_iter(char *key, int keylen, void *val,
                                   void *user_data) {
    HashMap *macros = (HashMap *)user_data;
    if (!hashmap_get2(macros, key, keylen))
        hashmap_put2(macros, key, keylen, val);
    return 0; // continue
}

// Strip all source-file #define macros from vm->compiler.macros so that the
// comptime preprocessing pass starts with an isolated macro state containing
// only CCCC builtins, command-line -D defines (define_tok == NULL), and
// #define @shared opt-ins (m->is_shared, #888).
// Primary-file user #defines are NOT forwarded; users opt macros into the
// comptime context via @shared includes/defines. @shared / @comptime header
// macros are removed here but re-added when their queued re-includes are
// re-preprocessed.
// Safe to call while compile_macro_program holds a snapshot: hashmap_delete2
// writes a TOMBSTONE and hashmap_foreach iterates by bucket index, matching the
// pattern in undefine_guard_macro_iter. (ticket #627)
void isolate_comptime_macros(VirtualMachine *vm) {
    hashmap_foreach(&vm->compiler.macros, isolate_comptime_macro_iter,
                    &vm->compiler.macros);
    // #888: a same-named source #define may have overwritten a -D value in
    // vm->compiler.macros before the strip pass above ran (the hashmap holds
    // one entry per name, and the source #define wins the last write). That
    // shadowed -D never gets deleted by the loop above -- it's simply gone,
    // taking the -D value down with it. Re-apply anything from the
    // pre-preprocessing CLI snapshot that isn't present now.
    if (vm->compiler.has_cli_macro_snapshot)
        hashmap_foreach(&vm->compiler.cli_macro_snapshot,
                        reapply_cli_define_iter, &vm->compiler.macros);
}

// Entry point function of the preprocessor.
static int warn_unused_macro_cb(char *key, int keylen, void *val,
                                void *user_data) {
    (void)key;
    (void)keylen;
    VirtualMachine *vm = (VirtualMachine *)user_data;
    Macro          *m  = (Macro *)val;
    if (!m->handler && m->use_count == 0 && m->define_tok &&
        m->define_tok->file && !m->define_tok->file->is_system_header &&
        !hashmap_get(&vm->compiler.guard_macros, m->name))
        warn_tok(vm, m->define_tok, CCCC_WARN_UNUSED_MACROS,
                 "macro '%s' defined but not used", m->name);
    return 0;
}

Token *preprocess(VirtualMachine *vm, Token *tok) {
    tok = preprocess2(vm, tok);
    // Bare comptime entries (no begin/end, runs to EOF) are silently closed.
    // begin/end entries that reach EOF without an explicit close are errors.
    while (vm->compiler.ctx_stack_len > 0 && !ctx_top(vm)->needs_end)
        ctx_pop(vm);
    if (vm->compiler.ctx_stack_len > 0) {
        ComptimeCtxEntry *top = ctx_top(vm);
        error_tok(vm, top->open_tok,
                  top->type == CTX_EMIT
                      ? "unclosed #pragma cccc emit begin"
                      : "unclosed #pragma cccc comptime begin");
    }
    if (vm->compiler.suite_stack_len > 0)
        error_tok(vm, vm->compiler.suite_len_stack[0].open_tok,
                  "unclosed #pragma cccc suite begin");
    if (vm->compiler.cond_incl) {
        Token      *ci_tok = vm->compiler.cond_incl->tok;
        const char *hint   = "";
        if (ci_tok->file && ci_tok->file->name &&
            strstr(ci_tok->file->name, "implicit-reflection.h"))
            hint = "\n  hint: embedded reflection.h may be truncated — run "
                   "`make bootstrap` to regenerate src/std.c";
        error_tok(vm, ci_tok,
                  "unterminated conditional directive (started with #%.*s)%s",
                  ci_tok->len, ci_tok->loc, hint);
    }
    if ((vm->compiler.warnings & CCCC_WARN_UNUSED_MACROS) &&
        !vm->compiler.in_macro_mode)
        hashmap_foreach(&vm->compiler.macros, warn_unused_macro_cb, vm);
    convert_pp_tokens(vm, tok);
    join_adjacent_string_literals(vm, tok);

    for (Token *t = tok; t; t = t->next)
        t->line_no += t->line_delta;
    return tok;
}

// ---------------------------------------------------------------------------
// cc_parse_test_flags — parse a whitespace-separated CLI-flag string from
// [[cccc::test(flags = "...")]] into a CcTestFlagsDelta for per-test
// recompilation.  Supports:
//   safety presets (-0/-1/-2/-3, --safety=N)
//   optimisation levels (-O/-On/--optimize=N)
//   individual CCCCFlags check flags (--bounds-checks, -b, etc.)
//   warning flags (-W*, -Wno-*, -Werror, -Werror=*, -Wno-error=*)   [#612]
//   optimisation-pass flags (-f<pass>, -fno-<pass>)                  [#612]
//
// Unknown or malformed flags are reported via error_tok() at src_tok's
// source location and terminate compilation.
// ---------------------------------------------------------------------------
void cc_parse_test_flags(VirtualMachine *vm, Token *src_tok,
                         const char *flags_str, const char *test_name,
                         CcTestFlagsDelta *out) {
    memset(out, 0, sizeof(*out));

    if (!flags_str || !*flags_str)
        return;

    char *buf = strdup(flags_str);
    char *p   = buf;

    while (*p) {
        // Skip leading whitespace
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r')
            p++;
        if (!*p)
            break;

        // Delimit this token
        char *tok = p;
        while (*p && *p != ' ' && *p != '\t' && *p != '\n' && *p != '\r')
            p++;
        if (*p)
            *p++ = '\0';

        // --- safety presets ---
        if (strcmp(tok, "-0") == 0 ||
            (strncmp(tok, "--safety=", 9) == 0 &&
             (strcmp(tok + 9, "none") == 0 || strcmp(tok + 9, "0") == 0))) {
            out->or_bits   = out->or_bits & ~(uint32_t)CCCC_SAFETY_PRESET_BITS;
            out->set_mask |= (uint32_t)CCCC_SAFETY_PRESET_BITS;
        } else if (strcmp(tok, "-1") == 0 ||
                   (strncmp(tok, "--safety=", 9) == 0 &&
                    (strcmp(tok + 9, "basic") == 0 ||
                     strcmp(tok + 9, "1") == 0))) {
            out->or_bits = (out->or_bits & ~(uint32_t)CCCC_SAFETY_PRESET_BITS) |
                           (uint32_t)CCCC_SAFETY_BASIC;
            out->set_mask |= (uint32_t)CCCC_SAFETY_PRESET_BITS;
        } else if (strcmp(tok, "-2") == 0 ||
                   (strncmp(tok, "--safety=", 9) == 0 &&
                    (strcmp(tok + 9, "standard") == 0 ||
                     strcmp(tok + 9, "2") == 0))) {
            out->or_bits = (out->or_bits & ~(uint32_t)CCCC_SAFETY_PRESET_BITS) |
                           (uint32_t)CCCC_SAFETY_STANDARD;
            out->set_mask |= (uint32_t)CCCC_SAFETY_PRESET_BITS;
        } else if (strcmp(tok, "-3") == 0 ||
                   (strncmp(tok, "--safety=", 9) == 0 &&
                    (strcmp(tok + 9, "max") == 0 ||
                     strcmp(tok + 9, "3") == 0))) {
            out->or_bits = (out->or_bits & ~(uint32_t)CCCC_SAFETY_PRESET_BITS) |
                           (uint32_t)CCCC_SAFETY_MAX;
            out->set_mask |= (uint32_t)CCCC_SAFETY_PRESET_BITS;

            // --- individual check flags (matching long_options names in
            // main.c) ---
#define SET_FLAG(bit)                                                          \
    do {                                                                       \
        out->or_bits  |= (uint32_t)(bit);                                      \
        out->set_mask |= (uint32_t)(bit);                                      \
    } while (0)
        } else if (strcmp(tok, "-b") == 0 ||
                   strcmp(tok, "--bounds-checks") == 0) {
            SET_FLAG(CCCC_BOUNDS_CHECKS);
        } else if (strcmp(tok, "-u") == 0 ||
                   strcmp(tok, "--uaf-detection") == 0) {
            SET_FLAG(CCCC_UAF_DETECTION);
        } else if (strcmp(tok, "-T") == 0 ||
                   strcmp(tok, "--type-checks") == 0) {
            SET_FLAG(CCCC_TYPE_CHECKS);
        } else if (strcmp(tok, "--overflow-checks") == 0) {
            SET_FLAG(CCCC_OVERFLOW_CHECKS);
        } else if (strcmp(tok, "--uninitialized-detection") == 0) {
            SET_FLAG(CCCC_UNINIT_DETECTION);
        } else if (strcmp(tok, "--stack-canaries") == 0) {
            SET_FLAG(CCCC_STACK_CANARIES);
        } else if (strcmp(tok, "-H") == 0 ||
                   strcmp(tok, "--heap-canaries") == 0) {
            SET_FLAG(CCCC_HEAP_CANARIES);
        } else if (strcmp(tok, "-p") == 0 ||
                   strcmp(tok, "--pointer-sanitizer") == 0) {
            SET_FLAG(CCCC_POINTER_SANITIZER);
        } else if (strcmp(tok, "-m") == 0 ||
                   strcmp(tok, "--memory-leak-detection") == 0) {
            SET_FLAG(CCCC_MEMORY_LEAK_DETECT);
        } else if (strcmp(tok, "--stack-instrumentation") == 0) {
            SET_FLAG(CCCC_STACK_INSTR);
        } else if (strcmp(tok, "--stack-errors") == 0) {
            SET_FLAG(CCCC_STACK_INSTR_ERRORS);
        } else if (strcmp(tok, "--dangling-pointers") == 0) {
            SET_FLAG(CCCC_DANGLING_DETECT);
        } else if (strcmp(tok, "--alignment-checks") == 0) {
            SET_FLAG(CCCC_ALIGNMENT_CHECKS);
        } else if (strcmp(tok, "--provenance-tracking") == 0) {
            SET_FLAG(CCCC_PROVENANCE_TRACK);
        } else if (strcmp(tok, "--invalid-arithmetic") == 0) {
            SET_FLAG(CCCC_INVALID_ARITH);
        } else if (strcmp(tok, "--format-string-checks") == 0) {
            SET_FLAG(CCCC_FORMAT_STR_CHECKS);
        } else if (strcmp(tok, "-R") == 0 ||
                   strcmp(tok, "--random-canaries") == 0) {
            SET_FLAG(CCCC_RANDOM_CANARIES);
        } else if (strcmp(tok, "--memory-poisoning") == 0) {
            SET_FLAG(CCCC_MEMORY_POISONING);
        } else if (strcmp(tok, "--memory-tagging") == 0) {
            SET_FLAG(CCCC_MEMORY_TAGGING);
        } else if (strcmp(tok, "--thread-safety") == 0) {
            SET_FLAG(CCCC_THREAD_SAFETY);
        } else if (strcmp(tok, "-V") == 0 ||
                   strcmp(tok, "--require-vm-heap") == 0) {
            // Test-dialect only: -V/--require-vm-heap means "this test
            // requires the VM heap" (force it on). Deliberately NOT the CLI's
            // --no-vm-heap, which means the opposite (disable the heap) --
            // accepting it here would silently force the heap on for a test
            // that asked to disable it.
            SET_FLAG(CCCC_VM_HEAP);
        } else if (strcmp(tok, "-C") == 0 ||
                   strcmp(tok, "--control-flow-integrity") == 0) {
            SET_FLAG(CCCC_CFI);
        } else if (strcmp(tok, "-g") == 0 || strcmp(tok, "--debug") == 0) {
            SET_FLAG(CCCC_ENABLE_DEBUGGER);
        } else if (strcmp(tok, "--ffi-errors-fatal") == 0) {
            SET_FLAG(CCCC_FFI_ERRORS_FATAL);
        } else if (strcmp(tok, "--trap-fp-divzero") == 0) {
            SET_FLAG(CCCC_TRAP_FP_DIVZERO);
        } else if (strncmp(tok, "--ffi-allow=", 12) == 0) {
            const char *name = tok + 12;
            if (*name) {
                out->ffi_allow = realloc(
                    out->ffi_allow, sizeof(*out->ffi_allow) *
                                        (size_t)(out->ffi_allow_count + 1));
                if (!out->ffi_allow)
                    error("cc_parse_test_flags: realloc failed");
                out->ffi_allow[out->ffi_allow_count++] = strdup(name);
            }
#undef SET_FLAG

            // --- warning flags (#612): -W*, -Wno-*, -Werror, -Werror=*,
            // -Wno-error=* ---
        } else if (strncmp(tok, "-W", 2) == 0) {
            const char *arg = tok + 2; // everything after "-W"
            if (strcmp(arg, "error") == 0) {
                out->warn_as_errors     = true;
                out->warn_as_errors_set = true;
            } else if (strncmp(arg, "error=", 6) == 0) {
                const char *name = arg + 6;
                uint64_t    mask = cccc_warning_mask_for_name(name);
                if (!mask || cccc_warning_is_group_name(name)) {
                    char bad[256];
                    snprintf(bad, sizeof(bad), "%s", tok);
                    free(buf);
                    error_tok(vm, src_tok,
                              "[[cccc::test]] flags=\"...\": unknown warning"
                              " option '%s' in test '%s'",
                              bad, test_name ? test_name : "?");
                }
                out->warn_or            |= mask;
                out->warn_mask          |= mask;
                out->warn_errors_or     |= mask;
                out->warn_errors_mask   |= mask;
                out->warn_as_errors_set  = true;
            } else if (strncmp(arg, "no-error=", 9) == 0) {
                const char *name = arg + 9;
                uint64_t    mask = cccc_warning_mask_for_name(name);
                if (!mask || cccc_warning_is_group_name(name)) {
                    char bad[256];
                    snprintf(bad, sizeof(bad), "%s", tok);
                    free(buf);
                    error_tok(vm, src_tok,
                              "[[cccc::test]] flags=\"...\": unknown warning"
                              " option '%s' in test '%s'",
                              bad, test_name ? test_name : "?");
                }
                out->warn_errors_or     &= ~mask;
                out->warn_errors_mask   |= mask;
                out->warn_as_errors_set  = true;
            } else {
                bool        disable = (strncmp(arg, "no-", 3) == 0);
                const char *name    = disable ? arg + 3 : arg;
                uint64_t    mask    = cccc_warning_mask_for_name(name);
                if (!mask) {
                    char bad[256];
                    snprintf(bad, sizeof(bad), "%s", tok);
                    free(buf);
                    error_tok(vm, src_tok,
                              "[[cccc::test]] flags=\"...\": unknown warning"
                              " option '%s' in test '%s'",
                              bad, test_name ? test_name : "?");
                }
                if (disable) {
                    out->warn_or   &= ~mask;
                    out->warn_mask |= mask;
                } else {
                    out->warn_or   |= mask;
                    out->warn_mask |= mask;
                }
            }

        } else {
            // Copy tok before freeing buf (tok points into buf).
            char bad[256];
            snprintf(bad, sizeof(bad), "%s", tok);
            free(buf);
            error_tok(vm, src_tok,
                      "[[cccc::test]] flags=\"...\": unknown flag '%s'"
                      " in test '%s'",
                      bad, test_name ? test_name : "?");
        }
    }

    free(buf);
}
