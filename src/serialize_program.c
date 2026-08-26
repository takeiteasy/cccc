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

// Serialization: whole-program emission -- collision renames,
// deferred-label collection, captured-#include replay, synthesized
// declarations/helpers, nested-function/block preambles, test
// harness, cc_serialize_program itself (#1150).
#include "./serialize_internal.h"

// #1025: an asm("symbol") label names the real linker symbol directly,
// bypassing the compiler's own C-name-mangling -- which on Darwin adds a
// leading '_' to every external symbol and on Linux/ELF does not. Writing
// the label as a plain string literal (`asm("puts")`) only links on a
// platform with no such prefix; on Darwin the linker looks for the raw
// "puts" symbol, which doesn't exist (the real one is "_puts"), and fails.
// __USER_LABEL_PREFIX__ is a *token* (an actual `_` character, or nothing)
// clang/gcc predefine for exactly this, string-pasted via the standard
// double-macro stringize idiom so it works with the token being empty (an
// empty macro argument stringizes to "", not an error). Emitted once, only
// when some function actually carries an asm label, and ahead of every
// asm-labeled declaration since it's used there as `asm(__CCCC_ASM_PREFIX__
// "name")`.
//
// Deliberately does NOT check obj->is_used: serialize_function_signature
// prints the asm(...) clause purely off fn->asm_label, with no is_used
// gate of its own, and the function-prototype pass (cc_serialize_program,
// further down this file) can emit a bodiless declaration's prototype
// (e.g. `int f(void) asm("name");` with no definition anywhere in this TU)
// regardless of whether anything in the program actually calls it. Gating
// this preamble on is_used while that emission site isn't would leave
// __CCCC_ASM_PREFIX__ referenced-but-undefined for exactly that case
// ("expected string literal in 'asm'", confirmed) -- three unconditional
// #defines cost nothing, so match unconditionally instead.
static bool prog_uses_asm_label(Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next)
        if (obj->is_function && obj->asm_label)
            return true;
    return false;
}

static void serialize_asm_prefix_preamble(FILE *f, Obj *prog) {
    if (!prog_uses_asm_label(prog))
        return;
    fprintf(f, "#define __cccc_asm_str2(x) #x\n"
               "#define __cccc_asm_str1(x) __cccc_asm_str2(x)\n"
               "#define __CCCC_ASM_PREFIX__ "
               "__cccc_asm_str1(__USER_LABEL_PREFIX__)\n\n");
}

// #1113(a): _Decimal32/64/128 declarations, literals (the `df`/`dd`/`dl`
// suffix, serialize_expr.c's ND_NUM arm) and global-initializer BID bytes
// (serialize_decl.c's serialize_init_bytes()) all pass through to native/-m
// output as plain GNU decimal syntax -- there is no other option, since
// unlike __builtin_decimal_to_chars/<decimal_math.h> (which hard-error at
// cccc's own compile time, no host equivalent exists at all) a decimal
// declaration or literal genuinely does compile on SOME hosts. Confirmed
// directly: gcc implements the GNU decimal extension and predefines
// __DEC64_MAX__ (both macOS gcc-16 and Linux gcc 15.2); clang implements
// neither, on either platform, and rejects the syntax outright ("GNU
// decimal type extension not supported"). test_suite_decimal.c already
// passes -c=native under CCCC_NATIVE_CC=gcc (NATIVE_SKIP_TESTS_CLANG,
// tools/testing/__init__.py, #1186) -- an unconditional refusal here would
// regress that working configuration.
//
// So refusal is deferred to the HOST preprocessor instead of cccc's own
// error(): whichever compiler actually reads this file evaluates
// __DEC64_MAX__ itself, which is exactly the axis that decides whether the
// decimal syntax below will compile. This can't be a real cccc-side
// error() -- unlike -c=native (which knows its own host cc via
// cccc_find_native_cc()), -m's whole point is that cccc doesn't know who
// will read the output. Latched via ctx->saw_decimal (collect_type(),
// serialize_type.c), set during the pre-emission collection walk that
// already runs before this preamble, so it's known here regardless of
// which of the three call sites above first uses a decimal type.
static void serialize_decimal_native_guard(FILE *f, SerializeContext *ctx) {
    if (!ctx->saw_decimal)
        return;
    fprintf(f, "#if !defined(__DEC64_MAX__)\n"
               "#error \"cccc: _Decimal32/64/128 has no native/-m lowering "
               "on this host compiler (GNU decimal extension; gcc only). "
               "See man/COVERAGE.md.\"\n"
               "#endif\n\n");
}

// #925/#928: new_anon_gvar() (parse.c) and reflect_new_anon_gvar()
// (reflection.c) both hand out the same `.L..N` name to string literals,
// static locals, and compound literals alike -- a dot isn't a valid C
// identifier character, so every non-string-literal use needs a real name
// before anything below references it. Runs once, before any
// collection/emission pass, so every later `is_string_literal`/dotted-name
// check sees the final state. Also runs under generated_only (-c=generated):
// #928 found that reflection API compound-literal/init-struct globals built
// while running under -c=generated (e.g. a comptime macro calling
// CompoundLiteral()/ InitArray()/InitStruct() at file scope) hit this exact gap
// when renaming was skipped here -- the emit-event walk's own dotted-name skip
// (see `obj->name[0] != '.'` further down) only prevented emitting a bogus
// reference, it never gave the global a real name or definition.
static void rename_anon_globals(VirtualMachine *vm, Obj *prog,
                                SerializeContext *ctx) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        // #965: a lifted block literal function (block_literal(), parse.c)
        // is named ".L..N" from the same new_unique_name() counter as a
        // string literal or compound literal, but it's a *function* -- the
        // generic branch below is skipped for those (`obj->is_function`
        // continues past it) since a real function normally already has a
        // legal name. Rename it to a C-legal identifier here too, sharing
        // the same counter; serialize_block_preamble() reuses this same
        // numeric suffix for the function's paired env struct name, so the
        // two stay paired without extra state.
        if (obj->is_function) {
            if (obj->is_block && obj->name[0] == '.')
                obj->name = arena_format(vm, "__cccc_block_%d",
                                         ctx->anon_global_counter++);
            continue;
        }
        if (obj->name[0] != '.' || obj->is_string_literal)
            continue;
        // new_gvar() (parse.c) defaults display_name to the same dotted
        // name it was created with; only a static local overrides it (to
        // the real source identifier) after the fact. A still-dotted
        // display_name means no such override happened (a compound
        // literal) -- fall back to a plain "anon" tag rather than
        // splicing the dot into the new name too.
        const char *tag = (obj->display_name && obj->display_name[0] != '.')
                              ? obj->display_name
                              : "anon";
        obj->name =
            arena_format(vm, "__cccc_%s_%d", tag, ctx->anon_global_counter++);
        // An anonymous global (compound literal or static local) can never
        // be referenced from another translation unit -- internal linkage
        // makes the #918 forward-declaration pass ahead of global
        // definitions emit a valid `static T name;` + `static T name = ...;`
        // tentative-definition pair instead of `extern` plus an external
        // definition.
        obj->is_static = true;
    }
}

// #1044: best-effort descent over every statement/expression child field a
// Node can have, used by collect_deferred_static_labels() below for two
// purposes -- finding every ND_LABEL reachable from a function body, and
// checking whether a given Obj is referenced from outside its supposed
// owner. A node kind this doesn't know to visit through (there is no
// exhaustive list of Node's child fields, unlike serialize_stmt/
// serialize_expr's own kind-by-kind switches) just means a label or
// reference goes unnoticed -- which only ever makes collect_deferred_
// static_labels() more conservative, falling back to today's existing
// "unresolved relocation target" hard error rather than emitting broken C.
// A node reachable through more than one field (e.g. a switch's case_next
// list overlaps its body's next-chain) is visited only once -- see the
// dedup set below, load-bearing rather than an optimization.
//
// Deliberately an explicit heap-backed work stack, not recursion: a real-
// world program's AST (tests/test_minilua.c, a ~30k-line single-file Lua
// interpreter) both chains thousands of statements through `next` and
// nests expressions (long `lhs`/`rhs` chains from deeply parenthesized or
// chained-operator source) deep enough to overflow the C call stack either
// way -- confirmed by AddressSanitizer during development (SIGSEGV/stack-
// overflow at what looked, from the outside, like an unrelated later
// function, since the corrupted-vs-overflowed stack's actual fault site is
// disconnected from which AST this walk was ever asked to cover).
//
// A dedup set (open-addressed, keyed by pointer identity) is load-bearing,
// not an optimization: a node reachable through more than one of these
// fields (the same case_next/body overlap noted above) would otherwise get
// its *entire* subtree re-pushed once per incoming path -- for a large,
// heavily-shared DAG this is exponential, not merely wasteful, and blew up
// into an integer-overflowing allocation request during development on
// this exact file. Marking a node visited the moment it's popped, before
// its children are ever pushed, bounds total work to one push per (node,
// field) pair regardless of how many paths reach that node.
typedef struct {
    Node **slots;
    int    cap; // power of two, 0 means not yet allocated
    int    count;
} NodeSet;

static bool node_set_add(NodeSet *set, Node *n) {
    if (set->count * 4 >= set->cap * 3) { // grow at 75% load (also the
                                          // initial 0/0 case)
        int    old_cap = set->cap;
        Node **old     = set->slots;
        set->cap       = old_cap ? old_cap * 2 : 1024;
        set->slots     = calloc(set->cap, sizeof(*set->slots));
        set->count     = 0;
        for (int i = 0; i < old_cap; i++)
            if (old[i])
                node_set_add(set, old[i]); // reinsert into the new table
        free(old);
    }
    uintptr_t h = (uintptr_t)n >> 4;       // Node* is always more than 16-byte
                                           // aligned in practice; spreads bits
    int idx = (int)(h & (uintptr_t)(set->cap - 1));
    while (set->slots[idx] && set->slots[idx] != n)
        idx = (idx + 1) & (set->cap - 1);
    if (set->slots[idx] == n)
        return false; // already present
    set->slots[idx] = n;
    set->count++;
    return true;
}

static void ast_walk_1044(Node *root, void (*visit)(Node *, void *),
                          void *ctx) {
    Node  **stack = NULL;
    int     len = 0, cap = 0;
    NodeSet seen = {0};
#define AST_WALK_1044_PUSH(child)                                              \
    do {                                                                       \
        Node *__c = (child);                                                   \
        if (__c) {                                                             \
            if (len == cap) {                                                  \
                cap   = cap ? cap * 2 : 256;                                   \
                stack = realloc(stack, cap * sizeof(*stack));                  \
            }                                                                  \
            stack[len++] = __c;                                                \
        }                                                                      \
    } while (0)

    AST_WALK_1044_PUSH(root);
    while (len > 0) {
        Node *n = stack[--len];
        if (!node_set_add(&seen, n))
            continue; // already visited via another path
        visit(n, ctx);
        AST_WALK_1044_PUSH(n->lhs);
        AST_WALK_1044_PUSH(n->rhs);
        AST_WALK_1044_PUSH(n->cond);
        AST_WALK_1044_PUSH(n->then);
        AST_WALK_1044_PUSH(n->els);
        AST_WALK_1044_PUSH(n->init);
        AST_WALK_1044_PUSH(n->inc);
        AST_WALK_1044_PUSH(n->body);
        AST_WALK_1044_PUSH(n->args);
        AST_WALK_1044_PUSH(n->case_next);
        AST_WALK_1044_PUSH(n->default_case);
        AST_WALK_1044_PUSH(n->next);
    }
#undef AST_WALK_1044_PUSH
    free(stack);
    free(seen.slots);
}

typedef struct {
    SerializeContext *ctx;
    Obj              *owner_fn;
} LabelCollectCtx;

static void collect_label_visit(Node *n, void *vctx) {
    if (n->kind != ND_LABEL || !n->unique_label)
        return;
    LabelCollectCtx *lc = vctx;
    if (find_label_owner(lc->ctx, n->unique_label))
        return; // already recorded (e.g. reached twice via case_next)
    if (lc->ctx->label_owners_len == lc->ctx->label_owners_cap) {
        lc->ctx->label_owners_cap =
            lc->ctx->label_owners_cap ? lc->ctx->label_owners_cap * 2 : 8;
        lc->ctx->label_owners =
            realloc(lc->ctx->label_owners,
                    lc->ctx->label_owners_cap * sizeof(*lc->ctx->label_owners));
    }
    LabelOwner *entry   = &lc->ctx->label_owners[lc->ctx->label_owners_len++];
    entry->unique_label = n->unique_label;
    entry->label        = n->label;
    entry->owner_fn     = lc->owner_fn;
}

typedef struct {
    Obj *var;
    bool found;
} VarRefCtx;

static void var_ref_visit(Node *n, void *vctx) {
    VarRefCtx *vr = vctx;
    if (n->var == vr->var)
        vr->found = true;
}

// #1044: an anonymous global whose relocation(s) resolve only against a
// label (never against a real Obj -- see serialize_reloc_init()'s own
// comment) must be defined inside the one function that owns that label
// instead of at file scope. Builds ctx->label_owners (every label in the
// program) and then ctx->deferred_label_statics (the subset of globals that
// actually need this treatment), run once from cc_serialize_program()
// immediately after the renaming passes above and before anything else
// reads obj->name/rel. A candidate referenced from more than one function
// (a block literal or nested function lexically inside the owner, which
// -c=native lifts to its own separate file-scope C function, #965/#1074) is
// deliberately left undeferred -- deferring it would only trade today's
// clean "unresolved relocation target" diagnostic for a "use of undeclared
// identifier" one from the host compiler, the opposite of the #918
// fail-loudly policy this file follows throughout.
static void collect_deferred_static_labels(VirtualMachine *vm, Obj *prog,
                                           SerializeContext *ctx) {
    // Cheap early-out for the common case: this whole pass (two full-
    // program AST walks below, one per candidate) only matters when at
    // least one non-function global has a Relocation that doesn't resolve
    // to a real Obj -- true only for a labels-as-values dispatch table, a
    // vanishingly rare construct. Every other program (the overwhelming
    // majority compiled with `-m`/`-c=native`) skips straight past this
    // function for free.
    bool any_unresolved_reloc = false;
    for (Obj *var = prog; var && !any_unresolved_reloc; var = var->next) {
        if (var->is_function || !var->rel)
            continue;
        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (rel->label && *rel->label &&
                !serialize_find_global(vm, *rel->label)) {
                any_unresolved_reloc = true;
                break;
            }
        }
    }
    if (!any_unresolved_reloc)
        return;

    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body)
            continue;
        LabelCollectCtx lc = {.ctx = ctx, .owner_fn = fn};
        ast_walk_1044(fn->body, collect_label_visit, &lc);
    }

    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function || !var->rel)
            continue;
        Obj *owner = NULL;
        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                continue;
            if (serialize_find_global(vm, *rel->label))
                continue; // resolves to a real Obj -- not a label reference
            const LabelOwner *lo = find_label_owner(ctx, *rel->label);
            if (lo)
                owner = lo->owner_fn;
        }
        if (!owner)
            continue;

        // Cross-function reference guard -- see this function's own
        // comment above.
        bool referenced_elsewhere = false;
        for (Obj *fn = prog; fn; fn = fn->next) {
            if (!fn->is_function || !fn->body || fn == owner)
                continue;
            VarRefCtx vr = {.var = var, .found = false};
            ast_walk_1044(fn->body, var_ref_visit, &vr);
            if (vr.found) {
                referenced_elsewhere = true;
                break;
            }
        }
        // Another global's own initializer taking `var`'s address (e.g.
        // `static void **p = tab;` reading a `static void *tab[] = {&&L};`
        // declared alongside it) is legal C, and `p` itself has nothing
        // wrong with its own relocation -- it resolves to a real Obj, so
        // it is never itself a deferral candidate and keeps its ordinary
        // file-scope definition. But once `var` moves inside its owner
        // function's body, that file-scope reference to it would name a
        // symbol that no longer exists at file scope ("use of undeclared
        // identifier"). Declining the deferral here falls back to the
        // pre-existing "unresolved relocation target" diagnostic for `var`
        // itself, same fail-loudly policy as the cross-function guard
        // above.
        if (!referenced_elsewhere) {
            for (Obj *other = prog; other && !referenced_elsewhere;
                 other      = other->next) {
                if (other == var || other->is_function || !other->rel)
                    continue;
                for (Relocation *rel = other->rel; rel; rel = rel->next) {
                    if (rel->label && *rel->label &&
                        serialize_find_global(vm, *rel->label) == var) {
                        referenced_elsewhere = true;
                        break;
                    }
                }
            }
        }
        if (referenced_elsewhere)
            continue;

        if (ctx->deferred_label_statics_len ==
            ctx->deferred_label_statics_cap) {
            ctx->deferred_label_statics_cap =
                ctx->deferred_label_statics_cap
                    ? ctx->deferred_label_statics_cap * 2
                    : 8;
            ctx->deferred_label_statics =
                realloc(ctx->deferred_label_statics,
                        ctx->deferred_label_statics_cap *
                            sizeof(*ctx->deferred_label_statics));
        }
        DeferredStaticLabel *entry =
            &ctx->deferred_label_statics[ctx->deferred_label_statics_len++];
        entry->var      = var;
        entry->owner_fn = owner;
    }
}

// #1032: two File records can spell the identical on-disk header two
// different ways -- one command-line input given as an absolute path and
// another as a relative one (the ordinary shape when a build/test harness
// mixes both, e.g. tools/testing/native.py's own compile_cmd) causes each
// TU's own #include resolution (dirname(including file) + the quoted
// spelling) to record a differently-spelled-but-identical path for the same
// shared header. A raw strcmp of File.name (the #1006 "no canonicalization,
// exact command-line spelling" design, cc_file_is_command_line_input's own
// comment) then wrongly treats the two as different files. Used only by
// rename_colliding_static_names() below, where getting this wrong renames a
// header-defined static function's *call sites* (every use resolves through
// the Obj, so the rename is "free") while the function's own definition is
// never re-emitted at all -- it reaches the output solely via the replayed
// #include, still under its original name -- producing a call to an
// undeclared symbol. realpath() failing (a synthetic/embedded path with no
// real file, e.g. the src/std.c embedded-header table's own paths) falls
// back to the exact-string comparison this replaces, matching prior
// behavior for anything that was never a real difference anyway.
static bool files_are_same(const char *a, const char *b) {
    if (!a || !b)
        return false;
    if (strcmp(a, b) == 0)
        return true;
    char ra[PATH_MAX], rb[PATH_MAX];
    if (!realpath(a, ra) || !realpath(b, rb))
        return false;
    return strcmp(ra, rb) == 0;
}

// #1002: cc_link_progs (linker.c) deliberately never canonicalizes `static`
// (internal-linkage) Objs across translation units -- two different .c
// inputs each defining `static int helper(void)` contribute two distinct
// Objs, both named "helper", into the one flat merged `prog` list this file
// serializes. The VM doesn't care (each Obj has its own body/bytecode), but
// -c=native/-m print by name, so the host compiler sees two definitions of
// the same identifier ("redefinition of 'helper'"). Renames every
// static Obj's name but the first for any name shared by Objs declared in
// more than one distinct file, so a name with no collision -- the common
// case -- is left exactly as the user wrote it. Must run after
// rename_anon_globals() (whose own renames can't collide with a
// user-written name -- see that function) and before any pass that reads
// obj->name, since every emit site (serialize_function_signature, ND_VAR,
// serialize_global_var, ...) resolves the name through the Obj pointer, so
// a rename here is automatically consistent everywhere except the one
// string-keyed lookup, serialize_find_global()'s first-match strcmp scan
// over vm->compiler.globals -- that scan runs after this pass too, so it
// resolves a relocation's label against the (possibly already renamed)
// Obj it actually points at, not a stale name.
// #1075: a nested function is always Obj.is_static (#1039), but its name
// may now legally collide with an ordinary, non-static, SAME-FILE outer
// function (distinct scope+linkage, C17 6.2.1p4) -- something this pass
// previously assumed could never happen (see the "same-file same-name
// would already be a parse-time redefinition error" comment below, still
// true for two ordinary statics). Handled with two hashmaps: `anchors`
// captures every non-static defining Obj's name in its own pass, up
// front, so a static/nested Obj can detect the collision regardless of
// prog's own ordering -- a nested function's Obj is always pushed ahead of
// its enclosing function's own (see codegen_func.c's "nested functions are
// compiled before their parents" comment), so a single combined pass would
// see the nested name registered FIRST and rename the wrong (non-static)
// side. A non-static name is never a rename candidate, so `anchors` is
// populated once and never mutated again.
// #1042(c): resolve the same platform-specific libc path find_libc()
// (src/vm.c) does, so this probe queries the SAME library the VM's own FFI
// path would -- deliberately NOT RTLD_DEFAULT/dlopen(NULL): those also
// search the main executable (this compiler process itself), so a static
// name matching one of cccc's OWN exported symbols would rename differently
// depending on which cccc binary happened to run it (stage0 `./cccc` vs.
// the full `build/cccc` with libbacktrace/readline linked in) -- emitted C
// must be a function of the host libc only, never of the compiler build.
static void *open_libc_handle_for_probe(void) {
#if defined(_WIN32)
    return NULL; // no dlopen/dlsym on this target; probe is a no-op there
#else
    static const char *const candidates[] = {
#if defined(__APPLE__)
        "/usr/lib/libSystem.dylib",
#elif defined(__linux__)
        "/lib64/libc.so.6",
        "/lib/x86_64-linux-gnu/libc.so.6",
        "/lib/aarch64-linux-gnu/libc.so.6", // glibc's aarch64 multiarch dir --
                                            // find_libc() (src/vm.c) is
                                            // missing this one too, but the
                                            // trailing bare "libc.so.6" below
                                            // still resolves it there via the
                                            // dynamic linker's own search path
        "/lib/libc.so.6",
        "/usr/lib64/libc.so.6",
        "/usr/lib/libc.so.6",
        "libc.so.6",
#elif defined(__FreeBSD__)
        "/lib/libc.so.7",
        "/usr/lib/libc.so.7",
#else
        "/lib/libc.so",
        "/usr/lib/libc.so",
#endif
        NULL,
    };
    for (int i = 0; candidates[i]; i++) {
        void *h = dlopen(candidates[i], RTLD_LAZY);
        if (h)
            return h;
    }
    return NULL;
#endif
}

// #1042(c) regression found on Linux/glibc 2.39 (Ubuntu 24.04): glibc has
// started exporting real symbols for some of the newer C23 <stdbit.h>
// functions (confirmed directly -- `dlsym` finds `stdc_leading_zeros_ui`
// there), so the probe below fired against a name that could never
// actually collide -- `<stdbit.h>` is `is_cccc_supplied_only_header`, its
// own `#include` is deliberately NEVER replayed to the host compiler
// (test_serialize_polyfill_header_not_replayed.c's own point), so no real
// declaration of that name ever reaches the emitted C for the host to see
// a "static declaration follows non-static declaration" collision against
// in the first place. Gate the whole probe on there being at least one
// ACTUALLY-replayed (non-cccc-only, non-setjmp.h, non-conditional-shell)
// `#include` anywhere in the program -- mirrors exactly the filter the
// `#include`-replay loop itself applies (`cc_serialize_program`, below in
// this file) -- so a program that never hands the host compiler a real
// header at all (this test; also any program with zero `#include`s) can
// never trip the probe, matching the actual hazard's own precondition.
// `emit_directives` captures every top-level directive CCCC replays --
// #include lines AND ordinary ones (#define/#pragma/...), the latter with
// no entry in `emit_include_paths` at all (only ever populated for the
// PP_INCLUDE case). Only a line WITH a resolved path is an #include in the
// first place; anything else (a re-derived cccc-only header's own #define
// lines included) must be skipped outright, not fall through to "no
// suppression rule matched, so this counts as real" -- that fallthrough was
// the actual bug in an earlier version of this function: a re-derived
// polyfill header's own #define lines have no `resolved` path either, so
// they hit every one of the three `resolved &&`-guarded skip conditions
// below as false and were wrongly counted as a real replayed include.
static bool any_real_include_replayed(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.emit_directives.len; i++) {
        char *line     = vm->compiler.emit_directives.data[i];
        char *resolved = hashmap_get(&vm->compiler.emit_include_paths, line);
        if (!resolved)
            continue; // not a captured #include line at all
        if (!vm->compiler.emit_cccc && cc_file_is_cccc_only(vm, resolved))
            continue;
        if (!vm->compiler.emit_cccc && path_basename_is(resolved, "setjmp.h"))
            continue;
        return true;
    }
    return false;
}

static void rename_colliding_static_names(VirtualMachine *vm, Obj *prog,
                                          SerializeContext *ctx) {
    HashMap anchors = {0}; // name -> the non-static Obj* that owns it
    HashMap claimed = {
        0}; // name -> first static Obj* claiming it (old semantics)
    // #1042(c): tests/test_minilua.c's own `static int getmode(...)` is
    // legal C in the source's own declaration order (its `#include
    // <unistd.h>` comes AFTER the static definition -- a later, weaker
    // declaration of an already-defined static doesn't redefine it) --
    // confirmed directly, `clang -fsyntax-only` on the real source compiles
    // clean. -c=native's own #include-replay block hoists every captured
    // include to the very top of the output, unconditionally, ahead of
    // every prototype/definition -- inverting that legal order and
    // manufacturing a "static declaration follows non-static declaration"
    // collision against macOS libc's real `mode_t getmode(...)` that the
    // user's program never actually has. Any static, defining Obj whose
    // name resolves in the host libc's own symbol namespace gets the same
    // "%s__cccc_dupN" rename this pass already applies for an ordinary
    // cross-TU collision -- renaming a static is always safe (file-local,
    // every reference resolves through the same Obj) regardless of what
    // name it lands on. Known, deliberate over-approximation: dlsym proves
    // a DEFINITION exists in the host's symbol namespace, not that a
    // replayed header actually DECLARES it -- harmless, since the rename is
    // invisible outside this one translation unit. `main` is excluded: it's
    // never actually a libc symbol collision candidate (dlsym would find
    // the process's own libc startup glue, not a real hazard), and renaming
    // it would break the emitted binary's entry point.
    void *libc_handle =
        any_real_include_replayed(vm) ? open_libc_handle_for_probe() : NULL;

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static || obj->is_macro_generated || obj->name[0] == '.')
            continue;
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining)
            continue;
        if (!hashmap_get(&anchors, obj->name))
            hashmap_put_borrowed(&anchors, obj->name, obj);
    }

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_static || obj->is_macro_generated || obj->name[0] == '.')
            continue;
        // Only an Obj that actually reaches the output as a definition can
        // collide with another TU's same-named one -- a bodyless static
        // function prototype or a tentative (non-defining) declaration
        // prints nothing a host compiler would reject twice.
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining)
            continue;

        // #1103: a static whose *definition* is never re-emitted -- it
        // reaches the output solely via a replayed `#include`, under its
        // original name (function_is_header_supplied(), defined below) --
        // must not be renamed by any tier past this point. Every call site
        // resolves through this same Obj*, so renaming it renames every use
        // too, while the header-supplied definition keeps printing under
        // the old name: a call to an identifier that was never declared.
        // include/ndbm.h's five `static inline dbm_*` shims hit exactly
        // this via the dlsym tier just below (they really do exist in
        // macOS/glibc's ndbm implementation) -- see files_are_same()'s own
        // doc comment above for the general shape of this hazard, of which
        // this is the "definition never re-emitted at all" half.
        if (function_is_header_supplied(vm, ctx, obj))
            continue;

        if (libc_handle && strcmp(obj->name, "main") != 0 &&
            dlsym(libc_handle, obj->name)) {
            obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                     ctx->anon_global_counter++);
            continue;
        }

        if (hashmap_get(&anchors, obj->name)) {
            // Collides with a non-static name -- the non-static side must
            // keep it; always rename this one (covers #1075's nested
            // function shadowing a same-named outer function, same file or
            // not).
            obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                     ctx->anon_global_counter++);
            continue;
        }

        Obj *first = hashmap_get(&claimed, obj->name);
        if (!first) {
            hashmap_put_borrowed(&claimed, obj->name, obj);
            continue;
        }
        // Only a genuine cross-TU collision -- two Objs of the same name
        // declared in different files -- needs renaming; same-file
        // same-name would already be a parse-time redefinition error for
        // two ordinary statics, EXCEPT when at least one is a nested
        // function's own hoisted Obj (#1075's other shape: two distinct
        // same-named nested functions, or a nested one colliding with an
        // outer static of the same name -- both now legal same-file C).
        const char *first_file =
            first->tok && first->tok->file ? first->tok->file->name : NULL;
        const char *this_file =
            obj->tok && obj->tok->file ? obj->tok->file->name : NULL;
        bool same_file       = files_are_same(first_file, this_file);
        bool nested_involved = (obj->is_nested && !obj->is_block) ||
                               (first->is_nested && !first->is_block);
        if (same_file && !nested_involved)
            continue;
        obj->name = arena_format(vm, "%s__cccc_dup%d", obj->name,
                                 ctx->anon_global_counter++);
    }
    hashmap_deinit_borrowed(&anchors);
    hashmap_deinit_borrowed(&claimed);
#if !defined(_WIN32)
    if (libc_handle)
        dlclose(libc_handle);
#endif
}

// #1014: does `ty` (or anything reachable through a PTR/ARRAY/VLA/FUNC
// chain -- deliberately not through struct/union members, mirroring
// hoist_local_type_to_file_scope()'s own reasoning: a member's type is
// reached through its own uses/definitions elsewhere, and this scan doesn't
// need a cycle guard as a result) match `group_ty` under same_type_strong()?
// Used to decide whether an externally-visible Obj's signature "votes for"
// a tag-collision group (rename_colliding_type_tags()'s tier-2 keeper
// signal).
static bool type_reaches_group(Type *ty, Type *group_ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_ENUM)
        return same_type_strong(ty, group_ty);
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_reaches_group(ty->base, group_ty);
    if (ty->kind == TY_FUNC) {
        if (type_reaches_group(ty->return_ty, group_ty))
            return true;
        for (Type *p = ty->params; p; p = p->next)
            if (type_reaches_group(p, group_ty))
                return true;
        return false;
    }
    return false;
}

// #1014: two translation units can each independently *complete* a
// same-named but differently-shaped struct/union/enum tag -- the
// opaque-handle idiom, where a shared header only forward-declares the tag
// and each .c privately completes it (e.g. one .c per backend
// implementation). same_type_or_origin() correctly treats the two
// completions as different types (tag matches, structural comparison
// fails), so they are never wrongly deduped by collect_type() -- but
// nothing renames them apart either, and both reach serialize_struct_def()/
// serialize_enum_def() under the identical plain tag name, producing a hard
// "redefinition of 'DyGC'" from the host compiler. This is the tag-level
// analogue of rename_colliding_static_names() just above -- that pass only
// ever renames Obj (function/variable) names, never a struct/union/enum
// tag.
//
// Unlike an Obj name collision, at most one of the colliding groups can
// keep the plain spelling: a replayed `#include` of the shared header binds
// its own uses of the tag *textually*, so whichever group is "header-
// exposed" (a from_include TypeName record resolves to it) MUST keep the
// plain name or the replayed header's own prototypes stop matching
// (verified: renaming the header-exposed group produces a host "conflicting
// types" error where the un-renamed one compiles clean). If more than one
// group is header-exposed -- a replayed header genuinely declares entities
// of *both* shapes -- the collision is unrepresentable in flat C by any
// renaming; this pass still renames deterministically (first-created wins)
// rather than leaving the collision maximally ambiguous, and the host
// compiler is left to report whatever residual conflict remains (see
// man/COVERAGE.md's serialized-output-divergences section).
//
// Renames every non-keeper group's records -- both in ctx->tags (spelling)
// and in ctx->typedefs (a `typedef struct DyGC DyGC;` written in the .c
// itself, not the header, would otherwise turn a struct redefinition into a
// typedef-redefinition-with-different-types error once the struct itself is
// renamed) -- to `<name>__cccc_dup<N>`, sharing rename_colliding_static_
// names()'s suffix and ctx->anon_global_counter. A from_include typedef
// record is left untouched: serialize_typedef_alias() already suppresses
// those (#891), and the header text supplying the plain spelling can't be
// rewritten anyway.
static void rename_colliding_type_tags(VirtualMachine *vm, Obj *prog,
                                       SerializeContext *ctx) {
    // One entry per distinct colliding group discovered so far.
    typedef struct {
        Type *rep;        // representative (first-seen) Type* for this group
        int   name_len;
        char *name;
        int   first_seen; // lower = created earlier (creation-order index)
        bool  header_exposed; // a from_include tag/typedef record names this
                              // group
        bool extern_ref; // an externally-visible definition's type reaches this
                         // group
    } TagGroup;
    TagGroup *groups     = NULL;
    int       groups_len = 0, groups_cap = 0;
    // Names seen more than once by more than one distinct group -- only
    // these need renaming at all.
    bool *collided = NULL;

    // ctx->tags is in reverse record-creation order (record_type_name()
    // prepends; collect_scope_names() walks head-first) -- walk it back to
    // front so `first_seen` below is a true creation-order index, matching
    // the doc comment above and giving a deterministic first-created
    // tie-break.
    for (int i = ctx->tags_len - 1; i >= 0; i--) {
        TypeName *rec = &ctx->tags[i];
        if (rec->owner_fn !=
            NULL) // function-local: hoist_local_type_to_file_scope()'s
                  // territory
            continue;
        if (!type_is_complete_tagged(rec->ty)) // an incomplete record must
                                               // never define/claim a group
            continue;

        // A record only ever joins an *existing* group when it names the
        // same tag AND matches its shape; a same-named-but-differently-
        // shaped record instead falls through and becomes its own new
        // group below -- the dedicated pass right after this loop is what
        // actually detects and marks the resulting name collision.
        int found = -1;
        for (int g = 0; g < groups_len; g++) {
            if (groups[g].name_len == rec->name_len &&
                strncmp(groups[g].name, rec->name, rec->name_len) == 0 &&
                same_type_strong(groups[g].rep, rec->ty)) {
                found = g;
                break;
            }
        }
        if (found >= 0)
            continue;

        if (groups_len == groups_cap) {
            groups_cap = groups_cap ? groups_cap * 2 : 8;
            groups     = realloc(groups, sizeof(TagGroup) * groups_cap);
            collided   = realloc(collided, sizeof(bool) * groups_cap);
        }
        groups[groups_len].rep      = rec->ty;
        groups[groups_len].name_len = rec->name_len;
        groups[groups_len].name     = rec->name;
        groups[groups_len].first_seen =
            groups_len; // creation order, since we walk creation-ordered
        groups[groups_len].header_exposed = false;
        groups[groups_len].extern_ref     = false;
        collided[groups_len]              = false;
        groups_len++;
    }

    if (groups_len == 0)
        return;

    // Mark actual name collisions: any two distinct groups sharing a name.
    for (int g1 = 0; g1 < groups_len; g1++)
        for (int g2 = g1 + 1; g2 < groups_len; g2++)
            if (groups[g1].name_len == groups[g2].name_len &&
                strncmp(groups[g1].name, groups[g2].name,
                        groups[g1].name_len) == 0) {
                collided[g1] = true;
                collided[g2] = true;
            }

    bool any_collision = false;
    for (int g = 0; g < groups_len; g++)
        if (collided[g])
            any_collision = true;
    if (!any_collision) {
        free(groups);
        free(collided);
        return;
    }

    // Tier 1: header-exposed -- a from_include tag or typedef record names
    // this group (from_include is command-line-input-keyed since #1006, so
    // this is exactly "a replayed #include names this tag").
    for (int i = 0; i < ctx->tags_len; i++) {
        if (!ctx->tags[i].from_include || ctx->tags[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (collided[g] && same_type_strong(ctx->tags[i].ty, groups[g].rep))
                groups[g].header_exposed = true;
    }
    for (int i = 0; i < ctx->typedefs_len; i++) {
        if (!ctx->typedefs[i].from_include || ctx->typedefs[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (collided[g] &&
                same_type_strong(ctx->typedefs[i].ty, groups[g].rep))
                groups[g].header_exposed = true;
    }

    // Tier 2: an externally-visible definition's type reaches this group --
    // needed because tier 1 alone can't pick the implementation TU's group
    // when the private TU happens to be listed (and hence created) first.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static)
            continue;
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining || !obj->ty)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (collided[g] && type_reaches_group(obj->ty, groups[g].rep))
                groups[g].extern_ref = true;
    }

    // Keeper choice per colliding name, composed as successive filters:
    // restrict to header-exposed groups if any exist among that name's
    // colliding groups, then prefer extern_ref, then lowest first_seen.
    for (int g = 0; g < groups_len; g++) {
        if (!collided[g] || groups[g].rep == NULL)
            continue; // already resolved as part of an earlier group's pass, or
                      // not a collider

        // Gather every group sharing this exact name.
        int members[64];
        int members_len = 0;
        for (int g2 = g; g2 < groups_len && members_len < 64; g2++)
            if (collided[g2] && groups[g2].rep &&
                groups[g2].name_len == groups[g].name_len &&
                strncmp(groups[g2].name, groups[g].name, groups[g].name_len) ==
                    0)
                members[members_len++] = g2;
        if (members_len < 2)
            continue;

        bool any_header_exposed = false;
        for (int m = 0; m < members_len; m++)
            if (groups[members[m]].header_exposed)
                any_header_exposed = true;

        int keeper = -1;
        for (int m = 0; m < members_len; m++) {
            int idx = members[m];
            if (any_header_exposed && !groups[idx].header_exposed)
                continue;
            if (keeper < 0)
                keeper = idx;
            else if (groups[idx].extern_ref && !groups[keeper].extern_ref)
                keeper = idx;
            else if (groups[idx].extern_ref == groups[keeper].extern_ref &&
                     groups[idx].first_seen < groups[keeper].first_seen)
                keeper = idx;
        }
        if (keeper < 0)
            keeper = members[0];

        for (int m = 0; m < members_len; m++) {
            int idx = members[m];
            if (idx == keeper) {
                groups[idx].rep =
                    NULL; // mark resolved, skip on future outer iterations
                continue;
            }
            char *new_name =
                arena_format(vm, "%.*s__cccc_dup%d", groups[idx].name_len,
                             groups[idx].name, ctx->anon_global_counter++);
            int   new_len = (int)strlen(new_name);
            Type *victim  = groups[idx].rep;
            for (int i = 0; i < ctx->tags_len; i++)
                if (same_type_strong(ctx->tags[i].ty, victim)) {
                    ctx->tags[i].name     = new_name;
                    ctx->tags[i].name_len = new_len;
                }
            for (int i = 0; i < ctx->typedefs_len; i++)
                if (!(ctx->typedefs[i].from_include &&
                      !ctx->typedefs[i].always_emit) &&
                    same_type_strong(ctx->typedefs[i].ty, victim))
                    ctx->typedefs[i].name     = new_name,
                    ctx->typedefs[i].name_len = new_len;
            groups[idx].rep  = NULL; // mark resolved
            ctx->tag_renamed = true;
        }
    }

    free(groups);
    free(collided);
}

// #1015: two translation units can each independently declare a same-named
// enumerator inside a differently-shaped enum -- reachable even when the
// enclosing enum's own tag doesn't collide (different tags, same
// enumerator) or has no tag at all (a tagless `typedef enum { ... } T;`,
// which never forms a group in rename_colliding_type_tags() above, since
// that pass only ever walks ctx->tags). Renaming the tag apart (#1014)
// does nothing for this: same_type_or_origin()'s TY_ENUM arm compares
// enumerators by strcmp on EnumConstant.name, entirely independent of
// whichever (possibly renamed) tag spelling ctx->tags now carries -- it's
// ec->name that collides in the emitted C, not the enum's own name.
//
// Groups every distinct complete enum Type (same_type_strong-deduped,
// found via either a tag or a typedef record so a tagless typedef'd enum
// is covered too), then for every enumerator name shared by two or more
// distinct groups, renames every group's copy but one -- via the print-
// time ctx->enum_renames table (consulted by enum_const_spelling()), never
// by mutating EnumConstant.name itself; see that field's doc comment on
// SerializeContext for why a mutation would silently reintroduce the
// collision (same_type_or_origin's own enumerator comparison would then
// disagree with the pre-rename groups this pass computed).
//
// Keeper selection deliberately mirrors rename_colliding_type_tags()'s own
// tiers, in the same order, so the two passes always agree on which group
// keeps the plain spelling -- disagreeing would print `enum E__cccc_dup0 {
// AA }` next to a *different* group's `enum E { AA__cccc_dup1 }`: legal C,
// but visibly incoherent output. Tier 1 here is a hard rule, not a
// preference: a header-exposed group's enumerators are never renamed, full
// stop -- the replayed #include binds AA textually inside the header's own
// code, and renaming it there breaks the header (the same failure #1014
// verified by hand-compiling a renamed header-exposed tag).
static void rename_colliding_enum_constants(VirtualMachine *vm, Obj *prog,
                                            SerializeContext *ctx) {
    typedef struct {
        Type *rep;
        int   first_seen;
        bool  header_exposed;
        bool  extern_ref;
        // #1017: the from_include record's file_path, captured alongside
        // header_exposed below -- names the header in the residual warning
        // when this group collides with an un-renameable Obj. May stay NULL
        // (TypeName.file_path itself can be NULL), in which case the
        // warning falls back to not naming a header.
        const char *header_path;
    } EnumGroup;
    EnumGroup *groups     = NULL;
    int        groups_len = 0, groups_cap = 0;

    // #1016: neither this pass nor rename_colliding_type_tags()/
    // rename_colliding_static_names() looks at the other's namespace, but C
    // has one ordinary identifier namespace at file scope -- an enumerator
    // can collide with a plain static/extern/function name just as easily
    // as with another enumerator. Build the set of every emitted file-scope
    // Obj name once, up front, so the per-name loop below can also treat an
    // Obj as a (single, un-renameable) "group" occupying a name. Must run
    // after rename_anon_globals()/rename_colliding_static_names() -- reading
    // obj->name here needs their final, possibly-already-renamed spelling,
    // the same ordering requirement #1002's own comment documents for a
    // different reason. Deliberately no is_defining filter (unlike #1002's
    // own Obj scan just above): #1002 only cares about two *definitions*
    // colliding, but here a bare prototype (`int AA(void);`) or `extern`
    // declaration already occupies the ordinary-identifier namespace an
    // enum constant shares, so it must be in this set too.
    HashMap obj_names = {0};
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->name[0] == '.')
            continue;
        if (ctx->generated_only && !obj->is_macro_generated)
            continue;
        hashmap_put_borrowed(&obj_names, obj->name, obj);
    }

    // Collect one entry per distinct complete enum Type, from ctx->tags and
    // ctx->typedefs alike, each walked back-to-front for an approximate
    // creation order (exact only within each list -- the two don't share a
    // common index -- used only as a last-resort tie-break, the same rigor
    // rename_colliding_type_tags() itself relies on for its own first_seen).
    for (int pass = 0; pass < 2; pass++) {
        TypeName *recs     = pass == 0 ? ctx->tags : ctx->typedefs;
        int       recs_len = pass == 0 ? ctx->tags_len : ctx->typedefs_len;
        for (int i = recs_len - 1; i >= 0; i--) {
            TypeName *rec = &recs[i];
            if (rec->owner_fn !=
                NULL) // function-local: not this pass's concern
                continue;
            if (!rec->ty || rec->ty->kind != TY_ENUM)
                continue;
            if (!type_is_complete_tagged(rec->ty))
                continue;

            bool found = false;
            for (int g = 0; g < groups_len; g++)
                if (same_type_strong(groups[g].rep, rec->ty)) {
                    found = true;
                    break;
                }
            if (found)
                continue;

            if (groups_len == groups_cap) {
                groups_cap = groups_cap ? groups_cap * 2 : 8;
                groups     = realloc(groups, sizeof(EnumGroup) * groups_cap);
            }
            groups[groups_len].rep            = rec->ty;
            groups[groups_len].first_seen     = groups_len;
            groups[groups_len].header_exposed = false;
            groups[groups_len].extern_ref     = false;
            groups[groups_len].header_path    = NULL;
            groups_len++;
        }
    }

    // #1016: a single enum group can still collide with an Obj name, so the
    // old groups_len < 2 bail-out (nothing to compare a lone group against)
    // is only safe when there are no Obj names to check it against either.
    if (groups_len < 1 || (groups_len < 2 && obj_names.used == 0)) {
        free(groups);
        hashmap_deinit_borrowed(&obj_names);
        return;
    }

    // Tier 1: header-exposed -- a from_include tag or typedef record names
    // this group.
    for (int i = 0; i < ctx->tags_len; i++) {
        if (!ctx->tags[i].from_include || ctx->tags[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (same_type_strong(ctx->tags[i].ty, groups[g].rep)) {
                groups[g].header_exposed = true;
                if (!groups[g].header_path)
                    groups[g].header_path = ctx->tags[i].file_path;
            }
    }
    for (int i = 0; i < ctx->typedefs_len; i++) {
        if (!ctx->typedefs[i].from_include || ctx->typedefs[i].always_emit)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (same_type_strong(ctx->typedefs[i].ty, groups[g].rep)) {
                groups[g].header_exposed = true;
                if (!groups[g].header_path)
                    groups[g].header_path = ctx->typedefs[i].file_path;
            }
    }

    // Tier 2: an externally-visible definition's type reaches this group.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_static)
            continue;
        bool is_defining =
            obj->is_function ? obj->body != NULL : obj->is_definition;
        if (!is_defining || !obj->ty)
            continue;
        for (int g = 0; g < groups_len; g++)
            if (type_reaches_group(obj->ty, groups[g].rep))
                groups[g].extern_ref = true;
    }

    // Every distinct enumerator name declared by at least one group,
    // resolved exactly once below.
    char **names     = NULL;
    int    names_len = 0, names_cap = 0;
    for (int g = 0; g < groups_len; g++)
        for (EnumConstant *ec = groups[g].rep->enum_constants; ec;
             ec               = ec->next) {
            if (!ec->name)
                continue;
            bool seen = false;
            for (int n = 0; n < names_len; n++)
                if (strcmp(names[n], ec->name) == 0) {
                    seen = true;
                    break;
                }
            if (seen)
                continue;
            if (names_len == names_cap) {
                names_cap = names_cap ? names_cap * 2 : 16;
                names     = realloc(names, sizeof(char *) * names_cap);
            }
            names[names_len++] = ec->name;
        }

    for (int n = 0; n < names_len; n++) {
        const char *name = names[n];

        // Every distinct group declaring this exact enumerator name.
        int members[64];
        int members_len = 0;
        for (int g = 0; g < groups_len && members_len < 64; g++) {
            for (EnumConstant *ec = groups[g].rep->enum_constants; ec;
                 ec               = ec->next)
                if (ec->name && strcmp(ec->name, name) == 0) {
                    members[members_len++] = g;
                    break;
                }
        }
        // #1016: does an emitted file-scope Obj already occupy this
        // spelling? An Obj is never renamed by this pass (external linkage
        // makes that unsafe in general, see the function's own doc comment
        // above), so when true no enum group below can keep the plain name
        // either -- the Obj holds it unconditionally. #1017: keep the Obj*
        // itself (not just a bool) so the tier-1 residual warning below can
        // name and point at it.
        Obj *colliding_obj = hashmap_get(&obj_names, name);
        bool obj_collision = colliding_obj != NULL;
        if (members_len < 2 && !obj_collision)
            continue;

        bool any_header_exposed = false;
        for (int m = 0; m < members_len; m++)
            if (groups[members[m]].header_exposed)
                any_header_exposed = true;

        int keeper = -1;
        if (!obj_collision) {
            for (int m = 0; m < members_len; m++) {
                int idx = members[m];
                if (any_header_exposed && !groups[idx].header_exposed)
                    continue;
                if (keeper < 0)
                    keeper = idx;
                else if (groups[idx].extern_ref && !groups[keeper].extern_ref)
                    keeper = idx;
                else if (groups[idx].extern_ref == groups[keeper].extern_ref &&
                         groups[idx].first_seen < groups[keeper].first_seen)
                    keeper = idx;
            }
            if (keeper < 0)
                keeper = members[0];
        }

        for (int m = 0; m < members_len; m++) {
            int idx = members[m];
            if (idx == keeper)
                continue;
            // #1016: tier 1 stays a hard rule even when an Obj occupies the
            // name -- a header-exposed group's enumerators are never
            // renamed (the replayed #include binds the name textually
            // inside the header's own code, same reasoning #1014/#1015
            // already established). The residual Obj-vs-header conflict is
            // genuinely unrepresentable in flat C (neither name can be
            // renamed without breaking something else) and is left for the
            // host compiler to report; see man/COVERAGE.md. #1017: at
            // least point at it first, since the host compiler's own
            // diagnostic names a deleted /tmp temp file under -c=native
            // with no indication cccc's renamer is involved. Guard
            // colliding_obj->tok != NULL -- a comptime-synthesized Obj
            // need not carry a token, and warn_tok() dereferences
            // tok->file->name unconditionally.
            if (obj_collision && groups[idx].header_exposed) {
                if (colliding_obj->tok) {
                    if (groups[idx].header_path)
                        warn_tok(vm, colliding_obj->tok,
                                 CCCC_WARN_NATIVE_NAME_COLLISION,
                                 "enumerator '%s' is declared by an enum "
                                 "reached through a "
                                 "replayed #include ('%s') and cannot be "
                                 "renamed; the "
                                 "file-scope '%s' declared here cannot be "
                                 "renamed either, "
                                 "so the generated C will not compile",
                                 name, groups[idx].header_path, name);
                    else
                        warn_tok(vm, colliding_obj->tok,
                                 CCCC_WARN_NATIVE_NAME_COLLISION,
                                 "enumerator '%s' is declared by an enum "
                                 "reached through a "
                                 "replayed #include and cannot be renamed; the "
                                 "file-scope "
                                 "'%s' declared here cannot be renamed either, "
                                 "so the "
                                 "generated C will not compile",
                                 name, name);
                }
                continue;
            }
            char *new_name = arena_format(vm, "%s__cccc_dup%d", name,
                                          ctx->anon_global_counter++);
            if (ctx->enum_renames_len >= ctx->enum_renames_cap) {
                ctx->enum_renames_cap =
                    ctx->enum_renames_cap ? ctx->enum_renames_cap * 2 : 8;
                ctx->enum_renames =
                    realloc(ctx->enum_renames,
                            sizeof(EnumConstRename) * ctx->enum_renames_cap);
            }
            ctx->enum_renames[ctx->enum_renames_len].rep      = groups[idx].rep;
            ctx->enum_renames[ctx->enum_renames_len].orig     = (char *)name;
            ctx->enum_renames[ctx->enum_renames_len].new_name = new_name;
            ctx->enum_renames_len++;
        }
    }

    free(names);
    free(groups);
    hashmap_deinit_borrowed(&obj_names);
}

// #1015: print-time lookup for serialize_enum_def()'s enumerator loop,
// consulting the table rename_colliding_enum_constants() built above -- see
// ctx->enum_renames' doc comment on SerializeContext for why this is a
// lookup rather than an EnumConstant.name mutation. Returns `name`
// unchanged when nothing was renamed for this (ty, name) pair, so a
// program with no enumerator collision serializes byte-identically to
// before this pass existed.
const char *enum_const_spelling(SerializeContext *ctx, Type *ty,
                                const char *name) {
    if (!name)
        return name;
    for (int i = 0; i < ctx->enum_renames_len; i++)
        if (same_type_strong(ctx->enum_renames[i].rep, ty) &&
            strcmp(ctx->enum_renames[i].orig, name) == 0)
            return ctx->enum_renames[i].new_name;
    return name;
}

// #953: hashmap_foreach callback collecting emit_include_paths' values
// (resolved paths of auto-captured #include directives) into
// ctx->captured_paths for path_is_captured() to scan.
static int collect_captured_path(char *key, int keylen, void *val,
                                 void *user_data) {
    (void)key;
    (void)keylen;
    SerializeContext *ctx = user_data;
    ctx->captured_paths   = realloc(
        ctx->captured_paths, sizeof(char *) * (ctx->captured_paths_len + 1));
    ctx->captured_paths[ctx->captured_paths_len++] = val;
    return 0;
}

// #965: does `node` (or anything reachable from it) directly call `target`
// -- matched by identity against the callee's own ND_VAR, the shape
// Block_copy(block) lowers to (parse.c). Mirrors collect_node_types's
// traversal shape. Used only to decide whether serialize_block_preamble
// needs to emit the native __cccc_block_copy_impl replacement.
static bool node_calls_obj(Node *node, Obj *target) {
    if (!node)
        return false;
    if (node->kind == ND_FUNCALL && node->lhs && node->lhs->kind == ND_VAR &&
        node->lhs->var == target)
        return true;
    return node_calls_obj(node->lhs, target) ||
           node_calls_obj(node->rhs, target) ||
           node_calls_obj(node->cond, target) ||
           node_calls_obj(node->then, target) ||
           node_calls_obj(node->els, target) ||
           node_calls_obj(node->init, target) ||
           node_calls_obj(node->inc, target) ||
           node_calls_obj(node->body, target) ||
           node_calls_obj(node->args, target) ||
           node_calls_obj(node->next, target);
}

// #1050: a comptime builder (e.g. Serialize's Memcpy()) can call
// memcpy/strlen/strcmp/etc via a synthetic Obj that ensure_libc_fn_decl()
// (reflection.c) creates on the fly, with no #include of its own in the
// TU (the whole point -- it works even when the TU never #includes
// <string.h>). That Obj has no token/file, so the ordinary auto-capture
// machinery has nothing to replay for it, and -c=native would otherwise
// print a bare, undeclared call. Emit the real header instead of a
// prototype (a prototype's necessarily-loose signature -- void* args,
// unsigned long size -- could conflict with the exact one <string.h>
// itself brings in, transitively, elsewhere in the same TU); reuses
// node_calls_obj's identity match, same as the Block_copy/free check just
// above, so a program that never calls a given synthesized decl doesn't
// get its header either. Deliberately not attempted for -c=generated
// (generated_only returns earlier in cc_serialize_program, replaying
// includes via CCCC_EMIT_SOURCE events instead) -- residual, not this
// ticket's scope.
// #1050: true if `prog` contains a *different* Obj, written in one of the
// user's own command-line input files, sharing `name` -- i.e. the program
// declares its own memcpy/strlen/strcmp/etc (however unusual; shadowing a
// libc name at file scope is legal C). register_synth_libc_call()
// (reflection.c) already skips registering such an Obj directly, but the
// reflection API's own identifier resolution can still resolve a call to a
// *different*, CCCC-injected Obj of the same name first (var_ref_lookup's
// scope search finds whichever declaration is nearer, and reflection.h's
// own implicit `#include <string.h>` parse can sit ahead of the user's
// own declaration) -- so the registered entry's Obj identity alone isn't
// enough to rule this out. Forcing `#include <string.h>` in on top of the
// user's own real declaration is worse than the gap this ticket fixes (a
// straight 'static declaration follows non-static declaration' compile
// failure that didn't exist before); skip the header entirely when the
// user has their own colliding declaration; the ordinary auto-capture/
// forward-declare-every-function machinery already covers *that* Obj.
static bool has_colliding_user_decl(VirtualMachine *vm, Obj *prog,
                                    const char *name, Obj *registered_obj) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj == registered_obj || !obj->is_function)
            continue;
        if (strcmp(obj->name, name) != 0)
            continue;
        if (obj->tok && obj->tok->file &&
            cc_file_is_command_line_input(vm, obj->tok->file->name))
            return true;
    }
    return false;
}

static void serialize_synth_libc_includes(FILE *f, VirtualMachine *vm,
                                          Obj *prog) {
    SynthLibcDeclArray *reg = &vm->compiler.synth_libc_decls;
    const char         *emitted[32];
    int                 emitted_len = 0;
    bool                any         = false;
    for (int i = 0; i < reg->len; i++) {
        SynthLibcDecl *entry  = &reg->data[i];
        bool           called = false;
        for (Obj *obj = prog; obj && !called; obj = obj->next) {
            if (!obj->is_function || !obj->body)
                continue;
            called = node_calls_obj(obj->body, entry->obj);
        }
        if (!called)
            continue;
        if (has_colliding_user_decl(vm, prog, entry->obj->name, entry->obj))
            continue;
        bool already = false;
        for (int j = 0; j < emitted_len; j++)
            if (!strcmp(emitted[j], entry->header)) {
                already = true;
                break;
            }
        if (already)
            continue;
        fprintf(f, "#include <%s>\n", entry->header);
        any = true;
        if (emitted_len < (int)(sizeof(emitted) / sizeof(emitted[0])))
            emitted[emitted_len++] = entry->header;
    }
    if (any)
        fprintf(f, "\n");
}

// #1054/#1030: setjmp/longjmp (and their _setjmp/_longjmp POSIX-variant
// aliases, parse_decl.c) are on is_compiler_owned_header's list -- CCCC's
// own jmp_buf is a VM-bytecode-specific 5-slot layout with no host ABI
// equivalent (include/setjmp.h) -- but unlike every *other* owned header
// (stdbool.h/stdint.h/etc, type-only, nothing to call), setjmp.h also
// declares real functions that -c=native's generated C needs to actually
// *call* into the real host libc. Relying on the auto-captured `#include
// <setjmp.h>` line to resolve to the real host header at native-compile
// time is fragile: it depends on include search-path ordering the
// generated C has no control over (a user -I path that happens to also
// contain CCCC's own bundled headers -- e.g. the whole test suite's own
// `-I./include` -- shadows the real header with CCCC's declaration-free
// copy, "call to undeclared library function"). Sidestep entirely: never
// replay `#include <setjmp.h>` into -c=native/-m output (see this
// function's caller), and instead declare exactly the two symbols these
// four builtins are unconditionally lowered to (see the ND_FUNCALL case
// below) ourselves, with a signature (`void *`) that needs no jmp_buf type
// at all. `_setjmp`/`_longjmp` are chosen over plain `setjmp`/`longjmp`
// deliberately: on macOS both pairs are ordinary exported functions, but
// on glibc `setjmp` is a *macro* (`#define setjmp(env) _setjmp(env)`,
// verified against the real glibc header) while `_setjmp`/`_longjmp`
// remain plain `extern` declarations on both platforms -- so declaring and
// calling them directly needs nothing from any header, on either host.
// This also matches VM semantics exactly: the VM's own SETJMP/LONGJMP
// opcodes never touch a signal mask (ops.c), the same behavior `_setjmp`/
// `_longjmp` document (parse_decl.c's own comment on this).
static void serialize_synth_setjmp_decls(FILE *f, VirtualMachine *vm,
                                         Obj *prog) {
    Obj *family[4] = {
        vm->compiler.builtin_setjmp,
        vm->compiler.builtin_longjmp,
        vm->compiler.builtin__setjmp,
        vm->compiler.builtin__longjmp,
    };
    bool used = false;
    for (Obj *obj = prog; obj && !used; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        for (int i = 0; i < 4 && !used; i++)
            if (family[i] && node_calls_obj(obj->body, family[i]))
                used = true;
    }
    if (!used)
        return;
    fprintf(f, "extern int _setjmp(void *);\n");
    fprintf(f,
            "extern void _longjmp(void *, int) __attribute__((noreturn));\n\n");
}

// #1068: real-floating -> non-floating cast helpers, emitted on demand for
// -c=native/-m output -- see the ND_CAST case in serialize_expr (above in
// this file) for the full rationale. Near-verbatim ports of
// cccc_f64_to_i64/cccc_f32_to_i64/cccc_f64_to_u64/cccc_f32_to_u64
// (src/internal.h, #775/#780) so native output agrees with the VM's own
// F2I3/F2U3 opcodes by construction. Deliberately avoid <math.h>/<limits.h>:
// <math.h> is a bundled header whose own polyfill content (isnan() among
// it) would need the identical #include_next hand-off <fenv.h>/<setjmp.h>
// already need (see include/fenv.h's own comment) to resolve correctly
// under the real -I./include-forwarding harness, and there is no reason to
// take on that dependency here when a bit-pattern NaN test needs nothing
// from any header -- `x != x` is a reliable IEEE-754 NaN test (NaN is the
// only value unequal to itself) as long as the host isn't built with
// -ffast-math, which -c=native's own invocation never passes. The 2^63/2^64
// bounds and INT64_MIN/UINT64_MAX values are spelled as literals for the
// same reason internal.h's own versions are: `(double)LLONG_MAX` rounds
// *up* to exactly 2^63, so a "<=" guard against it would wrongly admit
// x == 2^63. `#pragma STDC FENV_ACCESS ON` is block-scoped to each
// function body (verified in-container to survive -O2/-O3 there; a
// file-scope pragma placed just once before all four would also work but
// would needlessly extend to the rest of the generated TU) -- without it,
// clang can fold the u64 helpers' guard back into the branchless
// double/float->uint64 lowering that spuriously raises FE_INVALID on
// x86_64 even for a proven in-range value, defeating the whole point of
// the trailing feclearexcept(FE_INVALID) below.
static const char *const f2i64_def =
    "static long long __cccc_f2i64(double x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 9223372036854775808.0) { feraiseexcept(FE_INVALID); return "
    "9223372036854775807LL; }\n"
    "    if (x < -9223372036854775808.0) { feraiseexcept(FE_INVALID); return "
    "(-9223372036854775807LL - 1); }\n"
    "    return (long long)x;\n"
    "}\n";
static const char *const f2i64_f32_def =
    "static long long __cccc_f2i64_f32(float x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 9223372036854775808.0f) { feraiseexcept(FE_INVALID); return "
    "9223372036854775807LL; }\n"
    "    if (x < -9223372036854775808.0f) { feraiseexcept(FE_INVALID); return "
    "(-9223372036854775807LL - 1); }\n"
    "    return (long long)x;\n"
    "}\n";
static const char *const f2u64_def =
    "static unsigned long long __cccc_f2u64(double x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 18446744073709551616.0) { feraiseexcept(FE_INVALID); return "
    "0xFFFFFFFFFFFFFFFFULL; }\n"
    "    if (x <= -1.0) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    unsigned long long r = (unsigned long long)x;\n"
    "    feclearexcept(FE_INVALID);\n"
    "    return r;\n"
    "}\n";
static const char *const f2u64_f32_def =
    "static unsigned long long __cccc_f2u64_f32(float x) {\n"
    "#pragma STDC FENV_ACCESS ON\n"
    "    if (x != x) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    if (x >= 18446744073709551616.0f) { feraiseexcept(FE_INVALID); return "
    "0xFFFFFFFFFFFFFFFFULL; }\n"
    "    if (x <= -1.0f) { feraiseexcept(FE_INVALID); return 0; }\n"
    "    unsigned long long r = (unsigned long long)x;\n"
    "    feclearexcept(FE_INVALID);\n"
    "    return r;\n"
    "}\n";

typedef struct {
    bool want_i64, want_i64_f32, want_u64, want_u64_f32;
} F2ISynthNeed;

// Same recursive-field traversal shape as node_calls_obj (above) --
// exhaustive over every child-pointing field Node has, not just the ones
// this particular predicate happens to reach in this repo's own test
// corpus.
static void node_scan_f2i_native(Node *node, F2ISynthNeed *need) {
    if (!node)
        return;
    if (node->kind == ND_CAST && node->lhs) {
        Type *dst = node->ty;
        Type *src = node->lhs->ty;
        if (src && dst && is_flonum(src) && !is_flonum(dst) &&
            dst->kind != TY_VECTOR &&
            !(dst->kind == TY_BITINT && dst->bit_width > 64)) {
            bool u64_dst =
                is_integer(dst) && dst->is_unsigned && dst->size == 8;
            bool f32_src = src->kind == TY_FLOAT;
            if (u64_dst && f32_src)
                need->want_u64_f32 = true;
            else if (u64_dst)
                need->want_u64 = true;
            else if (f32_src)
                need->want_i64_f32 = true;
            else
                need->want_i64 = true;
        }
    }
    node_scan_f2i_native(node->lhs, need);
    node_scan_f2i_native(node->rhs, need);
    node_scan_f2i_native(node->cond, need);
    node_scan_f2i_native(node->then, need);
    node_scan_f2i_native(node->els, need);
    node_scan_f2i_native(node->init, need);
    node_scan_f2i_native(node->inc, need);
    node_scan_f2i_native(node->body, need);
    node_scan_f2i_native(node->args, need);
    node_scan_f2i_native(node->next, need);
}

static void serialize_synth_f2i_helpers(FILE *f, Obj *prog) {
    F2ISynthNeed need = {0};
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        node_scan_f2i_native(obj->body, &need);
    }
    if (!need.want_i64 && !need.want_i64_f32 && !need.want_u64 &&
        !need.want_u64_f32)
        return;
    fprintf(f, "#include <fenv.h>\n\n");
    if (need.want_i64)
        fprintf(f, "%s", f2i64_def);
    if (need.want_i64_f32)
        fprintf(f, "%s", f2i64_f32_def);
    if (need.want_u64)
        fprintf(f, "%s", f2u64_def);
    if (need.want_u64_f32)
        fprintf(f, "%s", f2u64_f32_def);
    fprintf(f, "\n");
}

// #1117: the bundled include/complex.h and include/tgmath.h spell every
// complex accessor as a cccc-internal builtin -- creal(z) -> __cccc_creal(z)
// etc. (include/complex.h), and tgmath's type-generic _Generic arms reach
// the l/f variants through cabsl/cargl (include/tgmath.h). Those names only
// have definitions inside the VM; parse_postfix.c lowers the calls to
// __builtin_creal*/__builtin_cimag*/__builtin_conj at AST level, but any
// plain spelled call that survives into the generated text as an ordinary
// identifier gets expanded by the HOST compiler instead -- and because
// run_native_backend forwards the guest's -I paths to the host cc
// (src/main.c), a replayed `#include <complex.h>`/`#include <tgmath.h>`
// resolves to CCCC's OWN bundled copies, whose macros then expand to
// __cccc_* names nothing ever defined for the host ("use of undeclared
// identifier '__cccc_creall'", test_suite_floats.c). Only the long-double
// arms happened to error under that corpus's exact invocation, but
// preprocessing the same output shows every double/float arm equally
// exposed depending on host/std -- so the whole family is emitted, not just
// the failing pair.
//
// Fix follows the #1050/#1054 synth-decl precedent: emit static inline
// definitions mapping each helper straight onto its __builtin_*, whenever
// the names are reachable. Reachable means ANY of:
//   - a captured #include replay resolved to a bundled complex.h or
//     tgmath.h (the replay re-defines creal/cabs/I/... as macros pointing
//     at __cccc_* for everything the host compiles afterwards),
//   - the program contains any TY_COMPLEX-typed object (the AST-level
//     lowering emits __builtin_* calls directly, but the same TU usually
//     spells accessors too, and there is no downside to covering it),
//   - the program declares any Obj whose name is one of the __cccc_c*
//     helpers themselves (e.g. reached via a private-header parse such as
//     reflection.h's implicit includes, which are deliberately NOT
//     auto-captured and so never replayed).
// The __cccc_cmplx/f/l constructors are included alongside the nine
// accessors: the replayed complex.h spells _Complex_I/I and CMPLX() in
// terms of them, so any macro text the host expands reaches them too.
// Emitted unconditionally rather than emit_cccc-gated, mirroring #1068's
// f2i reasoning: spelled text can appear under --emit-cccc output too.
// Unused static inline functions cost no codegen, so over-triggering is
// harmless; under-triggering is what produced #1117.
static const char *const complex_shim_defs[] = {
    "static inline double _Complex __cccc_cmplx(double re, double im) {\n"
    "    return __builtin_complex(re, im);\n"
    "}\n",
    "static inline float _Complex __cccc_cmplxf(float re, float im) {\n"
    "    return __builtin_complex(re, im);\n"
    "}\n",
    "static inline long double _Complex __cccc_cmplxl(long double re, long "
    "double im) {\n"
    "    return __builtin_complex(re, im);\n"
    "}\n",
#define CCCC_COMPLEX_SHIM(name, ret, arg, builtin)                             \
    "static inline " ret " " name "(" arg " _Complex z) {\n"                   \
    "    return __builtin_" builtin "(z);\n"                                   \
    "}\n",
    CCCC_COMPLEX_SHIM("__cccc_creal", "double", "double", "creal")
        CCCC_COMPLEX_SHIM("__cccc_crealf", "float", "float", "crealf")
            CCCC_COMPLEX_SHIM("__cccc_creall", "long double", "long double",
                              "creall") CCCC_COMPLEX_SHIM("__cccc_cimag",
                                                          "double", "double",
                                                          "cimag")
                CCCC_COMPLEX_SHIM("__cccc_cimagf", "float", "float", "cimagf")
                    CCCC_COMPLEX_SHIM("__cccc_cimagl", "long double",
                                      "long double", "cimagl")
                        CCCC_COMPLEX_SHIM("__cccc_conj", "double _Complex",
                                          "double", "conj")
                            CCCC_COMPLEX_SHIM("__cccc_conjf", "float _Complex",
                                              "float", "conjf")
                                CCCC_COMPLEX_SHIM("__cccc_conjl",
                                                  "long double _Complex",
                                                  "long double", "conjl")
#undef CCCC_COMPLEX_SHIM
};

// hashmap_foreach callback over emit_include_paths: true when any captured
// include resolved to a bundled complex.h/tgmath.h (#1117 -- see the shim
// table above for why those two specifically).
static int collect_complex_header_path(char *key, int keylen, void *val,
                                       void *user_data) {
    (void)key;
    (void)keylen;
    bool *want = user_data;
    if (path_basename_is(val, "complex.h") || path_basename_is(val, "tgmath.h"))
        *want = true;
    return 0;
}

// Cycle guard for type_scan_complex_native: a struct that points to its own
// kind (tree nodes, lua_State links, ...) would otherwise recurse forever,
// since peeling the pointer lands back on the same aggregate. collect_type()
// guards the equivalent walk with ctx->seen; this scanner is standalone, so
// it carries its own (tiny) visited list.
typedef struct {
    Type **data;
    int    len, cap;
} TypeSeenSet;

static bool type_seen_set_has(TypeSeenSet *set, Type *ty) {
    for (int i = 0; i < set->len; i++)
        if (set->data[i] == ty)
            return true;
    return false;
}

static void type_seen_set_push(TypeSeenSet *set, Type *ty) {
    if (set->len >= set->cap) {
        set->cap  = set->cap ? set->cap * 2 : 8;
        set->data = realloc(set->data, sizeof(Type *) * set->cap);
    }
    set->data[set->len++] = ty;
}

static void type_scan_complex_native(Type *ty, bool *want, TypeSeenSet *seen) {
    while (ty &&
           (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA))
        ty = ty->base;
    if (!ty || *want)
        return;
    if (ty->kind == TY_COMPLEX) {
        *want = true;
        return;
    }
    if ((ty->kind == TY_STRUCT || ty->kind == TY_UNION) && ty->members &&
        !type_seen_set_has(seen, ty)) {
        type_seen_set_push(seen, ty);
        for (Member *m = ty->members; m; m = m->next)
            type_scan_complex_native(m->ty, want, seen);
        return;
    }
    if (ty->kind == TY_FUNC) {
        type_scan_complex_native(ty->return_ty, want, seen);
        for (Type *p = ty->params; p; p = p->next)
            type_scan_complex_native(p, want, seen);
    }
}

// Same recursive-field traversal shape as node_scan_f2i_native (above) --
// exhaustive over every child-pointing field Node has.
static void node_scan_complex_native(Node *node, bool *want,
                                     TypeSeenSet *seen) {
    if (!node || *want)
        return;
    type_scan_complex_native(node->ty, want, seen);
    if (node->var)
        type_scan_complex_native(node->var->ty, want, seen);
    if (node->member)
        type_scan_complex_native(node->member->ty, want, seen);
    if (node->func_ty)
        type_scan_complex_native(node->func_ty, want, seen);
    node_scan_complex_native(node->lhs, want, seen);
    node_scan_complex_native(node->rhs, want, seen);
    node_scan_complex_native(node->cond, want, seen);
    node_scan_complex_native(node->then, want, seen);
    node_scan_complex_native(node->els, want, seen);
    node_scan_complex_native(node->init, want, seen);
    node_scan_complex_native(node->inc, want, seen);
    node_scan_complex_native(node->body, want, seen);
    node_scan_complex_native(node->args, want, seen);
    node_scan_complex_native(node->next, want, seen);
}

// True when `name` is one of the cccc-internal complex helpers themselves
// (#1117): reachable via a private-header parse (e.g. reflection.h's
// implicit includes), which is deliberately never auto-captured and hence
// never replayed -- nothing else would define them for the host.
static bool obj_name_is_complex_shim(const char *name) {
    static const char *const prefixes[] = {"__cccc_creal", "__cccc_cimag",
                                           "__cccc_conj", "__cccc_cmplx"};
    if (!name)
        return false;
    for (size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); i++)
        if (!strncmp(name, prefixes[i], strlen(prefixes[i])))
            return true;
    return false;
}

static void serialize_synth_complex_decls(FILE *f, VirtualMachine *vm,
                                          Obj *prog) {
    bool        want = false;
    TypeSeenSet seen = {0};
    hashmap_foreach(&vm->compiler.emit_include_paths,
                    collect_complex_header_path, &want);
    for (Obj *obj = prog; obj && !want; obj = obj->next) {
        if (obj_name_is_complex_shim(obj->name))
            want = true;
        else {
            type_scan_complex_native(obj->ty, &want, &seen);
            for (Obj *param = obj->params; param && !want; param = param->next)
                type_scan_complex_native(param->ty, &want, &seen);
            for (Obj *local = obj->locals; local && !want; local = local->next)
                type_scan_complex_native(local->ty, &want, &seen);
        }
        if (!want && obj->is_function && obj->body)
            node_scan_complex_native(obj->body, &want, &seen);
    }
    free(seen.data);
    if (!want)
        return;
    fprintf(f, "\n/* #1117: cccc-internal complex accessors the bundled "
               "complex.h/tgmath.h\n   macros expand to; mapped onto the "
               "host's own builtins */\n\n");
    for (size_t i = 0;
         i < sizeof(complex_shim_defs) / sizeof(complex_shim_defs[0]); i++)
        fprintf(f, "%s", complex_shim_defs[i]);
    fprintf(f, "\n");
}

// #1057: type-name sibling of #1050's synth-libc-call mechanism just above.
// A comptime builder can fold a standard scalar typedef name -- GetType(
// "size_t")/"ptrdiff_t"/"wchar_t" -- into a generated function's signature
// or body via cc_comptime_resolve_type_name()'s demand-driven splice
// (macros.c), which re-parses the typedef out of CCCC's own bundled
// include/stddef.h with no #include of it ever appearing in the TU. That
// leaves the recorded TypeNameRecord marked from_include=true (record_type_
// name, parse_core.c), so typedef_alias_header_suppressed() drops its alias
// line under -c=native/-m -- correctly, since the ordinary assumption is
// that the user's own #include supplies it -- but nothing here ever does,
// leaving a bare, undeclared name. Scoped to exactly the trio verified to
// match the real host's own typedef on every supported combo (LP64 macOS/
// Linux x aarch64/x86_64): long/unsigned long/int respectively (include/
// stddef.h). nullptr_t excluded (C23-only, typeof(nullptr)-defined, no
// repro); stdint.h's fixed-width names left for their own ticket if a repro
// turns up.
static const struct {
    const char *name;
    const char *header;
} synth_typedef_headers[] = {
    {"size_t", "stddef.h"},
    {"ptrdiff_t", "stddef.h"},
    {"wchar_t", "stddef.h"},
};

static const char *synth_typedef_header_for_name(const char *name,
                                                 int         name_len) {
    for (size_t i = 0;
         i < sizeof(synth_typedef_headers) / sizeof(synth_typedef_headers[0]);
         i++) {
        const char *cand = synth_typedef_headers[i].name;
        if ((int)strlen(cand) == name_len && !strncmp(cand, name, name_len))
            return synth_typedef_headers[i].header;
    }
    return NULL;
}

// #1057: pointer-identity lookup mirroring find_typedef_name_exact()'s #999
// ->origin-chain walk, deliberately *without* its name_visible() gate. That
// gate answers "would this name resolve inside function X's own scope" --
// the right question when deciding what to *print*, but the wrong one here:
// a comptime-spliced typedef's TypeNameRecord.owner_fn can point at whatever
// scratch/comptime context was current_fn when the splice ran, unrelated to
// which ordinary function's Type this is being checked against. This is only
// asking "does *any* recorded typedef, anywhere, identify this exact Type,"
// which is scope-independent.
static TypeName *find_typedef_record_any_scope(SerializeContext *ctx,
                                               Type             *ty) {
    if (!ctx || !ty)
        return NULL;
    for (int hop = 0; ty && hop < 8; ty = ty->origin, hop++)
        for (int i = 0; i < ctx->typedefs_len; i++)
            if (ctx->typedefs[i].ty == ty)
                return &ctx->typedefs[i];
    return NULL;
}

// #1057: skip the compensating #include when the program already declares
// its own top-level typedef of the same name, however it's shaped -- type-
// side analogue of #1050's has_colliding_user_decl(). An *identical*-shape
// user redeclaration is legal C either way (C11 6.7p3, confirmed: `typedef
// unsigned long size_t;` alongside CCCC's own comptime-resolved size_t
// compiles fine with or without this guard), but a differently-shaped one
// (e.g. a user `typedef struct {...} size_t;`) turns the forced #include
// into a hard "typedef redefinition with different types" that didn't exist
// before this fix -- confirmed directly, not just by analogy to #1050.
// Deliberately doesn't try to tell the two apart: any user declaration of
// the name is enough to defer to it and skip the header, matching #1050's
// own "skip entirely" choice for the same shape of risk.
static bool has_colliding_user_typedef(SerializeContext *ctx, const char *name,
                                       int name_len) {
    for (int i = 0; i < ctx->typedefs_len; i++) {
        TypeName *tn = &ctx->typedefs[i];
        if (tn->from_include || tn->always_emit)
            continue;
        if (tn->name_len == name_len && !strncmp(tn->name, name, name_len))
            return true;
    }
    return false;
}

// #1057: does `ty` (or anything structurally reachable from it) resolve to
// one of synth_typedef_headers' names whose alias line is being suppressed
// -- i.e. reached the program only through the comptime splice described
// above, with no user #include for serialize_synth_typedef_includes()
// (below) to piggyback on. Mirrors type_mentions_block()'s PTR/ARRAY/VLA/
// FUNC traversal shape, plus struct/union member recursion (guarded by
// `seen`, since unlike type_mentions_block a self-referential struct is a
// realistic shape to hit here, e.g. a linked-list node with a `size_t`
// field alongside a `struct node *next`).
static bool type_needs_synth_typedef_header(SerializeContext *ctx, Type *ty,
                                            const char *header, TypeVec *seen) {
    if (!ty)
        return false;
    TypeName *tn = find_typedef_record_any_scope(ctx, ty);
    if (tn && typedef_alias_header_suppressed(ctx, tn)) {
        const char *want =
            synth_typedef_header_for_name(tn->name, tn->name_len);
        if (want && !strcmp(want, header) &&
            !has_colliding_user_typedef(ctx, tn->name, tn->name_len))
            return true;
    }
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_needs_synth_typedef_header(ctx, ty->base, header, seen);
    if (ty->kind == TY_FUNC) {
        if (type_needs_synth_typedef_header(ctx, ty->return_ty, header, seen))
            return true;
        for (Type *p = ty->params; p; p = p->next)
            if (type_needs_synth_typedef_header(ctx, p, header, seen))
                return true;
        return false;
    }
    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        if (type_vec_contains(seen, ty))
            return false;
        type_vec_push(seen, ty);
        for (Member *m = ty->members; m; m = m->next)
            if (type_needs_synth_typedef_header(ctx, m->ty, header, seen))
                return true;
    }
    return false;
}

// #1057: mirrors collect_node_types()'s traversal shape (see also #990/#993's
// node_mentions_block, the same pattern for TY_BLOCK).
static bool node_needs_synth_typedef_header(SerializeContext *ctx, Node *node,
                                            const char *header, TypeVec *seen) {
    if (!node)
        return false;
    if (type_needs_synth_typedef_header(ctx, node->ty, header, seen))
        return true;
    if (node->var &&
        type_needs_synth_typedef_header(ctx, node->var->ty, header, seen))
        return true;
    if (node->member &&
        type_needs_synth_typedef_header(ctx, node->member->ty, header, seen))
        return true;
    if (node->func_ty &&
        type_needs_synth_typedef_header(ctx, node->func_ty, header, seen))
        return true;

    return node_needs_synth_typedef_header(ctx, node->lhs, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->rhs, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->cond, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->then, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->els, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->init, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->inc, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->body, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->args, header, seen) ||
           node_needs_synth_typedef_header(ctx, node->next, header, seen);
}

// #1057: mirrors collect_obj_types()'s traversal shape.
static bool obj_needs_synth_typedef_header(SerializeContext *ctx, Obj *obj,
                                           const char *header, TypeVec *seen) {
    if (type_needs_synth_typedef_header(ctx, obj->ty, header, seen))
        return true;
    if (node_needs_synth_typedef_header(ctx, obj->init_expr, header, seen))
        return true;
    for (Obj *param = obj->params; param; param = param->next)
        if (type_needs_synth_typedef_header(ctx, param->ty, header, seen))
            return true;
    for (Obj *local = obj->locals; local; local = local->next)
        if (type_needs_synth_typedef_header(ctx, local->ty, header, seen))
            return true;
    return node_needs_synth_typedef_header(ctx, obj->body, header, seen);
}

static void serialize_synth_typedef_includes(FILE *f, SerializeContext *ctx,
                                             Obj *prog) {
    const char *emitted[8];
    int         emitted_len = 0;
    bool        any         = false;
    for (size_t i = 0;
         i < sizeof(synth_typedef_headers) / sizeof(synth_typedef_headers[0]);
         i++) {
        const char *header  = synth_typedef_headers[i].header;
        bool        already = false;
        for (int j = 0; j < emitted_len; j++)
            if (!strcmp(emitted[j], header)) {
                already = true;
                break;
            }
        if (already)
            continue;

        bool needed = false;
        for (Obj *obj = prog; obj && !needed; obj = obj->next) {
            if (obj->is_function && !obj->is_definition && !obj->body)
                continue;
            TypeVec seen = {0};
            needed = obj_needs_synth_typedef_header(ctx, obj, header, &seen);
            free(seen.data);
        }
        if (!needed)
            continue;

        fprintf(f, "#include <%s>\n", header);
        any = true;
        if (emitted_len < (int)(sizeof(emitted) / sizeof(emitted[0])))
            emitted[emitted_len++] = header;
    }
    if (any)
        fprintf(f, "\n");
}

// #990/#993: does `ty` (or anything reachable from it) mention TY_BLOCK --
// used to decide whether `struct __cccc_block` itself needs a definition
// even when the TU declares no block *literal* (e.g. a function that only
// takes a block parameter and calls Block_copy/Block_release/the block
// itself). Mirrors collect_type()'s PTR/ARRAY/VLA/FUNC traversal shape, but
// deliberately does NOT recurse into struct/union members: a block-typed
// member is stored as a pointer, and any *use* of it (a read, a call)
// necessarily produces an expression whose own ->ty is TY_BLOCK, which
// node_mentions_block below already catches -- recursing into members here
// would need a seen-set to be cycle-safe for no additional coverage.
static bool type_mentions_block(Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_BLOCK)
        return true;
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY || ty->kind == TY_VLA)
        return type_mentions_block(ty->base);
    if (ty->kind == TY_FUNC) {
        if (type_mentions_block(ty->return_ty))
            return true;
        for (Type *p = ty->params; p; p = p->next)
            if (type_mentions_block(p))
                return true;
    }
    return false;
}

// #990/#993: mirrors collect_node_types()'s traversal shape to find any node
// whose type -- or a var/member/func_ty attached to it -- mentions TY_BLOCK.
static bool node_mentions_block(Node *node) {
    if (!node)
        return false;
    if (type_mentions_block(node->ty))
        return true;
    if (node->var && type_mentions_block(node->var->ty))
        return true;
    if (node->member && type_mentions_block(node->member->ty))
        return true;
    if (node->func_ty && type_mentions_block(node->func_ty))
        return true;

    // #1005: no ND_SWITCH/ND_CASE special case (see collect_node_types());
    // the generic traversal below already reaches every case via node->then.
    return node_mentions_block(node->lhs) || node_mentions_block(node->rhs) ||
           node_mentions_block(node->cond) || node_mentions_block(node->then) ||
           node_mentions_block(node->els) || node_mentions_block(node->init) ||
           node_mentions_block(node->inc) || node_mentions_block(node->body) ||
           node_mentions_block(node->args) || node_mentions_block(node->next);
}

// #990/#993: mirrors collect_obj_types()'s traversal shape.
static bool obj_uses_block_type(Obj *obj) {
    if (type_mentions_block(obj->ty))
        return true;
    if (node_mentions_block(obj->init_expr))
        return true;
    for (Obj *param = obj->params; param; param = param->next)
        if (type_mentions_block(param->ty))
            return true;
    for (Obj *local = obj->locals; local; local = local->next)
        if (type_mentions_block(local->ty))
            return true;
    return node_mentions_block(obj->body);
}

// #1074: does `var` belong to `fn`'s own locals list (which, for a
// function Obj, always includes its params and __static_link too -- see
// parse_decl.c's `fn->params = vm->compiler.locals;`)? Independent copy of
// parse_analysis.c's identically-shaped (and identically-named-in-spirit)
// var_in_fn_locals() -- that one is `static` in a different translation
// unit, so it isn't reachable from here.
bool nested_var_is_own(Obj *fn, Obj *var) {
    for (Obj *v = fn->locals; v; v = v->next)
        if (v == var)
            return true;
    return false;
}

// #1074: find (or, on first use, create) `owner`'s NestedEnvEntry.
static NestedEnvEntry *find_or_create_nested_env(VirtualMachine   *vm,
                                                 SerializeContext *ctx,
                                                 Obj              *owner) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        if (ctx->nested_envs[i].owner_fn == owner)
            return &ctx->nested_envs[i];
    if (ctx->nested_envs_len == ctx->nested_envs_cap) {
        ctx->nested_envs_cap =
            ctx->nested_envs_cap ? ctx->nested_envs_cap * 2 : 8;
        ctx->nested_envs = realloc(ctx->nested_envs, sizeof(NestedEnvEntry) *
                                                         ctx->nested_envs_cap);
    }
    NestedEnvEntry *e  = &ctx->nested_envs[ctx->nested_envs_len++];
    e->owner_fn        = owner;
    e->env_struct_name = arena_format(vm, "struct __cccc_nenv_%s", owner->name);
    e->upvars          = NULL;
    e->upvars_len      = 0;
    e->upvars_cap      = 0;
    return e;
}

// #1074: record `var` (owned by `e`'s function) as an upvar if it isn't
// already, returning its field index either way.
static int add_nested_upvar(Obj ***upvars_out, int *len, int *cap, Obj *var) {
    Obj **upvars = *upvars_out;
    for (int i = 0; i < *len; i++)
        if (upvars[i] == var)
            return i;
    if (*len == *cap) {
        *cap        = *cap ? *cap * 2 : 4;
        upvars      = realloc(upvars, sizeof(Obj *) * (*cap));
        *upvars_out = upvars;
    }
    upvars[*len] = var;
    return (*len)++;
}

// #1074: `node` (inside nested function `fn`'s own body) reads/writes
// `var`, which -- per the caller's own scan -- belongs to some ancestor of
// `fn`, not to `fn` itself. Reject the three shapes serialize_nested_
// preamble()'s env-struct lowering cannot represent (each needs `&var` to
// be a stable, already-valid address at the point the owning function's env
// is initialized -- serialize_function's hoist loop, mirrored in the
// comments below, is exactly what can't supply one for these), otherwise
// find `var`'s owning ancestor and register it as an upvar of that
// ancestor's env.
static void record_nested_upvar(VirtualMachine *vm, SerializeContext *ctx,
                                Obj *fn, Node *node, Obj *var) {
    Obj *owner       = NULL;
    Obj *block_owner = NULL;
    for (Obj *anc = fn->parent_fn; anc; anc = anc->parent_fn) {
        if (nested_var_is_own(anc, var)) {
            owner = anc;
            break;
        }
        // #1081: a block ancestor that does NOT directly own `var` is a
        // hard boundary for outward resolution -- `var` belongs to one of
        // ITS OWN ancestors, which this climb must not chase past. Rather
        // than an env-struct upvar of the real, further-out owner, `var`
        // was already captured transitively by this block at parse time
        // (block_literal()'s nested_children climb, parse_blocks.c) the
        // exact same way a sibling direct block read already sees it --
        // and must be read the same way here too (serialize_nested_upvar_
        // ref(), block_ancestor_desc_ptr_expr()), by-value snapshot rather
        // than a live read of the real owner's frame (no reference
        // implementation to defer to for this combination -- internal
        // consistency with the block's own direct captures is the spec).
        // A block ancestor that DOES directly own `var` (its own local/
        // param, matched by nested_var_is_own above first) is a distinct,
        // already-correct shape -- see this function's own upvar-of-a-
        // block-owner arm below, unaffected by this branch.
        if (anc->is_block) {
            block_owner = anc;
            break;
        }
    }
    if (block_owner) {
        // Nothing to register here -- serialize_nested_upvar_ref() finds
        // the same block ancestor independently at each read site and
        // reads `var` out of its own capture descriptor. Validate now,
        // at the point a diagnostic can still point at the reference,
        // that the capture the read side will assume really exists.
        if (block_capture_index(block_owner, var) < 0)
            error_tok(vm, node->tok ? node->tok : fn->tok,
                      "internal error: '%s' is read by a nested function "
                      "through block ancestor but was never captured by "
                      "it (#1081)",
                      var->name);
        return;
    }
    if (!owner)
        return; // defensive only -- the real scope chain guarantees this

    // #964: a VLA's declaration can't be hoisted ahead of the point it
    // reads its own length expression -- serialize_function's hoist loop
    // skips it for exactly this reason (see its own #964 comment), so no
    // `&var` is available yet when the owning function's env would need to
    // be initialized.
    if (var->ty && var->ty->kind == TY_VLA) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: variable-length-array "
                  "local '%s', read by a nested function, has no fixed "
                  "address to hand across the static link (#1074)",
                  var->name);
        return;
    }
    // #973: same reasoning, for a pointer-to-VLA local whose own declarator
    // reads a runtime variable and is likewise emitted in place rather than
    // hoisted.
    if (var->deferred_vla_ptr_init) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: pointer-to-VLA local "
                  "'%s', read by a nested function, is declared too late "
                  "for the static-link environment to capture its address "
                  "(#1074)",
                  var->name);
        return;
    }
    // #965/#1080: a block descriptor local itself (block_desc_of) has no
    // meaningful "value" to hand across a static link -- reject that one
    // shape outright. A __block-storage local's own C storage is already a
    // pointer (its slot holds the shared heap box), so the env field for it
    // is one level of indirection deeper (`T **` instead of `T *`,
    // serialize_nested_preamble()) and every read/write goes through an
    // extra dereference (serialize_nested_upvar_ref()) -- no longer
    // rejected as of #1080.
    if (var->block_desc_of) {
        error_tok(vm, node->tok ? node->tok : fn->tok,
                  "cannot serialize to native code: a block literal's own "
                  "descriptor local '%s' cannot be captured by a nested "
                  "function's static link (#1074)",
                  var->name);
        return;
    }

    NestedEnvEntry *e = find_or_create_nested_env(vm, ctx, owner);
    add_nested_upvar(&e->upvars, &e->upvars_len, &e->upvars_cap, var);
}

// #1074: walks `fn`'s own body (a nested function) looking for two things:
// a reference to a local/param owned by an ancestor (an "upvar", handed to
// record_nested_upvar()), and a bare reference to another nested function's
// Obj that ISN'T the direct callee of a call to it -- e.g. `int (*p)(int) =
// inner;` or passing `inner` itself as a callback argument. The latter has
// no portable spelling: the hoisted signature carries a leading
// `__static_link` parameter no real function-pointer type can express, so
// it's rejected here rather than serialized wrong. The one legal bare
// reference (a direct call's own callee, handled by ND_FUNCALL's own
// emission -- see the #1074 comment there) is a leaf ND_VAR node with
// nothing beneath it to walk, so it's simply never descended into below,
// rather than needing its own exemption flag.
static void collect_nested_refs(VirtualMachine *vm, SerializeContext *ctx,
                                Obj *fn, Node *node) {
    for (; node; node = node->next) {
        if (node->kind == ND_VAR && node->var) {
            // #1074: the "is this an upvar of an ancestor" question only
            // makes sense when `fn` is itself a nested function -- pass 2
            // now walks EVERY function's body (including ordinary
            // top-level functions and Apple block literals, is_block,
            // which are their own separate Obj too) so the reference-check
            // below reaches every context a bad reference could appear in,
            // but a block's own capture of an outer local (block_fn's
            // locals never include what it captures either, by the same
            // shape) is a completely different, already-correct mechanism
            // (serialize_block_capture_ref) -- not an upvar, and must not
            // be misdetected as one here.
            if (fn->is_nested && !fn->is_block && node->var->is_local &&
                !nested_var_is_own(fn, node->var))
                record_nested_upvar(vm, ctx, fn, node, node->var);
            else if (node->var->is_function && node->var->is_nested &&
                     !node->var->is_block)
                error_tok(vm, node->tok ? node->tok : fn->tok,
                          "cannot serialize to native code: a reference to "
                          "nested function '%s' is only supported as the "
                          "direct callee of a call to it -- its native "
                          "signature carries a hidden static-link "
                          "parameter, so it has no portable function-"
                          "pointer type (#1074)",
                          node->var->name);
        }

        // #1080 (was a #1074-follow-up rejection): a block literal directly
        // inside a genuinely nested function (fn->is_nested && !fn->is_block)
        // that captures a variable belonging to one of THAT function's own
        // ancestors (an upvar of `fn`, not `fn`'s own local) now registers
        // the capture as an upvar of the real owner, exactly like a bare
        // ND_VAR reference above -- ND_BLOCK_LITERAL's own serialize_expr
        // case grows a matching source arm that reads it back out through
        // the same env chase (nested_env_ptr_expr) instead of printing an
        // unnameable `cap->name`.
        if (node->kind == ND_BLOCK_LITERAL && fn->is_nested && !fn->is_block) {
            for (int __bc_i = 0; __bc_i < node->num_block_captures; __bc_i++) {
                Obj *cap = node->block_captures[__bc_i];
                if (cap->is_local && !nested_var_is_own(fn, cap))
                    record_nested_upvar(vm, ctx, fn, node, cap);
            }
        }

        bool lhs_is_direct_nested_call =
            node->kind == ND_FUNCALL && node->lhs &&
            node->lhs->kind == ND_VAR && node->lhs->var &&
            node->lhs->var->is_function && node->lhs->var->is_nested &&
            !node->lhs->var->is_block;
        // #1081 residual (tracked separately, not fixed here): calling a
        // nested function whose own parent sits beyond a block ancestor of
        // `fn` (a sibling/cousin call reached only by climbing OUT of a
        // block first) needs the block's *enclosing frame*, which a
        // heap-copyable block's descriptor deliberately never stores (the
        // same reason a plain variable read through such a chain is
        // rejected in codegen -- see emit_static_chain_var_addr's own
        // #1081 fix, codegen_addr.c). serialize_expr's ND_FUNCALL case
        // (nested_env_ptr_expr) has no equivalent fix, so reject here
        // rather than let it cast a block descriptor as if it were an
        // ordinary nested-function env struct.
        if (lhs_is_direct_nested_call && fn->is_nested && !fn->is_block &&
            node->lhs->var->parent_fn != fn) {
            for (Obj *anc                                     = fn->parent_fn;
                 anc && anc != node->lhs->var->parent_fn; anc = anc->parent_fn)
                if (anc->is_block) {
                    error_tok(vm, node->tok ? node->tok : fn->tok,
                              "cannot serialize to native code: calling "
                              "nested function '%s', whose own parent is "
                              "beyond a block ancestor, is not supported "
                              "(#1081 residual)",
                              node->lhs->var->name);
                    break;
                }
        }
        if (!lhs_is_direct_nested_call)
            collect_nested_refs(vm, ctx, fn, node->lhs);
        collect_nested_refs(vm, ctx, fn, node->rhs);
        collect_nested_refs(vm, ctx, fn, node->cond);
        collect_nested_refs(vm, ctx, fn, node->then);
        collect_nested_refs(vm, ctx, fn, node->els);
        collect_nested_refs(vm, ctx, fn, node->init);
        collect_nested_refs(vm, ctx, fn, node->inc);
        collect_nested_refs(vm, ctx, fn, node->body);
        collect_nested_refs(vm, ctx, fn, node->cas_addr);
        collect_nested_refs(vm, ctx, fn, node->cas_old);
        collect_nested_refs(vm, ctx, fn, node->cas_new);
        for (Node *a = node->args; a; a = a->next)
            collect_nested_refs(vm, ctx, fn, a);
    }
}

// #1186: a compiler-synthesized temporary (empty name -- var->name[0] ==
// '\0', see serialize_decl.c's own __cccc_tmpN naming pass, run later than
// this one) whose type is a genuinely anonymous struct/union (no tag, no
// typedef, and no structurally-matching typedef anywhere --
// type_needs_anon_aggregate()) gets re-derived as a *fresh* textual
// `struct { ... }` body at every emission site that touches it -- e.g. once
// at its own local declaration, again at a cast that targets the same type
// (the ++/--/op= desugaring pattern that introduces these temps routinely
// casts back to the member's own type to complete a pointer-arithmetic
// trick). Two independently-derived `struct { ... }` spellings are two
// distinct types in C even with byte-identical member lists, so an
// assignment between them is `-Wincompatible-pointer-types` -- a warning
// under most compilers, but a hard error under GCC 14+/some clang
// configurations (found via test_minilua.c, a real Lua interpreter
// exercising exactly this compound-lvalue-through-a-union-member shape).
// hoist_local_type_to_file_scope() (#989) already solves the identical
// problem one level up, for a block literal's captured-variable types --
// this reuses it for every such temp, not just captures, so the whole class
// gets a single, stably-named file-scope definition instead of a fresh
// inline body at each site. Same reachability filter as
// serialize_nested_preamble() below, and must run at the same point in the
// emission order (after serialize_type_defs_for_owner(f, ctx, NULL), before
// any function body is emitted) for the same reason: a temp's anonymous
// type may itself be declared-in-a-function (rare, but possible if it
// mirrors a local aggregate's shape) and needs hoisting ahead of that
// function too.
static void hoist_compiler_temp_anon_types(FILE *f, VirtualMachine *vm,
                                           SerializeContext *ctx, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        for (Obj *var = obj->locals; var; var = var->next) {
            if (var->is_param || !var->name || var->name[0] != '\0')
                continue;
            if (type_needs_anon_aggregate(ctx, var->ty))
                hoist_local_type_to_file_scope(f, vm, ctx, var->ty);
        }
    }
}

// #1074: emits one `struct __cccc_nenv_<name> { void *__up; T0 *__uv0; ...
// };` for every function that directly parents at least one nested
// function -- even one with an empty upvars list still needs `__up`, to
// carry an intervening level of a multi-level nest chain. Must run after
// serialize_type_defs_for_owner(f, ctx, NULL) (file-scope types) so an
// upvar whose own struct/union/enum type was declared inside a function can
// be hoisted ahead of it here, mirroring #989's identical reasoning for a
// block capture's type -- see hoist_local_type_to_file_scope()'s own
// comment. Called from cc_serialize_program next to serialize_block_
// preamble(), in both branches, at the same point in the emission order.
static void serialize_nested_preamble(FILE *f, VirtualMachine *vm,
                                      SerializeContext *ctx, Obj *prog) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        // is_block reuses is_nested for VM codegen purposes (parse_blocks.c)
        // -- a block literal is not one of "our" nested functions (it has
        // its own complete, separate lowering, #965) and must not trigger
        // creating an env for its parent here.
        if (!obj->is_function || !obj->is_nested || obj->is_block || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        // Pass 1: guarantee obj->parent_fn has an entry regardless of
        // whether it turns out to own any upvars -- an intervening level of
        // a multi-level nest needs one purely to carry __up.
        find_or_create_nested_env(vm, ctx, obj->parent_fn);
    }
    if (ctx->nested_envs_len == 0)
        return;

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        // Pass 2: collect each owner's upvars, and reject an unsupported
        // bare reference to a nested function's value -- run over EVERY
        // function's body, not just nested ones. The bad-reference check
        // has no dependency on `obj` itself being nested (a nested
        // function's own *enclosing* function, or an unrelated sibling, can
        // just as easily write `int (*fp)(int) = inner;` or pass `inner` as
        // a callback); nested_var_is_own()'s climb up `obj->parent_fn`
        // naturally no-ops for a non-nested `obj` (parent_fn is NULL, the
        // loop never runs, `owner` stays NULL, record_nested_upvar()
        // returns immediately) so this is safe to run unconditionally.
        collect_nested_refs(vm, ctx, obj, obj->body);
    }

    for (int i = 0; i < ctx->nested_envs_len; i++)
        for (int j = 0; j < ctx->nested_envs[i].upvars_len; j++)
            hoist_local_type_to_file_scope(f, vm, ctx,
                                           ctx->nested_envs[i].upvars[j]->ty);

    for (int i = 0; i < ctx->nested_envs_len; i++) {
        NestedEnvEntry *e = &ctx->nested_envs[i];
        fprintf(f, "%s {\n    void *__up;\n", e->env_struct_name);
        for (int j = 0; j < e->upvars_len; j++) {
            char field_name[16];
            snprintf(field_name, sizeof(field_name), "__uv%d", j);
            fprintf(f, "    ");
            // #1080: a __block-storage upvar's own C storage is already a
            // pointer to the shared heap box (T *) -- the env field holding
            // its address is one level deeper, T **, so
            // serialize_nested_upvar_ref() can deref twice to reach the
            // value.
            Type *field_ty =
                e->upvars[j]->is_block_var
                    ? pointer_to(vm, pointer_to(vm, e->upvars[j]->ty))
                    : pointer_to(vm, e->upvars[j]->ty);
            serialize_type_decl(f, ctx, field_ty, field_name);
            fprintf(f, ";\n");
        }
        fprintf(f, "};\n\n");
    }
}

// #1074: frees every NestedEnvEntry's own upvars array before freeing the
// table itself -- the table's realloc'd blocks (env_struct_name is
// arena-allocated, not heap) are the only per-entry heap allocation.
static void free_nested_envs(SerializeContext *ctx) {
    for (int i = 0; i < ctx->nested_envs_len; i++)
        free(ctx->nested_envs[i].upvars);
    free(ctx->nested_envs);
}

// #965: emits, once, everything a lowered block literal needs at file
// scope: the common-initial-sequence `struct __cccc_block` every env
// struct shares (so a block value's pointer type is well-defined
// regardless of which block literal produced it), one
// `struct __cccc_block_env_N` per block function (its captures, in the
// exact order codegen's descriptor layout uses -- ND_BLOCK_LITERAL,
// codegen.c), and -- only if Block_copy/Block_release is actually
// reachable -- a native replacement for the VM-only __cccc_block_copy_impl
// FFI shim (its real implementation, src/stdlib/stdlib.c, exists only
// inside the VM's host runtime and would otherwise leave a call to an
// undeclared symbol in the generated C) / an `extern void free(void *);`
// declaration (#990: vm->compiler.builtin_free has no obj->tok, so the
// prototype pass's from_primary filter always drops it). Runs after
// rename_anon_globals() (block functions already have their final
// __cccc_block_N names) and before type/prototype collection, so both the
// generated_only and normal cc_serialize_program branches share it -- a
// macro-generated block literal (via Quote(), unlikely but not excluded)
// gets the same treatment as an ordinary one.
//
// #990/#993: `struct __cccc_block` itself, and the copy-impl/free
// declarations, are needed even in a TU with no block *literal* at all --
// e.g. a function that only takes a block parameter and calls
// Block_copy/Block_release/the block itself. Gated on `any_block ||
// uses_block_type || copy_used || release_used` rather than `any_block`
// alone; the env-struct loop (and its #989 hoist pass) still only makes
// sense when there's an actual block literal to describe, so those stay
// gated on `any_block`.
static void serialize_block_preamble(FILE *f, VirtualMachine *vm,
                                     SerializeContext *ctx, Obj *prog) {
    bool any_block       = false;
    bool uses_block_type = false;
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->is_function && obj->is_block)
            any_block = true;
        if (obj_uses_block_type(obj))
            uses_block_type = true;
        if (any_block && uses_block_type)
            break;
    }

    // #990: gated on the same `reachable` condition the #989 hoist loop
    // below uses -- under generated_only (-c=generated), a call inside an
    // ordinary (non-macro-generated) function never reaches the output, so
    // scanning it here would emit a copy-impl/free declaration nothing
    // actually calls.
    bool copy_used    = false;
    bool release_used = false;
    for (Obj *obj = prog; obj && (!copy_used || !release_used);
         obj      = obj->next) {
        if (!obj->is_function || !obj->body)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        if (!copy_used && vm->compiler.builtin_block_copy)
            copy_used =
                node_calls_obj(obj->body, vm->compiler.builtin_block_copy);
        if (!release_used && vm->compiler.builtin_free)
            release_used = node_calls_obj(obj->body, vm->compiler.builtin_free);
    }

    if (!any_block && !uses_block_type && !copy_used && !release_used)
        return;

    fprintf(f, "struct __cccc_block { void *__invoke; long __size; };\n\n");

    if (!any_block)
        goto emit_copy_and_free;

    // #989: hoist every capture's own struct/union/enum type to file scope
    // -- if it was declared inside a function, this env struct (below) is
    // emitted ahead of the function that would otherwise bring its tag into
    // scope, and serialize_type/serialize_anon_aggregate would otherwise
    // silently inline a fresh, nominally-distinct anonymous copy of the
    // body at each use site (confirmed via a real clang "assigning to ...
    // from incompatible type" error before this fix landed). Must run
    // before the env-struct loop below so the definitions are already in
    // ctx->hoisted (and already emitted) by the time serialize_type_decl
    // needs to spell a capture's field. Gated on the same `reachable`
    // condition the emit-event loop further down uses to decide what
    // actually reaches the output (#969's precedent: hoist only what is
    // actually serialized, not what merely exists in `prog`) -- under
    // generated_only (-c=generated), an ordinary (non-macro-generated)
    // block's code is never emitted at all, so hoisting its capture's type
    // here would push a real file-scope tag into output that could collide
    // with the consumer's own.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_block)
            continue;
        bool reachable = !ctx->generated_only || obj->is_macro_generated;
        if (!reachable)
            continue;
        for (int i = 0; i < obj->num_captures; i++)
            hoist_local_type_to_file_scope(f, vm, ctx, obj->captures[i]->ty);
    }

    static const char *BLOCK_FN_PREFIX = "__cccc_block_";
    size_t             prefix_len      = strlen(BLOCK_FN_PREFIX);

    for (Obj *obj = prog; obj; obj = obj->next) {
        if (!obj->is_function || !obj->is_block)
            continue;

        // obj->name was rewritten to "__cccc_block_<N>" by
        // rename_anon_globals() just above -- reuse its numeric suffix so
        // the env struct name pairs with it without extra state.
        const char *suffix =
            (strncmp(obj->name, BLOCK_FN_PREFIX, prefix_len) == 0)
                ? obj->name + prefix_len
                : obj->name;
        char *env_name = arena_format(vm, "struct __cccc_block_env_%s", suffix);

        fprintf(f, "%s {\n    void *__invoke;\n    long __size;\n", env_name);
        for (int i = 0; i < obj->num_captures; i++) {
            Obj *cap = obj->captures[i];

            // #989: a capture whose own struct/union/enum type was declared
            // inside a function is already hoisted to file scope (with
            // renaming on collision) by the loop above, before this one
            // runs -- see hoist_local_type_to_file_scope(). Previously
            // (#965) this was a hard error; the fix landed here.
            Type *field_ty =
                cap->is_block_var ? pointer_to(vm, cap->ty) : cap->ty;
            char field_name[32];
            snprintf(field_name, sizeof(field_name), "__cap%d", i);
            fprintf(f, "    ");
            serialize_type_decl(f, ctx, field_ty, field_name);
            fprintf(f, ";\n");
        }
        fprintf(f, "};\n\n");

        if (ctx->block_envs_len == ctx->block_envs_cap) {
            ctx->block_envs_cap =
                ctx->block_envs_cap ? ctx->block_envs_cap * 2 : 8;
            ctx->block_envs = realloc(ctx->block_envs, sizeof(BlockEnvEntry) *
                                                           ctx->block_envs_cap);
        }
        ctx->block_envs[ctx->block_envs_len].block_fn        = obj;
        ctx->block_envs[ctx->block_envs_len].env_struct_name = env_name;
        ctx->block_envs_len++;
    }

emit_copy_and_free:
    if (copy_used) {
        fprintf(f,
                "static void *__cccc_block_copy_impl(void *__d) {\n"
                "    long __n = ((struct __cccc_block *)__d)->__size;\n"
                "    void *__c = __builtin_malloc((unsigned long)__n);\n"
                "    if (__c) __builtin_memcpy(__c, __d, (unsigned long)__n);\n"
                "    return __c;\n"
                "}\n\n");
    }
    // #990: vm->compiler.builtin_free is a synthesized `free` prototype
    // with no obj->tok (parse.c's Block_release path falls back to it when
    // no user-visible `free` is in scope, #458) -- the prototype pass's
    // from_primary filter always drops a tok-less Obj, so without this the
    // generated C called an undeclared `free`. A redundant declaration
    // here is always compatible with a real <stdlib.h> one if both end up
    // in the output (builtin_free is only ever used when parse.c found no
    // user `free`, so there is nothing for this to conflict with in
    // practice either way).
    if (release_used)
        fprintf(f, "extern void free(void *);\n\n");
}

// #999: a `static` function with a body, declared in a plain #include'd
// header (not a command-line input file, not a cccc-only-routed one -- #896)
// rather than synthesized/macro-generated, is already supplied to the output by
// that header's own auto-captured #include text. Emitting it again from
// `prog` -- which holds one Obj *per TU* that included the header, since
// `static` internal-linkage functions are deliberately left uncanonicalized
// across translation units by cc_link_progs (#957) -- produces a
// "redefinition" error the moment more than one input file shares that
// header (dandy's `internal.h`, `static inline` NaN-boxing accessors,
// #999). Mirrors the from_primary check the prototype pass already uses
// for a *bodyless* declaration just below, generalized to also cover a
// function that has one. In generated_only mode (-c=generated), the same
// header text is only in scope if it was actually auto-captured -- see
// #953's identical reasoning for a struct/enum tag definition just above
// this function -- so path_is_captured() gates it there; a plain -m/
// -c=native always replays every captured #include verbatim, so
// from_primary alone is sufficient.
// #1002 (investigation): true when `name` is the exact path of one of the
// files the user listed on the command line, as opposed to a header any of
// them #included. Replaces a plain `== vm->compiler.primary_file` token-file
// comparison, which only ever names input_files[0] (cc_preprocess/linker.c
// pin primary_file to the *first* input file forever) -- so a static
// function or bodyless declaration written in input_files[1..N] used to be
// misidentified as "supplied by a replayed header" and silently dropped
// from -c=native/-m output (found investigating #1002; not what that ticket
// itself reported, but blocks it -- see CLAUDE.md). #1006: promoted to a
// shared cc_file_is_command_line_input() (preprocess.c) so record_type_name()
// (parse.c) and the auto-capture gate (preprocess.c) could adopt the exact
// same test for their own primary_file-keyed drops; kept here as a thin
// wrapper so this file's existing call sites/comments didn't need to move.
static bool file_is_command_line_input(VirtualMachine *vm, const char *name) {
    return cc_file_is_command_line_input(vm, name);
}

bool function_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                                 Obj *obj) {
    if (!obj->is_static || !obj->body || obj->is_macro_generated)
        return false;
    Token *t = obj->tok;
    if (!t || !t->file)
        return false;
    if (file_is_command_line_input(vm, t->file->name) ||
        cc_file_is_cccc_only(vm, t->file->name))
        return false;
    return !ctx->generated_only || path_is_captured(ctx, t->file->name);
}

// #1151: the bodiless-declaration counterpart of the from_input/
// cccc_bundled_uncaptured test the prototype-emission loop below applies to
// a `!obj->is_definition && !obj->body` Obj (a bare declaration, e.g. `extern
// long strlen(const char *s);`). Factored out so the #999 reloc-forward-
// declare loop further up this file can ask the identical question for a
// function only referenced by address in a global initializer (e.g. `static
// FfiOps ops = {strlen, strcmp};`) -- that loop used to emit a signature
// unconditionally, which for a real libc function like `strlen` re-declares
// it with CCCC's own bundled-header spelling (`long` where the host's
// string.h says `size_t`), conflicting with the real declaration the
// replayed `#include <string.h>` already supplied (#1151).
//
// Returns true when `obj`'s bodiless declaration is NOT already supplied by
// an ordinary replayed #include -- i.e. the caller still needs to emit its
// own signature. Deliberately does not fold in the is_implicit/
// is_macro_generated arms the caller (both loops) already special-cases:
// those two have opposite-direction safety implications for a reloc target
// (skipping there would reintroduce the undeclared-identifier bug #999
// fixed in the first place) and this helper does not decide them.
static bool bodyless_decl_from_input_or_bundled(VirtualMachine   *vm,
                                                SerializeContext *ctx,
                                                Obj              *obj) {
    Token *t          = obj->tok;
    bool   from_input = t && t->file &&
                        (file_is_command_line_input(vm, t->file->name) ||
                         cc_file_is_cccc_only(vm, t->file->name));
    bool cccc_bundled_uncaptured = obj->is_used && t && t->file &&
                                   cc_file_is_cccc_bundled(vm, t->file->name) &&
                                   !path_is_captured(ctx, t->file->name);
    return from_input || cccc_bundled_uncaptured;
}

// #1047: the global-variable counterpart to function_is_header_supplied()
// just above. Unlike functions, globals had no include-provenance gate at
// all -- the #918 forward-declare-every-global pass and serialize_global_var
// both only checked is_function/is_string_literal/init_data-presence, so a
// header declaring `static int x = 0;` produced three copies of `x` in
// -c=native output: the replayed `#include`, the forward declaration, and
// the definition -- a hard "redefinition" from the host compiler. Same
// safe-default guards as the function version (no token/file -> emit
// rather than silently drop; macro-generated -> emit, it has no header of
// its own to collide with), and the same `!generated_only ||
// path_is_captured(...)` tail, but without the is_static/body checks
// (function_is_header_supplied only suppresses a *definition*, since a
// bodyless declaration is handled by its own from_input branch further
// down; an ordinary global's replayed header line is its only
// declaration+definition either way, so there's no separate case to split
// out here).
bool global_is_header_supplied(VirtualMachine *vm, SerializeContext *ctx,
                               Obj *obj) {
    if (obj->is_macro_generated)
        return false;
    Token *t = obj->tok;
    if (!t || !t->file)
        return false;
    if (file_is_command_line_input(vm, t->file->name) ||
        cc_file_is_cccc_only(vm, t->file->name))
        return false;
    return !ctx->generated_only || path_is_captured(ctx, t->file->name);
}

// #1064: true if `line` (a raw captured directive line, `#...`, from
// copy_raw_directive_line()/copy_routed_directive_line() in preprocess.c) is
// one of the conditional-group directives -- see the call site in
// cc_serialize_program()'s emit_directives loop for why these are dropped
// from ordinary replay. Matches on the directive word after `#` and
// optional whitespace; deliberately textual rather than pp_directive()
// (token-level, not available on this already-flattened string).
static bool line_is_conditional_directive(const char *line) {
    if (!line || line[0] != '#')
        return false;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    static const char *const kw[] = {
        "if", "ifdef", "ifndef", "elif", "elifdef", "elifndef", "else", "endif",
    };
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        size_t len = strlen(kw[i]);
        if (strncmp(p, kw[i], len) == 0) {
            char c = p[len];
            // Require a word boundary so "ifdef" doesn't also match a
            // (nonexistent) directive starting "ifdefine" etc, and "if"
            // doesn't wrongly match "ifdef"/"ifndef" as a prefix hit --
            // checked longest-first below via the table order isn't
            // relied on; the boundary check alone is sufficient since "if"
            // followed by 'd'/'n' fails the boundary test and falls
            // through to the next table entry.
            if (c == '\0' || c == ' ' || c == '\t' || c == '(')
                return true;
        }
    }
    return false;
}

// #1118: true if `line` (a raw captured directive line, `#...`, from
// copy_raw_directive_line()/copy_routed_directive_line() in preprocess.c) is
// a #define or #undef whose macro NAME starts with a non-ASCII byte (UTF-8
// lead/continuation bytes -- emoji and other non-ASCII identifiers, a CCCC
// extension the host preprocessor rejects outright: "macro name must be an
// identifier"). See the call site in cc_serialize_program()'s
// emit_directives loop for why these lines are dropped from ordinary
// replay. Matches on the directive word after `#` and optional whitespace,
// deliberately textual rather than pp_directive() (token-level, not
// available on this already-flattened string), same style as
// line_is_conditional_directive above.
static bool line_macro_name_is_non_ascii(const char *line) {
    if (!line || line[0] != '#')
        return false;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    static const char *const kw[] = {"define", "undef"};
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        size_t len = strlen(kw[i]);
        if (strncmp(p, kw[i], len) != 0)
            continue;
        // Word boundary: "#defined" is not a directive (and a function-like
        // "#define NAME(" still has whitespace before NAME, so the plain
        // space/tab boundary covers both spellings).
        char c = p[len];
        if (c != '\0' && c != ' ' && c != '\t')
            continue;
        const char *name = p + len;
        while (*name == ' ' || *name == '\t')
            name++;
        // Only the NAME's first byte matters: any UTF-8 encoding of a
        // non-ASCII identifier starts with a byte >= 0x80, and an ASCII name
        // never does. A replacement list referencing an ASCII macro is not
        // touched -- only names that are themselves non-ASCII are filtered.
        return (unsigned char)*name >= 0x80;
    }
    return false;
}

// #1184-adjacent (found verifying #1157 on real sr.ht Linux hardware): a
// captured `#define once_flag ...`/`#define ONCE_FLAG_INIT ...`/
// `#define call_once ...` line from include/threads.h's own #1183/#1184
// private-name aliases must never be replayed into -c=native/-m output.
// Unlike every other captured macro, these aliases are guest-side only by
// construction -- every guest use of `once_flag`/`ONCE_FLAG_INIT`/
// `call_once` is already resolved by CCCC's own preprocessor before the
// AST is built (see threads.h's own comment), and the shim/re-derived
// declarations that DO reach native output already spell the private name
// (__cccc_once_flag/__cccc_call_once) directly, never through the alias.
// Left live, a #define stays in scope for the rest of the generated
// translation unit -- including the real host <stdlib.h>
// serialize_threads_shims replays right after it -- and the host
// preprocessor substitutes the bare identifier inside GLIBC's OWN
// declarations too: first found for `once_flag`'s typedef (renamed to
// `__cccc_once_flag`, colliding with this header's own typedef of that
// name on their differing underlying types), then for `call_once` itself
// once that leak was plugged (a new-enough glibc's own real ISO C11
// `call_once` declaration -- reachable via <stdlib.h> alone, no
// <threads.h> needed -- renamed the same way, colliding on a genuinely
// different signature). A matching #undef right after the declaration in
// threads.h looks like the obvious fix and was tried first -- it also
// un-defines the alias for CCCC's OWN preprocessing, which breaks a
// GUEST source's own subsequent use of any of these three names (the
// alias and the guest's own preprocessing share one preprocessor, not a
// native-output-only one), so the alias has to be dropped from replay
// instead, the same way #1118 dropped non-ASCII macro names above.
// Matches by exact macro name after `#define`/`#undef`, not by
// originating file, since the guest never legitimately defines/undefs
// any of these three names itself (all reserved: ONCE_FLAG_INIT by C11
// 7.26.1, once_flag as a C11 typedef name, call_once as a C11 function
// name).
static bool line_is_threads_h_private_alias_directive(const char *line) {
    if (!line || line[0] != '#')
        return false;
    const char *p = line + 1;
    while (*p == ' ' || *p == '\t')
        p++;
    static const char *const kw[] = {"define", "undef"};
    for (size_t i = 0; i < sizeof(kw) / sizeof(kw[0]); i++) {
        size_t len = strlen(kw[i]);
        if (strncmp(p, kw[i], len) != 0)
            continue;
        char c = p[len];
        if (c != '\0' && c != ' ' && c != '\t')
            continue;
        const char *name = p + len;
        while (*name == ' ' || *name == '\t')
            name++;
        static const char *const names[] = {"once_flag", "ONCE_FLAG_INIT",
                                            "call_once"};
        for (size_t j = 0; j < sizeof(names) / sizeof(names[0]); j++) {
            size_t nlen = strlen(names[j]);
            if (strncmp(name, names[j], nlen) == 0) {
                char after = name[nlen];
                if (after == '\0' || after == ' ' || after == '\t' ||
                    after == '(')
                    return true;
            }
        }
        return false;
    }
    return false;
}

// #1033: real C operator text for a CmpOp, so a return= comparison can be
// baked directly into the generated C as `if (!(__ret <op> <expect>))`
// instead of a runtime dispatch -- the operator is already known at
// serialize time (r->ret_op), same as every other test-table field.
static const char *cmp_op_c_operator(CmpOp op) {
    switch (op) {
        case CMP_NE:
            return "!=";
        case CMP_LT:
            return "<";
        case CMP_LE:
            return "<=";
        case CMP_GT:
            return ">";
        case CMP_GE:
            return ">=";
        case CMP_EQ:
        default:
            return "==";
    }
}

// #1033: the ~28 __builtin_assert_* functions include/cccc/testing.h
// declares (a cccc-private header, never replayed to the host compiler --
// see cc_serialize_program's #include-replay loop) transliterated into real
// C with the same typed prototypes the guest program's own already-
// type-checked call sites expect. Behaviorally identical to testing.c's
// impl_assert_* family: on failure, snprintf a diagnostic into the current
// __cccc_test_run_state and longjmp back to the per-test wrapper's setjmp.
// _setjmp/_longjmp themselves reuse serialize_synth_setjmp_decls's own
// raw-extern pattern rather than #include <setjmp.h> -- see the redundant
// declaration below for why a real jmp_buf type would conflict when the
// same TU also lowers the guest setjmp builtin.
static const char *const CCCC_TEST_ASSERT_RUNTIME_SRC =
    // #1054/#1030: setjmp.h is compiler-owned (see
    // serialize_synth_setjmp_decls's own comment) -- reuse its exact
    // raw-extern pattern (_setjmp/_longjmp over a void* buffer) instead of
    // #include <setjmp.h>, so a TU that also uses the guest setjmp builtin
    // never sees two conflicting declarations of the same symbol.
    "extern int _setjmp(void *);\n"
    "extern void _longjmp(void *, int) __attribute__((noreturn));\n"
    "#include <string.h>\n"
    "#include <stdio.h>\n"
    "typedef struct {\n"
    // long long (not unsigned char) so the buffer inherits 8-byte natural
    // alignment -- _setjmp on some hosts (e.g. glibc/aarch64's
    // __sigsetjmp) writes callee-saved FP registers with real alignment
    // requirements a byte-aligned buffer wouldn't satisfy.
    "    long long jmp[512];\n"
    "    int failed;\n"
    "    char fail_msg[512];\n"
    "} __cccc_test_run_state;\n"
    "static __cccc_test_run_state *__cccc_s_run = NULL;\n"
    "static void __builtin_assert(int cond, const char *expr, const char "
    "*file, int line) {\n"
    "    if (!cond) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"Assert called outside a "
    "test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s (%s:%d)\", expr, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_false(int cond, const char *expr, const "
    "char *file, int line) {\n"
    "    if (cond) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertFalse called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"!%s (%s:%d)\", expr, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_fail(const char *file, int line) {\n"
    "    if (!__cccc_s_run) { fprintf(stderr, \"AssertFail called outside a "
    "test run at %s:%d\\n\", file, line); return; }\n"
    "    snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"forced failure (%s:%d)\", file, line);\n"
    "    __cccc_s_run->failed = 1;\n"
    "    _longjmp(__cccc_s_run->jmp, 1);\n"
    "}\n"
    "static void __builtin_assert_fail_msg(const char *msg, const char "
    "*file, int line) {\n"
    "    if (!__cccc_s_run) { fprintf(stderr, \"AssertFailMsg called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "    snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s (%s:%d)\", msg, file, line);\n"
    "    __cccc_s_run->failed = 1;\n"
    "    _longjmp(__cccc_s_run->jmp, 1);\n"
    "}\n"
    "static void __builtin_assert_eq(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (a != b) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertEq called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (%lld != %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_neq(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (a == b) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNeq called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s == %s (both %lld) (%s:%d)\", as, bs, a, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_gt(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a > b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertGt called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s <= %s (%lld <= %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_lt(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a < b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertLt called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s >= %s (%lld >= %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_ge(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a >= b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertGe called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s < %s (%lld < %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_le(long long a, long long b, const char "
    "*as, const char *bs, const char *file, int line) {\n"
    "    if (!(a <= b)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertLe called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s > %s (%lld > %lld) (%s:%d)\", as, bs, a, b, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_within(long long delta, long long "
    "expected, long long actual, const char *ds, const char *es, const char "
    "*as, const char *file, int line) {\n"
    "    long long diff = expected - actual;\n"
    "    if (diff < 0) diff = -diff;\n"
    "    if (diff > delta) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertWithin called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s |%s - %s| = %lld > %s (%lld) (%s:%d)\", as, es, as, diff, ds, "
    "delta, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_null(const void *p, const char *ps, const "
    "char *file, int line) {\n"
    "    if (p != NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNull called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is not null (%s:%d)\", ps, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_not_null(const void *p, const char *ps, "
    "const char *file, int line) {\n"
    "    if (p == NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNotNull called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is null (%s:%d)\", ps, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_streq(const char *a, const char *b, const "
    "char *as, const char *bs, const char *file, int line) {\n"
    "    if (strcmp(a, b) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertStrEq called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (\\\"%s\\\" != \\\"%s\\\") (%s:%d)\", as, bs, a, b, file, "
    "line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_streq_len(const char *a, const char *b, "
    "long long len, const char *as, const char *bs, const char *file, int "
    "line) {\n"
    "    if (strncmp(a, b, (size_t)len) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertStrEqLen called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (first %lld chars differ) (%s:%d)\", as, bs, len, file, "
    "line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_mem_eq(const void *expected, const void "
    "*actual, long long len, const char *es, const char *as, const char "
    "*file, int line) {\n"
    "    if (memcmp(expected, actual, (size_t)len) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertMemEq called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (%lld bytes differ) (%s:%d)\", es, as, len, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_float_within(double delta, double "
    "expected, double actual, const char *ds, const char *es, const char "
    "*as, const char *file, int line) {\n"
    "    double diff = expected - actual;\n"
    "    if (diff < 0) diff = -diff;\n"
    "    if (diff > delta) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertFloatWithin called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s |%s - %s| = %g > %s (%g) (%s:%d)\", as, es, as, diff, ds, delta, "
    "file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_double_within(double delta, double "
    "expected, double actual, const char *ds, const char *es, const char "
    "*as, const char *file, int line) {\n"
    "    __builtin_assert_float_within(delta, expected, actual, ds, es, as, "
    "file, line);\n"
    "}\n"
    "static void __builtin_assert_bits(long long mask, long long expected, "
    "long long actual, const char *ms, const char *es, const char *as, "
    "const char *file, int line) {\n"
    "    if ((actual & mask) != (expected & mask)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBits called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s & %s = 0x%llx != %s & %s = 0x%llx (%s:%d)\", as, ms, (unsigned "
    "long long)(actual & mask), es, ms, (unsigned long long)(expected & "
    "mask), file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_bit_high(int bit, long long actual, const "
    "char *bs, const char *as, const char *file, int line) {\n"
    "    if (!(actual & (1LL << bit))) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBitHigh called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s bit %d of %s is low (%s:%d)\", bs, bit, as, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_bit_low(int bit, long long actual, const "
    "char *bs, const char *as, const char *file, int line) {\n"
    "    if (actual & (1LL << bit)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBitLow called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s bit %d of %s is high (%s:%d)\", bs, bit, as, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_eq_array(const void *expected, const void "
    "*actual, long long elem_size, long long count, const char *es, const "
    "char *as, const char *file, int line) {\n"
    "    size_t total = (size_t)elem_size * (size_t)count;\n"
    "    if (memcmp(expected, actual, total) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertArrayEq called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s[0..%lld] != %s[0..%lld] (%lld bytes differ) (%s:%d)\", es, "
    "count - 1, as, count - 1, (long long)total, file, line);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_msg(int cond, const char *expr, const "
    "char *msg, const char *file, int line) {\n"
    "    if (!cond) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertMsg called outside "
    "a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s (%s:%d) - %s\", expr, file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_eq_msg(long long a, long long b, const "
    "char *as, const char *bs, const char *msg, const char *file, int "
    "line) {\n"
    "    if (a != b) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertEqMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (%lld != %lld) (%s:%d) - %s\", as, bs, a, b, file, line, "
    "msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_streq_msg(const char *a, const char *b, "
    "const char *as, const char *bs, const char *msg, const char *file, "
    "int line) {\n"
    "    if (strcmp(a, b) != 0) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertStrEqMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s != %s (\\\"%s\\\" != \\\"%s\\\") (%s:%d) - %s\", as, bs, a, b, "
    "file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_null_msg(const void *p, const char *ps, "
    "const char *msg, const char *file, int line) {\n"
    "    if (p != NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNullMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is not null (%s:%d) - %s\", ps, file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_not_null_msg(const void *p, const char "
    "*ps, const char *msg, const char *file, int line) {\n"
    "    if (p == NULL) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertNotNullMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s is null (%s:%d) - %s\", ps, file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n"
    "static void __builtin_assert_bits_msg(long long mask, long long "
    "expected, long long actual, const char *ms, const char *es, const "
    "char *as, const char *msg, const char *file, int line) {\n"
    "    if ((actual & mask) != (expected & mask)) {\n"
    "        if (!__cccc_s_run) { fprintf(stderr, \"AssertBitsMsg called "
    "outside a test run at %s:%d\\n\", file, line); return; }\n"
    "        snprintf(__cccc_s_run->fail_msg, sizeof(__cccc_s_run->fail_msg), "
    "\"%s & %s = 0x%llx != %s & %s = 0x%llx (%s:%d) - %s\", as, ms, "
    "(unsigned long long)(actual & mask), es, ms, (unsigned long "
    "long)(expected & mask), file, line, msg);\n"
    "        __cccc_s_run->failed = 1;\n"
    "        _longjmp(__cccc_s_run->jmp, 1);\n"
    "    }\n"
    "}\n";

// #1033: fork-per-test TAP harness. See cc_serialize_program's own
// emit_test_harness gate for the CLI-side refusals (test_setup/teardown,
// negative tests) that keep this function's own scope narrow -- by the
// time this runs, vm->compiler.test_setups is guaranteed NULL and no
// vm->compiler.test_fns record has error_pat/expect_compile_error set.
static void serialize_test_harness(FILE *f, VirtualMachine *vm, Obj *prog) {
    if (vm->compiler.emit_cccc || !vm->compiler.test_fns)
        return;

    fputs("\n/* #1033: --testing=native generated test harness */\n", f);
    fputs(CCCC_TEST_ASSERT_RUNTIME_SRC, f);
    fputs("#include <sys/types.h>\n"
          "#include <sys/time.h>\n"
          "#include <unistd.h>\n"
          "#include <fcntl.h>\n"
          "#include <regex.h>\n"
          "#include <stdlib.h>\n"
          "#include <errno.h>\n"
          // #1033: when -I./include is on the compile line (as
          // tools/tests.py always passes it), CCCC's own bundled
          // signal.h/sys/wait.h -- polyfills for VM-internal use, see
          // man/HEADERS.md -- shadow the real host headers. That's merely
          // inconvenient for the missing kill() prototype below, but
          // outright breaks the build under a per-test `--std=c89
          // -Wpedantic`: the bundled signal.h uses the C99 `restrict`
          // keyword (a syntax error, not just a warning, under host
          // -std=c89), and bundled sys/wait.h itself #includes signal.h
          // for siginfo_t. Sidestepped entirely: no #include <signal.h>
          // or <sys/wait.h>, just the handful of symbols this harness
          // actually needs, declared directly. SIGALRM/SIGKILL/SIG_DFL
          // values and the WIFEXITED/WEXITSTATUS/WIFSIGNALED/WTERMSIG
          // status-word encoding are POSIX-traditional and identical on
          // Linux and Darwin, this project's only two supported
          // platforms (CLAUDE.md).\n"
          "extern pid_t waitpid(pid_t, int *, int);\n"
          "#define __CCCC_WIFEXITED(s) (((s) & 0x7f) == 0)\n"
          "#define __CCCC_WEXITSTATUS(s) (((s) >> 8) & 0xff)\n"
          "#define __CCCC_WIFSIGNALED(s) ((((signed char)(((s) & 0x7f) + "
          "1)) >> 1) > 0)\n"
          "#define __CCCC_WTERMSIG(s) ((s) & 0x7f)\n"
          "#define __CCCC_SIGALRM 14\n"
          "#define __CCCC_SIGKILL 9\n"
          "#define __CCCC_SIG_DFL ((void (*)(int))0)\n"
          "extern void (*signal(int, void (*)(int)))(int);\n"
          "extern int kill(pid_t, int);\n\n",
          f);

    // Reverse to declaration order (test_fns is built by prepending, same
    // as cc_run_tests, testing.c:1165).
    int n = 0;
    for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next)
        n++;
    TestFnRecord **ordered = malloc((size_t)n * sizeof(TestFnRecord *));
    {
        int i = n - 1;
        for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next)
            ordered[i--] = r;
    }

    // Per-test wrapper functions. status: 0 = will run, 1 = SKIP (not
    // found / per-test flags=).
    int *skip = calloc((size_t)n, sizeof(int));
    for (int i = 0; i < n; i++) {
        TestFnRecord *r  = ordered[i];
        Obj          *fn = NULL;
        for (Obj *o = prog; o; o = o->next) {
            if (o->is_function && o->name && strcmp(o->name, r->name) == 0) {
                fn = o;
                break;
            }
        }
        bool has_flags = r->test_flags_mask || r->test_opt_set ||
                         r->test_warn_mask || r->test_warn_errors_mask ||
                         r->test_warn_as_errors_set || r->test_f_set ||
                         r->test_ffi_allow_count > 0;
        if (!fn || has_flags) {
            skip[i] = 1;
            continue;
        }

        if (r->expect_exit_code >= 0) {
            // exit_code= tests skip the assertion-comparison wrapper
            // entirely -- the child _exit()s with the guest function's own
            // return value (VM parity: cc_run_at's fork path never sets up
            // __cccc_s_run either, so an Assert* inside such a test's body
            // degrades to a stderr warning rather than failing the test,
            // same as testing.c's own exit_code fork branch).
            bool is_void = (fn->ty && fn->ty->return_ty) &&
                           fn->ty->return_ty->kind == TY_VOID;
            fprintf(f, "static int __cccc_test_exit_%d(void) {\n", i);
            if (is_void)
                fprintf(f, "    %s();\n    return 0;\n", fn->name);
            else
                fprintf(f, "    return (int)(long long)%s();\n", fn->name);
            fputs("}\n\n", f);
            continue;
        }

        fprintf(f,
                "static int __cccc_test_run_%d(char *__msg, size_t __cap) "
                "{\n",
                i);
        fputs("    __cccc_test_run_state __st;\n"
              "    __st.failed = 0;\n"
              "    __st.fail_msg[0] = '\\0';\n"
              "    __cccc_s_run = &__st;\n"
              "    if (_setjmp(__st.jmp)) {\n"
              "        __cccc_s_run = NULL;\n"
              "        if (__msg) snprintf(__msg, __cap, \"%s\", "
              "__st.fail_msg);\n"
              "        return 0;\n"
              "    }\n",
              f);

        TypeKind ret_kind =
            (fn->ty && fn->ty->return_ty) ? fn->ty->return_ty->kind : TY_VOID;
        const char *op = cmp_op_c_operator(r->ret_op);
        switch (r->ret_kind) {
            case RET_INT:
                fprintf(f, "    long long __ret = (long long)%s();\n",
                        fn->name);
                fprintf(f,
                        "    if (!(__ret %s (long long)%lldLL)) {\n"
                        "        __cccc_s_run = NULL;\n"
                        "        if (__msg) snprintf(__msg, __cap, "
                        "\"expected return value %%s %%lld, got %%lld\", "
                        "\"%s\", (long long)%lldLL, __ret);\n"
                        "        return 0;\n"
                        "    }\n",
                        op, (long long)r->ret_expect.ret_int, op,
                        (long long)r->ret_expect.ret_int);
                break;
            case RET_FLOAT: {
                double eps = (r->ret_epsilon > 0.0) ? r->ret_epsilon : 1e-9;
                fprintf(f, "    double __ret = (double)%s();\n", fn->name);
                fprintf(f,
                        "    { double __diff = __ret - (%.17g); if (__diff "
                        "< 0) __diff = -__diff;\n"
                        "      if (__diff > %.17g) {\n"
                        "        __cccc_s_run = NULL;\n"
                        "        if (__msg) snprintf(__msg, __cap, "
                        "\"expected return value %%s %%g, got %%g\", "
                        "\"%s\", (double)(%.17g), __ret);\n"
                        "        return 0;\n"
                        "      } }\n",
                        r->ret_expect.ret_float, eps, op,
                        r->ret_expect.ret_float);
                break;
            }
            case RET_STR: {
                fprintf(f, "    const char *__ret = (const char *)%s();\n",
                        fn->name);
                fputs("    { const char *__exp = ", f);
                if (r->ret_expect.ret_str)
                    serialize_string_n(f, r->ret_expect.ret_str,
                                       (int)strlen(r->ret_expect.ret_str));
                else
                    fputs("NULL", f);
                fputs(";\n"
                      "      int __cmp = (__ret && __exp) ? strcmp(__ret, "
                      "__exp) : (__ret ? 1 : (__exp ? -1 : 0));\n",
                      f);
                fprintf(f,
                        "      if (!(__cmp %s 0)) {\n"
                        "        __cccc_s_run = NULL;\n"
                        "        if (__msg) snprintf(__msg, __cap, "
                        "\"expected return string %%s \\\"%%s\\\", got "
                        "\\\"%%s\\\"\", \"%s\", __exp ? __exp : \"(null)\", "
                        "__ret ? __ret : \"(null)\");\n"
                        "        return 0;\n"
                        "      } }\n",
                        op, op);
                break;
            }
            case RET_STRUCT:
                // #1033 v1: struct return= assertions need type-directed
                // per-field comparison codegen (see testing.c's
                // cmp_ret_aggregate) -- narrow enough in practice (a
                // handful of tests repo-wide) that it's deferred rather
                // than attempted here. The test still runs (asserts inside
                // it are honored); only the return-value check is skipped.
                (void)ret_kind;
                fprintf(f, "    (void)%s();\n", fn->name);
                break;
            case RET_NONE:
            default:
                fprintf(f, "    (void)%s();\n", fn->name);
                break;
        }
        fputs("    __cccc_s_run = NULL;\n"
              "    return 1;\n"
              "}\n\n",
              f);
    }

    // Test table.
    fputs("typedef struct {\n"
          "    const char *name;\n"
          "    const char *suite;\n"
          "    int (*run)(char *, size_t);\n"
          "    int (*run_exit)(void);\n"
          "    long timeout_ms;\n"
          "    int expect_exit_code;\n"
          "    const char *expect_stdout;\n"
          "    const char *reject_stdout;\n"
          "    const char *expect_stderr;\n"
          "    const char *reject_stderr;\n"
          "    const char *skip_reason;\n"
          "} __cccc_test_case;\n\n",
          f);
    fprintf(f, "static __cccc_test_case __cccc_tests[%d] = {\n", n > 0 ? n : 1);
    for (int i = 0; i < n; i++) {
        TestFnRecord *r    = ordered[i];
        const char   *disp = r->display_name ? r->display_name : r->name;
        fputs("    { ", f);
        serialize_string_n(f, disp, (int)strlen(disp));
        fputs(", ", f);
        if (r->suite)
            serialize_string_n(f, r->suite, (int)strlen(r->suite));
        else
            fputs("NULL", f);
        if (skip[i]) {
            fputs(", NULL, NULL, ", f);
        } else if (r->expect_exit_code >= 0) {
            fprintf(f, ", NULL, __cccc_test_exit_%d, ", i);
        } else {
            fprintf(f, ", __cccc_test_run_%d, NULL, ", i);
        }
        fprintf(f, "%ldL, %d, ", r->timeout_ms, r->expect_exit_code);
        const char *strs[4] = {r->expect_stdout, r->reject_stdout,
                               r->expect_stderr, r->reject_stderr};
        for (int k = 0; k < 4; k++) {
            if (strs[k])
                serialize_string_n(f, strs[k], (int)strlen(strs[k]));
            else
                fputs("NULL", f);
            fputs(", ", f);
        }
        if (skip[i])
            fputs("\"not supported by --testing=native (#1033 v1)\" },\n", f);
        else
            fputs("NULL },\n", f);
    }
    fputs("};\n\n", f);

    // main(): fork-per-test TAP runner.
    fputs("static volatile int __cccc_alarm_fired = 0;\n"
          "static volatile pid_t __cccc_fork_child = 0;\n"
          "static void __cccc_test_alarm(int sig) {\n"
          "    (void)sig;\n"
          "    __cccc_alarm_fired = 1;\n"
          "    if (__cccc_fork_child > 0) kill(__cccc_fork_child, "
          "__CCCC_SIGKILL);\n"
          "}\n"
          "static void __cccc_set_timeout(long ms) {\n"
          "    struct itimerval itv;\n"
          "    if (ms > 0) {\n"
          "        itv.it_interval.tv_sec = 0; itv.it_interval.tv_usec = 0;\n"
          "        itv.it_value.tv_sec = ms / 1000; itv.it_value.tv_usec = "
          "(ms % 1000) * 1000;\n"
          "    } else {\n"
          "        itv.it_interval.tv_sec = 0; itv.it_interval.tv_usec = 0;\n"
          "        itv.it_value.tv_sec = 0; itv.it_value.tv_usec = 0;\n"
          "    }\n"
          "    setitimer(ITIMER_REAL, &itv, NULL);\n"
          "}\n"
          "static char *__cccc_drain(int fd) {\n"
          "    if (fd < 0) return NULL;\n"
          "    size_t cap = 4096, len = 0;\n"
          "    char *buf = malloc(cap);\n"
          "    if (!buf) return NULL;\n"
          "    for (;;) {\n"
          "        if (len + 1024 > cap) { cap *= 2; char *nb = realloc(buf, "
          "cap); if (!nb) break; buf = nb; }\n"
          "        ssize_t r = read(fd, buf + len, cap - len - 1);\n"
          "        if (r <= 0) break;\n"
          "        len += (size_t)r;\n"
          "    }\n"
          "    buf[len] = '\\0';\n"
          "    return buf;\n"
          "}\n"
          "static int __cccc_check_pattern(const char *pat, const char *buf, "
          "int negate) {\n"
          "    if (!pat) return 1;\n"
          "    regex_t re;\n"
          "    if (regcomp(&re, pat, REG_EXTENDED) != 0) return 1;\n"
          "    int m = regexec(&re, buf ? buf : \"\", 0, NULL, 0) == 0;\n"
          "    regfree(&re);\n"
          "    return negate ? !m : m;\n"
          "}\n\n",
          f);

    fputs("int main(void) {\n", f);
    fprintf(f, "    int n = %d;\n", n);
    // #1186: `for (int i = ...)` is a C99 declaration -- the harness main()
    // this function emits is itself run through a compiled --std=c89 test
    // (test_suite_std_c89.c, whose own CCCC_FLAGS carry --testing, routed
    // to --testing=native under -c=native), which rejects it outright
    // ("'for' loop initial declarations are only allowed in C99 or C11
    // mode"). `i` hoisted to an ordinary top-of-block declaration, which
    // c89 allows, keeps every C standard this harness might be compiled
    // under working with no behavior change for the rest.
    fputs("    int i;\n"
          "    int passed = 0, failed = 0, skipped = 0, timedout = 0;\n"
          "    printf(\"TAP version 13\\n\");\n"
          "    printf(\"1..%d\\n\", n);\n"
          "    const char *prev_suite = NULL;\n"
          "    int have_prev_suite = 1;\n"
          "    signal(__CCCC_SIGALRM, __cccc_test_alarm);\n"
          "    for (i = 0; i < n; i++) {\n"
          "        __cccc_test_case *tc = &__cccc_tests[i];\n"
          "        int suite_changed = !have_prev_suite ||\n"
          "            ((tc->suite == NULL) != (prev_suite == NULL)) ||\n"
          "            (tc->suite && prev_suite && strcmp(tc->suite, "
          "prev_suite) != 0);\n"
          "        if (suite_changed) {\n"
          "            printf(\"# Suite: %s\\n\", tc->suite ? tc->suite : "
          "\"(none)\");\n"
          "            prev_suite = tc->suite;\n"
          "            have_prev_suite = 1;\n"
          "        }\n"
          "        if (!tc->run && !tc->run_exit) {\n"
          "            printf(\"ok %d - %s # SKIP %s\\n\", i + 1, tc->name, "
          "tc->skip_reason ? tc->skip_reason : \"\");\n"
          "            skipped++;\n"
          "            continue;\n"
          "        }\n"
          "        int out_pipe[2] = {-1, -1}, err_pipe[2] = {-1, -1}, "
          "msg_pipe[2] = {-1, -1};\n"
          "        int need_out = tc->expect_stdout || tc->reject_stdout;\n"
          "        int need_err = tc->expect_stderr || tc->reject_stderr;\n"
          "        if (need_out) pipe(out_pipe);\n"
          "        if (need_err) pipe(err_pipe);\n"
          "        pipe(msg_pipe);\n"
          "        fflush(stdout);\n"
          "        fflush(stderr);\n"
          "        __cccc_alarm_fired = 0;\n"
          "        pid_t pid = fork();\n"
          "        if (pid == 0) {\n"
          "            signal(__CCCC_SIGALRM, __CCCC_SIG_DFL);\n"
          "            close(msg_pipe[0]);\n"
          "            if (need_out) { close(out_pipe[0]); dup2(out_pipe[1], "
          "STDOUT_FILENO); close(out_pipe[1]); }\n"
          "            if (need_err) { close(err_pipe[0]); dup2(err_pipe[1], "
          "STDERR_FILENO); close(err_pipe[1]); }\n"
          "            if (tc->expect_exit_code >= 0) {\n"
          "                int rc = tc->run_exit();\n"
          "                _exit((unsigned char)rc);\n"
          "            }\n"
          "            char msg[512] = {0};\n"
          "            int ok = tc->run(msg, sizeof(msg));\n"
          "            if (!ok) write(msg_pipe[1], msg, strlen(msg));\n"
          "            close(msg_pipe[1]);\n"
          "            fflush(stdout);\n"
          "            fflush(stderr);\n"
          "            _exit(ok ? 0 : 1);\n"
          "        }\n"
          "        close(msg_pipe[1]);\n"
          "        if (need_out) close(out_pipe[1]);\n"
          "        if (need_err) close(err_pipe[1]);\n"
          "        __cccc_fork_child = pid;\n"
          "        __cccc_set_timeout(tc->timeout_ms);\n"
          "        int wstatus = 0;\n"
          "        pid_t waited;\n"
          "        do { waited = waitpid(pid, &wstatus, 0); } while "
          "(waited < 0 && errno == EINTR && !__cccc_alarm_fired);\n"
          "        __cccc_set_timeout(0);\n"
          "        __cccc_fork_child = 0;\n"
          "        int timed_out = __cccc_alarm_fired;\n"
          "        char *msg = __cccc_drain(msg_pipe[0]);\n"
          "        char *cap_out = __cccc_drain(need_out ? out_pipe[0] : "
          "-1);\n"
          "        char *cap_err = __cccc_drain(need_err ? err_pipe[0] : "
          "-1);\n"
          "        if (msg_pipe[0] >= 0) close(msg_pipe[0]);\n"
          "        if (out_pipe[0] >= 0) close(out_pipe[0]);\n"
          "        if (err_pipe[0] >= 0) close(err_pipe[0]);\n"
          "        if (timed_out) {\n"
          "            waitpid(pid, NULL, 0);\n"
          "            printf(\"not ok %d - %s\\n# TIMEOUT\\n\", i + 1, "
          "tc->name);\n"
          "            timedout++;\n"
          "        } else if (tc->expect_exit_code >= 0) {\n"
          "            int actual = -1;\n"
          "            if (__CCCC_WIFEXITED(wstatus)) actual = "
          "__CCCC_WEXITSTATUS(wstatus);\n"
          "            else if (__CCCC_WIFSIGNALED(wstatus)) actual = 128 + "
          "__CCCC_WTERMSIG(wstatus);\n"
          "            if (actual == tc->expect_exit_code) { printf(\"ok "
          "%d - %s\\n\", i + 1, tc->name); passed++; }\n"
          "            else { printf(\"not ok %d - %s\\n# expected "
          "exit_code %d, got %d\\n\", i + 1, tc->name, tc->expect_exit_code, "
          "actual); failed++; }\n"
          "        } else if (__CCCC_WIFSIGNALED(wstatus)) {\n"
          "            printf(\"not ok %d - %s\\n# aborted by signal "
          "%d\\n\", i + 1, tc->name, __CCCC_WTERMSIG(wstatus));\n"
          "            failed++;\n"
          "        } else if (__CCCC_WIFEXITED(wstatus) && "
          "__CCCC_WEXITSTATUS(wstatus) == "
          "0 &&\n"
          "                   __cccc_check_pattern(tc->expect_stdout, "
          "cap_out, 0) &&\n"
          "                   __cccc_check_pattern(tc->reject_stdout, "
          "cap_out, 1) &&\n"
          "                   __cccc_check_pattern(tc->expect_stderr, "
          "cap_err, 0) &&\n"
          "                   __cccc_check_pattern(tc->reject_stderr, "
          "cap_err, 1)) {\n"
          "            printf(\"ok %d - %s\\n\", i + 1, tc->name);\n"
          "            passed++;\n"
          "        } else {\n"
          "            printf(\"not ok %d - %s\\n# %s\\n\", i + 1, tc->name, "
          "(msg && msg[0]) ? msg : \"failed\");\n"
          "            failed++;\n"
          "        }\n"
          "        free(msg); free(cap_out); free(cap_err);\n"
          "    }\n"
          "    printf(\"# passed: %d, failed: %d, skipped: %d, timed out: "
          "%d\\n\", passed, failed, skipped, timedout);\n"
          "    return (passed + skipped == n) ? 0 : 1;\n"
          "}\n",
          f);

    free(ordered);
    free(skip);
}

void cc_serialize_program(FILE *f, VirtualMachine *vm, Obj *prog,
                          bool generated_only, bool emit_test_harness) {
    if (!f || !prog)
        return;

    SerializeContext ctx = {.generated_only = generated_only,
                            .emit_strict    = vm->compiler.emit_strict != 0,
                            .emit_cccc      = vm->compiler.emit_cccc,
                            .vm             = vm};
    // #1096: populated unconditionally now, not only under generated_only --
    // the bodiless-declaration prototype pass below needs path_is_captured()
    // to tell a *replayed* bundled-header #include (already supplying the
    // declaration) apart from an unreplayed one (which does not) in plain
    // -m/-c=native output too. Safe for every existing caller:
    // type_def_is_from_include_suppressed()'s own path_is_captured() call is
    // still gated `!ctx->generated_only || path_is_captured(...)`, so it
    // short-circuits before ever consulting captured_paths whenever
    // generated_only is false, exactly as before this change.
    hashmap_foreach(&vm->compiler.emit_include_paths, collect_captured_path,
                    &ctx);
    collect_scope_names(&ctx, vm);
    rename_anon_globals(vm, prog, &ctx);
    rename_colliding_static_names(vm, prog, &ctx);   // #1002
    rename_colliding_type_tags(vm, prog, &ctx);      // #1014
    rename_colliding_enum_constants(vm, prog, &ctx); // #1015
    collect_deferred_static_labels(vm, prog, &ctx);  // #1044
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function && !obj->is_definition && !obj->body)
            continue;
        if (!obj->is_function && obj->name[0] == '.')
            continue;
        // #1167: layout_type_needs_collecting() (serialize_type.c) gates a
        // folded sizeof/_Alignof's operand type on the same scope-visible
        // type_has_printable_name() check serialize_layout_const() itself
        // uses at emission time -- and that check is scope-sensitive
        // (name_visible(), gated on ctx->current_fn). Emission later runs
        // this exact obj as ctx->current_fn (serialize_type_defs_for_owner()
        // below), so a LOCAL tagged aggregate (a function-local `struct Foo
        // { ... };`, TypeNameRecord.owner_fn == this obj) only has a
        // printable name once ctx->current_fn matches -- collection must set
        // it the same way, or a struct referenced only via sizeof and
        // declared local to a function is wrongly judged unprintable here
        // and silently dropped again (the very regression #1167 exists to
        // fix), even though its own emission pass would have printed it
        // fine.
        ctx.current_fn = obj->is_function ? obj : NULL;
        collect_obj_types(&ctx, obj);
        ctx.current_fn = NULL;
    }
    // #1167: file-scope _Static_assert conditions (StaticAssertRecord,
    // #1098) live on their own standalone list, not on any Obj -- the
    // collect_obj_types() loop above never reaches them. Collect
    // unconditionally, matching the same-ticket fix to collect_node_types()/
    // collect_type() for a folded sizeof/_Alignof re-materialized elsewhere
    // (serialize_expr.c, serialize_type.c) -- otherwise a re-materialized
    // `sizeof(T)`/`_Alignof(T)` inside the assert can be the only reference
    // to T left in the AST, and no definition is emitted for it.
    for (StaticAssertRecord *sa = vm->compiler.static_asserts; sa;
         sa                     = sa->next)
        collect_static_assert_types(&ctx, sa->cond);
    reorder_defs_by_byval_deps(&ctx); // #1042(a)

    // Header comment
    fprintf(f, "/* Generated by CCCC pragma macro expansion */\n\n");

    // #1113(a): as early as possible, so a host compiler that can't lower
    // _Decimal (clang) reports cccc's own diagnostic first rather than
    // scrolling the user past every other line of generated output before
    // reaching the real "GNU decimal type extension not supported" error.
    serialize_decimal_native_guard(f, &ctx);

    // Re-emit libraries queued via #pragma cccc link() using CCCC's own
    // spelling, so the queue round-trips if this output is ever re-fed to
    // cccc itself (#1149). The previous "#pragma comment(lib, ...)" spelling
    // looked portable but wasn't an input form cccc understood either --
    // handle_pragma_body (src/preprocess.c) has no `comment` branch, so a
    // round-trip silently dropped the library requirement. gcc/clang ignore
    // "#pragma cccc link(...)" exactly as harmlessly as they ignored
    // "#pragma comment(lib, ...)" -- neither is a pragma either compiler
    // recognizes -- so this loses nothing for a plain downstream `cc`.
    //
    // Skipped entirely under -c=native: main.c's own pragma_link_libs -> -l
    // merge (before run_native_backend is ever called) already gets the
    // library to the host linker, so the pragma here would be pure noise
    // sitting next to a real -l for the same name.
    if (!vm->compiler.native_mode) {
        for (int i = 0; i < vm->compiler.pragma_link_libs.len; i++)
            fprintf(f, "#pragma cccc link(\"%s\")\n",
                    vm->compiler.pragma_link_libs.data[i]);
        if (vm->compiler.pragma_link_libs.len > 0)
            fprintf(f, "\n");
    }

    // #965/#993: block env structs (see serialize_block_preamble) are
    // emitted once both mechanisms that can bring a *capture's* type into
    // scope have already run: serialize_type_defs_for_owner(f, &ctx, NULL)
    // (file-scope struct/union/enum definitions -- including a
    // cccc-only-routed include's type, which #896 deliberately re-derives
    // here rather than re-emitting its #include) and, in the
    // !generated_only branch, the #include replay further down (a plain
    // captured header like <time.h>). Originally this call sat ahead of
    // everything (see history) -- a by-value capture of a *header-declared*
    // type (e.g. `struct tm`) was serialized while that type wasn't
    // complete yet, the mirror image of the function-local-type problem
    // #989 fixed (there the env struct was ahead of the declaring function;
    // here it needed to be *behind* whichever mechanism brings the header
    // type into scope). Moving the include replay alone is not sufficient:
    // a cccc-only-routed include's type reaches the output via
    // serialize_type_defs_for_owner, not the replay (#896), so both must
    // precede this call. Placed identically in both branches below, right
    // after each one's own serialize_type_defs_for_owner call.
    //
    // This flips the #989 hoist (inside serialize_block_preamble) relative
    // to serialize_type_defs_for_owner: a function-local capture type still
    // has owner_fn != NULL when the file-scope pass above runs, so it's
    // skipped there (not yet hoisted), then hoisted/emitted here -- verified
    // no double-emission against the #989 regression case.
    //
    // Residual, not fixed here: in the generated_only branch below, a
    // captured #include is replayed via CCCC_EMIT_SOURCE events interleaved
    // with generated functions (pinned there by #953), so a header-type
    // capture in *generated* code can still precede its #include -- filed
    // as #995.
    if (generated_only && vm->compiler.emit_events_head) {
        serialize_type_defs_for_owner(f, &ctx, NULL);
        serialize_block_preamble(f, vm, &ctx, prog);
        serialize_nested_preamble(f, vm, &ctx, prog);      // #1074
        hoist_compiler_temp_anon_types(f, vm, &ctx, prog); // #1186
        // #928: forward-declare every macro-generated global before any
        // definition, mirroring the #918 pass below (serialize_global_var's
        // sibling loop, further down this function) and for the same
        // reason -- the drain that populates these emit events
        // (macros.c:2775-2783) walks vm->compiler.globals newest-first, so
        // objects created earlier in one macro invocation are recorded
        // *later*. A file-scope CompoundLiteral()/InitStruct() call (whose
        // anon gvar is created before the function that references it)
        // would otherwise emit that function body ahead of the global's own
        // definition -- a forward reference with nothing in scope yet.
        for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next) {
            if (ev->kind != CCCC_EMIT_OBJECT)
                continue;
            Obj *obj = ev->obj;
            if (!obj || !obj->is_macro_generated || obj->is_function ||
                obj->name[0] == '.')
                continue;
            // #1023: see type_needs_anon_aggregate's comment on the #918
            // loop below -- an untagged, alias-less struct/union global
            // can't be forward-declared at all without re-deriving a
            // structurally distinct anonymous type.
            if (type_needs_anon_aggregate(&ctx, obj->ty))
                continue;
            // #1044: deferred into its owning function's own body -- see
            // collect_deferred_static_labels()'s own comment.
            if (var_is_deferred_label_static(&ctx, obj))
                continue;
            fprintf(f, obj->is_static ? "static " : "extern ");
            if (obj->is_tls) // #1022: see serialize_global_var's own comment
                fprintf(f, "_Thread_local ");
            // #1136: see serialize_alignas_if_needed's own comment.
            serialize_alignas_if_needed(f, obj);
            // #1095: same rule as serialize_global_var's own -- only when
            // no byte-image initializer will follow for this object.
            ctx.allow_layout_dims = !obj->init_data;
            serialize_type_decl(f, &ctx, obj->ty, obj->name);
            ctx.allow_layout_dims = false;
            fprintf(f, ";\n");
        }
        // #956: forward-declare a macro-generated function's callees the
        // moment its own event is reached, rather than hoisting every
        // prototype up front -- emission order here follows
        // PublishNode/MakeFunction event order, which has no relation to
        // the call graph, so a function's body can reference another
        // generated function whose own event appears later. Hoisting
        // every prototype unconditionally (tried first) broke two other
        // guarantees: a prototype placed ahead of the #include that
        // defines one of its struct-tag types gets function-prototype
        // scope for that tag, conflicting with the type's real,
        // later-in-scope definition (#953); and a function generated
        // inside a preprocessor-routed `#ifdef` block (test_emit_ordered_
        // ifdef.c) needs its prototype to stay inside that block, not
        // float above it. Doing this on demand, scanning each function's
        // body for calls to not-yet-declared generated functions right
        // before emitting it, keeps unrelated functions and #ifdef-guarded
        // ones exactly where they were and only forward-declares what a
        // caller actually needs.
        ObjVec declared = {0};
        for (EmitEvent *ev = vm->compiler.emit_events_head; ev; ev = ev->next) {
            if (ev->kind == CCCC_EMIT_SOURCE) {
                fprintf(f, "%s\n", ev->source);
                continue;
            }
            Obj *obj = ev->obj;
            if (!obj || !obj->is_macro_generated)
                continue;
            if (obj->is_function) {
                if (obj->body) {
                    ObjVec needed = {0};
                    collect_generated_call_targets(obj->body, &needed);
                    for (int i = 0; i < needed.len; i++) {
                        Obj *callee = needed.data[i];
                        if (obj_vec_contains(&declared, callee))
                            continue;
                        serialize_function_signature(f, &ctx, callee, true);
                        fprintf(f, ";\n");
                        obj_vec_push(&declared, callee);
                    }
                    free(needed.data);
                }
                // A FunctionPrototype()+PublishNode() that never gets a
                // body still needs to reach the output -- previously
                // dropped entirely by this loop's `!is_definition &&
                // !body` skip.
                if (!obj_vec_contains(&declared, obj)) {
                    serialize_function_signature(f, &ctx, obj, true);
                    fprintf(f, ";\n\n");
                    obj_vec_push(&declared, obj);
                }
                if (obj->body)
                    serialize_function(f, vm, &ctx, obj);
            } else if (obj->name[0] != '.') {
                serialize_global_var(f, vm, &ctx, obj);
            }
        }
        free(declared.data);
        free(ctx.seen.data);
        free(ctx.defs.data);
        free(ctx.tags);
        free(ctx.typedefs);
        free(ctx.captured_paths);
        free(ctx.block_envs);
        free_nested_envs(&ctx);
        free(ctx.hoisted.data);
        free(ctx.emitted_defs.data);
        return;
    }

    // Prepend preprocessor directives routed to generated output.
    // #896: an auto-captured #include line whose resolved file (directly, or
    // transitively through its own plain #includes) contains cccc-only
    // routing syntax (@comptime/@shared/@emit/@build/@test, or the
    // [[cccc::...]] spellings) is never re-emitted here -- a downstream
    // system compiler opening that file directly would choke on syntax it
    // doesn't understand (see run_native_backend, main.c).
    // serialize_typedef_alias / serialize_type_defs_for_owner compensate by no
    // longer treating that file's types as from_include, so their definitions
    // are still emitted below instead of being silently dropped.
#ifdef __APPLE__
    // #1140: isalpha_l/toupper_l/nl_langinfo_l/strfmon_l are real macOS
    // symbols, but declared in xlocale/_ctype.h, xlocale/_langinfo.h,
    // xlocale/_monetary.h -- not reachable through a plain #include
    // <ctype.h>/<langinfo.h>/<monetary.h> the way they are registered here
    // (src/stdlib/ctype.c's own #ifdef __APPLE__ / #include <xlocale.h>).
    // A replayed `#include <ctype.h>` etc alone leaves them undeclared on
    // the host cc even though CCCC's own FFI registration takes their
    // address just fine (they're `static inline` in the SDK, legal from
    // within any TU that includes them). One latched injection covers all
    // three headers since a program can include more than one.
    bool emitted_xlocale = false;
#endif
    // #1143: CCCC's own bundled copy of a handful of standard headers
    // quote-#includes a second, related standard header purely as a
    // convenience (fts.h -> sys/stat.h for `struct stat *fts_statp`,
    // unistd.h -> sys/uio.h for struct iovec/readv/writev, sys/un.h ->
    // sys/socket.h for socklen_t/bind()) -- true of both macOS's and
    // glibc's real headers too, verified directly, unlike sys/mount.h's
    // struct statfs split above. Before #1143 demoted CCCC's own bundled
    // include dir to `-idirafter`, a replayed `#include <fts.h>` etc at
    // native-compile time actually resolved to CCCC's OWN copy (searched
    // ahead of the real system headers), so this convenience pull-in rode
    // along for free; the real host headers those angle-bracket includes
    // now correctly resolve to are deliberately minimal and don't bundle
    // the second header the way CCCC's own copy does (confirmed: real
    // macOS fts.h only forward-declares `struct stat *`, doesn't include
    // <sys/stat.h>). Same latched-injection shape as emitted_xlocale
    // above -- one #include per program regardless of how many of that
    // header's own directives got captured.
    bool emitted_sys_stat_h   = false;
    bool emitted_sys_uio_h    = false;
    bool emitted_sys_socket_h = false;
    // #1143 regression: CCCC's own bundled unistd.h also declares mkstemp
    // (include/unistd.h:114) as a same-directory convenience -- real BSD/
    // macOS headers do too (declared in both unistd.h and stdlib.h there),
    // but real glibc puts it in <stdlib.h> only. Before #1143's directory
    // demotion, a replayed `#include <unistd.h>` resolved to CCCC's own
    // copy and mkstemp rode along the same way fts.h/unistd.h/sys/un.h's
    // own companion pull-ins above did; now that it resolves to the real,
    // minimal glibc header on Linux, mkstemp needs the same "emit a
    // companion #include" treatment they already got. Unconditional (not
    // __linux__-gated) like sys_stat_h/sys_uio_h/sys_socket_h above --
    // an extra `#include <stdlib.h>` is harmless on macOS/BSD, where
    // mkstemp is already visible through unistd.h alone.
    bool emitted_stdlib_h_for_mkstemp = false;
    // #1143 regression: the real host's <tgmath.h> (never intercepted --
    // only math.h/float.h are, see the math.h/float.h substitution below)
    // includes the real host's own <math.h> internally to build its
    // type-generic macros (remquo, etc), sharing that real header's own
    // include guard. CCCC's bundled math.h has a *different* guard macro
    // name, so once tgmath.h's macros are live, a later captured `#include
    // <math.h>` forced to CCCC's own copy is NOT skipped as the natural
    // no-op a real header's matching guard would produce -- its plain
    // `double remquo(double, double, int *);` declaration gets corrupted
    // by tgmath.h's own already-active `remquo` macro mid-declaration
    // ("a type specifier is required for all declarations", confirmed:
    // tests/suites/test_suite_floats.c, which includes tgmath.h before
    // math.h). Track whether tgmath.h was already replayed; only force
    // CCCC's own copy when it wasn't (or hasn't been reached yet in source
    // order) -- source order after tgmath.h falls back to the ordinary
    // replay, matching the safe pre-#1143 behaviour for that ordering
    // (this file's own real declarations were always sufficient there,
    // since it doesn't call the C23 IEEE family this substitution exists
    // for).
    bool seen_tgmath_h = false;
    for (int i = 0; i < vm->compiler.emit_directives.len; i++) {
        char *line     = vm->compiler.emit_directives.data[i];
        char *resolved = hashmap_get(&vm->compiler.emit_include_paths, line);
        // --emit-cccc: re-emit cccc-only includes too -- the caller has
        // opted into dialect-fidelity output, so a downstream reader is
        // expected to understand the routing syntax those files carry.
        if (!vm->compiler.emit_cccc && resolved &&
            cc_file_is_cccc_only(vm, resolved)) {
            // #1003: <decimal_math.h>'s static inline wrappers all bottom
            // out in `extern __cccc_dec_*` symbols that exist only inside
            // the VM's FFI runtime (src/stdlib/decimal_math.c) -- unlike
            // every other header this loop suppresses (whose content the
            // type/function-definition passes below can genuinely
            // re-derive as real, linkable C), there is no host definition
            // to link against here. Re-deriving would only trade "file not
            // found" for "undefined symbol"; hard error instead, matching
            // the existing _Decimal serialization refusal
            // (__builtin_decimal_to_chars, serialize_expr.c -- moved there
            // by the b1c3d21 serialize.c split, "above in this file" is
            // stale post-split).
            if (path_basename_is(resolved, "decimal_math.h"))
                error("cccc: <decimal_math.h> is not supported in "
                      "native/serialized output (__cccc_dec_* helpers have "
                      "no host definition)");
            continue;
        }
        // #1054/#1030: setjmp.h is *owned* (VM-specific jmp_buf ABI), not
        // cccc-only, so it doesn't take the branch above -- but it still
        // must never reach native/-m output verbatim, for a different
        // reason: relying on this replayed line to resolve to the real
        // host <setjmp.h> at native-compile time is exactly the fragile
        // include-search-path dependency serialize_synth_setjmp_decls()'s
        // own comment (below) explains. `--emit-cccc` is exempted like the
        // cccc-only branch above -- its whole point is dialect fidelity,
        // and a cccc reader understands this header directly.
        if (!vm->compiler.emit_cccc && resolved &&
            path_basename_is(resolved, "setjmp.h"))
            continue;
        // #1064: a captured conditional-group directive line
        // (#if/#ifdef/#ifndef/#elif/#elifdef/#elifndef/#else/#endif) is
        // always an empty shell here -- CCCC's own preprocessor has already
        // resolved the guarded content (a skipped branch's body is never
        // captured at all; a taken branch's content is captured as its own
        // separate lines/directives), so the shell carries no information.
        // Replaying it anyway hands the *evaluation* to the host compiler a
        // second time, for no benefit and two real hazards: a host that
        // lacks a feature-test macro CCCC's own preprocessor already
        // resolved (e.g. clang 18 rejecting a captured
        // `#if __has_embed(...)` shell outright, "function-like macro
        // '__has_embed' is not defined" -- CCCC evaluated it fine, the
        // empty shell is the only thing reaching the host), and a captured
        // `#ifdef __CCCC__` shell being silently false at the host (which
        // never defines that macro), dropping whatever a taken branch
        // inside it captured. `--emit-cccc` is exempted like the two
        // filters above -- dialect-fidelity output expects a cccc-aware
        // reader.
        if (!vm->compiler.emit_cccc && line_is_conditional_directive(line))
            continue;
        // #1114: a captured #embed line must never be replayed. The
        // directive's whole effect -- reading the file and splicing its
        // bytes (plus prefix/suffix/limit/if_empty) into the token stream --
        // already happened at parse time (handle_embed_directive,
        // preprocess.c), so the serialized AST carries the evaluated bytes
        // and the replay would duplicate them at top level, where a host's
        // expansion of the directive is a bare byte list with no initializer
        // context (a syntax error even when the file resolves). Replaying
        // also re-resolves the filename against the native compile's own
        // temp directory (make_tmp_path, exec.c) instead of the original
        // source file's directory, so a source-relative operand breaks
        // outright ("file not found"). `--emit-cccc` is exempted like the
        // filters above -- dialect-fidelity output expects a cccc-aware
        // reader.
        if (!vm->compiler.emit_cccc && !strncmp(line, "#embed", 6))
            continue;
        // #1184-adjacent: drop unconditionally, not exempted under
        // --emit-cccc like the filters around it -- this isn't a
        // dialect-fidelity concern, the collision this prevents is real
        // regardless of whether the reader understands CCCC's own dialect
        // (see line_is_threads_h_private_alias_directive()'s own comment
        // above).
        if (line_is_threads_h_private_alias_directive(line))
            continue;
        // #1118: a captured #define/#undef whose macro NAME contains
        // non-ASCII bytes (emoji identifiers -- an accepted CCCC extension,
        // e.g. tests/suites/test_suite_misc.c's worm/snake operator macros)
        // must never be replayed: every in-AST use of the macro was already
        // expanded at parse time, and the host preprocessor rejects a
        // non-ASCII macro name outright ("macro name must be an identifier",
        // xN for the defines plus their matching #undefs), so replaying the
        // line can only fail an otherwise-clean native compile. No other
        // replayed directive text can legally reference such a name either --
        // the host applies the same rejection there. The demand-driven
        // alternative (emit a captured define only when some other replayed
        // line references it) would be safer by construction against hidden
        // consumers, but no consumer is known to remain post-#1114 (the
        // LIMIT_EXPR-inside-#embed-limit case that motivated define replay
        // is gone), so the plain name filter matches the surrounding
        // per-line filters in both mechanism and cost. `--emit-cccc` is
        // exempted like the filters above -- dialect-fidelity output expects
        // a cccc-aware reader.
        if (!vm->compiler.emit_cccc && line_macro_name_is_non_ascii(line))
            continue;
        // #1143 regression: math.h/float.h are complete, self-contained
        // polyfills with no #include_next hand-off of their own (unlike
        // pthread.h/errno.h/etc, or sched.h/locale.h -- see
        // find_cccc_bundled_header_path's own comment, src/preprocess.c),
        // so a replayed bare `#include <math.h>`/`<float.h>` must never be
        // allowed to resolve to the real host's copy the way #1143's
        // directory-wide -idirafter demotion otherwise lets it -- the real
        // host doesn't declare the C23 IEEE family (fmaximum/setpayload/
        // etc) the way CCCC's bundled copy unconditionally does. Substitute
        // an absolute-path include to CCCC's own copy instead of the bare
        // replay when a bundled dir was actually marked (the only scenario
        // the demotion, and therefore this hazard, can fire); otherwise
        // fall through to the ordinary replay below, unchanged.
        if (!vm->compiler.emit_cccc && !seen_tgmath_h && resolved &&
            (path_basename_is(resolved, "math.h") ||
             path_basename_is(resolved, "float.h"))) {
            char *bundled_path = find_cccc_bundled_header_path(
                vm,
                path_basename_is(resolved, "math.h") ? "math.h" : "float.h");
            if (bundled_path) {
                fprintf(f, "#include \"%s\"\n", bundled_path);
                continue;
            }
        }
        if (resolved && path_basename_is(resolved, "tgmath.h"))
            seen_tgmath_h = true;
        fprintf(f, "%s\n", line);
        // On Linux, a replayed `#include <sys/mount.h>` does NOT bring
        // `struct statfs` into scope the way it does on macOS/BSD -- real
        // glibc's own <sys/mount.h> only carries mount(2) flags; the struct
        // lives in <sys/vfs.h> instead (include/sys/mount.h's own #ifdef
        // __linux__ branch documents this same asymmetry for the
        // explicit-`-I` case, but that branch is never reached here since
        // this loop replays the bare `#include` line verbatim, resolved
        // against the host's OWN header search path, not CCCC's bundled
        // one). Without this, a folded sizeof(struct statfs)/_Alignof
        // re-materialized textually (#1031/#1095/#1098) hits "invalid
        // application of 'sizeof' to an incomplete type" on a real Linux
        // host. run_native_backend never cross-compiles -- the host cc it
        // spawns always targets the same OS this cccc binary itself runs
        // on -- so gating on __linux__ here (this translation unit's own
        // host, not some target flag) is safe.
#ifdef __linux__
        if (resolved && path_basename_is(resolved, "mount.h") &&
            strstr(resolved, "sys/mount.h"))
            fprintf(f, "#include <sys/vfs.h>\n");
#endif
#ifdef __APPLE__
        // #1140: see emitted_xlocale's own comment above.
        if (!emitted_xlocale && resolved &&
            (path_basename_is(resolved, "ctype.h") ||
             path_basename_is(resolved, "langinfo.h") ||
             path_basename_is(resolved, "monetary.h"))) {
            fprintf(f, "#include <xlocale.h>\n");
            emitted_xlocale = true;
        }
#endif
        // #1143: see
        // emitted_sys_stat_h/emitted_sys_uio_h/emitted_sys_socket_h's own
        // comment above this loop -- unlike sys/mount.h's split (Linux- only)
        // and xlocale.h's injection (macOS-only), these three convenience
        // pull-ins are missing from the real, minimal header on BOTH supported
        // hosts, so the emission is unconditional.
        if (!emitted_sys_stat_h && resolved &&
            path_basename_is(resolved, "fts.h")) {
            fprintf(f, "#include <sys/stat.h>\n");
            emitted_sys_stat_h = true;
        }
        if (!emitted_sys_uio_h && resolved &&
            path_basename_is(resolved, "unistd.h")) {
            fprintf(f, "#include <sys/uio.h>\n");
            emitted_sys_uio_h = true;
        }
        if (!emitted_stdlib_h_for_mkstemp && resolved &&
            path_basename_is(resolved, "unistd.h")) {
            fprintf(f, "#include <stdlib.h>\n");
            emitted_stdlib_h_for_mkstemp = true;
        }
        if (!emitted_sys_socket_h && resolved &&
            path_basename_is(resolved, "un.h") &&
            strstr(resolved, "sys/un.h")) {
            fprintf(f, "#include <sys/socket.h>\n");
            emitted_sys_socket_h = true;
        }
    }
    if (vm->compiler.emit_directives.len > 0)
        fprintf(f, "\n");

    // #1050: headers for comptime-synthesized libc calls with no #include
    // of their own to auto-capture -- see serialize_synth_libc_includes's
    // own comment. Placed after the replayed user includes (so it can't
    // shadow them) and before the accessor shims (some of which reference
    // libc-declared types).
    if (!generated_only) {
        serialize_synth_libc_includes(f, vm, prog);
        if (!vm->compiler.emit_cccc)
            serialize_synth_setjmp_decls(f, vm, prog);
        // #1068: unlike serialize_synth_setjmp_decls, NOT gated on
        // emit_cccc -- the ND_CAST rewrite these helpers back are emitted
        // unconditionally (see serialize_expr's ND_CAST case), so
        // --emit-cccc output needs them defined too, or it calls an
        // undefined function.
        serialize_synth_f2i_helpers(f, prog);
        // #1117: same unconditional-on-emit_cccc placement (#1068
        // reasoning) -- the bundled complex.h/tgmath.h macros expand plain
        // spelled accessors to cccc-internal names that need host-side
        // definitions no replay can supply. Sits after the f2i pass and
        // before the typedef/accessor-shim passes; nothing here references
        // anything those emit, and everything downstream of the include
        // replay above can already see it.
        serialize_synth_complex_decls(f, vm, prog);
    }

    // #1057: headers for comptime-folded standard typedef names (size_t/
    // ptrdiff_t/wchar_t) with no #include of their own to auto-capture --
    // see serialize_synth_typedef_includes's own comment. Same placement
    // rationale as the synth-libc-call pass just above; ctx->typedefs is
    // already populated by collect_scope_names() near the top of this
    // function.
    if (!generated_only)
        serialize_synth_typedef_includes(f, &ctx, prog);

    // #904: real symbols for internal host-accessor shims (stdout/errno/
    // etc) -- only meaningful once the real headers above are visible, and
    // only outside generated_only (-c=generated), matching the from_include
    // filter's gating for the same reason (see the comment on this function).
    if (!generated_only) {
        serialize_native_accessor_shims(f, prog);
        serialize_reallocarray_shim(f, prog); // #1155
    }
    serialize_asm_prefix_preamble(f, prog);

    // Serialize file-scope type definitions before declarations that reference
    // them.
    serialize_type_defs_for_owner(f, &ctx, NULL);

    // #1088: real definitions for the C11 <threads.h> family -- run after
    // serialize_type_defs_for_owner just above (the shim bodies name mtx_t/
    // cnd_t/thrd_t/tss_t, re-derived there like any other cccc-only header's
    // types) and before the block/nested preambles below, which don't
    // reference threads.h's own types. !generated_only-gated like its
    // neighbours; also skips --emit-cccc internally (see its own comment).
    if (!generated_only)
        serialize_threads_shims(f, vm, prog);

    // #1141: real definitions for <uchar.h>'s mbrtoc16/c16rtomb/mbrtoc32/
    // c32rtomb/mbrtoc8/c8rtomb -- same placement rationale (and the same
    // !generated_only gating) as serialize_threads_shims just above.
    if (!generated_only)
        serialize_uchar_shims(f, vm, prog);

    // #1140: real definitions for ppoll/sched_*/gethostbyname_r/
    // gethostbyaddr_r/getnetbyname_r where the VM's own equivalent isn't a
    // passthrough to a real host symbol -- struct pollfd/sched_param/
    // hostent/netent are only referenced if the guest itself included the
    // corresponding header, which the replayed-include pass already put in
    // scope, so this must run after that (like its neighbours above) but
    // has no ordering dependency on serialize_type_defs_for_owner (poll.h/
    // sched.h/netdb.h are real host headers, not cccc-only ones re-derived
    // there). Same !generated_only gating as its neighbours.
    if (!generated_only)
        serialize_posix_compat_shims(f, vm, prog);

    // #1195: same placement rationale as serialize_posix_compat_shims just
    // above -- math.h is a real host header CCCC merely bundles its own
    // copy of (forced into scope by the #1143 substitution), no dependency
    // on serialize_type_defs_for_owner.
    if (!generated_only)
        serialize_c23_fromfp_shims(f, vm, prog);

    // #1146: same placement rationale as serialize_posix_compat_shims just
    // above (struct/typedef visibility from the #include replay, no
    // dependency on serialize_type_defs_for_owner since poll.h/langinfo.h/
    // locale.h/sched.h are all real host headers). Order relative to
    // serialize_posix_compat_shims doesn't matter -- the two touch disjoint
    // symbol sets except for sharing nothing but the file's own fprintf
    // stream, and neither reads output the other wrote.
    if (!generated_only)
        serialize_canonical_const_shims(f, vm, prog);

    // #1105: real definitions for dlopen/dlsym/dlclose/dlerror that mirror
    // the VM's own dynamic-library registry (src/vm.c's cccc_rt_dl*) rather
    // than passing straight through to the host's -- specifically its
    // "dlclose refuses a handle with live callable symbols" policy. Same
    // placement/gating rationale as serialize_posix_compat_shims and
    // serialize_canonical_const_shims just above (bundled dlfcn.h is a real
    // host header CCCC merely ships its own copy of; no dependency on
    // serialize_type_defs_for_owner).
    if (!generated_only)
        serialize_dlfcn_shims(f, vm, prog);

    // #965/#993: see the comment on the generated_only branch's own call
    // above -- must run after both the #include replay and the file-scope
    // type-def pass just above, so a capture's type (however it reaches the
    // output) is already visible.
    serialize_block_preamble(f, vm, &ctx, prog);
    serialize_nested_preamble(f, vm, &ctx, prog);      // #1074
    hoist_compiler_temp_anon_types(f, vm, &ctx, prog); // #1186

    // #918: forward-declare every global before any definition, mirroring
    // the function-prototype pass below and for the same reason -- a
    // global's initializer can take the address of another global that
    // appears later in `prog` (e.g. `int *p = &g;` parsed/emitted before
    // `g`'s own definition), which used to compile "successfully" only
    // because that address was silently serialized as a null pointer
    // (defect C) rather than the real `&g` reference. Once the real
    // reference is emitted, the forward case needs a declaration in scope.
    // Redundant for the (common) non-forward-referencing case, but a
    // duplicate `extern`/tentative-`static` declaration is always valid C.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function || obj->name[0] == '.')
            continue;
        // #1023: an untagged, alias-less struct/union global (e.g.
        // `static const struct { ... } codes[74]`) can't be
        // forward-declared -- see type_needs_anon_aggregate's comment.
        // Skipping it here is strictly better than the alternative
        // (re-deriving a second, structurally distinct anonymous type that
        // the host compiler rejects as a redefinition): the real
        // definition below (serialize_global_var) still carries the only
        // copy of the type, so nothing is lost except the (here,
        // impossible) forward reference this pass exists to support.
        if (type_needs_anon_aggregate(&ctx, obj->ty))
            continue;
        // #1044: deferred into its owning function's own body -- see
        // collect_deferred_static_labels()'s own comment.
        if (var_is_deferred_label_static(&ctx, obj))
            continue;
        // #1047: a header-supplied global is already forward-visible via
        // the replayed #include -- see global_is_header_supplied()'s
        // comment.
        if (global_is_header_supplied(vm, &ctx, obj))
            continue;
        fprintf(f, obj->is_static ? "static " : "extern ");
        if (obj->is_tls) // #1022: see serialize_global_var's own comment
            fprintf(f, "_Thread_local ");
        // #1136: see serialize_alignas_if_needed's own comment.
        serialize_alignas_if_needed(f, obj);
        // #1095: same rule as serialize_global_var's own -- only when no
        // byte-image initializer will follow for this object, so the
        // forward declaration and the real definition further down never
        // disagree on how the dimension is spelled.
        ctx.allow_layout_dims = !obj->init_data;
        serialize_type_decl(f, &ctx, obj->ty, obj->name);
        ctx.allow_layout_dims = false;
        fprintf(f, ";\n");
    }

    // #999: forward-declare any function a global's initializer references
    // by address (`var->rel`, resolved the same way serialize_reloc_init
    // resolves it further down) -- e.g. `static const VT k = { .open =
    // none_open };` where `none_open` is a `static` function defined later
    // in `prog`. The #918 loop just above only forward-declares *globals*;
    // the function-prototype pass below (which would otherwise supply
    // `none_open`'s own declaration) doesn't run until after every global
    // definition has already been emitted, so a forward reference like this
    // one reached the output with nothing in scope yet ("use of undeclared
    // identifier"). Resolved on demand here rather than by moving the whole
    // prototype pass above the global-definitions pass: #953 records that
    // hoisting every prototype unconditionally can put a struct-tag
    // parameter type in function-prototype scope ahead of the #include that
    // actually defines it, conflicting with the tag's real, later
    // definition -- the same reasoning #956 used for generated-function
    // forward declarations. Deduped so a vtable naming the same function
    // twice (or two vtables sharing one) doesn't declare it twice.
    {
        ObjVec reloc_fns = {0};
        for (Obj *obj = prog; obj; obj = obj->next) {
            if (generated_only && !obj->is_macro_generated)
                continue;
            if (obj->is_function || obj->name[0] == '.')
                continue;
            for (Relocation *rel = obj->rel; rel; rel = rel->next) {
                if (!rel->label || !*rel->label)
                    continue;
                Obj *target = serialize_find_global(vm, *rel->label);
                if (!target || !target->is_function ||
                    target == vm->compiler.builtin_block_copy ||
                    obj_vec_contains(&reloc_fns, target))
                    continue;
                // #1151: a header-supplied declaration (either a `static`
                // definition re-derived from its own replayed #include, or
                // a bodiless extern like libc's own `strlen`/`strcmp`)
                // already has a prototype in scope from that replay --
                // emitting a second one here, spelled in CCCC's own
                // bundled-header types, conflicts with the real one
                // ("conflicting types for 'strlen'"). Deliberately does NOT
                // skip an is_implicit or is_macro_generated target -- unlike
                // the header-supplied case, dropping either of those here
                // would leave the reloc'd initializer with nothing in scope
                // at all, the exact undeclared-identifier bug #999 this loop
                // exists to prevent.
                if (function_is_header_supplied(vm, &ctx, target) ||
                    (!target->is_definition && !target->body &&
                     !target->is_implicit && !target->is_macro_generated &&
                     !bodyless_decl_from_input_or_bundled(vm, &ctx, target)))
                    continue;
                obj_vec_push(&reloc_fns, target);
                serialize_function_signature(f, &ctx, target, true);
                fprintf(f, ";\n");
            }
        }
        free(reloc_fns.data);
    }

    // #1098: file-scope _Static_assert/static_assert records. Placed here
    // -- after tag forward decls, type definitions, and the #include
    // replay above, so an asserted type's own definition (or its replayed
    // #include) is already visible; before global-variable definitions,
    // which is the only real ordering constraint (no global depends on a
    // preceding assert or vice versa). Every record here came from a
    // hand-written source declaration (never macro-generated -- there is
    // no synthesis path that produces one), so -c=generated (which only
    // re-emits macro-generated additions layered onto the original
    // source) skips this pass entirely, the same reasoning the
    // global-variable loop just below applies per-Obj via is_macro_generated.
    if (!generated_only)
        for (StaticAssertRecord *sa = vm->compiler.static_asserts; sa;
             sa                     = sa->next)
            serialize_static_assert(f, vm, &ctx, sa->cond, sa->msg, sa->msg_len,
                                    sa->tok, 0);

    // Serialize global variables
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (!obj->is_function)
            serialize_global_var(f, vm, &ctx, obj);
    }

    // Serialize function prototypes before bodies so generated C is valid when
    // a function is called before its definition appears in the Obj list.
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (!obj->is_function)
            continue;
        // #965: __cccc_block_copy_impl is a VM-only FFI shim (its real
        // implementation is host-side, src/stdlib/stdlib.c) -- it has no
        // obj->tok (ty->name was never set for this builtin prototype, see
        // its registration in parse.c), so the from_primary check just
        // below already leaves it unemitted here in practice. Skip it
        // explicitly regardless, so a native replacement is only ever
        // supplied by serialize_block_preamble's own static definition
        // (emitted when Block_copy is actually reachable) and this loop
        // can never introduce a second, conflicting extern declaration.
        if (obj == vm->compiler.builtin_block_copy)
            continue;
        // #999: a header-sourced `static` definition is already supplied
        // by that header's own replayed #include text -- see
        // function_is_header_supplied()'s comment. This is the "has a
        // body" counterpart to the from_primary check the bodyless branch
        // just below already applies.
        if (function_is_header_supplied(vm, &ctx, obj))
            continue;
        if (!obj->is_definition && !obj->body) {
            // #901: a bare declaration with no body (e.g. `int abs(int
            // x);`) used to be dropped entirely here. The VM path needs
            // no native declaration -- it resolves the call as an FFI
            // symbol with a known signature -- but the downstream system
            // compiler does, so silently omitting it produced an
            // undeclared-function error in the generated C. Emit it when
            // it was written in a command-line input file (or in a cccc-only-
            // routed include, whose own #include is never re-emitted --
            // #896); a header-sourced declaration is left out, since the
            // auto-captured #include (see TypeNameRecord.from_include)
            // already supplies it to the native compiler. An implicit
            // declaration's guessed signature is skipped outright -- it
            // could conflict with the real one from a re-emitted header.
            if (obj->is_implicit)
                continue;
            // #956: a FunctionPrototype()+PublishNode() generated function
            // has no obj->tok (it was synthesized, not parsed from any
            // file), so the from_primary check below would always drop
            // it -- treat every macro-generated prototype as eligible
            // regardless of origin, matching the emit-event path's
            // unconditional hoist above.
            // #1002 (investigation): file_is_command_line_input(), not a
            // primary_file-only comparison -- see that function's comment.
            // #1096: a declaration sourced from one of CCCC's own bundled
            // headers (e.g. bundled fcntl.h's own `#include "unistd.h"`
            // declaring close()) is NOT supplied by the auto-captured
            // #include the way a real host header's transitively-reached
            // declaration is -- the replayed `#include <fcntl.h>` resolves
            // to the *host's* fcntl.h under -c=native, which may not
            // declare it (the real bug this whole branch is scoped to
            // catch: see is_compiler_owned_header's own scope note and
            // test_sys_mount_statfs.c). Only applies when that bundled
            // header's own #include was never replayed (path_is_captured())
            // -- when it was replayed, the usual from_include suppression is
            // correct and this declaration really is already supplied by
            // that replay. Also gated on obj->is_used: a bundled header like
            // unistd.h declares dozens of functions the primary file never
            // references -- emitting every one of them (rather than just
            // the handful the program actually calls) would needlessly
            // bloat the output and risk a real conflict for some
            // declaration whose signature the host's own header spells
            // slightly differently. is_used is set by the parser on any
            // identifier lookup (see its own doc comment on Obj), which is
            // exactly "does this TU actually reference it". #1151: both
            // conditions factored into bodyless_decl_from_input_or_bundled()
            // (this file) so the #999 reloc-forward-declare loop above can
            // ask the identical question for a function only referenced by
            // address.
            if (!obj->is_macro_generated &&
                !bodyless_decl_from_input_or_bundled(vm, &ctx, obj))
                continue;
        }
        serialize_function_signature(f, &ctx, obj, true);
        fprintf(f, ";\n\n");
    }

    // #1033: after every guest function's own prototype (just above) so the
    // harness's per-test wrapper functions -- which call test symbols
    // directly by name -- always see a prototype in scope, regardless of
    // where in `prog` the real definition sits.
    if (!generated_only && emit_test_harness)
        serialize_test_harness(f, vm, prog);

    // Serialize functions
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (generated_only && !obj->is_macro_generated)
            continue;
        if (obj->is_function && !function_is_header_supplied(vm, &ctx, obj))
            serialize_function(f, vm, &ctx, obj);
    }

    free(ctx.seen.data);
    free(ctx.defs.data);
    free(ctx.tags);
    free(ctx.typedefs);
    free(ctx.captured_paths);
    free(ctx.block_envs);
    free_nested_envs(&ctx);
    free(ctx.hoisted.data);
    free(ctx.emitted_defs.data);
    free(ctx.enum_renames); // #1016: was missing from this list
}
