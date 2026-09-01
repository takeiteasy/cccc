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

// Serialization: declarations -- va_list shim detection, function
// signatures and bodies, global variables and their initializer
// bytes/relocations, alignas (#1150).
#include "./serialize_internal.h"

// Forward declaration
// #1124: serialize_expr's actual switch-on-node->kind body, renamed so the
// public serialize_expr can wrap it with the _BitInt width-mask check below
// without recursing into itself.
// #1062/#1085: matches CCCC's own struct va_list structurally (defined
// further down, near its own long comment) -- forward-declared here so
// ND_FUNCALL (inside serialize_expr's own switch, above the definition
// textually) can call it too.
// #1074/#1080: does `var` belong to `fn`'s own locals list? Defined near
// serialize_nested_preamble() (with the rest of the nested-function-upvar
// machinery); forward-declared here so ND_BLOCK_LITERAL's capture-copy
// (serialize_expr, above that machinery in file order) can reuse it.
// #1136: defined near serialize_global_var (with the rest of the
// declaration-printing machinery); forward-declared here so the hoisted-local
// declarator (serialize_function, above that machinery in file order) can
// reuse it too.
// #1103: defined near the other include-provenance predicates (with
// global_is_header_supplied); forward-declared here so
// rename_colliding_static_names() (above that machinery in file order) can
// share the exact same "does this definition actually reach the output"
// test the emitter itself uses, instead of a hand-rolled paraphrase that can
// drift out of sync with it.
// #964: mutually recursive with serialize_stmt -- see the comment on its
// definition, near ND_BLOCK below.

// #1062: does `ty` name CCCC's own struct va_list (include/stdarg.h --
// reg_ptr/stack_ptr/reg_count/__reserved)? Matched structurally (member
// names/kinds), not by typedef spelling -- a user's own `typedef va_list
// mylist;` still forwards through this the same way a bare `va_list`
// parameter does, where a name-based match would silently miss it.
static bool member_named(Member *m, const char *name) {
    if (!m || !m->name)
        return false;
    size_t len = strlen(name);
    return m->name->len == (int)len && strncmp(m->name->loc, name, len) == 0;
}

bool type_is_cccc_va_list(Type *ty) {
    if (!ty || ty->kind != TY_STRUCT || !ty->members)
        return false;
    Member *m = ty->members;
    if (!member_named(m, "reg_ptr") || !m->ty || m->ty->kind != TY_PTR)
        return false;
    m = m->next;
    if (!m || !member_named(m, "stack_ptr") || !m->ty || m->ty->kind != TY_PTR)
        return false;
    m = m->next;
    if (!m || !member_named(m, "reg_count") || !m->ty || m->ty->kind != TY_INT)
        return false;
    m = m->next;
    if (!m || !member_named(m, "__reserved") || !m->ty ||
        m->ty->kind != TY_ARRAY)
        return false;
    return m->next == NULL;
}

// #1062: CCCC's own va_list (a plain struct, always genuinely by-value
// since #1078) is forwarded verbatim as a function parameter's *type* under
// -c=native -- the type name resolves correctly (the user's own `#include
// <stdarg.h>` is replayed and picks up the real host header), but the real
// host's own va_list has different by-value semantics depending on host:
// macOS's is a bare `char *` (an ordinary scalar, genuinely by-value,
// matching the VM), while glibc's is `typedef struct __va_list_tag
// va_list[1]` -- an array type, which decays to a pointer in parameter
// position (C17 6.7.6.3p7), aliasing the caller's own va_list. A callee
// that does `va_arg(ap, T)` on such a parameter silently advances the
// *caller's* va_list on glibc, never on macOS or on the VM -- the same
// program gives two different answers depending on backend, no build
// failure, no diagnostic.
//
// Fixed with a callee-side va_copy shim rather than either alternative
// considered: (a) making CCCC's own va_list an array-of-one-struct on
// Linux specifically, to alias like glibc's -- a real, platform-divergent
// change to the VM's own variadic ABI for no benefit, since the VM's
// current by-value semantics are the parity target (this batch's own
// scope: native must match the VM, not match a native gcc build of the
// same source); or (b) diagnosing/rejecting a va_list parameter under
// -c=native on Linux -- the batch's own policy reserves rejection for "no
// cheap translation available", which isn't true here.
//
// The shim changes only the emitted parameter's *name*, never the
// function's type, so address-taken calls and calls through function
// pointers are unaffected (a pointer-parameter rewrite would need
// call-site changes and would break those). It's applied uniformly on
// every host (no #ifdef __linux__): `va_copy` of a scalar `char *` on
// Darwin is a trivial, harmless copy.
//
// Deliberately does NOT touch the parameter's own Obj (no rename, no new
// state) -- only the *printed* name in the signature differs from what the
// body's own ND_VAR references print (both read the same Obj->name). Only
// applied when fn->body is set: a bodiless prototype has no body to inject
// the shim into, and C doesn't require declaration/definition parameter
// names to match anyway, so a plain `va_list ap` prototype (unmodified) and
// a shimmed `va_list __cccc_va_param_ap` definition for the same function
// are both legal, compatible declarations of the same function type.
//
// Residual this shim alone doesn't close, fixed separately by #1085: host
// libc `v*`-family consumers (vprintf/vsnprintf/vfprintf/vsscanf/vsyslog/
// ...) are the *host's own* functions, with no callee prologue of ours to
// inject this shim into. See the ND_FUNCALL case above (the
// has_va_list_arg / va_fwd_names block) for the call-site fix: any call
// passing a va_list-typed argument gets its own va_copy'd statement
// expression there instead, which covers this case too (and, since it's
// not narrowed to bodiless callees, indirect calls through a function
// pointer as well). See man/STDLIB.md's <stdarg.h> row for the full
// writeup.
static const char *va_list_shim_param_name(char *buf, size_t bufsz,
                                           const char *orig) {
    snprintf(buf, bufsz, "__cccc_va_param_%s", orig ? orig : "ap");
    return buf;
}

// #897 (RESOLVED): a struct/union-by-value parameter's type used to be
// mis-serialized here as "struct <param-name>" instead of its real tag --
// e.g. `int helper(struct Point q)` emitted "struct q" in the generated
// native C, which clang then rejected as an incomplete/undeclared type. The
// serializer was innocent: the real bug was in struct_union_decl/
// struct_decl/union_decl (src/parse.c) re-running install_tag_definition()
// on a bare *reference* to an already-defined tag, which re-registered the
// shared Type under whatever name declarator() had most recently written to
// ty->name (a previous parameter's own identifier) -- fixed by reporting
// whether an actual `{ ... }` body was parsed and installing via
// ty->struct_tag (immune to the overwrite) instead of ty->name.
void serialize_function_signature(FILE *f, SerializeContext *ctx, Obj *fn,
                                  bool with_asm_label) {
    // #1025/#1039: an asm("symbol")-labeled block-scope declaration (`Put
    // local_puts asm("puts");`) aliases an *external* symbol -- internal
    // linkage on the declaration is meaningless for it and, since the
    // symbol is never defined under the local name, actively wrong (the
    // native compiler emits an internal-linkage reference nothing ever
    // defines, and the link fails). Originally worked around here by
    // suppressing `static` whenever an asm label was present despite
    // fn->is_static being forced true regardless (#1025); parse_decl.c now
    // only forces is_static on a nested/block-scope function when no asm
    // label is present (#1039), so fn->is_static alone is accurate here.

    // #1020:
    // __attribute__((constructor[(priority)]))/((destructor[(priority)])) was
    // never lowered here at all -- under -c=native the function was emitted as
    // an ordinary function nothing calls, so it simply never ran. Emitted as a
    // *prefix* attribute (not appended after the declarator the way asm_label
    // is below): GCC rejects a trailing attribute on a function *definition*
    // while clang accepts it, so a suffix form would pass on macOS and fail to
    // compile on Linux.
    if (fn->is_constructor) {
        if (fn->init_priority == CCCC_NO_INIT_PRIORITY)
            fprintf(f, "__attribute__((constructor)) ");
        else
            fprintf(f, "__attribute__((constructor(%d))) ", fn->init_priority);
    }
    if (fn->is_destructor) {
        if (fn->init_priority == CCCC_NO_INIT_PRIORITY)
            fprintf(f, "__attribute__((destructor)) ");
        else
            fprintf(f, "__attribute__((destructor(%d))) ", fn->init_priority);
    }

    if (fn->is_static)
        fprintf(f, "static ");

    // #1026: a function returning a function pointer (`int (*f(void))(int,
    // int)`) can't be spelled as "<return-type> <name>(<params>)" -- the
    // return type's own declarator has to wrap around the whole
    // "name(params)" unit, the same way TY_ARRAY/TY_PTR recurse in
    // serialize_type_decl. Render "name(params)[ asm("label")]" into a
    // buffer first, then hand it to serialize_type_decl as the declarator
    // name so a pointer/function return type nests correctly.
    char  *decl   = NULL;
    size_t declsz = 0;
    FILE  *df     = open_memstream(&decl, &declsz);
    fprintf(df, "%s(", fn->name);

    bool first = true;
    if (fn->params) {
        for (Obj *param = fn->params; param; param = param->next) {
            if (!first)
                fprintf(df, ", ");
            first = false;
            // #1062: only when this signature is for a body-having
            // definition (see va_list_shim_param_name's own comment) --
            // fn->body's own local-decl emission (serialize_function)
            // injects the matching `va_list <param->name>; va_copy(...)`
            // shim right after this signature is printed.
            if (fn->body && type_is_cccc_va_list(param->ty)) {
                char shimbuf[64];
                serialize_type_decl(df, ctx, param->ty,
                                    va_list_shim_param_name(
                                        shimbuf, sizeof shimbuf, param->name));
            } else {
                serialize_type_decl(df, ctx, param->ty, param->name);
            }
        }
    } else if (fn->ty) {
        // #901: a bodiless declaration (e.g. `int abs(int x);`) never runs
        // the body-parsing path that populates fn->params (the Obj-based
        // parameter list created for stack-slot allocation) -- only
        // fn->ty->params (the Type-based prototype list) exists. Fall back
        // to it so such a declaration serializes its real parameter types
        // instead of degrading to "()"/"(void)".
        int anon = 0;
        for (Type *param = fn->ty->params; param; param = param->next) {
            if (!first)
                fprintf(df, ", ");
            first = false;
            char buf[64];
            if (param->name) {
                int len = param->name->len;
                if (len > (int)sizeof(buf) - 1)
                    len = (int)sizeof(buf) - 1;
                memcpy(buf, param->name->loc, len);
                buf[len] = '\0';
            } else {
                snprintf(buf, sizeof buf, "__a%d", anon++);
            }
            serialize_type_decl(df, ctx, param, buf);
        }
    }

    if (fn->ty && fn->ty->is_variadic && !first) {
        fprintf(df, ", ...");
    } else if (first) {
        fprintf(df, "void");
    }
    fprintf(df, ")");

    if (fn->asm_label && with_asm_label)
        // __CCCC_ASM_PREFIX__ (see serialize_asm_prefix_preamble) supplies
        // the platform's real symbol prefix; adjacent string literals
        // concatenate at translation time, so this reads as e.g.
        // __asm__("_puts") on Darwin and __asm__("puts") on Linux from one
        // platform-independent emission. #1130: __asm__, not bare asm --
        // asm is a GNU alternate keyword GCC disables under a strict ISO
        // -std=cNN.
        //
        // with_asm_label is false only for the signature immediately
        // followed by a function BODY (serialize_function, just below):
        // GCC/clang both reject an asm-label attached directly to a
        // definition ("expected ';' after top level declarator") -- it is
        // only valid on a standalone declaration. The unconditional
        // "prototypes before bodies" pass (serialize_program.c) already
        // emits a labeled, bodyless prototype for every function ahead of
        // its definition, which is what actually binds the real symbol
        // name; repeating the label here would be both redundant and a
        // syntax error. Pre-existing bug independent of the __asm__
        // spelling above -- reproduced identically with the old bare
        // `asm(...)` on both clang and a real gcc-16, found while adding
        // #1130 test coverage for a self-defined (not merely aliased)
        // asm-labeled function.
        fprintf(df, " __asm__(__CCCC_ASM_PREFIX__ \"%s\")", fn->asm_label);

    fclose(df);
    serialize_type_decl(
        f, ctx, (fn->ty && fn->ty->return_ty) ? fn->ty->return_ty : ty_int,
        decl ? decl : "");
    free(decl);
}

// #1044: forward declaration -- serialize_function() (immediately below)
// needs to print a deferred static's own initializer inside the owning
// function's body, but serialize_init_bytes() isn't defined until further
// down this file (it recurses through serialize_reloc_init(), which itself
// needs find_label_owner() just below). Mirrors the existing forward
// declaration ahead of serialize_reloc_init()'s own definition.
static void serialize_init_bytes(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 int offset);

// #1044: look up a label by the same pointer-identity key a Relocation's
// own label field uses (a per-VM char** identity, see #1044's own comment
// on serialize_reloc_init() below) -- kept here since the forward
// declaration above is otherwise unused code motion; unrelated to it.
// `*rel->label` carries (see LabelOwner's own comment). NULL if `name`
// doesn't match any label collect_deferred_static_labels() found -- the
// ordinary case for every relocation target that resolves to a real Obj.
const LabelOwner *find_label_owner(SerializeContext *ctx, const char *name) {
    for (int i = 0; i < ctx->label_owners_len; i++)
        if (ctx->label_owners[i].unique_label == name)
            return &ctx->label_owners[i];
    return NULL;
}

// #1044: true iff `var` was deferred into its owning function's own body by
// collect_deferred_static_labels() -- serialize_global_var() and the #918
// forward-declaration passes (both above and below this point in the file)
// must all skip it, since it has no file-scope declaration to forward-
// declare or definition to emit at all.
bool var_is_deferred_label_static(SerializeContext *ctx, Obj *var) {
    for (int i = 0; i < ctx->deferred_label_statics_len; i++)
        if (ctx->deferred_label_statics[i].var == var)
            return true;
    return false;
}

// #1102: does this type, or anything reachable down its aggregate spine
// (array -> array -> ... -> element), carry a qualifier? A multi-dimensional
// `const int a[2][3]` spells const on the innermost element, arbitrarily
// far down.
static bool aggregate_spine_is_qualified(Type *ty) {
    while (ty) {
        if (ty->is_const)
            return true;
        if (ty->kind != TY_ARRAY && ty->kind != TY_VLA && ty->kind != TY_VECTOR)
            return false;
        ty = ty->base;
    }
    return false;
}

// #1102: a hoisted local's declarator must not carry any qualifier the
// split declaration/assignment scheme can't honour. #1029 already strips a
// scalar's top-level `const` (the declaration is hoisted, the initializer
// became an assignment in the body below). Aggregate locals need one more
// step: for `const int arr[3]` (or a const-element VLA/vector) C spells the
// qualifier on the *element* type, so is_const lives on ty->base -- one
// level further down than #1029's arm looked -- and the per-element
// initializer assignments (`*(const int *)((char *)arr + i) = v`) would
// still store into a genuinely-qualified object, which every host compiler
// rejects ("read-only variable is not assignable"). Copy the type chain,
// clearing is_const on the aggregate itself and recursively down its array/
// vector bases only; a pointer local's pointee qualifier (`const char *p`)
// is deliberately left alone, same as #1029. Returns ty untouched when
// there is nothing to strip.
static Type *hoist_mutable_type(VirtualMachine *vm, Type *ty) {
    if (!ty)
        return ty;
    bool agg =
        ty->kind == TY_ARRAY || ty->kind == TY_VLA || ty->kind == TY_VECTOR;
    if (!aggregate_spine_is_qualified(ty))
        return ty;
    Type *cpy     = copy_type(vm, ty);
    cpy->is_const = false;
    cpy->origin   = NULL;
    if (agg)
        cpy->base = hoist_mutable_type(vm, cpy->base);
    return cpy;
}

// Serialize a function
void serialize_function(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                        Obj *fn) {
    if (!fn->is_function)
        return;

    // Skip pragma macro functions (they were consumed)
    // Skip non-definitions
    if (!fn->is_definition && !fn->body)
        return;

    // #1130: suppress the asm-label on a body-having signature -- see
    // serialize_function_signature's own comment. The "prototypes before
    // bodies" pass (serialize_program.c) already emitted a labeled,
    // bodyless declaration for this function ahead of here.
    serialize_function_signature(f, ctx, fn, /*with_asm_label=*/!fn->body);

    if (fn->body) {
        // #1253: rename duplicate labels a hygienic Quote() template left in
        // this body so the emitted C has no repeated label in one function.
        serialize_dedupe_function_labels(vm, fn->body);

        fprintf(f, " {\n");
        Obj *saved_fn   = ctx->current_fn;
        ctx->current_fn = fn;

        // Function-local typedefs/tags are emitted at the top of the function,
        // matching the serializer's existing local declaration hoisting.
        serialize_type_defs_for_owner(f, ctx, fn);

        // #1062: for each va_list parameter, pair the shim-named parameter
        // serialize_function_signature() just printed (see its own comment
        // and va_list_shim_param_name()) with a genuinely independent local
        // under the *original* parameter name, initialized via va_copy.
        // Every ND_VAR reference inside the body still reads Obj->name
        // (unchanged, still the original name) and now resolves to this
        // local instead of the parameter -- restoring the VM's own
        // by-value va_list semantics under -c=native on every host,
        // including glibc (whose real va_list is an array type that would
        // otherwise decay to a pointer in parameter position and alias the
        // caller's va_list, C17 6.7.6.3p7). va_end is deliberately not
        // called on the shim copy: verified directly (both the macOS SDK
        // and, via the cccc-linux-amd64/cccc-linux-arm64 containers, real
        // glibc) that va_end expands to nothing observable for either
        // va_list representation (a no-op on Darwin's, a no-op on glibc's
        // struct-tag array), so skipping it costs nothing and avoids
        // needing to track an extra cleanup point for a function that may
        // return from multiple places.
        for (Obj *param = fn->params; param; param = param->next) {
            if (!type_is_cccc_va_list(param->ty))
                continue;
            char shimbuf[64];
            va_list_shim_param_name(shimbuf, sizeof shimbuf, param->name);
            fprintf(f, "    va_list %s; va_copy(%s, %s);\n", param->name,
                    param->name, shimbuf);
        }

        // Local variable declarations
        //
        // Hoisting every local to one flat top-of-function list assumes C
        // block scoping never needs to distinguish two locals with the same
        // name -- false when a name is reused in sibling (or nested) blocks,
        // e.g. two `for (int i = ...)` loops in the same function each
        // declaring their own `int i`. Renaming on collision (#926) below
        // avoids two declarations of the same identifier in the same
        // (flattened) scope; params occupy an identifier too (they are on
        // fn->locals with is_param set, just not declared here) so they
        // seed the collision check.
        for (Obj *var = fn->locals; var; var = var->next) {
            // Params are never renamed here -- serialize_function_signature
            // already printed the function's signature (with each param's
            // current name) before this loop runs, so renaming a param's
            // Obj this late would desync the signature from the body. C's
            // own rules already guarantee distinct params never collide
            // with each other; only a non-param can be renamed to resolve
            // a collision against a param or another non-param.
            if (var->is_param)
                continue;

            // #965: __static_link (block_literal(), parse.c) is spliced
            // into fn->params but never marked is_param -- that flag is
            // only ever set by assign_lvar_offsets (codegen.c), which
            // -m/-c=native never run. Match by list membership instead of
            // trusting the flag, so it isn't re-declared here as an
            // ordinary local (it's already a parameter, printed by
            // serialize_function_signature). codegen.c's own
            // assign_lvar_offsets (:8622-8633) does this exact membership
            // scan for the same reason.
            bool is_actual_param = false;
            for (Obj *p = fn->params; p; p = p->next)
                if (p == var) {
                    is_actual_param = true;
                    break;
                }
            if (is_actual_param)
                continue;

            // #18/#19 buffalo: a caller-side struct-return slot (funcall(),
            // parse_postfix.c) is a VM-codegen-only concept -- RETBUF/VSTR
            // read it purely as a frame offset, and no ND_VAR node in the
            // serialized body ever names it (see serialize_expr.c's
            // ND_FUNCALL case, which lowers the call to plain C and never
            // mentions ret_buffer). Declaring it here just to leave it
            // unused triggers -Wunused-variable in every generated wrapper
            // around a struct-returning call. Skip before the __cccc_tmp%d
            // naming below so the counter -- and every other temp's name --
            // is unaffected by whether a given call happens to return a
            // struct.
            if (var->is_ret_buffer)
                continue;

            if (var->name[0] == '\0')
                // Compiler-synthesized temporaries (e.g. from ++/--/op=
                // desugaring) have an empty name; give them one so they can
                // be declared and referenced as valid C identifiers.
                var->name =
                    arena_format(vm, "__cccc_tmp%d", ctx->anon_local_counter++);
            else if (var->name[0] == '.')
                // #1034: a local named via new_unique_name() (parse_core.c)
                // -- a macro/comptime-generated compound literal or block
                // temp given the same ".L..N" dotted scheme as an anonymous
                // *global* (rename_anon_globals(), further down this file)
                // -- is not a legal C identifier either, and unlike the
                // empty-name case above was never renamed here. Deliberately
                // a distinct "__cccc_local_" prefix, not
                // rename_anon_globals()'s own "__cccc_%s_%d" scheme (which
                // draws from a *different* counter, anon_global_counter) --
                // reusing that scheme here would let a renamed global and a
                // renamed local collide on the identical spelling (e.g. both
                // landing on
                // "__cccc_anon_0", one per counter) and silently shadow each
                // other in this function's scope. Same display_name-or-
                // "anon" tag rule rename_anon_globals() uses, but sharing
                // anon_local_counter with the __cccc_tmp%d case above (whose
                // own prefix keeps it out of this collision class too).
                var->name = arena_format(
                    vm, "__cccc_local_%s_%d",
                    (var->display_name && var->display_name[0] != '.')
                        ? var->display_name
                        : "anon",
                    ctx->anon_local_counter++);

            // #926: rename on collision against every *other* local/param
            // in the function -- not just those before it in the raw list,
            // since fn->locals is in reverse declaration order and a param
            // can sit after the body local shadowing it. Comparing against
            // the whole list (not only already-finalized entries) is still
            // sound: a later non-param entry that shares var's pre-rename
            // name simply detects the collision itself, against var's new
            // name, when its own turn comes. Linear scan per local (O(n^2)
            // in locals), matching this file's existing style; move to a
            // hashmap if a function with enough locals to matter shows up.
            bool renamed_again;
            do {
                renamed_again = false;
                for (Obj *other = fn->locals; other; other = other->next) {
                    if (other == var || strcmp(other->name, var->name) != 0)
                        continue;
                    var->name     = arena_format(vm, "%s__cccc_%d", var->name,
                                                 ctx->anon_local_counter++);
                    renamed_again = true;
                    break;
                }
            } while (renamed_again);

            // #964: a VLA's declaration can't be hoisted here -- its length
            // expression reads a variable (`int n=4; int v[n];`) that must
            // already be in scope at the point of the flattened declaration,
            // and the hoist loop runs before any of the function body has
            // been emitted. It keeps its slot in the collision-renaming
            // above (so a same-named non-VLA local elsewhere still detects
            // the collision), but the declaration itself is emitted in
            // place by the ND_EXPR_STMT case in serialize_stmt() that
            // recognizes its `ND_VLA_PTR = alloca(...)` initializer.
            if (var->ty->kind == TY_VLA)
                continue;

            // #973 follow-up: same reasoning, extended to a pointer-to-VLA
            // local (`int (*p)[n] = &v;`) -- its declarator also reads a
            // runtime variable. Only skip when we know there's an
            // initializer to anchor the in-place declaration to (see the
            // ND_EXPR_STMT case below, and Obj.deferred_vla_ptr_init in
            // cccc.h); a pointer-to-VLA local declared with no initializer
            // falls through to the normal hoist below, which re-emits a
            // declarator referencing a not-yet-declared variable and fails
            // to compile -- a pre-existing gap this fix doesn't widen,
            // tracked separately rather than fixed here.
            if (var->deferred_vla_ptr_init)
                continue;

            // #965: a block literal's descriptor local (Node.block_desc_var)
            // is typed `long[N]` at parse time only so it gets frame space --
            // its real C type is the paired block function's env struct
            // (serialize_block_preamble), which doesn't exist as a Type* and
            // so can't go through serialize_type_decl. Emit its declaration
            // directly instead.
            if (var->block_desc_of) {
                const char *env = find_block_env(ctx, var->block_desc_of);
                print_indent_level(f, 1);
                fprintf(f, "%s %s;\n", env ? env : "struct __cccc_block_env_?",
                        var->name);
                continue;
            }

            // #965: a __block local's stack slot holds a heap box pointer at
            // runtime (codegen.c's ALCB prologue) -- declare it as a pointer
            // and malloc it here, matching that prologue's per-function
            // allocation. Every ordinary read/write of it is rewritten to
            // `(*name)` by serialize_expr's ND_VAR case (and ND_MEMZERO's own
            // is_block_var arm). Never freed, matching the VM's own
            // never-reclaimed ALLOC_KIND_BLOCK_BOX.
            if (var->is_block_var) {
                print_indent_level(f, 1);
                serialize_type_decl(f, ctx, pointer_to(vm, var->ty), var->name);
                fprintf(f, ";\n");
                print_indent_level(f, 1);
                fprintf(f, "%s = __builtin_malloc(sizeof(*%s));\n", var->name,
                        var->name);
                continue;
            }

            print_indent_level(f, 1);
            // #1029: serialize_function hoists every local to a flat
            // declaration here, with any initializer lowered to a separate
            // assignment statement in the body below (const-qualified or
            // not -- the split itself is unconditional). A `const`-typed
            // local (`const long long max_spins = 2000000;`) would
            // therefore emit as `const long long max_spins;` here and
            // `max_spins = 2000000;` in the body -- an assignment to a
            // const object, which real C rejects outright even though the
            // VM (which never actually re-derives or enforces this split)
            // has no problem with the original, un-hoisted source. Strip
            // only the *top-level* const on the hoisted declarator; a
            // pointer-level const on the pointee (`const char *p`) lives on
            // the base type, one step down `var->ty->base`, and is
            // untouched by this.
            // #1095: a hoisted local has no byte-image initializer here --
            // any initializer was already split into a separate assignment
            // statement in the body (see #1029's own comment just above)
            // -- so re-materializing a host-owned sizeof/_Alignof array
            // dimension can't disagree with anything else emitted for this
            // object. See SerializeContext.allow_layout_dims's own comment.
            ctx->allow_layout_dims = true;
            // #1136: see serialize_alignas_if_needed's own comment.
            serialize_alignas_if_needed(f, var);
            // #1029: strip the top-level const on the hoisted declarator;
            // #1102: and any qualifier spelled on an aggregate's *element*
            // type (`const int a[3]`, arbitrarily deep for multi-dimensional
            // arrays) -- see hoist_mutable_type(), which returns the type
            // untouched when there is nothing to strip. A pointer-level
            // const on a pointee (`const char *p`) is untouched by both.
            // #1145: serialize_local_var_type_decl, not the plain
            // serialize_type_decl every other declarator site in this file
            // uses -- see its own comment for why the alias-preserving
            // check it adds is confined to exactly this one call site.
            serialize_local_var_type_decl(
                f, ctx, hoist_mutable_type(vm, var->ty), var->name);
            ctx->allow_layout_dims = false;
            fprintf(f, ";\n");
        }

        // #1044: a static (or compound-literal) global deferred here by
        // collect_deferred_static_labels() because its initializer takes
        // the address of one of `fn`'s own labels -- print its real
        // definition now, inside the one function whose labels it's legal
        // to reference. Ordinary anonymous-global naming already gave it a
        // legal C identifier (rename_anon_globals()); every reference to it
        // elsewhere in this function's body already reads that name via its
        // Obj pointer (ND_VAR), unaffected by where the declaration itself
        // ends up.
        for (int __dl_i = 0; __dl_i < ctx->deferred_label_statics_len;
             __dl_i++) {
            DeferredStaticLabel *__dl = &ctx->deferred_label_statics[__dl_i];
            if (__dl->owner_fn != fn)
                continue;
            print_indent_level(f, 1);
            fprintf(f, "static ");
            serialize_type_decl(f, ctx, __dl->var->ty, __dl->var->name);
            if (__dl->var->init_data) {
                fprintf(f, " = ");
                serialize_init_bytes(f, vm, ctx, __dl->var, __dl->var->ty, 0);
            }
            fprintf(f, ";\n");
        }

        // #1074: if `fn` directly parents at least one nested function,
        // declare and initialize its env struct instance here -- after
        // every ordinary local above so `&x` for an upvar field is always
        // already-declared storage, and before the body so a call to a
        // direct nested child (which reads `&__cccc_nenv`, see ND_FUNCALL's
        // own #1074 comment) always finds it initialized first. `__up`
        // carries `fn`'s own static link along for a deeper nest level to
        // chase; a non-nested `fn` (there's nothing to chase further) still
        // needs the field to exist so `struct __cccc_nenv_X`'s layout is
        // fixed regardless of which level owns it, but its value there is
        // never read.
        for (int __ne_i = 0; __ne_i < ctx->nested_envs_len; __ne_i++) {
            NestedEnvEntry *__ne = &ctx->nested_envs[__ne_i];
            if (__ne->owner_fn != fn)
                continue;
            fprintf(f, "    %s __cccc_nenv;\n", __ne->env_struct_name);
            fprintf(f, "    __cccc_nenv.__up = %s;\n",
                    fn->is_nested ? "__static_link" : "(void *)0");
            for (int __uv_i = 0; __uv_i < __ne->upvars_len; __uv_i++) {
                // #1209: a VLA (or pointer-to-VLA) upvar has no valid
                // address yet at this point -- its own declaration hasn't
                // run -- so its field is left uninitialized here and
                // assigned instead at its in-place declaration site
                // (serialize_stmt.c), once `&var` is finally valid. See
                // nested_upvar_is_deferred()'s own comment.
                if (nested_upvar_is_deferred(__ne->upvars[__uv_i]))
                    continue;
                fprintf(f, "    __cccc_nenv.__uv%d = &%s;\n", __uv_i,
                        __ne->upvars[__uv_i]->name);
            }
            break;
        }

        // Function body — unpack a single ND_BLOCK to avoid double-brace
        // wrapping. Both the parser and FunctionSetBody store the body as an
        // ND_BLOCK node.
        Node *body_stmts;
        if (fn->body && fn->body->kind == ND_BLOCK && !fn->body->next)
            body_stmts = fn->body->body;
        else
            body_stmts = fn->body;
        for (Node *s = body_stmts; s; s = s->next) {
            serialize_stmt_list_item(f, vm, ctx, s, 1);
        }

        fprintf(f, "}\n\n");
        ctx->current_fn = saved_fn;
    } else {
        fprintf(f, ";\n\n");
    }
}

// #918: resolve a Relocation's target symbol name to its Obj. Mirrors
// codegen.c's find_global_obj (static there, not visible from this file) --
// vm->compiler.globals is the full accumulated global+function list
// (bytecode.c sets it once parsing completes), and rel->label points at the
// target Obj's ->name field directly (see eval2()/eval_rval() in parse.c),
// so a plain name match is exact. &&label targets (a computed-goto label
// address stored in a static/global initializer) live in codegen.c's
// text-segment label map instead of as an Obj and are not resolved here --
// vanishingly rare in an initializer and not worth threading codegen state
// into the serializer for; falls through to the "unresolved relocation"
// hard error below.
Obj *serialize_find_global(VirtualMachine *vm, const char *name) {
    for (Obj *g = vm->compiler.globals; g; g = g->next)
        if (strcmp(g->name, name) == 0)
            return g;
    return NULL;
}

// Find the Relocation (if any) covering byte `offset` within `var`'s
// init_data -- a pointer-sized initializer slot that names a symbol (`&x`,
// a string literal, a function pointer, ...) has its raw bytes zeroed by
// write_gvar_data() (parse.c) and the real target recorded here instead.
static Relocation *serialize_find_reloc(Obj *var, int offset) {
    for (Relocation *r = var->rel; r; r = r->next)
        if (r->offset == offset)
            return r;
    return NULL;
}

static void serialize_init_bytes(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 int offset);
// #1207: emits the comma-separated ".name = value" entries of a struct or
// union initializer with NO enclosing braces -- the caller (serialize_init_
// bytes' TY_STRUCT/TY_UNION cases) prints those. Factored out so an
// anonymous struct/union member (C11 6.7.2.1p13: its own fields are
// directly designatable in the enclosing initializer) can be flattened
// into the SAME brace level as its parent, instead of nested braces --
// which is also what makes a union whose largest member is itself
// anonymous serializable at all (see the TY_UNION arm below). `*first`
// tracks whether a leading ", " is needed, shared across a whole flattened
// chain so commas land correctly regardless of nesting depth.
static void serialize_agg_member_list(FILE *f, VirtualMachine *vm,
                                      SerializeContext *ctx, Obj *var, Type *ty,
                                      int offset, bool *first);

// A pointer-typed initializer slot backed by a Relocation (#918 defect C):
// previously the zeroed init_data bytes were printed verbatim as a null
// pointer -- silent miscompilation, valid C that runs wrong. `rel->label`
// names the target Obj by its ->name field.
static void serialize_reloc_init(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 Relocation *rel) {
    if (!rel->label || !*rel->label)
        error("cccc: invalid relocation in initializer for global '%s'",
              var->name);

    const char *target_name = *rel->label;
    Obj        *target      = serialize_find_global(vm, target_name);
    if (!target) {
        // #1044: no Obj was ever created for a label -- codegen.c's own
        // text-segment label map is the only thing that ordinarily
        // resolves a `&&label` relocation, and that state never reaches
        // this file (see this function's own comment, further up, on why).
        // find_label_owner() rebuilds just enough of that mapping (built by
        // collect_deferred_static_labels()) to spell the label's address
        // back out as real C: `var` must already be one of ctx->deferred_
        // label_statics (only those globals' relocations are checked
        // against the label table, and only when serialize_function() is
        // about to print `var`'s own definition inside its owning
        // function's body, where the label is legal to name).
        const LabelOwner *label = find_label_owner(ctx, target_name);
        // Only spell `&&label` when `var` itself is one of ctx->deferred_
        // label_statics -- i.e. this call is reached from serialize_
        // function(), printing `var`'s definition inside its owning
        // function's body, where the address is legal. `var` can still
        // reach this arm un-deferred (collect_deferred_static_labels()
        // declines a candidate another global's own relocation points at,
        // e.g. `static void **p = tab;` sitting next to `static void
        // *tab[] = {&&L};`) -- falling through to the diagnostic below
        // for that case is correct: emitting `&&L` at file scope here
        // would be invalid C the host compiler would (and, pre-#1044,
        // did) reject anyway, so failing loudly here keeps the fail-
        // loudly policy intact rather than deferring to the host's own,
        // less specific error.
        if (!label || !var_is_deferred_label_static(ctx, var))
            error("cccc: cannot serialize initializer for global '%s' in "
                  "native mode: unresolved relocation target '%s'",
                  var->name, target_name);
        fprintf(f, "(");
        serialize_type(f, ctx, ty);
        fprintf(f, ")((char *)&&%s + %lld)", label->label,
                (long long)rel->addend);
        return;
    }

    // Anonymous string-literal global -- serialize_global_var() never
    // emits these on their own (see is_string_literal skip below), so
    // inline the literal here instead of naming a symbol that doesn't
    // exist in the output.
    if (target->is_string_literal && target->init_data) {
        int  len            = (target->ty && target->ty->kind == TY_ARRAY)
                                  ? target->ty->array_len
                                  : (int)strlen(target->init_data);
        bool plain_char_ptr = ty->kind == TY_PTR && ty->base &&
                              ty->base->kind == TY_CHAR && rel->addend == 0;
        if (!plain_char_ptr) {
            fprintf(f, "(");
            serialize_type(f, ctx, ty);
            fprintf(f, ")((char *)");
        }
        serialize_string_n(f, target->init_data, len);
        if (!plain_char_ptr)
            fprintf(f, " + %lld)", (long long)rel->addend);
        return;
    }

    // #925: any other anonymous (`.L..N`) global -- a compound literal or
    // static local -- is renamed to a valid identifier and given a real
    // definition by rename_anon_globals() before serialization proceeds.
    // If one still has a dotted name here, it was reachable through this
    // Relocation but never renamed (not on the `prog` list the pre-pass
    // walks) -- fail loudly rather than emit a reference to a symbol that
    // was never defined (#918's fail-loudly policy).
    if (target->name[0] == '.')
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: relocation target '%s' was never assigned a valid name",
              var->name, target_name);

    fprintf(f, "(");
    serialize_type(f, ctx, ty);
    fprintf(f, ")((char *)&%s + %lld)", target->name, (long long)rel->addend);
}

// #1207: TY_STRUCT's member walk, factored out of serialize_init_bytes()
// so a TY_UNION's largest-member reconstruction (below) can share it -- a
// union whose largest member is itself a struct/union recurses back into
// this same function rather than serialize_init_bytes()'s own bracing.
static void serialize_agg_member_list(FILE *f, VirtualMachine *vm,
                                      SerializeContext *ctx, Obj *var, Type *ty,
                                      int offset, bool *first) {
    if (ty->kind == TY_UNION) {
        // #1115/#1207: an empty (0-byte) union has no members to designate
        // at all -- nothing to flatten, nothing to recurse into. The
        // caller's own enclosing braces (already printed) are enough;
        // matches serialize_init_bytes()'s own TY_UNION short-circuit for
        // an empty union reached directly (as an object, not as an
        // anonymous member). Must come before the `!largest` check below,
        // which is a genuine internal-inconsistency error, not this
        // ordinary case.
        if (ty->size == 0)
            return;
        // Reconstruct via the largest member (first on a tie). #1207: every
        // union member sits at offset 0, so no member's bytes can extend
        // past largest->ty->size -- gvar_initializer() (parse_init.c)
        // allocates init_data at exactly ty->size and memsets it to 0
        // before write_gvar_data() runs, so [largest->ty->size, ty->size)
        // is always zero, relocation-free alignment padding, which a host
        // compiler zero-fills for static storage the same way it already
        // does for ordinary struct padding -- no member needs to span the
        // whole object for this to be byte-exact. (The ticket's suggested
        // fix -- a synthetic full-width byte view -- buys nothing over
        // this: there is no non-zero byte a fuller view could recover that
        // this doesn't already know is zero.) The assertion below is
        // defensive, not a real refusal path -- it would only fire on an
        // internal inconsistency (a Relocation or non-zero byte where
        // write_gvar_data() should never have left one).
        Member *largest = NULL;
        for (Member *m = ty->members; m; m = m->next)
            if (!largest || m->ty->size > largest->ty->size)
                largest = m;
        if (!largest)
            error("cccc: cannot serialize initializer for global '%s' in "
                  "native mode: union has no members",
                  var->name);
        for (int i = offset + largest->ty->size; i < offset + ty->size; i++)
            if (var->init_data[i] != 0 || serialize_find_reloc(var, i))
                error("cccc: cannot serialize initializer for global '%s' "
                      "in native mode: union's padding past its largest "
                      "member is unexpectedly non-zero (internal "
                      "inconsistency)",
                      var->name);
        if (largest->name) {
            if (!*first)
                fprintf(f, ", ");
            *first = false;
            fprintf(f, ".%.*s = ", largest->name->len, largest->name->loc);
            serialize_init_bytes(f, vm, ctx, var, largest->ty, offset);
        } else {
            // #1207: the largest member is itself anonymous -- flatten
            // through its own fields (or, if it's a union too, its own
            // largest member) as transparent designators in THIS same
            // brace level (C11 6.7.2.1p13), rather than a nested union
            // dispatch this function's caller never sees.
            serialize_agg_member_list(f, vm, ctx, var, largest->ty, offset,
                                      first);
        }
        return;
    }

    // TY_STRUCT: every member is designated (unless anonymous, or an
    // unnamed bitfield -- pure padding, nothing to designate).
    for (Member *m = ty->members; m; m = m->next) {
        if (m->is_bitfield && !m->name)
            continue;
        if (!m->name && !m->is_bitfield) {
            // #1207: an anonymous struct/union member -- flatten its own
            // fields into this same brace level (C11 6.7.2.1p13) instead
            // of a nested `{ ... }`.
            serialize_agg_member_list(f, vm, ctx, var, m->ty,
                                      offset + m->offset, first);
            continue;
        }
        if (!*first)
            fprintf(f, ", ");
        *first = false;
        if (m->name)
            fprintf(f, ".%.*s = ", m->name->len, m->name->loc);
        if (m->is_bitfield) {
            // #1126 (RESOLVED): the old code read the storage unit with a
            // memcpy clamped to 8 bytes regardless of m->ty->size (16 for a
            // wide-_BitInt-typed bitfield, e.g. `_BitInt(128) f : 100;`),
            // so any bit at or above bit 64 of the field's value was
            // silently dropped. Extract byte-granularly instead over the
            // field's exact [bit_offset, bit_offset+bit_width) span --
            // mirrors __cccc_bitfield_extract (src/stdlib/wide_bitint.c,
            // #1125's runtime counterpart) and never over-reads the
            // object, so it's correct for any bit_offset/bit_width
            // combination, not just the wide case.
            if (m->bit_width > 128)
                error("cccc: cannot serialize initializer for "
                      "global '%s' in native mode: bitfield wider "
                      "than 128 bits",
                      var->name);
            int               start = m->offset * 8 + m->bit_offset;
            unsigned __int128 bits  = 0;
            for (int i = 0; i < m->bit_width; i++) {
                int b = start + i;
                if ((var->init_data[offset + b / 8] >> (b % 8)) & 1)
                    bits |= (unsigned __int128)1 << i;
            }
            bool     is_signed = !m->ty->is_unsigned && m->ty->kind != TY_BOOL;
            __int128 sbits     = (__int128)bits;
            if (is_signed && m->bit_width < 128 &&
                (bits & ((unsigned __int128)1 << (m->bit_width - 1))))
                sbits -= (__int128)1 << m->bit_width;
            if (m->bit_width < 64) {
                // Always in range for %lld/%lluu at this width -- no
                // most-negative-literal hazard (that only bites at
                // bit_width >= 64, handled by the hex arm below).
                if (is_signed)
                    fprintf(f, "%lld", (long long)sbits);
                else
                    fprintf(f, "%lluu", (unsigned long long)bits);
            } else {
                // bit_width >= 64: a sign-extended INT64_MIN printed as
                // %lld would be "-9223372036854775808", not a valid `long
                // long` constant in C -- and an unsigned value may not fit
                // 64 bits either. Use the same 128-bit hex literal shape as
                // the TY_BITINT scalar arm below.
                unsigned __int128 uv =
                    is_signed ? (unsigned __int128)sbits : bits;
                fprintf(f,
                        "((%s__int128)(((unsigned __int128)0x%llxULL "
                        "<< 64) | 0x%llxULL))",
                        is_signed ? "" : "unsigned ",
                        (unsigned long long)(uint64_t)(uv >> 64),
                        (unsigned long long)(uint64_t)uv);
            }
        } else {
            serialize_init_bytes(f, vm, ctx, var, m->ty, offset + m->offset);
        }
    }
}

// #1208: print one real-floating part of a _Complex initializer image.
// Byte-identical to what serialize_init_bytes's TY_COMPLEX arm used to emit
// for the (real-only) case -- float via format_float_literal + "f", double/
// long double via "%.17g" with no suffix (long double is stored as a plain
// 8-byte double everywhere in this compiler, see the TY_LDOUBLE arm).
static void serialize_complex_part(FILE *f, SerializeContext *ctx, Type *base,
                                   const char *p) {
    if (base && base->kind == TY_FLOAT) {
        float fv;
        memcpy(&fv, p, 4);
        if (!serialize_flonum_special(f, (long double)fv, "f")) {
            char buf[64];
            format_float_literal(buf, sizeof buf, (double)fv);
            fprintf(f, "%sf", buf);
        }
    } else {
        double dv;
        memcpy(&dv, p, 8);
        if (!serialize_flonum_special(f, (long double)dv, ""))
            fprintf(f, "%.17g", dv);
    }
}

// Reconstruct a global variable's initializer from its raw `init_data`
// bytes (plus any Relocations) as C source text, recursing through
// arrays/vectors/structs/unions. Replaces the old scalar-only dispatch that
// fell back to the placeholder comment `/* init data */` for every
// aggregate shape -- text a host compiler rejects outright (#918 defect B).
static void serialize_init_bytes(FILE *f, VirtualMachine *vm,
                                 SerializeContext *ctx, Obj *var, Type *ty,
                                 int offset) {
    if (!ty)
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: unknown type",
              var->name);

    if (ty->kind == TY_PTR || ty->kind == TY_NULLPTR_T) {
        Relocation *rel = serialize_find_reloc(var, offset);
        if (rel) {
            serialize_reloc_init(f, vm, ctx, var, ty, rel);
            return;
        }
    }

    switch (ty->kind) {
        case TY_ARRAY:
            if (ty->base->kind == TY_CHAR &&
                !serialize_find_reloc(var, offset)) {
                serialize_string_n(f, var->init_data + offset, ty->array_len);
                return;
            }
            fprintf(f, "{ ");
            for (int i = 0; i < ty->array_len; i++) {
                if (i > 0)
                    fprintf(f, ", ");
                serialize_init_bytes(f, vm, ctx, var, ty->base,
                                     offset + i * ty->base->size);
            }
            fprintf(f, " }");
            return;

        case TY_VECTOR:
            fprintf(f, "{ ");
            for (int i = 0; i < ty->vec_len; i++) {
                if (i > 0)
                    fprintf(f, ", ");
                serialize_init_bytes(f, vm, ctx, var, ty->base,
                                     offset + i * ty->base->size);
            }
            fprintf(f, " }");
            return;

        case TY_STRUCT: {
            fprintf(f, "{ ");
            bool first = true;
            serialize_agg_member_list(f, vm, ctx, var, ty, offset, &first);
            fprintf(f, " }");
            return;
        }

        case TY_UNION: {
            // #1115: an empty (0-byte) union has no members at all -- an
            // empty brace initializer is accepted by every host for a
            // zero-sized object and matches the VM's own semantics exactly
            // (no bytes to represent).
            if (ty->size == 0) {
                fprintf(f, "{ }");
                return;
            }
            fprintf(f, "{ ");
            bool first = true;
            serialize_agg_member_list(f, vm, ctx, var, ty, offset, &first);
            fprintf(f, " }");
            return;
        }

        case TY_FLOAT: {
            float fv;
            memcpy(&fv, var->init_data + offset, 4);
            if (!serialize_flonum_special(f, (long double)fv, "f")) {
                char buf[64];
                format_float_literal(buf, sizeof buf, (double)fv);
                fprintf(f, "%sf", buf);
            }
            return;
        }

        case TY_DOUBLE:
        case TY_LDOUBLE: {
            // TY_LDOUBLE shares TY_DOUBLE's 8-byte read and unsuffixed %g here,
            // matching this function's pre-#918 behavior exactly -- a latent
            // long-double-precision/suffix gap, but pre-existing and unrelated
            // to #918's scope. Still present after #1038 (which fixed the
            // ND_NUM/expression-literal counterpart of this same gap, not this
            // global-initializer path) -- left alone here for the same reason:
            // out of scope, tracked separately, not touched incidentally.
            double dv;
            memcpy(&dv, var->init_data + offset, 8);
            if (!serialize_flonum_special(f, (long double)dv, ""))
                fprintf(f, "%.17g", dv);
            return;
        }

        case TY_COMPLEX: {
            // #1122/#1208: real-then-imag contiguous parts, each at stride
            // ty->base->size (matching complex_part_offset() in codegen).
            // A zero imaginary half -- the common case, a real constant
            // converted to complex -- still prints as a bare real literal:
            // byte-identical to pre-#1208 output, so the native corpus diff
            // stays minimal. A non-zero imaginary half (now reachable via
            // eval_complex folding `I`/`CMPLX()`/complex arithmetic) emits
            // `__builtin_complex((elem)re, (elem)im)` -- the same shape
            // serialize_expr's ND_COMPLEX construction arm uses, and which
            // clang and gcc both accept in a static initializer. "Zero
            // imaginary" is tested on the raw bytes so -0.0 counts as
            // non-zero and keeps its sign.
            int         part = ty->base ? ty->base->size : 8;
            int         psz  = (ty->base && ty->base->kind == TY_FLOAT) ? 4 : 8;
            const char *ip   = var->init_data + offset + part;
            bool        imag_zero = true;
            for (int i = 0; i < psz; i++)
                if ((unsigned char)ip[i] != 0) {
                    imag_zero = false;
                    break;
                }
            if (imag_zero) {
                serialize_complex_part(f, ctx, ty->base,
                                       var->init_data + offset);
                return;
            }
            fprintf(f, "__builtin_complex((");
            serialize_type(f, ctx, ty->base);
            fprintf(f, ")");
            serialize_complex_part(f, ctx, ty->base, var->init_data + offset);
            fprintf(f, ", (");
            serialize_type(f, ctx, ty->base);
            fprintf(f, ")");
            serialize_complex_part(f, ctx, ty->base, ip);
            fprintf(f, ")");
            return;
        }

        default:
            break;
    }

    if (is_decimal(ty)) {
        // #402: raw BID bytes in init_data -> C source text. Requires
        // CCCC_HAS_DECIMAL=1 (the same build that could have produced
        // these bytes in the first place); cccc_dec_format returns -1
        // in the off build, which can't happen here.
        char        buf[80];
        int         w      = dec_width_code(ty);
        const char *suffix = w == 0 ? "df" : w == 1 ? "dd" : "dl";
        if (cccc_dec_format(buf, sizeof buf, var->init_data + offset, w) >= 0)
            fprintf(f, "%s%s", buf, suffix);
        else
            fprintf(f, "0%s", suffix);
        return;
    }

    if (ty->kind == TY_BOOL || ty->kind == TY_CHAR || ty->kind == TY_SHORT ||
        ty->kind == TY_INT || ty->kind == TY_LONG || ty->kind == TY_ENUM ||
        ty->kind == TY_PTR || ty->kind == TY_NULLPTR_T ||
        ty->kind == TY_BLOCK) {
        // #965: TY_BLOCK is 8 bytes, pointer-shaped (see block_type(),
        // type.c) -- a block value can only be a compile-time-constant
        // global initializer as a null pointer anyway (the VM's own
        // is_const_expr rejects a real block literal there before this is
        // ever reached), so it reads exactly like TY_PTR.
        int64_t iv = 0;
        int     sz = ty->size < 8 ? ty->size : 8;
        memcpy(&iv, var->init_data + offset, sz);
        if (sz < 8 && ty->kind != TY_PTR && ty->kind != TY_NULLPTR_T &&
            (iv >> (sz * 8 - 1)) & 1)
            iv |= (-1LL << (sz * 8));
        fprintf(f, "%lld", (long long)iv);
        return;
    }

    if (ty->kind == TY_BITINT) {
        // #1121: was entirely absent from this function -- any _BitInt(N)
        // global initializer (narrow or wide) fell straight through to the
        // "cannot serialize" error below. size<=8 mirrors the TY_LONG-family
        // narrow-integer read just above (sign-extend by size); size==16
        // (__int128/unsigned __int128, the only wide width serialize_type()
        // now supports -- case TY_BITINT there refuses anything larger)
        // reads both little-endian words and reassembles the same
        // ((unsigned __int128)hi << 64) | lo shape the wide-literal ND_NUM
        // arm above uses.
        if (ty->size <= 8) {
            int64_t iv = 0;
            memcpy(&iv, var->init_data + offset, ty->size);
            if (ty->size < 8 && !ty->is_unsigned &&
                (iv >> (ty->size * 8 - 1)) & 1)
                iv |= (-1LL << (ty->size * 8));
            fprintf(f, "%lld", (long long)iv);
            return;
        }
        if (ty->size == 16) {
            uint64_t lo, hi;
            memcpy(&lo, var->init_data + offset, 8);
            memcpy(&hi, var->init_data + offset + 8, 8);
            fprintf(f,
                    "((%s__int128)(((unsigned __int128)0x%llxULL << 64) | "
                    "0x%llxULL))",
                    ty->is_unsigned ? "unsigned " : "", (unsigned long long)hi,
                    (unsigned long long)lo);
            return;
        }
        // size > 16 (bit_width > 128): named error, consistent with
        // serialize_type()'s own #1123 refusal for this width -- distinct
        // from the generic fallback below so this doesn't read as an
        // unrecognized/unknown type (#1128 audit).
        error("cccc: cannot serialize initializer for global '%s' in native "
              "mode: _BitInt(%d) exceeds 128 bits, which has no native/-m "
              "lowering (#1123)",
              var->name, ty->bit_width);
    }

    // Any kind with no verified byte layout here (TY_COMPLEX has had its own
    // case since #1122 -- this comment used to name it, stale since then):
    // fail loudly rather than guess (#918's whole point -- emitting a
    // plausible-but-wrong initializer is the bug class being fixed, not a
    // shape to reproduce for cases this function doesn't yet handle).
    error("cccc: cannot serialize initializer for global '%s' in native "
          "mode: unsupported initializer type (kind %d)",
          var->name, ty->kind);
}

// Serialize global variable
// #1022: `include/pthread.h` hands off `-c=native`'s replayed
// `#include <pthread.h>` to the real host header (#1021/#1040-style
// #include_next guard), so a static of type pthread_mutex_t/pthread_cond_t
// initialized with PTHREAD_MUTEX_INITIALIZER/PTHREAD_COND_INITIALIZER can no
// longer be serialized as CCCC's own projected designated-initializer image
// (`{ .__handle = 0, .__state = 0, .__type = 0 }`) -- the real host struct
// doesn't have those members at all. Narrow, type-keyed fix (not the general
// #1018 macro-provenance annotation, per user sign-off): if the global's
// type is one of these `from_include` pthread types and its init image is
// all-zero -- the only image CCCC's own macros ever produce -- print the
// bare host macro name instead of walking the projected members. Known
// limitation, documented rather than silently assumed away: a user's own
// literal `= {0}` on one of these types is indistinguishable from the macro
// and also becomes the macro spelling -- semantically equivalent either way
// (both are "the type's zero/default-initialized state"), so this is a safe
// over-approximation, not a soundness gap.
static const char *pthread_initializer_macro(SerializeContext *ctx, Obj *var) {
    if (!var->ty || !var->init_data)
        return NULL;
    TypeName *tn = find_typedef_name(ctx, var->ty);
    if (!tn || !tn->from_include)
        return NULL;

    // Only pthread_mutex_t/pthread_cond_t are declared in include/pthread.h
    // today -- rwlock/once have no CCCC type or FFI wrappers at all yet, so
    // there's no initializer image for either to collide with.
    static const struct {
        const char *type_name;
        const char *macro;
    } table[] = {
        {"pthread_mutex_t", "PTHREAD_MUTEX_INITIALIZER"},
        {"pthread_cond_t", "PTHREAD_COND_INITIALIZER"},
    };
    const char *macro = NULL;
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (tn->name_len == (int)strlen(table[i].type_name) &&
            !strncmp(tn->name, table[i].type_name, tn->name_len)) {
            macro = table[i].macro;
            break;
        }
    }
    if (!macro)
        return NULL;

    for (int i = 0; i < var->ty->size; i++)
        if (var->init_data[i] != 0)
            return NULL;
    return macro;
}

// #1136: an explicit _Alignas(N) (Obj.align) requesting more than the
// type's own natural alignment is otherwise dropped by -c=native -- neither
// _Alignas nor __attribute__((aligned)) was emitted anywhere in this file,
// so `_Alignas(32) int g;` compiled fine but round-tripped through native
// output as a plain `int g;`, silently losing the requested alignment (the
// same "stated vs actual" bug class as the VM-side data-segment allocator
// this ticket also fixes). Natural (<=type-align) cases need nothing here:
// the host compiler derives those from the emitted type on its own -- EXCEPT
// a vector global's own over-8 natural alignment (#1191): it rides solely on
// __attribute__((vector_size(N))) (serialize_type.c), which gcc on
// Darwin/arm64 does not honour for a global object the way clang does
// (confirmed: `float __attribute__((vector_size(32))) g;` lands 16-aligned
// under gcc-16, 32-aligned under clang, on the same host). State it
// explicitly for that one case rather than widening the general predicate --
// a blanket "any type align > 8" rule would also fire for e.g. long double
// and 16-byte structs, churning the 142-case native serializer smoke
// suite's own text assertions for no gain (those already round-trip fine
// under both compiler families).
// Called at every declaration site for one Obj (definition and forward
// declarations alike) -- C11 6.7.5p7 requires every declaration of an
// object to carry equivalent alignment, so they must all agree, not just
// the definition.
void serialize_alignas_if_needed(FILE *f, Obj *var) {
    // The gcc/Darwin gap is specifically the data-segment/TLS allocator
    // (#1136's own scope) -- a *local* vector's alignment is the stack
    // allocator's concern, unaffected by this, and both compilers already
    // honour it there, so restrict the explicit-vector-alignment carve-out
    // to non-local objects to avoid needlessly widening every local vector
    // declaration's own serialized form.
    bool vector_needs_explicit =
        !var->is_local && var->ty->kind == TY_VECTOR && var->ty->align > 8;
    if (var->align > var->ty->align)
        fprintf(f, "_Alignas(%d) ", var->align);
    else if (vector_needs_explicit)
        fprintf(f, "_Alignas(%d) ", var->ty->align);
}

void serialize_global_var(FILE *f, VirtualMachine *vm, SerializeContext *ctx,
                          Obj *var) {
    if (var->is_function)
        return;

    // String literals are inlined at their point of use (ND_VAR /
    // serialize_reloc_init) instead of getting their own definition. Every
    // other `.L..N`-named global (compound literal, static local) is
    // renamed to a valid identifier by rename_anon_globals() before this
    // runs, so it falls through and is serialized like any other global
    // (#925).
    if (var->is_string_literal)
        return;

    // #1044: deferred into its owning function's own body instead --
    // serialize_function() emits its real definition; see
    // collect_deferred_static_labels()'s own comment.
    if (var_is_deferred_label_static(ctx, var))
        return;

    // #1047: a global whose declaration lives entirely in a replayed
    // header is already supplied by that header's own #include text --
    // see global_is_header_supplied()'s comment.
    if (global_is_header_supplied(vm, ctx, var))
        return;

    // #1011: the #918/#928 forward-declaration pass (cc_serialize_program,
    // further down this file) already emitted a line for this global ahead
    // of every definition -- `static T name;` for a static with no
    // initializer, or `extern T name;` for a declaration with no
    // definition (is_definition false). When this global also has no
    // init_data, what follows below would print the exact same text a
    // second time, back to back (e.g. a function-local `static struct Foo
    // a;` hoisted to file scope by rename_anon_globals() as `__cccc_a_0`).
    // A global that *does* have an initializer still needs both lines (the
    // forward declaration, then the real `T name = ...;` definition), so
    // this only skips the no-initializer case.
    if (!var->init_data && (var->is_static || !var->is_definition))
        return;

    if (var->is_static)
        fprintf(f, "static ");
    else if (!var->is_definition)
        // #901: a global written `extern int g;` (no initializer, no
        // tentative-definition fallback -- parse.c sets is_definition =
        // !attr->is_extern) is a declaration, not a definition. Emitting
        // it as a bare `int g;` produces a tentative definition that
        // collides with the real symbol at link time.
        fprintf(f, "extern ");

    // #1022: Obj.is_tls (_Thread_local/__thread storage class) was parsed
    // and tracked but never re-emitted here -- a `_Thread_local` global
    // silently serialized as an ordinary global, so every thread shared one
    // instance instead of getting its own copy (confirmed:
    // test_thread_local_isolation.c's cross-thread-visibility check would
    // pass, i.e. the isolation it exists to test would be gone, under
    // -c=native). Emitted right after static/extern per C11 6.7.1's
    // storage-class-specifier ordering.
    if (var->is_tls)
        fprintf(f, "_Thread_local ");

    // #1136: see serialize_alignas_if_needed's own comment.
    serialize_alignas_if_needed(f, var);

    // #1095: only when no byte-image initializer follows -- an initialized
    // global's array dimension must stay folded so it can't disagree with
    // serialize_init_bytes' own byte image below, sized off the same
    // folded value. See SerializeContext.allow_layout_dims's own comment.
    ctx->allow_layout_dims = !var->init_data;
    serialize_type_decl(f, ctx, var->ty, var->name);
    ctx->allow_layout_dims = false;

    if (var->init_data) {
        fprintf(f, " = ");
        const char *pthread_init_macro = pthread_initializer_macro(ctx, var);
        if (pthread_init_macro)
            fprintf(f, "%s", pthread_init_macro);
        else
            serialize_init_bytes(f, vm, ctx, var, var->ty, 0);
    }

    fprintf(f, ";\n");
}
