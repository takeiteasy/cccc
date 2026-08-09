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

// Macro compilation and execution subsystem
// Compiles [[cccc::macro]] / __attribute__((macro)) functions and expands
// macro calls in the AST

#include "./internal.h"
#include <fenv.h> // host fenv.h -- #832 comptime fenv barrier (see below)

// #832: guards a comptime (compile-time macro) vm_eval() call against
// leaving the host FP environment dirty. A comptime macro can call a
// decimal <math.h> function (src/stdlib/decimal_math.c), which -- unlike
// the folder in src/parse.c's eval_decimal -- has no CCCC_DEC_ENV_STATIC/
// _DYNAMIC parameter to force to-nearest-and-discard; it always rounds
// under fegetround() and always raises host FP flags via feraiseexcept().
// Without this barrier, a comptime sqrtd64() call would leave the compiler
// process holding whatever rounding mode/exception flags that call raised.
//
// Only the *rounding mode* round-trips through save/restore -- deliberately
// NOT the exception-flag state via fegetenv()/fesetenv(). Round-tripping
// the whole fenv_t would silently reintroduce whatever the host FP
// environment already had dirty *before* this call (a real, independently
// verified pre-existing condition: tokenize.c's convert_pp_number scans
// every floating/decimal literal's extent via a host strtold() call whose
// value is discarded but whose side effect isn't -- strtold("1.1", NULL)
// alone sets FE_UNDERFLOW on at least one verified platform). The guest
// program's actual clean-start guarantee comes from cc_run() (src/vm.c)
// resetting the host FP environment exactly once, immediately before the
// compiled program begins executing; this barrier only needs to (a) run
// the comptime call under a fixed, known rounding mode, and (b) not leave
// new dirty exception flags of its own behind for whatever compiles next.
static void fenv_barrier_begin(int *saved_round) {
    *saved_round = fegetround();
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);
}
static void fenv_barrier_end(int saved_round) {
    feclearexcept(FE_ALL_EXCEPT);
    fesetround(saved_round);
}

// Forward declarations for reflection API functions (to register as FFI).
// Generated from include/cccc/reflection.h -- see tools/gen_reflection_ffi.py.
#include "reflection_ffi_protos.inc"

void cc_record_emit_source(VirtualMachine *vm, const char *source) {
    if (!vm || !source || !*source)
        return;
    EmitEvent *ev = arena_alloc(&vm->compiler.parser_arena, sizeof(*ev));
    memset(ev, 0, sizeof(*ev));
    ev->kind = CCCC_EMIT_SOURCE;
    ev->source = arena_strdup(vm, source);
    if (vm->compiler.emit_events_tail)
        vm->compiler.emit_events_tail->next = ev;
    else
        vm->compiler.emit_events_head = ev;
    vm->compiler.emit_events_tail = ev;
}

void cc_record_emit_object(VirtualMachine *vm, Obj *obj) {
    if (!vm || !obj || !obj->is_macro_generated)
        return;
    for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next)
        if (ev->kind == CCCC_EMIT_OBJECT && ev->obj == obj)
            return;
    EmitEvent *ev = arena_alloc(&vm->compiler.parser_arena, sizeof(*ev));
    memset(ev, 0, sizeof(*ev));
    ev->kind = CCCC_EMIT_OBJECT;
    ev->obj = obj;
    if (vm->compiler.emit_events_tail)
        vm->compiler.emit_events_tail->next = ev;
    else
        vm->compiler.emit_events_head = ev;
    vm->compiler.emit_events_tail = ev;
}

int __builtin_ast_vararg_count(void) {
    VirtualMachine *vm = __builtin_current_vm;
    return vm ? vm->compiler.macro_vararg_count : 0;
}

Node *__builtin_ast_vararg_at(int index) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;
    if (vm->compiler.macro_vararg_string_mode)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargAt is only valid for inline AST macros");
    if (index < 0 || index >= vm->compiler.macro_vararg_count)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargAt index %d out of range (count %d)",
                  index, vm->compiler.macro_vararg_count);
    return vm->compiler.macro_vararg_nodes[index];
}

Node **__builtin_ast_varargs_as_array(void) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;
    if (vm->compiler.macro_vararg_string_mode)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargAsArray is only valid for inline AST macros");
    if (vm->compiler.macro_vararg_count == 0)
        return NULL;
    return vm->compiler.macro_vararg_nodes;
}

const char *__builtin_ast_vararg_str_at(int index) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm)
        return NULL;
    if (!vm->compiler.macro_vararg_string_mode)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargStrAt is only valid for global-generation string macros");
    if (index < 0 || index >= vm->compiler.macro_vararg_count)
        error_tok(vm, vm->compiler.macro_call_tok,
                  "VarargStrAt index %d out of range (count %d)",
                  index, vm->compiler.macro_vararg_count);
    return vm->compiler.macro_vararg_strs[index];
}

// Register reflection API functions as FFI
static void register_reflection_ffi(VirtualMachine *vm) {
    // Generated from include/cccc/reflection.h -- see
    // tools/gen_reflection_ffi.py.
#include "reflection_ffi_register.inc"
}

static void init_vm_segments_for_macros(VirtualMachine *vm);

static Token *copy_macro_token(VirtualMachine *vm, Token *tok) {
    Token *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    *copy = *tok;
    copy->next = NULL;
    copy->origin = tok;
    return copy;
}

static Token *new_macro_punct(VirtualMachine *vm, char *str, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_PUNCT;
    tok->loc = str;
    tok->len = strlen(str);
    if (tmpl) {
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *new_macro_eof(VirtualMachine *vm, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = TK_EOF;
    if (tmpl) {
        tok->loc = tmpl->loc;
        tok->len = tmpl->len;
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

// Synthesize a marker token consumed by preprocess2 to snapshot/restore
// vm->compiler.macros around a single comptime function body's tokens,
// isolating its #define/#undef from sibling comptime functions (#283).
static Token *new_macro_scope_marker(VirtualMachine *vm, TokenKind kind, Token *tmpl) {
    Token *tok = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    memset(tok, 0, sizeof(Token));
    tok->kind = kind;
    tok->loc = "";
    tok->len = 0;
    if (tmpl) {
        tok->file = tmpl->file;
        tok->filename = tmpl->filename;
        tok->line_no = tmpl->line_no;
        tok->col_no = tmpl->col_no;
    }
    return tok;
}

static Token *append_macro_prototype(VirtualMachine *vm, Token *cur, MacroFn *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        if (equal(tok, "{"))
            break;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    cur = cur->next = new_macro_punct(vm, ";", last);
    return cur;
}

static Token *append_macro_definition(VirtualMachine *vm, Token *cur, MacroFn *pm) {
    Token *last = pm->body_tokens;
    for (Token *tok = pm->body_tokens; tok && tok->kind != TK_EOF;
         tok = tok->next) {
        last = tok;
        cur = cur->next = copy_macro_token(vm, tok);
    }
    (void)last;
    return cur;
}

static Token *append_token_list(VirtualMachine *vm, Token *cur, Token *tokens) {
    for (Token *tok = tokens; tok && tok->kind != TK_EOF; tok = tok->next)
        cur = cur->next = copy_macro_token(vm, tok);
    return cur;
}

static Token *find_matching_brace(Token *tok) {
    int depth = 0;
    for (Token *t = tok; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{"))
            depth++;
        else if (equal(t, "}")) {
            depth--;
            if (depth == 0)
                return t;
        }
    }
    return NULL;
}

static bool starts_file_scope_call(Token *tok) {
    return tok && tok->kind == TK_IDENT && tok->next && equal(tok->next, "(");
}

// #890: a file-scope declaration is forwarded to the comptime pass if it
// comes from the primary source file, OR from any file that itself defines
// a comptime entity: a [[cccc::comptime]] function, captured in macro_fns,
// or a [[cccc::comptime]] variable, captured in comptime_vars. Narrower than
// "any file with comptime-related syntax in it" — a #pragma cccc comptime
// begin/end block containing only a typedef, or a [[cccc::comptime]] typedef,
// declares a type rather than a comptime function or variable and does not
// widen the allowed set on its own (see man/MACROS.md's "Pre-parse macro
// declaration context" section for the exact rule stated in prose).
// Rule: a comptime function or variable can always see the declarations
// written alongside it in its own file, regardless of #include depth —
// #include is textual, so a call to it made from a file that #includes it
// must behave identically to one appended directly to that file. This is
// deliberately narrower than "any non-system header": declarations in a
// third file that defines no comptime code of its own still need @shared to
// reach the comptime pass, matching #551/#552's isolation intent.
static bool comptime_ctx_file_allowed(VirtualMachine *vm, const char *filename) {
    if (!filename)
        return false;
    if (vm->compiler.primary_file &&
        strcmp(filename, vm->compiler.primary_file->display_name) == 0)
        return true;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->body_tokens && pm->body_tokens->filename &&
            strcmp(filename, pm->body_tokens->filename) == 0)
            return true;
    }
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        if (cv->decl_tokens && cv->decl_tokens->filename &&
            strcmp(filename, cv->decl_tokens->filename) == 0)
            return true;
    }
    return false;
}

// #893: true if every token strictly between `eq` (the top-level '=') and
// `semi` (the terminating ';') is a literal, punctuator, or keyword --
// never a bare identifier. Tokens here are already macro-expanded (this
// runs on the same preprocessed streams cc_execute_inline_macros hands to
// build_combined_macro_tokens), so ordinary object-like #define constants
// and NULL are already gone by this point; a surviving TK_IDENT means a
// function call, another global, an enum constant, or a typedef-name cast,
// none of which are safely resolvable inside the isolated comptime
// program, so such initializers are rejected rather than guessed at.
static bool initializer_is_self_contained(Token *eq, Token *semi) {
    for (Token *t = eq->next; t && t != semi; t = t->next) {
        if (t->kind == TK_IDENT)
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------
// #894: demand-driven comptime declaration index.
//
// Earlier revisions of this file (#890/#893) took an eager, file-scoped
// snapshot of "safe" file-scope declarations up front and prepended it to
// every comptime program, filtered by a file-identity heuristic (forward a
// declaration only from the primary source file, or from a file that
// itself defines comptime code). That eager prelude is gone: this index
// instead records WHERE each file-scope declaration lives in the
// preprocessed runtime streams, keyed by the name(s) it declares, and
// resolves nothing until the comptime parse actually asks for a name it
// can't find (is_typename/find_tag/primary()'s hooks in src/parse.c, via
// cc_comptime_resolve_typename/_tag/_var below). Cost is now proportional
// to what comptime code actually references, and a third file that defines
// no comptime code of its own no longer needs @shared just to be visible.
//
// System headers are excluded from the index by default (matching the old
// prelude's "system headers need @shared/@comptime/--comptime-include-all"
// behavior for #551/#552 isolation) and join it only under
// --comptime-include-all, which now widens the index in addition to its
// existing job of forwarding all #define macros (isolate_comptime_macros,
// below). See man/MACROS.md's "Pre-parse macro declaration context".
// ---------------------------------------------------------------------

typedef enum {
    CDK_TYPEDEF,
    CDK_TAG,
    CDK_OBJECT,
    CDK_PROTO,
    CDK_ENUM_CONST,
} ComptimeDeclKind;

typedef enum {
    CD_NEW,
    CD_IN_PROGRESS,
    CD_DONE,
    CD_FAILED,
} ComptimeDeclState;

// One file-scope declaration *statement*. It may be reachable under several
// names -- a tag name and one or more typedef/object declarator names can
// all name the SAME statement (e.g. `typedef struct Foo { ... } Foo, *FooP;`
// registers "Foo" as a tag, "Foo" as a typedef, and "FooP" as a typedef, all
// pointing here). State lives on the shared statement, not per name, so
// splicing via any one of its names parses the statement exactly once --
// this is what makes it safe to register a tag-with-body and its trailing
// declarators as separate names without risking a "redefinition" error from
// parsing the same statement twice under two different lookups.
typedef struct ComptimeDecl {
    Token *start;       // first token of the declaration
    Token *end;         // token AFTER the terminating ';' (exclusive)
    File *file;
    const char *filename;
    bool has_top_level_eq;
    bool initializer_self_contained; // meaningful only if has_top_level_eq
    ComptimeDeclState state;
} ComptimeDecl;

// One name -> declaration registration, chained on hashmap-key collision.
// Collisions cover both a genuine same-name conflict (two different input
// files each declaring `static int counter;`) and ordinary chaining.
typedef struct ComptimeDeclName {
    struct ComptimeDeclName *next;
    ComptimeDecl *decl;
    ComptimeDeclKind kind;
} ComptimeDeclName;

static void register_comptime_decl_name(VirtualMachine *vm, HashMap *map,
                                        Token *name_tok, ComptimeDecl *decl,
                                        ComptimeDeclKind kind) {
    if (!name_tok || name_tok->kind != TK_IDENT)
        return;
    ComptimeDeclName *reg =
        arena_alloc(&vm->compiler.parser_arena, sizeof(ComptimeDeclName));
    reg->decl = decl;
    reg->kind = kind;
    reg->next = hashmap_get2(map, name_tok->loc, name_tok->len);
    hashmap_put2(map, name_tok->loc, name_tok->len, reg);
}

// Register each enumerator inside an enum body [brace, semi), where `brace`
// is the enum's own opening '{'. An enumerator is any identifier at
// brace-depth 1 immediately preceded by '{' or ','.
static void index_enum_constants(VirtualMachine *vm, HashMap *ordinary_map,
                                 Token *brace, Token *semi, ComptimeDecl *decl) {
    int depth = 0;
    Token *prev_sig = NULL;
    for (Token *t = brace; t && t != semi; t = t->next) {
        if (equal(t, "{")) {
            depth++;
        } else if (equal(t, "}")) {
            depth--;
            if (depth == 0)
                return;
        } else if (depth == 1 && t->kind == TK_IDENT && prev_sig &&
                   (equal(prev_sig, "{") || equal(prev_sig, ","))) {
            register_comptime_decl_name(vm, ordinary_map, t, decl, CDK_ENUM_CONST);
        }
        prev_sig = t;
    }
}

// Token immediately before the first top-level (depth-0, outside any brace
// body) '[' array-dimension group in [seg_start, seg_last], or seg_last
// itself if there is none -- the candidate declared name for a simple
// (non-function) declarator, with any trailing array dimensions skipped.
// Tokens are singly-linked, so this is a forward scan that remembers the
// position rather than a true backward walk.
//
// Two things a naive '['/']' depth counter gets wrong, both fixed here
// (#951):
//
//  - An array member inside an anonymous struct/union body declared in the
//    same statement as its own declarator (e.g. "typedef struct { char
//    n[32]; } A;") is *inside* the segment, since there is no depth-0 tag
//    pair to end the tag scan early. Without brace tracking, that member's
//    '[' is mistaken for A's own array dimension and the declaration gets
//    indexed under the member's name ("n") instead of "A" -- silently
//    misindexed, surfacing later as an unrelated "expected ','" when
//    something tries to splice "A" and finds no index entry.
//  - A leading C23 attribute-specifier-seq ("[[deprecated]] int dx;") is a
//    double '[' at the very start of the segment, before any real
//    declarator token. Without skipping it as a unit, its first '[' is
//    taken as an array-dimension start with no preceding token, so `result`
//    becomes NULL and the declaration is never indexed at all.
static Token *segment_declarator_name(Token *seg_start, Token *seg_last) {
    Token *result = seg_last;
    bool found_bracket = false;
    int depth = 0;
    int brace_depth = 0;
    Token *prev = NULL;
    for (Token *t = seg_start; t; ) {
        // Skip a C23 attribute-specifier-seq in its entirety -- its
        // brackets are never array dimensions and must not perturb the
        // depth counter below.
        if (brace_depth == 0 && equal(t, "[") && t->next && equal(t->next, "[")) {
            int attr_depth = 0;
            Token *u = t;
            for (;;) {
                if (equal(u, "["))
                    attr_depth++;
                else if (equal(u, "]"))
                    attr_depth--;
                bool at_end = (u == seg_last);
                if (attr_depth == 0 || at_end) {
                    prev = u;
                    t = at_end ? NULL : u->next;
                    break;
                }
                u = u->next;
            }
            continue;
        }
        if (brace_depth == 0 && equal(t, "[")) {
            if (depth == 0 && !found_bracket) {
                result = prev;
                found_bracket = true;
            }
            depth++;
        } else if (equal(t, "]") && depth > 0) {
            depth--;
        } else if (equal(t, "{")) {
            brace_depth++;
        } else if (equal(t, "}") && brace_depth > 0) {
            brace_depth--;
        }
        if (t == seg_last)
            break;
        prev = t;
        t = t->next;
    }
    return result;
}

// Returns the '(' token matching `close` (a ')' token in the same segment),
// scanning forward from `seg_start` with a small paren stack. NULL if
// unmatched or the segment is implausibly deeply nested.
#define CD_PAREN_STACK_MAX 32
static Token *find_matching_open_paren(Token *seg_start, Token *close) {
    Token *stack[CD_PAREN_STACK_MAX];
    int sp = 0;
    for (Token *t = seg_start; t; t = t->next) {
        if (equal(t, "(")) {
            if (sp >= CD_PAREN_STACK_MAX)
                return NULL;
            stack[sp++] = t;
        } else if (equal(t, ")")) {
            if (sp == 0)
                return NULL;
            Token *open = stack[--sp];
            if (t == close)
                return open;
        }
        if (t == close)
            break;
    }
    return NULL;
}

// Extract the declared name from one comma-delimited declarator segment
// [seg_start, seg_last]. Handles plain declarators (skipping trailing
// arrays), simple function prototypes ("NAME ( ... )"), and function-pointer
// declarators ("( * NAME ) ( ... )", any number of leading '*'). Returns
// NULL -- register nothing -- for anything less confident than that,
// including multi-level function-pointer nesting. Sets *is_simple_proto
// when the segment is specifically the "NAME ( ... )" shape, so the caller
// can tell a function prototype apart from a function-pointer object/typedef
// declarator (both end in ')', but only the former needs CDK_PROTO instead
// of CDK_OBJECT/CDK_TYPEDEF).
static Token *extract_declarator_name(Token *seg_start, Token *seg_last,
                                      bool *is_simple_proto) {
    *is_simple_proto = false;
    if (equal(seg_last, ")")) {
        Token *open = find_matching_open_paren(seg_start, seg_last);
        if (!open)
            return NULL;
        Token *before_open = NULL;
        for (Token *t = seg_start; t && t != open; t = t->next)
            before_open = t;
        if (before_open && before_open->kind == TK_IDENT) {
            *is_simple_proto = true;
            return before_open;
        }
        if (before_open && equal(before_open, ")")) {
            Token *inner_open = find_matching_open_paren(seg_start, before_open);
            if (inner_open) {
                Token *name = NULL;
                for (Token *t = inner_open->next; t && t != before_open; t = t->next)
                    if (t->kind == TK_IDENT)
                        name = t;
                if (name)
                    return name; // function-pointer-shaped declarator
            }
        }
        return NULL;
    }
    Token *name = segment_declarator_name(seg_start, seg_last);
    return (name && name->kind == TK_IDENT) ? name : NULL;
}

// Register one already-delimited file-scope declaration [start, semi]
// (`semi` is the terminating ';') into the comptime declaration index.
// `has_top_level_eq`/`eq_tok` come from cc_comptime_index_build's own
// depth-tracked scan (below), letting #893's initializer rule be applied
// later, per name, at splice time (comptime_index_splice) rather than
// eagerly here. Conservative throughout: any segment whose declared name
// can't be confidently identified is simply not registered, which just
// means it stays invisible to demand-driven resolution.
static void index_declaration(VirtualMachine *vm, Token *start, Token *semi,
                              bool has_top_level_eq, Token *eq_tok) {
    if (!start->filename)
        return;
    File *file = start->file;
    if (file && file->is_system_header && !vm->compiler.comptime_include_all)
        return;

    bool saw_typedef = false;
    for (Token *t = start; t != semi; t = t->next) {
        if (equal(t, "typedef")) {
            saw_typedef = true;
            break;
        }
    }

    ComptimeDecl *decl = arena_alloc(&vm->compiler.parser_arena, sizeof(ComptimeDecl));
    decl->start = start;
    decl->end = semi->next;
    decl->file = file;
    decl->filename = start->filename;
    decl->has_top_level_eq = has_top_level_eq;
    decl->initializer_self_contained =
        has_top_level_eq && eq_tok && initializer_is_self_contained(eq_tok, semi);
    decl->state = CD_NEW;

    // --- tag: only the first "struct|union|enum IDENT" pair AT DEPTH 0 is
    // inspected. A pair inside a function declarator's parameter list (e.g.
    // "typedef int (*fp)(struct S *);", or a plain prototype
    // "int f(struct S *);") is nested inside '(' ... ')' and is not this
    // declaration's own tag -- it must be skipped, not treated as the tag
    // and used to (wrongly) seed decl_list_start. Depth tracking also
    // correctly ignores a tag pair inside an initializer's "sizeof(struct
    // X)" and, via brace_depth, a nested tag inside an anonymous member
    // (e.g. "struct { struct T *p; } v;" no longer misregisters T as this
    // statement's tag). Mirrors the depth-tracked declarator-list split
    // loop just below.
    Token *decl_list_start = start;
    {
        int paren_depth = 0, bracket_depth = 0, brace_depth = 0;
        for (Token *t = start; t != semi; t = t->next) {
            if (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
                (equal(t, "struct") || equal(t, "union") || equal(t, "enum")) &&
                t->next && t->next->kind == TK_IDENT) {
                Token *tagname = t->next;
                Token *after = tagname->next;
                if (after == semi || equal(after, "{") || equal(after, "*") ||
                    equal(after, ",")) {
                    register_comptime_decl_name(vm, &vm->compiler.comptime_tag_index,
                                                tagname, decl, CDK_TAG);
                    decl_list_start = after;
                    if (equal(after, "{")) {
                        if (equal(t, "enum"))
                            index_enum_constants(vm, &vm->compiler.comptime_decl_index,
                                                 after, semi, decl);
                        Token *close = find_matching_brace(after);
                        decl_list_start = close ? close->next : after;
                    }
                }
                break;
            }
            if (equal(t, "("))
                paren_depth++;
            else if (equal(t, ")") && paren_depth > 0)
                paren_depth--;
            else if (equal(t, "["))
                bracket_depth++;
            else if (equal(t, "]") && bracket_depth > 0)
                bracket_depth--;
            else if (equal(t, "{"))
                brace_depth++;
            else if (equal(t, "}") && brace_depth > 0)
                brace_depth--;
        }
    }

    if (decl_list_start == semi)
        return; // tag-only: "struct Foo;" or "struct Foo { ... };"

    // --- declarator list: split on top-level (depth-0) commas ---
    int paren_depth = 0, bracket_depth = 0, brace_depth = 0;
    Token *seg_start = decl_list_start;
    for (Token *t = decl_list_start; ; t = t->next) {
        bool at_boundary = (t == semi) ||
            (paren_depth == 0 && bracket_depth == 0 && brace_depth == 0 &&
             equal(t, ","));
        if (at_boundary) {
            Token *seg_last = NULL;
            for (Token *u = seg_start; u != t; u = u->next)
                seg_last = u;
            // Truncate at this segment's own top-level '=', if any: an
            // initializer's tokens (e.g. the "plain_var" in
            // "int *p = &plain_var") must never be mistaken for the
            // declared name.
            if (seg_last) {
                int eq_depth = 0;
                for (Token *u = seg_start; ; u = u->next) {
                    if (equal(u, "(") || equal(u, "[") || equal(u, "{"))
                        eq_depth++;
                    else if (equal(u, ")") || equal(u, "]") || equal(u, "}"))
                        eq_depth--;
                    else if (eq_depth == 0 && equal(u, "=")) {
                        Token *before_eq = NULL;
                        for (Token *v = seg_start; v != u; v = v->next)
                            before_eq = v;
                        seg_last = before_eq;
                        break;
                    }
                    if (u == seg_last)
                        break;
                }
            }
            if (seg_last) {
                bool is_simple_proto = false;
                Token *name = extract_declarator_name(seg_start, seg_last,
                                                       &is_simple_proto);
                if (name) {
                    ComptimeDeclKind kind = saw_typedef ? CDK_TYPEDEF :
                        (is_simple_proto ? CDK_PROTO : CDK_OBJECT);
                    register_comptime_decl_name(vm, &vm->compiler.comptime_decl_index,
                                                name, decl, kind);
                }
            }
            if (t == semi)
                break;
            seg_start = t->next;
            continue;
        }
        if (equal(t, "("))
            paren_depth++;
        else if (equal(t, ")") && paren_depth > 0)
            paren_depth--;
        else if (equal(t, "["))
            bracket_depth++;
        else if (equal(t, "]") && bracket_depth > 0)
            bracket_depth--;
        else if (equal(t, "{"))
            brace_depth++;
        else if (equal(t, "}") && brace_depth > 0)
            brace_depth--;
    }
}

// Build the comptime declaration index over all input streams. Idempotent
// per compile (has_comptime_decl_index guards re-entry). The
// declaration-boundary scan (file-scope call skip, function-body skip via
// brace matching, depth-tracked '='/';' detection) records an index entry
// at each terminator via index_declaration, above.
static void cc_comptime_index_build(VirtualMachine *vm, Token **input_tokens,
                                    int count) {
    if (vm->compiler.has_comptime_decl_index)
        return;
    vm->compiler.has_comptime_decl_index = true;

    for (int fi = 0; fi < count; fi++) {
        Token *tok = input_tokens[fi];
        while (tok && tok->kind != TK_EOF) {
            Token *start = tok;

            if (starts_file_scope_call(tok)) {
                while (tok && tok->kind != TK_EOF && !equal(tok, ";"))
                    tok = tok->next;
                if (tok && equal(tok, ";"))
                    tok = tok->next;
                continue;
            }

            bool has_top_level_eq = false;
            bool is_function_body = false;
            Token *eq_tok = NULL;
            int paren_depth = 0;
            int bracket_depth = 0;
            int brace_depth = 0;
            Token *prev_sig = NULL;

            while (tok && tok->kind != TK_EOF) {
                if (brace_depth == 0 && paren_depth == 0 &&
                    bracket_depth == 0) {
                    if (equal(tok, "=")) {
                        has_top_level_eq = true;
                        if (!eq_tok)
                            eq_tok = tok;
                    }

                    if (equal(tok, "{") && prev_sig &&
                        equal(prev_sig, ")")) {
                        is_function_body = true;
                        Token *close = find_matching_brace(tok);
                        tok = close ? close->next : tok->next;
                        break;
                    }

                    if (equal(tok, ";")) {
                        index_declaration(vm, start, tok, has_top_level_eq, eq_tok);
                        tok = tok->next;
                        break;
                    }
                }

                if (equal(tok, "("))
                    paren_depth++;
                else if (equal(tok, ")") && paren_depth > 0)
                    paren_depth--;
                else if (equal(tok, "["))
                    bracket_depth++;
                else if (equal(tok, "]") && bracket_depth > 0)
                    bracket_depth--;
                else if (equal(tok, "{"))
                    brace_depth++;
                else if (equal(tok, "}") && brace_depth > 0)
                    brace_depth--;

                if (!equal(tok, ";"))
                    prev_sig = tok;
                tok = tok->next;
            }

            if (!tok || tok->kind == TK_EOF)
                break;
            if (is_function_body)
                continue;
        }
    }
}

// Copy [start, end) (the range recorded on a ComptimeDecl) into a fresh,
// EOF-terminated token list via copy_macro_token, so the runtime stream
// itself is never mutated or re-linked by the reentrant parse.
static Token *materialize_comptime_range(VirtualMachine *vm, Token *start, Token *end) {
    Token head = {};
    Token *cur = &head;
    for (Token *t = start; t && t != end && t->kind != TK_EOF; t = t->next)
        cur = cur->next = copy_macro_token(vm, t);
    cur->next = new_macro_eof(vm, cur != &head ? cur : start);
    return head.next;
}

// Shared lookup+splice path behind cc_comptime_resolve_typename/_tag/_var
// (token-based, for the parser's own miss hooks) and
// cc_comptime_resolve_type_name/_value_name (plain-string, for reflection.c
// API entry points like GetType()/VarRef() that look a name up at comptime
// *execution* time rather than during the comptime *parse* -- there is no
// token to intercept there, so those go through this same path by name
// instead). `map` is the appropriate namespace (comptime_decl_index for
// typedefs/objects/functions/enum constants, comptime_tag_index for
// struct/union/enum tags); `kind_mask` restricts which registration(s)
// under the name are eligible -- e.g. a typename lookup only wants a
// CDK_TYPEDEF registration, never an object of the same name that happens
// to share the ordinary-namespace map.
//
// Prefers a registration whose declaration is in the primary file when
// several files register the same name under the requested kind (the
// multi-input-file duplicate-name case); otherwise takes the first
// matching-kind registration.
static bool comptime_index_splice(VirtualMachine *vm, HashMap *map,
                                  const char *loc, int len, unsigned kind_mask) {
    // Note: NOT gated on vm->compiler.in_macro_mode here -- reflection.c's
    // callers (cc_comptime_resolve_type_name/_value_name) run at comptime
    // *execution* time, after compile_macro_program has already returned
    // and reset in_macro_mode to false. The CDK_OBJECT branch below applies
    // its own, narrower in_macro_mode requirement for the one kind where
    // that distinction is load-bearing.
    if (!vm->compiler.has_comptime_decl_index)
        return false;

    ComptimeDeclName *chain = hashmap_get2(map, loc, len);
    if (!chain)
        return false;

    ComptimeDeclName *pick = NULL;
    for (ComptimeDeclName *r = chain; r; r = r->next) {
        if (!(kind_mask & (1u << r->kind)))
            continue;
        if (!pick)
            pick = r;
        if (vm->compiler.primary_file && r->decl->file == vm->compiler.primary_file) {
            pick = r;
            break;
        }
    }
    if (!pick)
        return false;

    ComptimeDecl *decl = pick->decl;
    // CD_IN_PROGRESS: a cycle (mutually recursive tags/typedefs) -- the
    // declaration currently being spliced references itself, directly or
    // transitively; let the caller's own lookup fail normally (correct for
    // an incomplete-type reference, e.g. a pointer member). CD_DONE/
    // CD_FAILED: already resolved (successfully or not) -- never retried.
    if (decl->state != CD_NEW)
        return false;

    // #890/#893 object-splice gate: types, prototypes, and enum constants
    // splice freely once indexed (index-build time already excluded system
    // headers unless --comptime-include-all), but a plain OBJECT declarator
    // is real storage -- init_macro_globals zero-fills the macro program's
    // data segment for it, so an ungated splice would let a comptime body
    // silently read back 0 for a global it should never have been able to
    // see, replacing today's loud "undefined variable". Apply the same
    // #890/#893 rule this project has always applied to initialized
    // globals: no initializer -> primary file or a comptime-defining file
    // (comptime_ctx_file_allowed); has an initializer -> primary file only,
    // and only when the initializer is a self-contained constant. Anything
    // else is refused. A refused *initialized* global is recorded for the
    // "declared but not forwarded" diagnostic hint (#893,
    // cc_is_dropped_comptime_global, parse.c) -- that hint's wording
    // specifically describes an initializer, so a refused *uninitialized*
    // global (out-of-scope file, #890's rule) is deliberately left off the
    // list and falls through to the ordinary bare "undefined variable"
    // message instead.
    if (pick->kind == CDK_OBJECT) {
        // Objects additionally require in_macro_mode: compile_macro_program
        // allocates data-segment storage for every global (init_macro_globals,
        // Step 1) exactly once, immediately after the comptime *parse*
        // finishes and before in_macro_mode resets to false. A splice
        // triggered later -- from reflection.c's VarRef()/FindGlobal(),
        // called during comptime *execution* -- would prepend an Obj that
        // never gets a data-segment slot, i.e. a reference to uninitialized
        // storage. There is exactly one compile_macro_program call per
        // compile (macro_fns_compiled guards it), so this is a permanent,
        // not transient, refusal: mark CD_FAILED rather than CD_NEW so nothing
        // retries it. Types, prototypes, and enum constants have no such
        // restriction and splice fine at any point (see below).
        if (!vm->compiler.in_macro_mode) {
            decl->state = CD_FAILED;
            return false;
        }
        bool is_primary = vm->compiler.primary_file &&
                          decl->file == vm->compiler.primary_file;
        bool allowed = decl->has_top_level_eq
            ? (is_primary && decl->initializer_self_contained)
            : comptime_ctx_file_allowed(vm, decl->filename);
        if (!allowed) {
            decl->state = CD_FAILED;
            if (decl->has_top_level_eq)
                arena_strarray_push(vm, &vm->compiler.comptime_dropped_globals,
                                    arena_strndup(vm, loc, len));
            return false;
        }
    }

    decl->state = CD_IN_PROGRESS;
    Token *materialized = materialize_comptime_range(vm, decl->start, decl->end);
    bool ok = cc_parse_splice_range(vm, materialized);
    decl->state = ok ? CD_DONE : CD_FAILED;
    return ok;
}

bool cc_comptime_resolve_tag(VirtualMachine *vm, Token *name_tok) {
    return comptime_index_splice(vm, &vm->compiler.comptime_tag_index,
                                 name_tok->loc, name_tok->len, 1u << CDK_TAG);
}

bool cc_comptime_resolve_var(VirtualMachine *vm, Token *name_tok) {
    return comptime_index_splice(vm, &vm->compiler.comptime_decl_index,
                                 name_tok->loc, name_tok->len,
                                 (1u << CDK_OBJECT) | (1u << CDK_PROTO) |
                                 (1u << CDK_ENUM_CONST));
}

bool cc_comptime_resolve_typename(VirtualMachine *vm, Token *name_tok) {
    return comptime_index_splice(vm, &vm->compiler.comptime_decl_index,
                                 name_tok->loc, name_tok->len, 1u << CDK_TYPEDEF);
}

// #894: plain-string variants of the resolvers above, for reflection.c API
// entry points (GetType(), VarRef(), ...) that a comptime program calls at
// bytecode-execution time with a name string rather than a source token --
// there is no identifier token to intercept on a miss there, so
// __builtin_ast_find_type/__builtin_ast_var_ref call these directly by name
// instead of going through is_typename/find_tag/primary()'s hooks.
// GetType("Name") in particular needs BOTH namespaces tried: it resolves a
// struct/union/enum tag or a typedef through the same call, matching how
// __builtin_ast_find_type's own scope walk checks sc->tags and sc->vars'
// type_def entries together.
bool cc_comptime_resolve_type_name(VirtualMachine *vm, const char *name, int len) {
    if (!name || len <= 0)
        return false;
    if (comptime_index_splice(vm, &vm->compiler.comptime_tag_index, name, len,
                              1u << CDK_TAG))
        return true;
    return comptime_index_splice(vm, &vm->compiler.comptime_decl_index, name, len,
                                 1u << CDK_TYPEDEF);
}

bool cc_comptime_resolve_value_name(VirtualMachine *vm, const char *name, int len) {
    if (!name || len <= 0)
        return false;
    return comptime_index_splice(vm, &vm->compiler.comptime_decl_index, name, len,
                                 (1u << CDK_OBJECT) | (1u << CDK_PROTO) |
                                 (1u << CDK_ENUM_CONST));
}

// #893: used by parse.c's undefined-variable diagnostic (in_macro_mode only)
// to tell a genuinely undefined identifier apart from a runtime-TU global
// that comptime_index_splice saw and deliberately declined to splice in
// (non-constant initializer, or declared outside the primary file).
bool cc_is_dropped_comptime_global(VirtualMachine *vm, const char *name, int len) {
    for (int i = 0; i < vm->compiler.comptime_dropped_globals.len; i++) {
        char *g = vm->compiler.comptime_dropped_globals.data[i];
        if (g && (int)strlen(g) == len && strncmp(g, name, len) == 0)
            return true;
    }
    return false;
}

// Find the first top-level '=' in a token list (brace-depth 0).
// Returns the token AT '=', or NULL if none found.
static Token *find_top_level_eq(Token *tokens) {
    int depth = 0;
    for (Token *t = tokens; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{")) depth++;
        else if (equal(t, "}")) depth--;
        else if (depth == 0 && equal(t, "="))
            return t;
    }
    return NULL;
}

typedef enum {
    COMPTIME_AGG_CAST_NONE,
    COMPTIME_AGG_CAST_TAGGED,
    COMPTIME_AGG_CAST_TYPEDEF,
    COMPTIME_AGG_CAST_TYPEOF,
} ComptimeAggregateCastKind;

typedef struct {
    ComptimeAggregateCastKind kind;
    Token *kw;   // "struct" or "union" for tagged aggregates
    Token *name; // tag or typedef name
} ComptimeAggregateCast;

static bool token_matches_name(Token *tok, const char *name) {
    return tok && tok->kind == TK_IDENT &&
           strlen(name) == (size_t)tok->len &&
           strncmp(tok->loc, name, tok->len) == 0;
}

// Determine which compound-literal cast can initialize a comptime aggregate.
// Handles:
//   - tagged:    struct Dims { ... } dims -> (struct Dims){ ... }
//   - typedef'd: Dims dims                 -> (Dims){ ... }
//   - anonymous: struct { ... } dims       -> (typeof(dims)){ ... }
static ComptimeAggregateCast comptime_aggregate_cast(ComptimeVar *cv) {
    ComptimeAggregateCast cast = { COMPTIME_AGG_CAST_NONE, NULL, NULL };
    Token *typedef_name = NULL;
    int brace_depth = 0, bracket_depth = 0, paren_depth = 0;

    for (Token *t = cv->decl_tokens; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{")) brace_depth++;
        else if (equal(t, "}")) brace_depth--;
        else if (equal(t, "[")) bracket_depth++;
        else if (equal(t, "]")) bracket_depth--;
        else if (equal(t, "(")) paren_depth++;
        else if (equal(t, ")")) paren_depth--;

        if (brace_depth != 0 || bracket_depth != 0 || paren_depth != 0)
            continue;
        if (equal(t, "=") || equal(t, ";"))
            break;

        if (equal(t, "struct") || equal(t, "union")) {
            Token *next = t->next;
            if (next && next->kind == TK_IDENT) {
                cast.kind = COMPTIME_AGG_CAST_TAGGED;
                cast.kw = t;
                cast.name = next;
                return cast;
            }
            cast.kind = COMPTIME_AGG_CAST_TYPEOF;
            return cast;
        }

        if (t->kind == TK_IDENT && !token_matches_name(t, cv->name) &&
            !typedef_name)
            typedef_name = t;
    }

    if (typedef_name) {
        cast.kind = COMPTIME_AGG_CAST_TYPEDEF;
        cast.name = typedef_name;
    }
    return cast;
}

// True if this comptime var's initializer should be evaluated via
// __builtin_comptime_init (ticket #191/#192/#193) rather than the constant init_data
// path. Both build_combined_macro_tokens and build_comptime_init_fn_tokens
// call this so they agree on which vars are routed through the init fn.
//
// Scalar initializers (= <non-brace-expr>): always routed.
// Aggregate initializers (= { ... }): routed iff a compound-literal cast can
// be synthesized from a tag, typedef name, or typeof(var).
static bool comptime_var_uses_init_fn(ComptimeVar *cv) {
    Token *eq = find_top_level_eq(cv->decl_tokens);
    if (!eq)
        return false; // No initializer at all.
    if (!eq->next || !equal(eq->next, "{"))
        return true;  // Scalar expression init.
    // Aggregate init: routable only if a cast form can be synthesized.
    return comptime_aggregate_cast(cv).kind != COMPTIME_AGG_CAST_NONE;
}

// Inject decl_tokens up to (not including) eq_tok, then emit ';'.
// Produces a declaration without its initializer, e.g. "int buf_size ;"
static Token *append_decl_stripped(VirtualMachine *vm, Token *cur, Token *decl_tokens,
                                   Token *eq_tok) {
    for (Token *t = decl_tokens; t && t->kind != TK_EOF && t != eq_tok;
         t = t->next)
        cur = cur->next = copy_macro_token(vm, t);
    cur = cur->next = new_macro_punct(vm, ";", eq_tok);
    return cur;
}

// Build a __builtin_comptime_init function that assigns each comptime var's
// initializer expression in source order. Handles:
//   - scalar expression inits:  name = expr ;
//   - aggregate inits:          name = (<aggregate type>){ ... } ;
// Returns a token list via tokenize_string, or NULL when there are no vars
// routed through the init fn.
static Token *build_comptime_init_fn_tokens(VirtualMachine *vm,
                                             ComptimeVar **vars, int count) {
    bool has_any = false;
    for (int i = 0; i < count; i++) {
        if (comptime_var_uses_init_fn(vars[i])) {
            has_any = true;
            break;
        }
    }
    if (!has_any)
        return NULL;

    char buf[16384];
    char *p   = buf;
    char *end = buf + sizeof(buf) - 4;

    p += snprintf(p, end - p, "void __builtin_comptime_init(void){\n");

    for (int i = 0; i < count; i++) {
        ComptimeVar *cv = vars[i];
        if (!comptime_var_uses_init_fn(cv)) continue;

        Token *eq = find_top_level_eq(cv->decl_tokens);
        if (!eq) continue; // Should not happen if predicate is true, but be safe.

        if (eq->next && equal(eq->next, "{")) {
            // Aggregate init: emit  name = (<aggregate type>){ ... } ;
            ComptimeAggregateCast cast = comptime_aggregate_cast(cv);
            if (cast.kind == COMPTIME_AGG_CAST_TAGGED) {
                p += snprintf(p, end - p, "%s=(%.*s %.*s)",
                              cv->name,
                              cast.kw->len, cast.kw->loc,
                              cast.name->len, cast.name->loc);
            } else if (cast.kind == COMPTIME_AGG_CAST_TYPEDEF) {
                p += snprintf(p, end - p, "%s=(%.*s)",
                              cv->name, cast.name->len, cast.name->loc);
            } else if (cast.kind == COMPTIME_AGG_CAST_TYPEOF) {
                p += snprintf(p, end - p, "%s=(typeof(%s))",
                              cv->name, cv->name);
            } else {
                continue;
            }

            // Emit the brace-group '{ ... }' verbatim from eq->next.
            int depth = 0;
            for (Token *t = eq->next; t && t->kind != TK_EOF; t = t->next) {
                if (equal(t, "{")) depth++;
                else if (equal(t, "}")) { depth--; }
                else if (depth == 0 && equal(t, ";")) break;
                if (p + t->len + 2 >= end)
                    error("comptime init function source overflow (too many/long tokens)");
                p += snprintf(p, end - p, " %.*s", t->len, t->loc);
                if (equal(t, "}") && depth == 0) break;
            }
            p += snprintf(p, end - p, ";\n");
        } else {
            // Scalar expression init: emit  name = expr ;
            p += snprintf(p, end - p, "%s=", cv->name);

            int depth = 0;
            for (Token *t = eq->next; t && t->kind != TK_EOF; t = t->next) {
                if (equal(t, "{")) depth++;
                else if (equal(t, "}")) depth--;
                else if (depth == 0 && equal(t, ";")) break;
                if (p + t->len + 2 >= end)
                    error("comptime init function source overflow (too many/long tokens)");
                p += snprintf(p, end - p, " %.*s", t->len, t->loc);
            }
            p += snprintf(p, end - p, ";\n");
        }
    }

    p += snprintf(p, end - p, "}\n");

    Token *toks = tokenize_string(vm, "<comptime-init-fn>", buf);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);
    return toks;
}

// Names used as enum constants in reflection.h's TypeKind and NodeKind enums.
// User-code macros with these names (e.g. SQLite's #define TK_FLOAT 154 or
// parser-generator TK_*/NK_* tokens) must be temporarily removed before any
// preprocessing pass that includes reflection.h, otherwise the preprocessor
// expands them inside the enum initializers and produces a parse error.
static const char *reflection_enum_names[] = {
    // TypeKind
    "TK_VOID", "TK_BOOL", "TK_CHAR", "TK_SHORT", "TK_INT", "TK_LONG",
    "TK_FLOAT", "TK_DOUBLE", "TK_LDOUBLE", "TK_ENUM", "TK_PTR",
    "TK_FUNC", "TK_ARRAY", "TK_VLA", "TK_STRUCT", "TK_UNION",
    // NodeKind
    "NK_NULL_EXPR", "NK_ADD", "NK_SUB", "NK_MUL", "NK_DIV", "NK_NEG",
    "NK_MOD", "NK_BITAND", "NK_BITOR", "NK_BITXOR", "NK_SHL", "NK_SHR",
    "NK_EQ", "NK_NE", "NK_LT", "NK_LE", "NK_ASSIGN", "NK_COND",
    "NK_COMMA", "NK_MEMBER", "NK_ADDR", "NK_DEREF", "NK_NOT",
    "NK_BITNOT", "NK_LOGAND", "NK_LOGOR", "NK_RETURN", "NK_IF",
    "NK_FOR", "NK_DO", "NK_SWITCH", "NK_CASE", "NK_BLOCK", "NK_FUNCALL",
    "NK_EXPR_STMT", "NK_VAR", "NK_NUM", "NK_CAST", "NK_MACRO_CALL",
    NULL
};
#define REFLECTION_ENUM_NAMES_COUNT 55

// Remove reflection.h enum-constant macros from the live table, storing each
// previous value in saved[] (indexed by position in reflection_enum_names[]).
// Call reflection_enum_names_restore() to put them back.
static void reflection_enum_names_hide(VirtualMachine *vm, void **saved) {
    for (int i = 0; reflection_enum_names[i]; i++) {
        saved[i] = hashmap_get(&vm->compiler.macros,
                               (char *)reflection_enum_names[i]);
        if (saved[i])
            hashmap_delete(&vm->compiler.macros,
                           (char *)reflection_enum_names[i]);
    }
}

static void reflection_enum_names_restore(VirtualMachine *vm, void **saved) {
    for (int i = 0; reflection_enum_names[i]; i++)
        if (saved[i])
            hashmap_put(&vm->compiler.macros,
                        (char *)reflection_enum_names[i], saved[i]);
}

static Token *implicit_reflection_tokens(VirtualMachine *vm) {
    // Temporarily suppress user-defined TK_*/NK_* macros (TypeKind/NodeKind
    // enum constant names) so they can't expand inside reflection.h's enum
    // initializers.  reflection.h's own #define macros (WithFn, VM, etc.)
    // are unaffected.  See also compile_macro_program which suppresses them
    // for the broader comptime preprocess pass.
    void *saved_enum_macros[REFLECTION_ENUM_NAMES_COUNT];
    reflection_enum_names_hide(vm, saved_enum_macros);

    // The user's translation unit may already have included stdbool.h,
    // stddef.h, or stdint.h. Temporarily clear those guards so reflection.h's
    // private macro API is processed completely in the macro compilation
    // scope. We restore the guards afterwards so subsequent compilation phases
    // are unaffected.
    static const char *guards[] = {
        "CCCC_REFLECTION_H", "__STDBOOL_H", "__STDDEF_H", "__STDINT_H",
        "__STRING_H", NULL
    };
    void *saved_guards[5] = {};
    for (int i = 0; guards[i]; i++) {
        saved_guards[i] = hashmap_get(&vm->compiler.macros, (char *)guards[i]);
        if (saved_guards[i])
            hashmap_delete(&vm->compiler.macros, (char *)guards[i]);
    }

    // Preprocessing reflection.h can trigger lookahead declaration-parsing
    // (e.g. while scanning @macro bodies for locals) over expanded VM/$...
    // tokens, which spuriously warns about CCCC's own internal API surface.
    // Suppress all warnings/werrors for the duration of this internal
    // preprocess pass; restore afterwards so the user's TU is unaffected.
    uint64_t saved_warnings = vm->compiler.warnings;
    uint64_t saved_werror = vm->compiler.warning_errors;
    vm->compiler.warnings = 0;
    vm->compiler.warning_errors = 0;

    Token *tokens = tokenize_private_header(vm, "reflection.h", "<implicit-reflection.h>");
    Token *result = preprocess(vm, tokens);

    vm->compiler.warnings = saved_warnings;
    vm->compiler.warning_errors = saved_werror;

    for (int i = 0; guards[i]; i++)
        if (saved_guards[i])
            hashmap_put(&vm->compiler.macros, (char *)guards[i], saved_guards[i]);

    reflection_enum_names_restore(vm, saved_enum_macros);

    return result;
}

// Ticket #235: idempotently preprocess reflection.h so that its built-in
// @macro(attribute(...)) handlers (e.g. __builtin_attr_serialize) are registered
// into vm->compiler.macro_fns before the first attribute-dispatch lookup
// (find_attribute_macro / run_custom_attrs in parse.c). Without this, a
// translation unit with no @macro definitions of its own never triggers
// implicit_reflection_tokens() until compile_macro_program() — too late for
// the very first @serialize/@deserialize/etc. dispatch to find a handler.
// Safe to call mid-parse: implicit_reflection_tokens only tokenizes and
// preprocesses reflection.h and temporarily toggles include-guard macros.
void ensure_reflection_attrs_registered(VirtualMachine *vm) {
    if (vm->compiler.no_comptime)
        return;
    if (vm->compiler.reflection_attrs_registered)
        return;
    vm->compiler.reflection_attrs_registered = true;
    implicit_reflection_tokens(vm);
    __builtin_ensure_string_h_decls();
}

static Token *build_combined_macro_tokens(VirtualMachine *vm, Token *reflection_tokens,
                                          MacroFn **macros, int count) {
    Token head = {};
    Token *cur = &head;

    cur = append_token_list(vm, cur, reflection_tokens);

    // Inject routed comptime directives so they are processed by the comptime
    // preprocessing pass (#196/#368).
    for (int i = 0; i < vm->compiler.comptime_pending_includes.len; i++) {
        char *src = arena_format(vm, "%s\n",
                                 vm->compiler.comptime_pending_includes.data[i]);
        Token *inc_toks = tokenize_string(vm, "<comptime-include>", src);
        cur = append_token_list(vm, cur, inc_toks);
    }

    // Reverse comptime_vars to source order (list is prepended, so reversed).
    int nv = 0;
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next)
        nv++;
    ComptimeVar **vars = nv > 0 ? alloca(nv * sizeof(ComptimeVar *)) : NULL;
    if (nv > 0) {
        int idx = nv - 1;
        for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next)
            vars[idx--] = cv;
    }

    // Inject comptime variable declarations as file-scope globals.
    // Vars routed through __builtin_comptime_init (ticket #191/#192) have their
    // initializer stripped so the parser never sees a non-constant expression
    // as a global initializer (which would hard-error via eval2). The stripped
    // var is declared as a zero-initialized global; __builtin_comptime_init fills
    // it in at VM run time.
    // Uninitialised vars, and aggregate vars whose initializer is constant or
    // whose tag cannot be synthesized, are injected as-is (constant path).
    for (int i = 0; i < nv; i++) {
        ComptimeVar *cv = vars[i];
        if (comptime_var_uses_init_fn(cv)) {
            // Strip initializer; __builtin_comptime_init will assign it.
            Token *eq = find_top_level_eq(cv->decl_tokens);
            cur = append_decl_stripped(vm, cur, cv->decl_tokens, eq);
        } else {
            cur = append_token_list(vm, cur, cv->decl_tokens);
        }
    }

    for (int i = 0; i < count; i++)
        cur = append_macro_prototype(vm, cur, macros[i]);
    for (int i = 0; i < count; i++) {
        if (!vm->compiler.allow_comptime_pp_bleed)
            cur = cur->next = new_macro_scope_marker(vm, TK_MACRO_SCOPE_PUSH, macros[i]->body_tokens);
        cur = append_macro_definition(vm, cur, macros[i]);
        if (!vm->compiler.allow_comptime_pp_bleed)
            cur = cur->next = new_macro_scope_marker(vm, TK_MACRO_SCOPE_POP, macros[i]->body_tokens);
    }

    // Synthesized init function: runs after bytecode is compiled to evaluate
    // scalar comptime var initializers that call comptime functions.
    Token *init_fn = build_comptime_init_fn_tokens(vm, vars, nv);
    if (init_fn)
        cur = append_token_list(vm, cur, init_fn);

    Token *tmpl = count > 0 ? macros[count - 1]->body_tokens : NULL;
    cur->next = new_macro_eof(vm, tmpl);
    return head.next;
}

static Obj *find_macro_function(Obj *prog, const char *name) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function && obj->body &&
            strlen(obj->name) == strlen(name) &&
            strncmp(obj->name, name, strlen(name)) == 0)
            return obj;
    }
    return NULL;
}

static Obj *find_macro_global(Obj *prog, const char *name) {
    size_t len = strlen(name);
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function && strlen(obj->name) == len &&
            strncmp(obj->name, name, len) == 0)
            return obj;
    }
    return NULL;
}

static Obj *find_macro_obj(Obj *prog, const char *name) {
    size_t len = strlen(name);
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->name && strlen(obj->name) == len &&
            strncmp(obj->name, name, len) == 0)
            return obj;
    }
    return NULL;
}

// Read a scalar value from the macro VM's data segment.
// Valid after init_macro_globals has allocated storage; the value may have
// been written by a constant-initializer memcpy, by __builtin_comptime_init, or
// left as zero (no initializer).
static bool read_comptime_scalar(VirtualMachine *vm, Obj *obj, bool *is_float_out,
                                 int64_t *int_out, double *float_out) {
    if (!obj)
        return false;
    char *base = vm->data_seg + obj->offset;
    TypeKind kind = obj->ty->kind;
    switch (kind) {
    case TY_FLOAT:
        *is_float_out = true;
        *float_out = (double)(*(float *)base);
        return true;
    case TY_DOUBLE:
    case TY_LDOUBLE:
        *is_float_out = true;
        *float_out = *(double *)base;
        return true;
    default:
        if (obj->ty->size == 1) { *int_out = obj->ty->is_unsigned ? (int64_t)*(uint8_t *)base  : (int64_t)*(int8_t *)base; }
        else if (obj->ty->size == 2) { *int_out = obj->ty->is_unsigned ? (int64_t)*(uint16_t *)base : (int64_t)*(int16_t *)base; }
        else if (obj->ty->size == 4) { *int_out = obj->ty->is_unsigned ? (int64_t)*(uint32_t *)base : (int64_t)*(int32_t *)base; }
        else { *int_out = (int64_t)*(int64_t *)base; }
        *is_float_out = false;
        return true;
    }
}

static Obj *make_comptime_shadow_obj(VirtualMachine *vm, Obj *src) {
    if (!vm || !src || !src->ty || src->ty->size <= 0)
        return NULL;

    Obj *dst = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(dst, 0, sizeof(Obj));
    dst->name = arena_format(vm, ".L.comptime.%d",
                             vm->compiler.unique_name_counter++);
    dst->display_name = dst->name;
    dst->ty = src->ty;
    dst->align = src->ty->align;
    dst->tok = src->tok;
    dst->is_static = true;
    dst->is_definition = true;
    dst->is_macro_generated = true;
    dst->init_data = arena_alloc(&vm->compiler.parser_arena, src->ty->size);
    memcpy(dst->init_data, vm->data_seg + src->offset, src->ty->size);
    return dst;
}

static void link_comptime_shadow_objs(VirtualMachine *vm) {
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        Obj *obj = cv->ptr_obj;
        if (!obj || obj->next)
            continue;
        obj->next = vm->compiler.globals;
        vm->compiler.globals = obj;
    }
}

// Execute the synthesized __builtin_comptime_init function (if present) to
// evaluate scalar comptime variable initializers that call comptime
// functions. Must be called after gen_function + patch_macro_call_addresses
// so all bytecode and call targets are resolved.
static void run_comptime_var_initializers(VirtualMachine *vm, Obj *macro_prog) {
    Obj *init_fn = find_macro_function(macro_prog, "__builtin_comptime_init");
    if (!init_fn)
        return;

    if (vm->debug_vm)
        printf("Running __builtin_comptime_init for comptime variable initializers...\n");

    VirtualMachine *saved_current_vm = __builtin_current_vm;
    __builtin_current_vm = vm;

    Pc      saved_pc   = vm->pc;
    long long *saved_sp   = vm->sp;
    long long *saved_bp   = vm->bp;
    long long  saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    Obj *saved_current_fn = vm->compiler.current_fn;

    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;
    *(--vm->sp) = 0; // sentinel return address

    vm->pc = (Pc)init_fn->code_addr;

    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0;
    int fenv_saved_round;
    fenv_barrier_begin(&fenv_saved_round);
    int eval_rc = vm_eval(vm);
    fenv_barrier_end(fenv_saved_round);
    vm->debug_vm = saved_debug;

    __builtin_current_vm = saved_current_vm;

    vm->pc            = saved_pc;
    vm->sp            = saved_sp;
    vm->bp            = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    vm->compiler.current_fn = saved_current_fn;

    if (eval_rc == CCCC_HOST_SIGNAL_RC)
        error_tok(vm, init_fn->tok,
                  "compile-time execution terminated by host signal %d",
                  vm->dbg.host_fault_signal);

    if (vm->debug_vm)
        printf("__builtin_comptime_init completed.\n");
}

static void evaluate_comptime_vars(VirtualMachine *vm, Obj *macro_prog) {
    for (ComptimeVar *cv = vm->compiler.comptime_vars; cv; cv = cv->next) {
        if (cv->is_evaluated)
            continue;

        Obj *obj = find_macro_global(macro_prog, cv->name);
        if (!obj) {
            fprintf(stderr, "Warning: comptime var '%s' not found in macro program\n",
                    cv->name);
            continue;
        }

        // Pointer vars create relocations — rejected at preprocess time, but
        // guard here too in case something slips through.
        if (obj->rel) {
            fprintf(stderr, "Warning: comptime var '%s' has a relocation "
                    "(pointer/string vars are not yet supported)\n", cv->name);
            continue;
        }

        TypeKind kind = obj->ty->kind;
        cv->ptr_obj = make_comptime_shadow_obj(vm, obj);
        if (kind == TY_STRUCT || kind == TY_UNION) {
            // Routed vars: __builtin_comptime_init wrote the bytes
            // into the data segment, so init_data is NULL — that is expected.
            // Non-routed vars (constant path): init_data must be present.
            if (!obj->init_data && !comptime_var_uses_init_fn(cv)) {
                fprintf(stderr,
                        "Warning: comptime struct/union var '%s' has a "
                        "non-constant initializer that could not be routed "
                        "through __builtin_comptime_init\n",
                        cv->name);
                continue;
            }

            cv->is_struct = true;
            char *base = vm->data_seg + obj->offset;
            for (Member *mem = obj->ty->members; mem; mem = mem->next) {
                if (mem->is_bitfield)
                    continue;
                TypeKind mk = mem->ty->kind;
                // Only scalar integer and float members are exposed.
                if (mk == TY_STRUCT || mk == TY_UNION || mk == TY_ARRAY ||
                    mk == TY_PTR)
                    continue;

                ComptimeVarMember *m =
                    arena_alloc(&vm->compiler.parser_arena, sizeof(ComptimeVarMember));
                memset(m, 0, sizeof(ComptimeVarMember));
                if (mem->name) {
                    char *mname = arena_alloc(&vm->compiler.parser_arena,
                                             mem->name->len + 1);
                    memcpy(mname, mem->name->loc, mem->name->len);
                    mname[mem->name->len] = '\0';
                    m->name = mname;
                }

                char *mbase = base + mem->offset;
                if (mk == TY_FLOAT) {
                    m->is_float = true;
                    m->float_val = (double)(*(float *)mbase);
                } else if (mk == TY_DOUBLE || mk == TY_LDOUBLE) {
                    m->is_float = true;
                    m->float_val = *(double *)mbase;
                } else {
                    int sz = mem->ty->size;
                    if (sz == 1) m->int_val = mem->ty->is_unsigned ? (int64_t)*(uint8_t *)mbase  : (int64_t)*(int8_t *)mbase;
                    else if (sz == 2) m->int_val = mem->ty->is_unsigned ? (int64_t)*(uint16_t *)mbase : (int64_t)*(int16_t *)mbase;
                    else if (sz == 4) m->int_val = mem->ty->is_unsigned ? (int64_t)*(uint32_t *)mbase : (int64_t)*(int32_t *)mbase;
                    else m->int_val = *(int64_t *)mbase;
                }

                m->next = cv->members;
                cv->members = m;
            }
        } else {
            // Scalar: read from data segment unconditionally. The value is
            // valid after init_macro_globals allocates storage — it was either
            // written by a constant-initializer memcpy (init_data path), by
            // __builtin_comptime_init (ticket #191 non-constant path), or is zero
            // for an uninitialised var.
            read_comptime_scalar(vm, obj, &cv->is_float, &cv->int_val, &cv->float_val);
        }

        cv->is_evaluated = true;

        if (vm->debug_vm) {
            if (cv->is_struct)
                printf("Evaluated comptime struct '%s'\n", cv->name);
            else if (cv->is_float)
                printf("Evaluated comptime var '%s' = %f\n", cv->name, cv->float_val);
            else
                printf("Evaluated comptime var '%s' = %lld\n", cv->name, cv->int_val);
        }
    }
}

static void init_macro_globals(VirtualMachine *vm, Obj *macro_prog) {
    int num_globals = 0;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            num_globals++;
    }

    if (num_globals == 0)
        return;

    Obj **globals_arr = alloca(num_globals * sizeof(Obj *));
    int idx = num_globals - 1;
    for (Obj *var = macro_prog; var; var = var->next) {
        if (!var->is_function)
            globals_arr[idx--] = var;
    }

    for (int i = 0; i < num_globals; i++) {
        Obj *var = globals_arr[i];

        long long offset = vm->data_ptr - vm->data_seg;
        offset = (offset + 7) & ~7;
        vm->data_ptr = vm->data_seg + offset;
        if (vm_data_ensure(vm, var->ty->size) != 0)
            error("codegen: data segment overflow (limit: %d bytes)", vm->poolsize_max);
        var->offset = vm->data_ptr - vm->data_seg;

        if (var->init_data)
            memcpy(vm->data_ptr, var->init_data, var->ty->size);

        vm->data_ptr += var->ty->size;
    }
}

// Apply relocations for macro global initializers that reference other
// globals or comptime functions. Must run after gen_function has assigned
// code_addr to every macro function (Step 2), since function-pointer table
// entries resolve to text-segment addresses. Patches vm->data_seg directly
// and never touches vm->compiler.data_relocs — these relocations are
// VM-internal and must not pollute the runtime bytecode relocation table.
static void apply_macro_global_relocations(VirtualMachine *vm, Obj *macro_prog) {
    for (Obj *var = macro_prog; var; var = var->next) {
        if (var->is_function)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid macro function global relocation");

            Obj *target = find_macro_obj(macro_prog, *rel->label);
            if (!target)
                error("undefined macro function global relocation target: %s",
                      *rel->label);

            long long data_offset = var->offset + rel->offset;
            if (target->is_function) {
                if (!target->body)
                    error("unsupported macro relocation to undefined function: %s",
                          target->name);
                *(long long *)(vm->data_seg + data_offset) =
                    cc_pc_to_byte_offset((Pc)target->code_addr) + rel->addend;
            } else {
                *(long long *)(vm->data_seg + data_offset) =
                    (long long)(vm->data_seg + target->offset + rel->addend);
            }
        }
    }
}

static void patch_macro_call_addresses(VirtualMachine *vm, Obj *macro_prog) {
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj *fn = vm->compiler.call_patches[i].function;
        Pc loc = vm->compiler.call_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function in macro bytecode: %s", fn->name);
        vm->text_seg[loc] = (Pc)fn_def->code_addr;
    }

    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *fn = vm->compiler.func_addr_patches[i].function;
        Pc loc = vm->compiler.func_addr_patches[i].location;
        Obj *fn_def = find_macro_function(macro_prog, fn->name);
        if (!fn_def)
            error("undefined function address in macro bytecode: %s",
                  fn->name);
        cc_write_i64_at(vm, loc, cc_pc_to_byte_offset((Pc)fn_def->code_addr));
    }
}

// hashmap_foreach callback: delete the key from the macros table so that
// include guard macros are not visible during the comptime preprocessing pass.
// This allows @shared headers (and their transitive includes) to be fully
// re-included in the comptime context. The macro table is already snapshotted
// before compile_macro_program runs and is restored afterward.
static int undefine_guard_macro_iter(char *key, int keylen, void *val,
                                     void *user_data) {
    (void)keylen; (void)val;
    hashmap_delete((HashMap *)user_data, key);
    return 0; // continue
}

// Compile all macro functions and comptime helpers as one compile-time program so
// macro bytecode can make ordinary function calls across the whole set.
// Shared failure-path teardown for compile_macro_program: unwinds every bit
// of compiler state it pushed before attempting to compile the macro
// program, so a failed comptime compile leaves the runtime TU exactly as it
// would have been without the attempt. Used by all three of that function's
// abort points (parse-returned-NULL, a captured MacroFn missing from the
// parsed program, and -- #887 -- the comptime program having collected
// parse/type errors via error_tok_recover despite parse() still returning a
// non-NULL tree).
static void restore_macro_compile_state(VirtualMachine *vm, Obj *saved_locals,
                                        Obj *saved_current_fn, Obj *saved_globals,
                                        Scope *saved_scope, int saved_num_call_patches,
                                        int saved_num_func_addr_patches,
                                        HashMap saved_macros) {
    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.globals = saved_globals;
    for (Scope *sc = vm->compiler.scope; sc != saved_scope; sc = sc->next) {
        hashmap_deinit_borrowed(&sc->var_map);
        hashmap_deinit_borrowed(&sc->tag_map);
    }
    vm->compiler.scope = saved_scope;
    vm->compiler.in_macro_mode = false;
    vm->compiler.num_call_patches = saved_num_call_patches;
    vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
    vm->compiler.has_macro_snapshot = false;
    hashmap_restore(&vm->compiler.macros, saved_macros);
}

static bool compile_macro_program(VirtualMachine *vm) {
    int count = 0;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next)
        count++;
    if (count == 0)
        return true;

    MacroFn **macros = alloca(count * sizeof(MacroFn *));
    int idx = count - 1;
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next)
        macros[idx--] = pm;

    Obj *saved_locals = vm->compiler.locals;
    Obj *saved_current_fn = vm->compiler.current_fn;
    Obj *saved_globals = vm->compiler.globals;
    Scope *saved_scope = vm->compiler.scope;
    int saved_num_call_patches = vm->compiler.num_call_patches;
    int saved_num_func_addr_patches = vm->compiler.num_func_addr_patches;
    // Snapshot the preprocessor macro table so that #define directives emitted
    // by reflection.h, comptime-only includes, or comptime function bodies do
    // not persist into the runtime translation unit after this pass completes.
    // The snapshot is taken before isolation so the runtime TU always gets its
    // full original macro state back regardless of what the comptime pass does.
    vm->compiler.macro_snapshot_backup = hashmap_snapshot(&vm->compiler.macros);
    vm->compiler.has_macro_snapshot = true;
    HashMap saved_macros = vm->compiler.macro_snapshot_backup;

    // Isolate the comptime macro state: strip ALL source-file #define macros
    // (primary file and any included headers), keeping only CCCC builtins and
    // command-line -D defines (define_tok == NULL). This gives the comptime
    // pass a clean context where primary-file user #defines are NOT visible.
    // Users who need a #define in comptime code must put it in an @shared
    // header. @shared / @comptime macros removed here are re-added when their
    // queued re-includes are re-preprocessed below. Must run before
    // implicit_reflection_tokens() so that reflection.h macros (added
    // afterward) are not deleted. --comptime-include-all disables isolation
    // entirely (legacy: forward everything). (tickets #552, #627)
    if (!vm->compiler.comptime_include_all)
        isolate_comptime_macros(vm);

    // Suppress user-defined TK_*/NK_* macros for the ENTIRE comptime preprocess
    // pass (both implicit_reflection_tokens and the second preprocess below).
    // Primary-file macros like SQLite's #define TK_FLOAT 154 survive
    // gate_runtime_only_macros; without this they expand inside reflection.h's
    // TypeKind/NodeKind enum initializers causing "expected an identifier".
    // The macro_snapshot_backup taken above will restore them at the end.
    void *saved_enum_macros_cmp[REFLECTION_ENUM_NAMES_COUNT];
    reflection_enum_names_hide(vm, saved_enum_macros_cmp);

    vm->compiler.in_macro_mode = true;
    vm->compiler.locals = NULL;
    vm->compiler.globals = NULL;
    vm->compiler.num_call_patches = 0;
    vm->compiler.num_func_addr_patches = 0;

    Token *reflection_tokens = implicit_reflection_tokens(vm);
    Token *tokens =
        build_combined_macro_tokens(vm, reflection_tokens, macros, count);

    // Re-stamping during this preprocess pass would otherwise apply the
    // user TU's -W flags to reflection.h's internal implementation tokens
    // (e.g. VM expansions), producing warnings the user can't fix. Hard
    // errors (error_tok) are unaffected since they aren't gated by
    // vm->compiler.warnings.
    uint64_t saved_warnings = vm->compiler.warnings;
    uint64_t saved_werror = vm->compiler.warning_errors;
    vm->compiler.warnings = 0;
    vm->compiler.warning_errors = 0;
    // Isolate the comptime preprocessing from the runtime's include guard state
    // so that @shared headers (and any transitive dependencies) can be fully
    // re-included in the comptime context. The runtime preprocessing is already
    // complete at this point, so modifying these maps does not affect runtime code.
    // The macro table is already snapshotted (line above) and will be restored,
    // so undeclaring guard macros here is safe.
    HashMap saved_pragma_once = vm->compiler.pragma_once;
    HashMap saved_include_guards = vm->compiler.include_guards;
    vm->compiler.pragma_once = (HashMap){};
    vm->compiler.include_guards = (HashMap){};
    hashmap_foreach(&vm->compiler.guard_macros, undefine_guard_macro_iter,
                    &vm->compiler.macros);
    // #887: any error collected while preprocessing/parsing the comptime
    // program (e.g. an undefined identifier caught by error_tok_recover)
    // must abort the comptime compile rather than let a program built from
    // ty_error placeholder nodes reach gen_function and execute as garbage
    // bytecode. collect_errors defaults on (src/main.c), so parse() can
    // return a non-NULL tree that still isn't safe to compile.
    int saved_error_count = vm->error_count;
    tokens = preprocess(vm, tokens);
    // Restore include guard state; the comptime-specific maps own their key copies
    // (via hashmap_put) so must be freed with hashmap_deinit, not _borrowed.
    hashmap_deinit(&vm->compiler.pragma_once);
    hashmap_deinit(&vm->compiler.include_guards);
    vm->compiler.pragma_once = saved_pragma_once;
    vm->compiler.include_guards = saved_include_guards;
    Obj *macro_prog = parse(vm, tokens);
    vm->compiler.warnings = saved_warnings;
    vm->compiler.warning_errors = saved_werror;
    if (!macro_prog || vm->error_count > saved_error_count) {
        restore_macro_compile_state(vm, saved_locals, saved_current_fn,
                                    saved_globals, saved_scope,
                                    saved_num_call_patches,
                                    saved_num_func_addr_patches, saved_macros);
        return false;
    }
    vm->compiler.macro_context_scope = vm->compiler.scope;

    for (int i = 0; i < count; i++) {
        Obj *func = find_macro_function(macro_prog, macros[i]->name);
        if (!func) {
            if (vm->debug_vm)
                fprintf(stderr,
                        "Could not find macro function '%s' after parsing\n",
                        macros[i]->name);
            restore_macro_compile_state(vm, saved_locals, saved_current_fn,
                                        saved_globals, saved_scope,
                                        saved_num_call_patches,
                                        saved_num_func_addr_patches, saved_macros);
            return false;
        }
        macros[i]->compiled_fn = func;
        macros[i]->is_compiled = true;
    }

    // Step 1: allocate data segment storage for all globals and memcpy any
    //         constant initializer bytes (init_data path).
    init_macro_globals(vm, macro_prog);

    // Step 2: generate bytecode for all functions, including the synthesized
    //         __builtin_comptime_init helper produced by build_combined_macro_tokens.
    for (Obj *fn = macro_prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body)
            gen_function(vm, fn);
    }

    // Step 3: apply global relocations now that every macro function has a
    //         code_addr, so static initializer tables can reference comptime
    //         function addresses (ticket #309).
    apply_macro_global_relocations(vm, macro_prog);

    // Step 4: patch call addresses so __builtin_comptime_init can call comptime fns.
    patch_macro_call_addresses(vm, macro_prog);

    // Step 5: run __builtin_comptime_init to evaluate scalar comptime var
    //         initializers (ticket #191). This writes results into the data
    //         segment via normal VM store instructions.
    run_comptime_var_initializers(vm, macro_prog);

    // Step 6: read comptime var values out of the data segment.
    evaluate_comptime_vars(vm, macro_prog);

    vm->compiler.locals = saved_locals;
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.globals = saved_globals;
    vm->compiler.scope = saved_scope;
    vm->compiler.in_macro_mode = false;
    vm->compiler.num_call_patches = saved_num_call_patches;
    vm->compiler.num_func_addr_patches = saved_num_func_addr_patches;
    vm->compiler.has_macro_snapshot = false;
    hashmap_restore(&vm->compiler.macros, saved_macros);
    link_comptime_shadow_objs(vm);

    if (vm->debug_vm) {
        for (int i = 0; i < count; i++) {
            printf("Compiled compile-time function '%s' at code address %lld\n",
                   macros[i]->name, macros[i]->compiled_fn->code_addr);
        }
    }

    return true;
}

// #884: a bodyless `[[cccc::comptime]] ret name(params);` declaration is a
// no-op (forward declarations are unnecessary -- compile_all_macros already
// emits prototypes for every captured function before any definition). But
// if the name was never captured with an attributed definition, that's a
// mistake worth flagging rather than silently compiling nothing. Called from
// both compile_all_macros (the lazy, call-triggered path) and the top of
// cc_execute_inline_macros (which also runs when macro_fns is empty --
// otherwise a declared-but-unused, entirely bodyless comptime function would
// never be checked at all).
static void check_comptime_decls_defined(VirtualMachine *vm) {
    for (ComptimeDeclRecord *d = vm->compiler.comptime_decls; d; d = d->next) {
        bool defined = false;
        for (MacroFn *p = vm->compiler.macro_fns; p; p = p->next) {
            if (p->is_macro_entry && strcmp(p->name, d->name) == 0) {
                defined = true;
                break;
            }
        }
        if (!defined)
            error_tok(vm, d->tok,
                      "comptime function '%s' declared but never defined",
                      d->name);
    }
}

// Compile all macro functions and comptime helpers (idempotent)
static void compile_all_macros(VirtualMachine *vm) {
    if (vm->compiler.no_comptime)
        return;

    check_comptime_decls_defined(vm);

    if (!vm->compiler.macro_fns)
        return;
    // Guard: compile once even if called from both the pre-parse inline phase
    // and the post-parse cc_expand_macros phase.
    if (vm->compiler.macro_fns_compiled)
        return;
    vm->compiler.macro_fns_compiled = true;

    if (vm->debug_vm)
        printf("Compiling %d compile-time function(s)...\n", ({
                   int n = 0;
                   for (MacroFn *p = vm->compiler.macro_fns; p;
                        p = p->next)
                       n++;
                   n;
               }));

    // Register reflection API as FFI
    register_reflection_ffi(vm);

    // #887: compile_macro_program's own failure paths (including the
    // collected-error check) already report the real diagnostics via the
    // normal error-collection mechanism, printed in source order once the
    // caller reaches cc_print_all_errors. Printing a generic warning here
    // too puts a content-free banner ahead of (and out of order with) the
    // actual errors it's describing; keep it debug-only.
    if (!compile_macro_program(vm) && vm->debug_vm)
        fprintf(stderr, "Warning: Failed to compile macro functions\n");
}

static void setup_macro_call_slots(VirtualMachine *vm, long long *fixed_args,
                                   int fixed_count) {
    for (int i = fixed_count - 1; i >= 8; i--)
        *(--vm->sp) = fixed_args[i];
    for (int i = 0; i < fixed_count && i < 8; i++)
        vm->regs[REG_A0 + i] = fixed_args[i];
}

// Execute a macro function and return the generated AST node
static Node *execute_macro_fn(VirtualMachine *vm, MacroFn *pm, Token *call_tok,
                              Node *args, int arg_count,
                              long long *fixed_arg_values,
                              int fixed_arg_count) {
    if (!pm || !pm->is_compiled || !pm->compiled_fn)
        return NULL;

    if (pm->is_variadic && arg_count < pm->fixed_param_count)
        error_tok(vm, call_tok,
                  "macro '%s' called with %d arguments; expected at least %d",
                  pm->name, arg_count, pm->fixed_param_count);

    if (vm->debug_vm)
        printf("Executing macro function '%s' with %d args...\n", pm->name,
               arg_count);

    // Set the compiler-internal VM global that every __builtin_* in
    // reflection.c reads instead of taking a VirtualMachine* parameter.
    VirtualMachine *saved_current_vm = __builtin_current_vm;
    __builtin_current_vm = vm;

    // Save VM execution state (including current_fn so a macro that calls
    // __builtin_ast_push_fn without a matching pop cannot leak context).
    Pc saved_pc = vm->pc;
    long long *saved_sp = vm->sp;
    long long *saved_bp = vm->bp;
    long long saved_regs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    Obj *saved_current_fn = vm->compiler.current_fn;
    Token *saved_macro_call_tok = vm->compiler.macro_call_tok;
    Node **saved_vararg_nodes = vm->compiler.macro_vararg_nodes;
    char **saved_vararg_strs = vm->compiler.macro_vararg_strs;
    int saved_vararg_count = vm->compiler.macro_vararg_count;
    bool saved_vararg_string_mode = vm->compiler.macro_vararg_string_mode;
    vm->compiler.macro_call_tok = call_tok;

    // Reset stack for macro execution
    vm->sp = vm->initial_sp;
    vm->bp = vm->initial_bp;

    // Pass fixed arguments using the VM calling convention.
    // Inline macro arguments are Node* pointers to the AST nodes. Global
    // generation callers preload char* fixed arguments and set string-mode
    // varargs before entering this helper.
    if (fixed_arg_values) {
        setup_macro_call_slots(vm, fixed_arg_values, fixed_arg_count);
    } else if (args) {
        int fixed_count = pm->is_variadic ? pm->fixed_param_count : arg_count;
        long long *fixed_args =
            fixed_count > 0 ? alloca((size_t)fixed_count * sizeof(long long))
                            : NULL;
        Node *arg = args;
        for (int i = 0; i < fixed_count; i++) {
            fixed_args[i] = (long long)arg;
            if (arg)
                arg = arg->next;
        }
        setup_macro_call_slots(vm, fixed_args, fixed_count);

        if (pm->is_variadic) {
            int var_count = arg_count - pm->fixed_param_count;
            Node **var_nodes = var_count > 0 ? alloca(var_count * sizeof(Node *)) : NULL;
            for (int i = 0; i < var_count; i++) {
                var_nodes[i] = arg;
                if (arg)
                    arg = arg->next;
            }
            vm->compiler.macro_vararg_nodes = var_nodes;
            vm->compiler.macro_vararg_strs = NULL;
            vm->compiler.macro_vararg_count = var_count;
            vm->compiler.macro_vararg_string_mode = false;
        } else {
            vm->compiler.macro_vararg_nodes = NULL;
            vm->compiler.macro_vararg_strs = NULL;
            vm->compiler.macro_vararg_count = 0;
            vm->compiler.macro_vararg_string_mode = false;
        }
    }

    // Push sentinel return address (0) so we can detect when function
    // returns
    *(--vm->sp) = 0;

    // Set PC to function entry point
    vm->pc = (Pc)pm->compiled_fn->code_addr;

    // Execute the macro function
    int saved_debug = vm->debug_vm;
    vm->debug_vm = 0; // Disable debug output during macro execution
    int fenv_saved_round;
    fenv_barrier_begin(&fenv_saved_round);
    int eval_rc = vm_eval(vm);
    fenv_barrier_end(fenv_saved_round);
    vm->debug_vm = saved_debug;

    // Get the returned Node* from regs[REG_A0]. Void macros do not produce a
    // meaningful VM return value; treat them as side-effect-only.
    Node *result = pm->is_void_macro ? NULL : (Node *)vm->regs[REG_A0];

    // Restore whatever was current before this macro call (normally the
    // same vm, seeded by cc_init for the whole compile -- see src/vm.c).
    __builtin_current_vm = saved_current_vm;

    // Restore VM execution state (current_fn last so it overrides any leaked
    // push_fn call that wasn't matched by a pop_fn inside the macro).
    vm->pc = saved_pc;
    vm->sp = saved_sp;
    vm->bp = saved_bp;
    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    vm->compiler.current_fn = saved_current_fn;
    vm->compiler.macro_call_tok = saved_macro_call_tok;
    vm->compiler.macro_vararg_nodes = saved_vararg_nodes;
    vm->compiler.macro_vararg_strs = saved_vararg_strs;
    vm->compiler.macro_vararg_count = saved_vararg_count;
    vm->compiler.macro_vararg_string_mode = saved_vararg_string_mode;

    if (eval_rc == CCCC_HOST_SIGNAL_RC)
        error_tok(vm, call_tok,
                  "compile-time macro execution terminated by host signal %d",
                  vm->dbg.host_fault_signal);

    if (vm->debug_vm && result)
        printf("Macro function '%s' returned node of kind %d\n", pm->name,
               result->kind);

    return result;
}

void cc_execute_attribute_macro(VirtualMachine *vm, MacroFn *pm, Token *tok,
                                AttrTarget *target, Node *args,
                                int arg_count) {
    if (!vm || !pm || !target)
        return;
    if (!pm->is_attribute_handler) {
        error_tok(vm, tok, "macro '%s' is not a custom attribute handler",
                  pm->name);
        return;
    }

    init_vm_segments_for_macros(vm);
    compile_all_macros(vm);

    if (!pm->is_compiled) {
        error_tok(vm, tok, "attribute macro '%s' failed to compile",
                  pm->name);
        return;
    }

    int total_args = arg_count + 1;
    if (!pm->is_variadic && pm->fixed_param_count != total_args)
        error_tok(vm, tok,
                  "attribute macro '%s' called with %d arguments; expected %d",
                  pm->name, total_args, pm->fixed_param_count);
    if (pm->is_variadic && total_args < pm->fixed_param_count)
        error_tok(vm, tok,
                  "attribute macro '%s' called with %d arguments; expected at least %d",
                  pm->name, total_args, pm->fixed_param_count);

    int fixed_count = pm->is_variadic ? pm->fixed_param_count : total_args;
    long long *fixed_values =
        fixed_count > 0 ? alloca((size_t)fixed_count * sizeof(long long)) : NULL;
    if (fixed_count > 0)
        fixed_values[0] = (long long)target;

    Node *arg = args;
    for (int i = 1; i < fixed_count; i++) {
        fixed_values[i] = (long long)arg;
        if (arg)
            arg = arg->next;
    }

    Node **saved_vararg_nodes = vm->compiler.macro_vararg_nodes;
    char **saved_vararg_strs = vm->compiler.macro_vararg_strs;
    int saved_vararg_count = vm->compiler.macro_vararg_count;
    bool saved_vararg_string_mode = vm->compiler.macro_vararg_string_mode;

    if (pm->is_variadic) {
        int var_count = total_args - pm->fixed_param_count;
        Node **var_nodes =
            var_count > 0 ? alloca((size_t)var_count * sizeof(Node *)) : NULL;
        for (int i = 0; i < var_count; i++) {
            var_nodes[i] = arg;
            if (arg)
                arg = arg->next;
        }
        vm->compiler.macro_vararg_nodes = var_nodes;
        vm->compiler.macro_vararg_strs = NULL;
        vm->compiler.macro_vararg_count = var_count;
        vm->compiler.macro_vararg_string_mode = false;
    } else {
        vm->compiler.macro_vararg_nodes = NULL;
        vm->compiler.macro_vararg_strs = NULL;
        vm->compiler.macro_vararg_count = 0;
        vm->compiler.macro_vararg_string_mode = false;
    }

    Node *result = execute_macro_fn(vm, pm, tok, NULL, total_args,
                                    fixed_values, fixed_count);
    (void)result;

    vm->compiler.macro_vararg_nodes = saved_vararg_nodes;
    vm->compiler.macro_vararg_strs = saved_vararg_strs;
    vm->compiler.macro_vararg_count = saved_vararg_count;
    vm->compiler.macro_vararg_string_mode = saved_vararg_string_mode;
}

// Find macro function by name
static MacroFn *find_macro_fn_by_name(VirtualMachine *vm, const char *name) {
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == strlen(name) &&
            strncmp(pm->name, name, strlen(name)) == 0)
            return pm;
    }
    return NULL;
}

// Ticket #277: Lisp-style single-step macro expansion (macroexpand-1).
// Expands the outermost ND_MACRO_CALL exactly once; identity for anything else.
Node *__builtin_macroexpand_1(Node *node) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !node)
        return node;
    if (node->kind != ND_MACRO_CALL)
        return node;
    MacroFn *pm = find_macro_fn_by_name(vm, node->macro_name);
    if (!pm || !pm->is_compiled)
        return node;
    Scope *saved_scope = vm->compiler.scope;
    if (node->macro_scope)
        vm->compiler.scope = node->macro_scope;
    Node *result = execute_macro_fn(vm, pm, node->tok, node->args,
                                    node->macro_arg_count, NULL, 0);
    vm->compiler.scope = saved_scope;
    return result ? result : node;
}

// Ticket #277: Lisp-style full macro expansion (macroexpand).
// Repeatedly expands the outermost node via macroexpand_1 until it is no
// longer an ND_MACRO_CALL (i.e. until the form is stable at the top level).
// Does not recurse into child nodes — only the top-level call is expanded.
Node *__builtin_macroexpand(Node *node) {
    VirtualMachine *vm = __builtin_current_vm;
    if (!vm || !node)
        return node;
    int limit = vm->compiler.macro_recursion_limit;
    int depth = 0;
    Node *current = node;
    while (current && current->kind == ND_MACRO_CALL) {
        if (limit > 0 && depth >= limit) {
            error_tok(vm, current->tok,
                      "macroexpand: recursion limit exceeded expanding "
                      "'%s' (depth %d, limit %d)",
                      current->macro_name, depth + 1, limit);
            return current;
        }
        Node *next = __builtin_macroexpand_1(current);
        if (next == current)
            break;
        current = next;
        depth++;
    }
    return current;
}

void cc_execute_top_level_macro(VirtualMachine *vm, char *name, Token *tok,
                                       Node *args, int arg_count) {
    if (!vm || !name)
        return;

    MacroFn *pm = find_macro_fn_by_name(vm, name);
    if (!pm) {
        error_tok(vm, tok, "undefined macro: %s", name);
        return;
    }

    init_vm_segments_for_macros(vm);
    compile_all_macros(vm);

    if (!pm->is_compiled) {
        error_tok(vm, tok, "macro '%s' failed to compile", name);
        return;
    }

    Node *result = execute_macro_fn(vm, pm, tok, args, arg_count, NULL, 0);
    // Declaration position: NULL (or void return) is legal — the macro may have
    // emitted definitions as side-effects without having a node to splice.
    (void)result;
}

// Recursively transform macro calls in an AST node
static Node *transform_node(VirtualMachine *vm, Node *node, int depth);

static Node *transform_node(VirtualMachine *vm, Node *node, int depth) {
    if (!node)
        return NULL;

    // Handle ND_MACRO_CALL - this is where the magic happens
    if (node->kind == ND_MACRO_CALL) {
        if (vm->debug_vm)
            printf("  Found ND_MACRO_CALL for '%s'\n", node->macro_name);

        MacroFn *pm = find_macro_fn_by_name(vm, node->macro_name);
        if (!pm) {
            error_tok(vm, node->tok, "undefined macro: %s",
                      node->macro_name);
            return node;
        }

        if (pm->is_void_macro) {
            error_tok(vm, node->tok,
                      "void macro '%s' cannot be used as an expression; "
                      "it only emits definitions",
                      node->macro_name);
            return node;
        }

        // Expression-position macros must return Node* — a plain C return type
        // (int, struct, etc.) cannot be spliced into the AST.
        if (pm->compiled_fn && pm->compiled_fn->ty && pm->compiled_fn->ty->return_ty &&
            pm->compiled_fn->ty->return_ty->kind != TY_PTR) {
            error_tok(vm, node->tok,
                      "comptime function '%s' returns a non-pointer type and cannot "
                      "be used in expression position; expression-position comptime "
                      "functions must return Node*",
                      node->macro_name);
            return node;
        }

        if (!pm->is_compiled) {
            error_tok(vm, node->tok, "macro '%s' failed to compile",
                      node->macro_name);
            return node;
        }

        int limit = vm->compiler.macro_recursion_limit;
        if (limit > 0 && depth >= limit) {
            error_tok(vm, node->tok,
                      "macro recursion limit exceeded while expanding "
                      "'%s' (depth %d, limit %d)",
                      node->macro_name, depth + 1, limit);
            return node;
        }

        // Execute the macro to get the replacement AST
        if (vm->debug_vm)
            printf("  Executing macro '%s'...\n", pm->name);

        Scope *saved_scope = vm->compiler.scope;
        if (node->macro_scope)
            vm->compiler.scope = node->macro_scope;
        Node *result =
            execute_macro_fn(vm, pm, node->tok, node->args,
                             node->macro_arg_count, NULL, 0);
        vm->compiler.scope = saved_scope;

        if (vm->debug_vm)
            printf("  Macro returned %p (kind=%d)\n", (void *)result,
                   result ? result->kind : -1);

        if (!result) {
            error_tok(vm, node->tok, "macro '%s' returned NULL",
                      node->macro_name);
            // Return a placeholder
            Node *placeholder =
                arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
            memset(placeholder, 0, sizeof(Node));
            placeholder->kind = ND_NUM;
            placeholder->val = 0;
            placeholder->ty = ty_int;
            placeholder->tok = node->tok;
            return placeholder;
        }

        // Run add_type on the result to ensure types are set
        add_type(vm, result);

        // Recursively transform in case the macro result contains more
        // macro calls
        return transform_node(vm, result, depth + 1);
    }

    // For ND_EXPR_STMT: if the inner expression is replaced by a statement-kind
    // node (e.g. a macro returned ND_IF, ND_BLOCK, ND_RETURN, ...), lift the
    // statement up to replace the entire expression-statement wrapper.
    // Without this, codegen would try to gen_expr() a statement node and fail.
    //
    // When lifting, preserve the sibling chain: any statements that follow this
    // EXPR_STMT in the enclosing block must continue executing after the lifted
    // statement.  We attach them via ->next so the body traversal above picks
    // them up in subsequent loop iterations.
    if (node->kind == ND_EXPR_STMT) {
        node->lhs = transform_node(vm, node->lhs, depth);
        if (node->lhs) {
            NodeKind k = node->lhs->kind;
            if (k == ND_RETURN || k == ND_IF || k == ND_FOR || k == ND_DO ||
                k == ND_SWITCH || k == ND_BLOCK || k == ND_GOTO ||
                k == ND_LABEL || k == ND_EXPR_STMT) {
                Node *lifted = node->lhs;
                // Re-attach the sibling chain so statements after the macro
                // call are not dropped.  Walk to the tail of the lifted node
                // so we don't clobber a non-NULL ->next on the lifted result
                // (e.g. a macro that returned a pre-linked chain).
                if (node->next) {
                    Node *tail = lifted;
                    while (tail->next) tail = tail->next;
                    tail->next = node->next;
                }
                return lifted;
            }
        }
        return node;
    }

    // Recursively transform all child nodes
    node->lhs = transform_node(vm, node->lhs, depth);
    node->rhs = transform_node(vm, node->rhs, depth);
    node->cond = transform_node(vm, node->cond, depth);
    node->then = transform_node(vm, node->then, depth);
    node->els = transform_node(vm, node->els, depth);
    node->init = transform_node(vm, node->init, depth);
    node->inc = transform_node(vm, node->inc, depth);

    // For ND_BLOCK, body is a chain of statements linked via ->next
    // We need to transform each statement in the chain
    if (node->body) {
        node->body = transform_node(vm, node->body, depth);
        // Also transform sibling statements in the chain
        for (Node *stmt = node->body; stmt; stmt = stmt->next) {
            if (stmt->next) {
                stmt->next = transform_node(vm, stmt->next, depth);
            }
        }
    }

    // Transform argument lists (also a chain)
    if (node->args) {
        node->args = transform_node(vm, node->args, depth);
        for (Node *arg = node->args; arg && arg->next; arg = arg->next) {
            arg->next = transform_node(vm, arg->next, depth);
        }
    }

    // Transform case lists for switch
    for (Node *c = node->case_next; c; c = c->case_next) {
        c->body = transform_node(vm, c->body, depth);
    }
    if (node->default_case) {
        node->default_case->body =
            transform_node(vm, node->default_case->body, depth);
    }

    return node;
}

// Initialize VM segments for macro compilation (extracted from cc_compile)
static void init_vm_segments_for_macros(VirtualMachine *vm) {
    if (vm->text_seg)
        return; // Already initialized

    // Reserve and commit all segments (base pointers will never move)
    vm_alloc_segments(vm);

    // Initialize codegen state
    vm->compiler.current_codegen_fn = NULL;
    // sp/bp/stack_base already set correctly by vm_alloc_segments

    // Initialize source map for debugger (if enabled). Mirrors the block in
    // cc_compile() (src/bytecode.c) -- macro/comptime compilation runs
    // before the main program's cc_compile(), so without this the first
    // emit_source_location() call during macro codegen finds
    // source_map_capacity == 0 and corrupts the heap (ticket #405).
    if (vm->flags & CCCC_ENABLE_DEBUGGER) {
        vm->dbg.source_map_capacity = 1024;
        vm->dbg.source_map = malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
        if (!vm->dbg.source_map) {
            error("could not malloc for source map");
        }
        vm->dbg.source_map_count = 0;
        vm->dbg.last_debug_file = NULL;
        vm->dbg.last_debug_line = -1;
        vm->dbg.source_index = NULL;
        vm->dbg.source_index_count = 0;
        vm->dbg.num_debug_symbols = 0;
        vm->dbg.num_watchpoints = 0;
    }
}

// ---------------------------------------------------------------------------
// Inline macro pre-parse execution
// ---------------------------------------------------------------------------

// Write a C-syntax type string into buf[0..bufsize).
// Returns number of characters written (not counting the NUL).
// Handles primitives, pointer chains, struct/union/enum tags, and falls back
// to "int" for unrepresentable types. Does NOT handle function-pointer or
// array return types — those are uncommon for generated function signatures.
static int write_type_str(Type *ty, char *buf, int bufsize) {
    if (!ty || bufsize <= 1)
        return 0;

    int n = 0;

    if (ty->is_const && bufsize - n > 6)
        n += snprintf(buf + n, bufsize - n, "const ");

    switch (ty->kind) {
    case TY_VOID:
        n += snprintf(buf + n, bufsize - n, "void");
        break;
    case TY_BOOL:
        n += snprintf(buf + n, bufsize - n, "_Bool");
        break;
    case TY_CHAR:
        n += snprintf(buf + n, bufsize - n, "%schar",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_SHORT:
        n += snprintf(buf + n, bufsize - n, "%sshort",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_INT:
        n += snprintf(buf + n, bufsize - n, "%sint",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_LONG:
        n += snprintf(buf + n, bufsize - n, "%slong",
                      ty->is_unsigned ? "unsigned " : "");
        break;
    case TY_FLOAT:
        n += snprintf(buf + n, bufsize - n, "float");
        break;
    case TY_DOUBLE:
        n += snprintf(buf + n, bufsize - n, "double");
        break;
    case TY_PTR:
        n += write_type_str(ty->base, buf + n, bufsize - n);
        n += snprintf(buf + n, bufsize - n, " *");
        break;
    case TY_NULLPTR_T:
        // nullptr_t has the same size/representation as a pointer.
        n += snprintf(buf + n, bufsize - n, "void *");
        break;
    case TY_STRUCT:
        if (ty->name)
            n += snprintf(buf + n, bufsize - n, "struct %.*s",
                          ty->name->len, ty->name->loc);
        else
            n += snprintf(buf + n, bufsize - n, "void /*anon struct*/");
        break;
    case TY_UNION:
        if (ty->name)
            n += snprintf(buf + n, bufsize - n, "union %.*s",
                          ty->name->len, ty->name->loc);
        else
            n += snprintf(buf + n, bufsize - n, "void /*anon union*/");
        break;
    case TY_ENUM:
        if (ty->name)
            n += snprintf(buf + n, bufsize - n, "enum %.*s",
                          ty->name->len, ty->name->loc);
        else
            n += snprintf(buf + n, bufsize - n, "int");
        break;
    default:
        // Fallback: emit int. Covers edge cases like TY_LDOUBLE, TY_ARRAY,
        // TY_FUNC return types, etc.
        n += snprintf(buf + n, bufsize - n, "int");
        break;
    }
    return n;
}

// Build a C prototype token stream for fn, e.g.:
//   int generated_func(void);
// Prepend these tokens to a file's token stream to give the parser a
// forward declaration without requiring the user to write one.
static Token *synthesize_forward_decl_tokens(VirtualMachine *vm, Obj *fn) {
    if (!fn || !fn->ty || fn->ty->kind != TY_FUNC)
        return NULL;

    char buf[512];
    char *p   = buf;
    char *end = buf + sizeof(buf) - 2; // leave room for ";\n\0"

    // Return type
    p += write_type_str(fn->ty->return_ty, p, (int)(end - p));

    // Space + function name
    p += snprintf(p, end - p, " %s(", fn->name);

    // Parameter types
    if (fn->ty->params == NULL) {
        p += snprintf(p, end - p, "void");
    } else {
        bool first = true;
        for (Type *pt = fn->ty->params; pt; pt = pt->next) {
            if (!first)
                p += snprintf(p, end - p, ", ");
            first = false;
            p += write_type_str(pt, p, (int)(end - p));
        }
        if (fn->ty->is_variadic)
            p += snprintf(p, end - p, ", ...");
    }

    p += snprintf(p, end - p, ");\n");

    if (vm->debug_vm)
        printf("Synthesized forward decl: %s", buf);

    // Tokenise and convert to parser tokens
    Token *toks = tokenize_string(vm, "<inline-macro-fwd>", buf);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);
    return toks;
}

// Build an extern declaration token stream for a generated global variable.
// Handles arrays by walking the type chain: e.g. char[6] emits
//   extern char banner_data[6];
// rather than the incorrect  extern int banner_data;  that write_type_str's
// TY_ARRAY fallback would produce.
static Token *synthesize_global_decl_tokens(VirtualMachine *vm, Obj *var) {
    if (!var || var->is_function)
        return NULL;

    char buf[512];
    char *p   = buf;
    char *end = buf + sizeof(buf) - 2;

    p += snprintf(p, end - p, "extern ");

    // For array types, emit base_type name[len1][len2]... syntax.
    // Walk the type chain to collect dimensions, then emit them after the name.
    if (var->ty && var->ty->kind == TY_ARRAY) {
        // Collect array dimensions
        int dims[16];
        int ndims = 0;
        Type *t = var->ty;
        while (t && t->kind == TY_ARRAY && ndims < 16) {
            dims[ndims++] = t->array_len;
            t = t->base;
        }
        // t is now the element type
        p += write_type_str(t, p, (int)(end - p));
        p += snprintf(p, end - p, " %s", var->name);
        for (int i = 0; i < ndims; i++)
            p += snprintf(p, end - p, "[%d]", dims[i]);
        p += snprintf(p, end - p, ";\n");
    } else {
        p += write_type_str(var->ty, p, (int)(end - p));
        p += snprintf(p, end - p, " %s;\n", var->name);
    }

    if (vm->debug_vm)
        printf("Synthesized global extern decl: %s", buf);

    Token *toks = tokenize_string(vm, "<inline-macro-gvar>", buf);
    if (!toks)
        return NULL;
    convert_pp_tokens(vm, toks);
    return toks;
}

// Scan a single token stream for file-scope calls to non-inline macros,
// execute them, collect generated definitions, and remove the call tokens.
// Newly generated Obj definitions are drained into vm->compiler.macro_globals
// immediately after each execution using a saved-next walk, so globals is
// always restored to its pre-call state and no cycle can form.
static void scan_and_execute_global_calls(VirtualMachine *vm, Token **tokens_ptr) {
    Token *prev = NULL;
    Token *tok = *tokens_ptr;
    int brace_depth = 0;
    int paren_depth = 0;
    bool in_init = false; // true after '=' at depth 0, until ';' or '}'

    while (tok && tok->kind != TK_EOF) {
        // Track brace/paren depth
        if (equal(tok, "{")) brace_depth++;
        else if (equal(tok, "}")) { brace_depth--; if (brace_depth == 0) in_init = false; }
        else if (equal(tok, "(")) paren_depth++;
        else if (equal(tok, ")")) paren_depth--;
        else if (equal(tok, ";") && brace_depth == 0) in_init = false;
        else if (equal(tok, "=") && brace_depth == 0 && paren_depth == 0) in_init = true;

        if (brace_depth == 0 && paren_depth == 0 &&
            tok->kind == TK_IDENT && tok->len == 21 &&
            strncmp(tok->loc, "__builtin_emit_line__", 21) == 0 &&
            tok->next && equal(tok->next, "(") &&
            tok->next->next && tok->next->next->kind == TK_STR &&
            tok->next->next->next && equal(tok->next->next->next, ")") &&
            tok->next->next->next->next &&
            equal(tok->next->next->next->next, ";")) {
            Token *next_tok = tok->next->next->next->next->next;
            cc_record_emit_source(vm, tok->next->next->str);
            if (prev)
                prev->next = next_tok;
            else
                *tokens_ptr = next_tok;
            tok = next_tok;
            continue;
        }

        // Only match standalone file-scope calls (outside braces/parens, not in initializers)
        if (brace_depth == 0 && paren_depth == 0 && !in_init &&
            tok->kind == TK_IDENT && tok->next && equal(tok->next, "(")) {
            // Check if this identifier is a macro
            MacroFn *pm = NULL;
            for (MacroFn *m = vm->compiler.macro_fns; m; m = m->next) {
                if (m->is_macro_entry &&
                    strlen(m->name) == tok->len &&
                    strncmp(m->name, tok->loc, tok->len) == 0) {
                    pm = m;
                    break;
                }
            }

            if (pm) {
                // Find matching ')'
                Token *after_paren = tok->next->next;
                int call_depth = 1;
                while (after_paren && after_paren->kind != TK_EOF && call_depth > 0) {
                    if (equal(after_paren, "(")) call_depth++;
                    else if (equal(after_paren, ")")) {
                        call_depth--;
                        if (call_depth == 0) break;
                    }
                    after_paren = after_paren->next;
                }

                if (after_paren && call_depth == 0) {
                    Token *after_semi = after_paren->next;
                    if (after_semi && equal(after_semi, ";")) {
                        Token *next_tok = after_semi->next;

                        // Collect comma-separated arguments as char* strings.
                        // Each argument token sequence is stringified and
                        // placed directly in VM registers (REG_A0+) so that
                        // macro parameters declared as `char *` receive the
                        // actual string data, not a Node wrapper.
                        // TK_STR tokens pass their string value directly;
                        // keywords/idents/numbers pass their spelling.
                        int max_args = 0;
                        if (tok->next->next != after_paren) {
                            max_args = 1;
                            int count_depth = 0;
                            for (Token *t = tok->next->next;
                                 t && t != after_paren;
                                 t = t->next) {
                                if (equal(t, "("))
                                    count_depth++;
                                else if (equal(t, ")") && count_depth > 0)
                                    count_depth--;
                                else if (count_depth == 0 && equal(t, ","))
                                    max_args++;
                            }
                        }
                        char **arg_strs = max_args > 0
                            ? alloca(max_args * sizeof(char *))
                            : NULL;
                        int arg_count = 0;
                        Token *a = tok->next->next; // first token after '('
                        while (a && a != after_paren) {
                            int depth = 0;
                            Token *arg_start = a;
                            Token *arg_end = a;
                            while (a && a != after_paren) {
                                if (equal(a, "(")) depth++;
                                else if (equal(a, ")")) depth--;
                                if (depth == 0 && equal(a, ",")) {
                                    a = a->next;
                                    break;
                                }
                                arg_end = a;
                                a = a->next;
                            }
                            char *str;
                            if (arg_start == arg_end &&
                                arg_start->kind == TK_STR) {
                                str = arg_start->str;
                            } else {
                                int total = 0;
                                for (Token *t = arg_start;
                                     t && t != arg_end->next;
                                     t = t->next)
                                    total += t->len;
                                str = arena_alloc(
                                    &vm->compiler.parser_arena, total + 1);
                                int pos = 0;
                                for (Token *t = arg_start;
                                     t && t != arg_end->next;
                                     t = t->next) {
                                    memcpy(str + pos, t->loc, t->len);
                                    pos += t->len;
                                }
                                str[pos] = '\0';
                            }
                            arg_strs[arg_count++] = str;
                        }
                        if (pm->is_variadic && arg_count < pm->fixed_param_count) {
                            error_tok(vm, tok,
                                      "macro '%.*s' called with %d arguments; expected at least %d",
                                      tok->len, tok->loc, arg_count,
                                      pm->fixed_param_count);
                        }
                        // Place char* values using the VM calling convention
                        // before calling execute_macro_fn with NULL args so
                        // the arg-setup loop inside does not overwrite them.
                        int fixed_count = pm->is_variadic ? pm->fixed_param_count
                                                          : arg_count;
                        long long *fixed_args =
                            fixed_count > 0
                                ? alloca((size_t)fixed_count * sizeof(long long))
                                : NULL;
                        for (int i = 0; i < fixed_count; i++)
                            fixed_args[i] = (long long)arg_strs[i];

                        if (!pm->is_compiled) {
                            error_tok(vm, tok, "macro '%.*s' failed to compile",
                                      tok->len, tok->loc);
                        }

                        // Snapshot globals before execution so we can identify
                        // the objects this call generates.
                        Obj *globals_before = vm->compiler.globals;
                        Scope *scope_before = vm->compiler.scope;
                        Scope *saved_scope_next =
                            scope_before ? scope_before->next : NULL;
                        if (scope_before && vm->compiler.macro_context_scope)
                            scope_before->next =
                                vm->compiler.macro_context_scope;

                        Node **saved_vararg_nodes =
                            vm->compiler.macro_vararg_nodes;
                        char **saved_vararg_strs =
                            vm->compiler.macro_vararg_strs;
                        int saved_vararg_count =
                            vm->compiler.macro_vararg_count;
                        bool saved_vararg_string_mode =
                            vm->compiler.macro_vararg_string_mode;
                        if (pm->is_variadic) {
                            vm->compiler.macro_vararg_nodes = NULL;
                            vm->compiler.macro_vararg_strs =
                                arg_strs + pm->fixed_param_count;
                            vm->compiler.macro_vararg_count =
                                arg_count - pm->fixed_param_count;
                            vm->compiler.macro_vararg_string_mode = true;
                        } else {
                            vm->compiler.macro_vararg_nodes = NULL;
                            vm->compiler.macro_vararg_strs = NULL;
                            vm->compiler.macro_vararg_count = 0;
                            vm->compiler.macro_vararg_string_mode = true;
                        }

                        bool saved_emit_recording =
                            vm->compiler.macro_emit_recording;
                        vm->compiler.macro_emit_recording = true;
                        Node *block_result =
                            execute_macro_fn(vm, pm, tok, NULL, arg_count,
                                             fixed_args, fixed_count);
                        vm->compiler.macro_emit_recording =
                            saved_emit_recording;
                        vm->compiler.macro_vararg_nodes = saved_vararg_nodes;
                        vm->compiler.macro_vararg_strs = saved_vararg_strs;
                        vm->compiler.macro_vararg_count = saved_vararg_count;
                        vm->compiler.macro_vararg_string_mode =
                            saved_vararg_string_mode;
                        if (scope_before)
                            scope_before->next = saved_scope_next;
                        vm->compiler.scope = scope_before;

                        // Drain newly prepended objects into macro_globals using
                        // a saved-next walk so we never overwrite a next pointer
                        // we still need to follow (which would create a cycle).
                        Obj *o = vm->compiler.globals;
                        while (o && o != globals_before) {
                            Obj *next_obj = o->next;
                            cc_record_emit_object(vm, o);
                            o->next = vm->compiler.macro_globals;
                            vm->compiler.macro_globals = o;
                            o = next_obj;
                        }
                        vm->compiler.globals = globals_before;

                        // Ticket #233: if the macro returned an ND_BLOCK, splice
                        // its body tokens into the stream so they are re-parsed
                        // at global scope instead of being silently discarded.
                        // The block came from Quote("{ ... }") so the token
                        // chain is: outer-'{' -> body tokens -> outer-'}' -> EOF.
                        // We start from block_result->tok->next (the token after
                        // the outer '{') and walk forward, tracking brace depth
                        // starting at 0, stopping just before the outer '}'.
                        // Note: compound_stmt adds declarations as side effects
                        // so block->body may be NULL; use tok-level injection.
                        if (block_result &&
                            block_result->kind == ND_BLOCK &&
                            block_result->tok &&
                            block_result->tok->kind != TK_EOF &&
                            !equal(block_result->tok, "}")) {
                            Token *body_first = block_result->tok;
                            Token *body_last  = NULL;
                            int bdepth = 0;
                            for (Token *t = body_first;
                                 t && t->kind != TK_EOF;
                                 t = t->next) {
                                if (equal(t, "{"))
                                    bdepth++;
                                else if (equal(t, "}")) {
                                    if (bdepth == 0)
                                        break; // outer closing brace
                                    bdepth--;
                                }
                                body_last = t;
                            }
                            if (body_last) {
                                body_last->next = next_tok;
                                if (prev)
                                    prev->next = body_first;
                                else
                                    *tokens_ptr = body_first;
                                tok = body_first;
                                continue;
                            }
                        }

                        // Remove the call tokens from the stream
                        if (prev) {
                            prev->next = next_tok;
                        } else {
                            *tokens_ptr = next_tok;
                        }
                        tok = next_tok;
                        continue;
                    }
                }
            }
        }

        prev = tok;
        tok = tok->next;
    }
}

// Execute file-scope calls to non-inline macros before the main parse.
// For each file-scope call:
//   1. Scan the preprocessed token stream for zero-arg calls to non-inline
//      macros at file scope (brace depth 0, paren depth 0).
//   2. Execute the macro (it calls __builtin_ast_function etc.).
//   3. Drain newly-added Objs into vm->compiler.macro_globals immediately
//      (safe saved-next walk; globals is restored to its pre-call state).
//   4. Remove the call tokens so the parser never sees them.
//   5. Synthesize forward-declaration token streams for each generated
//      function/global and prepend them to every input_tokens[i].
//
// After this runs, vm->compiler.macro_globals contains the generated
// definitions. main.c appends them to the merged program before codegen.
void cc_execute_inline_macros(VirtualMachine *vm, Token **input_tokens, int count) {
    if (!vm)
        return;

    if (!vm->compiler.no_comptime)
        check_comptime_decls_defined(vm);

    if (!vm->compiler.macro_fns) {
        for (int fi = 0; fi < count; fi++)
            if (input_tokens[fi])
                scan_and_execute_global_calls(vm, &input_tokens[fi]);
        return;
    }

    // #894: build the demand-driven declaration index. Nothing is forwarded
    // to the comptime program up front any more (that was the eager
    // build_macro_context_tokens snapshot, removed) -- declarations resolve
    // lazily, on a lookup miss, via is_typename/find_tag/primary()'s hooks
    // in src/parse.c (splice_comptime_decl et al).
    cc_comptime_index_build(vm, input_tokens, count);

    // Quick check: are there any file-scope macro calls in the token streams?
    // If not, skip init+compile here — they will happen lazily in cc_expand_macros
    // after parsing, when all symbols are defined. This avoids premature $symbol
    // lookups in macros that are only called in expression position.
    bool any_file_scope_call = false;
    for (int fi = 0; fi < count && !any_file_scope_call; fi++) {
        Token *t = input_tokens[fi];
        int bd = 0, pd = 0;
        bool in_init = false;
        while (t && t->kind != TK_EOF) {
            if (equal(t, "{")) bd++;
            else if (equal(t, "}")) { bd--; if (bd == 0) in_init = false; }
            else if (equal(t, "(")) pd++;
            else if (equal(t, ")")) pd--;
            else if (equal(t, ";") && bd == 0) in_init = false;
            else if (equal(t, "=") && bd == 0 && pd == 0) in_init = true;
            if (bd == 0 && pd == 0 && !in_init &&
                t->kind == TK_IDENT && t->next && equal(t->next, "(")) {
                for (MacroFn *m = vm->compiler.macro_fns; m; m = m->next) {
                    if (m->is_macro_entry &&
                        strlen(m->name) == t->len &&
                        strncmp(m->name, t->loc, t->len) == 0) {
                        // Check for ';' after matching ')'
                        Token *ap = t->next->next;
                        int cd = 1;
                        while (ap && ap->kind != TK_EOF && cd > 0) {
                            if (equal(ap, "(")) cd++;
                            else if (equal(ap, ")")) { cd--; if (cd==0) break; }
                            ap = ap->next;
                        }
                        if (ap && cd == 0 && ap->next && equal(ap->next, ";"))
                            any_file_scope_call = true;
                        break;
                    }
                }
            }
            if (any_file_scope_call) break;
            t = t->next;
        }
    }

    if (!any_file_scope_call) {
        // No file-scope calls: just scan for __builtin_emit_line__ but defer
        // macro compilation to cc_expand_macros (after parsing).
        for (int fi = 0; fi < count; fi++)
            if (input_tokens[fi])
                scan_and_execute_global_calls(vm, &input_tokens[fi]);
        return;
    }

    if (vm->debug_vm)
        printf("Pre-parse: executing global macro calls...\n");

    // Segments must be initialised before macro bytecode can run.
    init_vm_segments_for_macros(vm);

    // Compile all macros (idempotent — subsequent call from
    // cc_expand_macros is a no-op).
    compile_all_macros(vm);

    // Ensure a top-level scope exists so that macros can register typedefs,
    // enums, and struct tags that later parser code needs to resolve.
    if (!vm->compiler.scope) {
        Scope *sc = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
        memset(sc, 0, sizeof(Scope));
        vm->compiler.scope = sc;
    }

    // Scan every input token stream for file-scope calls.
    // scan_and_execute_global_calls drains generated objects into
    // macro_globals directly and restores globals after each call,
    // so no bulk-move is needed here.
    for (int fi = 0; fi < count; fi++) {
        if (!input_tokens[fi])
            continue;
        scan_and_execute_global_calls(vm, &input_tokens[fi]);
    }

    // Synthesize forward declarations for every generated function and
    // extern declarations for every generated global variable, prepending
    // them to all input token streams so the parser can resolve references.
    for (Obj *o = vm->compiler.macro_globals; o; o = o->next) {
        bool is_fn_def  = o->is_function  && o->body &&
                          o->is_macro_generated;
        // #928: an anon gvar (dotted `.L..N` name -- reflect_new_anon_gvar,
        // reflection.c) is referenced directly through the Obj pointer
        // already embedded in its ND_VAR node, never by re-parsed textual
        // reference, and doesn't get a real identifier until
        // rename_anon_globals() renames it at serialization time (long
        // after this pre-parse pass runs). Synthesizing `extern T .L..N;`
        // here is both unnecessary and invalid C -- skip it.
        bool is_gvar_def = !o->is_function && o->is_definition &&
                            o->is_macro_generated && o->name[0] != '.';
        if (!is_fn_def && !is_gvar_def)
            continue;

        for (int fi = 0; fi < count; fi++) {
            if (!input_tokens[fi])
                continue;
            Token *decl = is_fn_def
                ? synthesize_forward_decl_tokens(vm, o)
                : synthesize_global_decl_tokens(vm, o);
            if (!decl)
                continue;
            Token *tail = decl;
            while (tail->next && tail->next->kind != TK_EOF)
                tail = tail->next;
            tail->next = input_tokens[fi];
            input_tokens[fi] = decl;
        }
    }

    if (vm->debug_vm)
        printf("Pre-parse global macro execution complete.\n");
}

// Expand a single ND_MACRO_CALL node using the already-compiled macros.
// Called from cc_finalize_macro_gvar_inits while in_macro_expansion is true,
// after compile_all_macros has run. Returns the expanded node (or the original
// node unchanged if it is not an ND_MACRO_CALL).
Node *cc_eager_expand_macro_call(VirtualMachine *vm, Node *node) {
    if (!node)
        return NULL;
    // transform_node handles all ND_MACRO_CALL expansion, type annotation,
    // void/return-type checks, NULL-result errors, and recursive re-expansion.
    return transform_node(vm, node, 0);
}

// Expand all macro calls in the program
void cc_expand_macros(VirtualMachine *vm, Obj *prog) {
    if (!vm || !prog)
        return;

    // If no macro functions were captured, nothing to do
    if (!vm->compiler.macro_fns)
        return;

    if (vm->debug_vm)
        printf("Expanding macros in program...\n");

    // Initialize VM segments if not already done (needed for macro
    // compilation)
    init_vm_segments_for_macros(vm);

    // Enter macro expansion mode
    vm->compiler.in_macro_expansion = true;

    // First, compile all macro functions
    compile_all_macros(vm);

    // Then walk the AST and expand macro calls
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;

        if (vm->debug_vm)
            printf("Expanding macros in function '%s'...\n", fn->name);

        // Set current function context, including the locals list so that
        // any new_lvar() calls inside __builtin_quote (e.g. pointer temps for
        // compound assignments in quote templates) are added to THIS
        // function's locals and get proper stack-offset allocation in codegen.
        vm->compiler.current_fn = fn;
        vm->compiler.locals = fn->locals;

        // Transform the function body
        fn->body = transform_node(vm, fn->body, 0);

        // Flush new locals (created by quote templates) back into fn->locals.
        fn->locals = vm->compiler.locals;
    }

    // Clear locals so a stray new_lvar in a global-init comptime context
    // cannot silently attach to the last function's frame.
    vm->compiler.locals = NULL;
    vm->compiler.current_fn = NULL;

    // Also check global initializers (constexpr init_expr expansion).
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;
        if (var->init_expr) {
            var->init_expr = transform_node(vm, var->init_expr, 0);
        }
    }

    // Finalize any global variable initializers whose scalar expression
    // contained an ND_MACRO_CALL at parse time (deferred by gvar_initializer).
    // Now that all macros are compiled and all symbols are defined, we can
    // safely expand and serialize those pending initializers (#613).
    cc_finalize_macro_gvar_inits(vm, prog);

    vm->compiler.in_macro_expansion = false;

    if (vm->debug_vm)
        printf("Macro expansion complete.\n");
}
