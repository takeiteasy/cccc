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

// Top-level declaration driver: function and global-variable declarations,
// goto/label resolution, dead-code marking, and the parse() entry points
// (including the REPL/splice entry points used by comptime).

#include "./parse_internal.h"

Token *parse_typedef(VirtualMachine *vm, Token *tok, Type *basety,
                     VarAttr *attr) {
    bool first = true;

    while (!consume(vm, &tok, tok, ";")) {
        if (!first)
            tok = skip(vm, tok, ",");
        first    = false;

        Type *ty = declarator(vm, &tok, tok, basety);
        if (!ty->name)
            error_tok(vm, ty->name_pos, "typedef name omitted");
        // #999: for a plain typedef of a builtin scalar (`typedef unsigned
        // long DyValue;`), declarator() above returns `basety` itself
        // unmodified (type_suffix() is a no-op with no `[`/`(` following
        // the identifier) -- one of the shared singleton Type objects
        // (ty_ulong, etc.), not a fresh one. Without copying, every
        // subsequent `DyValue`-typed node's ->ty is pointer-identical to
        // every unrelated plain `unsigned long`-typed node's, so a
        // serializer pass matching a typedef alias by Type identity
        // (find_typedef_name_exact, src/serialize_type.c) couldn't tell "spell
        // this as DyValue" apart from "this was never typedef'd, spell it
        // as unsigned long" -- and would have to pick one spelling for
        // both, wrongly renaming every plain use of the underlying type.
        // Copying gives each typedef its own Type identity (copy_type sets
        // ->origin, so same_type_or_origin compatibility with the
        // original -- and thus with any other typedef of the same
        // underlying type -- is unaffected); find_typedef()'s lookup
        // (called wherever `DyValue` is used as a type specifier) returns
        // this same copy via sc->type_def below, so every node the
        // typedef's name produces shares its identity, and only those
        // nodes match.
        //
        // Deliberately excluded: TY_STRUCT/TY_UNION/TY_ENUM. Those already
        // have a working, independent alias mechanism in the serializer
        // (find_tag_name takes priority over find_typedef_name, and it
        // matches structurally via same_type_or_origin, not by identity --
        // see serialize_type's TY_STRUCT/TY_UNION/TY_ENUM cases), so they
        // never needed this fix. Copying one would actively break the
        // opaque-handle idiom (`typedef struct Beta Beta;` forward-declared
        // here, its body defined later elsewhere in the same TU): a forward
        // -declared tag is completed by *mutating the same Type object in
        // place* when its body is later parsed (struct_union_decl's
        // `existing` reuse, parse_types.c) -- struct_decl looks the tag up
        // again by name and finds that same live object. copy_type() takes
        // a one-time snapshot; had it fired here, `Beta`'s recorded type
        // would have frozen at its still-incomplete size/members forever,
        // silently reproducing an incomplete `struct Beta` in the output
        // even after the real definition completed the original.
        if (ty->kind != TY_STRUCT && ty->kind != TY_UNION &&
            ty->kind != TY_ENUM)
            ty = copy_type(vm, ty);
        char     *name     = get_ident(vm, ty->name);
        VarScope *sc       = push_scope(vm, name, ty->name->len);
        sc->type_def       = ty;
        sc->is_deprecated  = ty->is_deprecated;
        sc->deprecated_msg = ty->deprecated_msg;
        record_type_name(vm, ty, name, ty->name->len, false, ty->name);
        run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_TYPEDEF, name, ty, NULL,
                              ty->name);
    }
    return tok;
}

static void create_param_lvars(VirtualMachine *vm, Type *param) {
    if (param) {
        create_param_lvars(vm, param->next);
        if (!param->name)
            error_tok(vm, param->name_pos, "parameter name omitted");
        Obj *var =
            new_lvar(vm, get_ident(vm, param->name), param->name->len, param);
        var->is_param = true;
    }
}

// Depth of the lowest common ancestor of two cleanup-scope chains. The scopes
// being exited by a goto are exactly those active at the goto whose depth is
// greater than this LCA depth, so this is the correct cleanup_target_depth even
// for cross-sibling jumps where the goto and label share a depth number but not
// an ancestor. Returns 0 when there is no common cleanup scope (function
// level).
static int cleanup_lca_depth(CleanupChainNode *g, CleanupChainNode *l) {
    while (g && (!l || g->depth > l->depth))
        g = g->parent;
    while (l && (!g || l->depth > g->depth))
        l = l->parent;
    while (g != l) {
        g = g->parent;
        l = l->parent;
    }
    return g ? g->depth : 0;
}

// This function matches gotos or labels-as-values with labels.
//
// We cannot resolve gotos as we parse a function because gotos
// can refer a label that appears later in the function.
// So, we need to do this after we parse the entire function.
static void resolve_goto_labels(VirtualMachine *vm) {
    for (Node *x = vm->compiler.gotos; x; x = x->goto_next) {
        for (Node *y = vm->compiler.labels; y; y = y->goto_next) {
            if (strlen(x->label) == strlen(y->label) &&
                strncmp(x->label, y->label, strlen(y->label)) == 0) {
                x->unique_label = y->unique_label;
                // Cleanup ancestry applies only to actual jumps. ND_LABEL_VAL
                // (labels-as-values, `&&label`) also flows through this loop
                // but merely takes an address — it neither exits nor enters
                // scopes.
                if (x->kind == ND_GOTO) {
                    // The goto must clean up every active cleanup scope that
                    // the label is not inside of, i.e. everything below their
                    // lowest common ancestor. Using the LCA depth (rather than
                    // the label's depth) handles cross-sibling jumps, where the
                    // goto and label share a depth number but no common cleanup
                    // scope.
                    int lca =
                        cleanup_lca_depth(x->cleanup_chain, y->cleanup_chain);
                    x->cleanup_target_depth = lca;
                    // Jumping *into* a cleanup scope (the label sits inside a
                    // cleanup scope the goto is not in) leaves that variable
                    // uninitialized when its cleanup runs at the label's block
                    // exit — ill-formed C.
                    if (y->cleanup_chain && y->cleanup_chain->depth > lca)
                        warn_tok(
                            vm, x->tok, CCCC_WARN_ATTRIBUTES,
                            "goto jumps into scope of variable with "
                            "__attribute__((cleanup)); cleanup may run on an "
                            "uninitialized object");
                }
                y->label_used = true;
                break;
            }
        }

        if (x->unique_label == NULL)
            error_tok(vm, x->tok->next, "use of undeclared label");
    }

    for (Node *label = vm->compiler.labels; label; label = label->goto_next)
        if (!label->label_used && !label->label_maybe_unused)
            warn_tok(vm, label->tok, CCCC_WARN_UNUSED, "unused label '%s'",
                     label->label);

    vm->compiler.gotos = vm->compiler.labels = NULL;
}

static Obj *find_func(VirtualMachine *vm, char *name, int name_len) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        for (VarScopeNode *node = sc->vars; node; node = node->next) {
            if (node->name_len == name_len &&
                strncmp(node->name, name, name_len) == 0) {
                VarScope *sc2 = (VarScope *)node;
                if (sc2->var && sc2->var->is_function)
                    return sc2->var;
                return NULL;
            }
        }
    }
    return NULL;
}

static Obj *find_func_in_current_scope(VirtualMachine *vm, char *name,
                                       int name_len) {
    Scope *sc = vm->compiler.scope;
    if (!sc)
        return NULL;

    for (VarScopeNode *node = sc->vars; node; node = node->next) {
        if (node->name_len == name_len &&
            strncmp(node->name, name, name_len) == 0) {
            VarScope *sc2 = (VarScope *)node;
            if (sc2->var && sc2->var->is_function)
                return sc2->var;
            return NULL;
        }
    }
    return NULL;
}

// #696: __attribute__((sentinel)) / [[gnu::sentinel]] only makes sense on a
// variadic function -- warn (under -Wattributes, matching GCC) when it's
// applied to one that isn't, instead of silently relying on
// validate_sentinel_call()'s per-call-site bounds guard to notice.
static void check_sentinel_variadic(VirtualMachine *vm, Type *ty) {
    if (ty && ty->kind == TY_FUNC && ty->is_sentinel && !ty->is_variadic &&
        (vm->compiler.warnings & CCCC_WARN_ATTRIBUTES))
        warn_tok(vm, ty->name, CCCC_WARN_ATTRIBUTES,
                 "sentinel attribute only applies to variadic functions");
}

static Obj *declare_function_prototype(VirtualMachine *vm, Type *ty,
                                       VarAttr *attr, Token *tok) {
    if (!ty->name)
        error_tok(vm, ty->name_pos, "function name omitted");

    char *name_str = get_ident(vm, ty->name);
    check_sentinel_variadic(vm, ty);

    if (attr->is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;

    // #1056: declare_function_prototype() is only ever reached from
    // function_declaration_list(), which is always bodyless (loops to ';')
    // -- so unlike function(), there is no definition case here that could
    // legitimately need nested-function tracking. A block-scope function
    // declaration with no storage-class specifier has *external* linkage
    // (C17 6.2.2p5): it must never be marked is_nested/is_static here just
    // because the current parse happens to be inside another function's
    // body (see the matching, larger comment in function()).
    Obj *fn = attr->is_static
                  ? find_func_in_current_scope(vm, name_str, ty->name->len)
                  : find_func(vm, name_str, ty->name->len);
    if (fn) {
        if (!fn->is_function)
            error_tok(vm, tok, "redeclared as a different kind of symbol");
        if (fn->is_implicit) {
            fn->ty          = ty;
            fn->is_implicit = false;
        }
        if (!fn->is_static && attr->is_static)
            error_tok(vm, tok,
                      "static declaration follows a non-static declaration");
        if (!fn->is_implicit && !fn->is_definition &&
            (vm->compiler.warnings & CCCC_WARN_REDUNDANT_DECLS))
            warn_tok(vm, ty->name, CCCC_WARN_REDUNDANT_DECLS,
                     "redundant redeclaration of '%s'", fn->name);
        fn->is_maybe_unused |= ty->is_maybe_unused;
        fn->is_deprecated   |= ty->is_deprecated;
        fn->is_noreturn     |= ty->is_noreturn;
        fn->is_pure         |= ty->is_pure;
        fn->is_func_const   |= ty->is_func_const;
        if (ty->asm_label)
            fn->asm_label = ty->asm_label;
        if (ty->deprecated_msg)
            fn->deprecated_msg = ty->deprecated_msg;
        if (ty->format_style && fn->ty) {
            fn->ty->format_style         = ty->format_style;
            fn->ty->format_string_index  = ty->format_string_index;
            fn->ty->format_fmt_first_arg = ty->format_fmt_first_arg;
        }
        if (fn->ty) {
            if (ty->nonnull_all)
                fn->ty->nonnull_all = true;
            fn->ty->nonnull_mask |= ty->nonnull_mask;
            if (ty->returns_nonnull)
                fn->ty->returns_nonnull = true;
            if (ty->is_sentinel) {
                fn->ty->is_sentinel  = true;
                fn->ty->sentinel_pos = ty->sentinel_pos;
            }
            if (ty->alloc_size_idx) {
                fn->ty->alloc_size_idx  = ty->alloc_size_idx;
                fn->ty->alloc_size_idx2 = ty->alloc_size_idx2;
            }
            fn->ty->is_malloc |= ty->is_malloc;
        }
    } else {
        fn                = new_gvar(vm, name_str, ty->name->len, ty);
        fn->is_function   = true;
        fn->is_definition = false;
        fn->is_static =
            attr->is_static || (attr->is_inline && !attr->is_extern);
        fn->is_inline = attr->is_inline;
        fn->asm_label = ty->asm_label;
    }

    fn->is_root = !(fn->is_static && fn->is_inline);
    run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_FUNCTION, fn->name, fn->ty,
                          fn, fn->tok);
    return fn;
}

Token *function_declaration_list(VirtualMachine *vm, Token *tok, Type *basety,
                                 VarAttr *attr) {
    bool first = true;

    while (!consume(vm, &tok, tok, ";")) {
        if (!first)
            tok = skip(vm, tok, ",");
        first    = false;

        Type *ty = declarator(vm, &tok, tok, basety);
        ty       = apply_var_attrs_to_type(vm, ty, attr);
        if (ty->kind != TY_FUNC)
            error_tok(vm, ty->name ? ty->name : tok,
                      "expected function declaration");
        declare_function_prototype(vm, ty, attr, tok);
    }

    return tok;
}

static void mark_live(VirtualMachine *vm, Obj *var) {
    if (!var->is_function || var->is_live)
        return;
    var->is_live = true;

    for (int i = 0; i < var->refs.len; i++) {
        Obj *fn = find_func(vm, var->refs.data[i], strlen(var->refs.data[i]));
        if (fn)
            mark_live(vm, fn);
    }
}

static bool statement_terminates(Node *node) {
    if (!node)
        return false;

    switch (node->kind) {
        case ND_RETURN:
            return true;
        case ND_BLOCK: {
            Node *last = node->body;
            if (!last)
                return false;
            while (last->next)
                last = last->next;
            return statement_terminates(last);
        }
        case ND_IF:
            return node->els && statement_terminates(node->then) &&
                   statement_terminates(node->els);
        default:
            return false;
    }
}

static void append_implicit_return(VirtualMachine *vm, Obj *fn, Token *tok) {
    Type *ty = fn->ty->return_ty;

    // Noreturn functions must not fall off the end
    if (fn->is_noreturn) {
        if (ty->kind != TY_VOID)
            warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
                     "noreturn function should not return a value");
        return;
    }

    if (ty->kind == TY_VOID || strcmp(fn->name, "main") == 0 ||
        statement_terminates(fn->body))
        return;

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION)
        error_tok(vm, tok,
                  "control reaches end of non-void aggregate function");

    warn_tok(vm, tok, CCCC_WARN_RETURN_TYPE,
             "control reaches end of non-void function");

    Node *ret  = new_node(vm, ND_RETURN, tok);
    ret->lhs   = new_cast(vm, new_num(vm, 0, tok), ty);

    Node **cur = &fn->body->body;
    while (*cur)
        cur = &(*cur)->next;
    *cur = ret;
}

static bool is_plain_signed_int(Type *ty) {
    return ty && ty->kind == TY_INT && !ty->base && !ty->is_unsigned;
}

static bool is_char_ptr_ptr(Type *ty) {
    return ty && ty->kind == TY_PTR && ty->base && ty->base->kind == TY_PTR &&
           ty->base->base && ty->base->base->size == 1;
}

static void validate_main_signature(VirtualMachine *vm, Obj *fn) {
    if (!fn || !fn->name || strcmp(fn->name, "main") != 0)
        return;

    bool warn = vm->compiler.warnings & CCCC_WARN_MAIN;

    // Check return type.
    if (warn && fn->ty && fn->ty->return_ty &&
        fn->ty->return_ty->kind != TY_INT)
        warn_tok(vm, fn->tok, CCCC_WARN_MAIN,
                 "return type of 'main' is not 'int'");

    // Count parameters and check types.
    int nparams = 0;
    for (Type *p = fn->ty ? fn->ty->params : NULL; p; p = p->next)
        nparams++;

    if (nparams == 0)
        return; // int main(void) — always fine

    if (warn && nparams != 2 && nparams != 3)
        warn_tok(vm, fn->tok, CCCC_WARN_MAIN,
                 "suspicious number of parameters for 'main': expected 0 or 2");

    Type *first = fn->ty->params;
    if (warn && first && !is_plain_signed_int(first))
        warn_tok(vm, first->name_pos ? first->name_pos : fn->tok,
                 CCCC_WARN_MAIN, "first parameter of 'main' should be 'int'");

    Type *second = first ? first->next : NULL;
    if (warn && second && !is_char_ptr_ptr(second))
        warn_tok(vm, second->name_pos ? second->name_pos : fn->tok,
                 CCCC_WARN_MAIN,
                 "second parameter of 'main' should be 'char **'");
}

Token *function(VirtualMachine *vm, Token *tok, Type *basety, VarAttr *attr) {
    if (attr->is_constexpr)
        error_tok(vm, tok,
                  "constexpr is only supported for object definitions");

    Type *ty = declarator(vm, &tok, tok, basety);
    ty       = apply_var_attrs_to_type(vm, ty, attr);
    if (!ty->name)
        error_tok(vm, ty->name_pos, "function name omitted");
    char *name_str = get_ident(vm, ty->name);
    check_sentinel_variadic(vm, ty);

    // Propagate noreturn from attribute to function type
    if (attr->is_noreturn && ty->kind == TY_FUNC)
        ty->is_noreturn = true;

    // Check if this is a nested function (defined inside another function)
    Obj                 *parent_fn             = vm->compiler.current_fn;
    bool                 is_nested             = (parent_fn != NULL);
    Obj                 *saved_locals          = NULL;
    int                  saved_nesting_depth   = 0;
    struct ObjSizeQuery *saved_objsize_queries = NULL;

    if (is_nested) {
        // Save parent's locals - we're about to start a new locals chain
        saved_locals        = vm->compiler.locals;
        saved_nesting_depth = vm->compiler.fn_nesting_depth;
        // #642: the nested function gets its own pending __builtin_object_size
        // query list; resolve_objsize_queries resets it to NULL when the
        // nested body finishes, so the parent's in-flight queries must be
        // parked here rather than lost.
        saved_objsize_queries        = vm->compiler.objsize_queries;
        vm->compiler.objsize_queries = NULL;
    }

    // #1075: a nested function *definition* must never merge with an
    // outer, same-named file-scope function -- C17 6.2.1p4 treats scope and
    // linkage as separate axes, and #1039 already established nested
    // functions are implicitly static (invisible outside their own scope).
    // #1056's own fix (the `attr->is_static` arm's sibling reasoning)
    // established the opposite requirement for a *bodyless* block-scope
    // declaration: it has external linkage (C17 6.2.2p5) and must still
    // resolve through the whole-chain find_func() to bind to the outer
    // function. `defines_body` distinguishes the two: checked the same way
    // as the existing bodyless early-return below (consume(";")), not
    // equal(tok, "{"), since that would miss a K&R definition (#1043's
    // lesson, called out in #1056's own comment on this same function).
    bool defines_body = !equal(tok, ";");
    Obj *fn = (attr->is_static || (is_nested && defines_body))
                  ? find_func_in_current_scope(vm, name_str, ty->name->len)
                  : find_func(vm, name_str, ty->name->len);
    // Save prototype state before the if/else can mutate fn->is_implicit.
    bool had_prior_decl = (fn != NULL) && !fn->is_implicit;
    bool had_full_proto =
        had_prior_decl &&
        (vm->compiler.c_std >= CCCC_STD_C23
             ? !fn->ty->is_variadic // C23: () == (void); non-variadic = full
                                    // proto
             : fn->ty->params != NULL); // pre-C23: need an explicit params list
    if (fn) {
        // Redeclaration
        if (!fn->is_function)
            error_tok(vm, tok, "redeclared as a different kind of symbol");
        if (fn->is_implicit) {
            fn->ty          = ty;
            fn->is_implicit = false;
        }
        if (fn->is_definition && equal(tok, "{"))
            error_tok(vm, tok, "redefinition of %s", name_str);
        if (!fn->is_static && attr->is_static)
            error_tok(vm, tok,
                      "static declaration follows a non-static declaration");
        if (!fn->is_implicit && !fn->is_definition && !equal(tok, "{") &&
            (vm->compiler.warnings & CCCC_WARN_REDUNDANT_DECLS))
            warn_tok(vm, ty->name, CCCC_WARN_REDUNDANT_DECLS,
                     "redundant redeclaration of '%s'", fn->name);
        fn->is_definition    = fn->is_definition || equal(tok, "{");
        fn->is_maybe_unused |= ty->is_maybe_unused;
        fn->is_deprecated   |= ty->is_deprecated;
        fn->is_noreturn     |= ty->is_noreturn;
        fn->is_pure         |= ty->is_pure;
        fn->is_func_const   |= ty->is_func_const;
        if (ty->asm_label)
            fn->asm_label = ty->asm_label;
        if (ty->deprecated_msg)
            fn->deprecated_msg = ty->deprecated_msg;
        if (ty->format_style && fn->ty) {
            fn->ty->format_style         = ty->format_style;
            fn->ty->format_string_index  = ty->format_string_index;
            fn->ty->format_fmt_first_arg = ty->format_fmt_first_arg;
        }
        if (fn->ty) {
            if (ty->nonnull_all)
                fn->ty->nonnull_all = true;
            fn->ty->nonnull_mask |= ty->nonnull_mask;
            if (ty->returns_nonnull)
                fn->ty->returns_nonnull = true;
            if (ty->is_sentinel) {
                fn->ty->is_sentinel  = true;
                fn->ty->sentinel_pos = ty->sentinel_pos;
            }
            if (ty->alloc_size_idx) {
                fn->ty->alloc_size_idx  = ty->alloc_size_idx;
                fn->ty->alloc_size_idx2 = ty->alloc_size_idx2;
            }
            fn->ty->is_malloc |= ty->is_malloc;
        }
    } else {
        fn                = new_gvar(vm, name_str, ty->name->len, ty);
        fn->is_function   = true;
        fn->is_definition = equal(tok, "{");
        fn->is_static =
            attr->is_static || (attr->is_inline && !attr->is_extern);
        fn->is_inline = attr->is_inline;
        fn->asm_label = ty->asm_label;
    }

    fn->is_root = !(fn->is_static && fn->is_inline);

    if (consume(vm, &tok, tok, ";")) {
        run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_FUNCTION, fn->name,
                              fn->ty, fn, fn->tok);
        return tok;
    }

    // #1056: nested-function tracking must only apply when a body actually
    // follows (checked above, via the early "no body" return) -- not to a
    // bare block-scope declaration/prototype. A block-scope function
    // declaration with no storage-class specifier has *external* linkage
    // (C17 6.2.2p5); it names the enclosing file-scope function, it does not
    // introduce a nested one. Running this unconditionally on whatever `fn`
    // find_func()/find_func_in_current_scope() returned above used to
    // retroactively flip an already-defined, already-codegen'd file-scope
    // function to is_nested/is_static merely because the current parse
    // happens to be inside another function's body -- corrupting the ABI of
    // every call site emitted afterward (codegen_expr.c's calling_nested
    // path starts passing a static link the callee's body was never
    // compiled to expect). Recompute is_root after this, since is_static may
    // just have changed.
    if (is_nested) {
        fn->parent_fn     = parent_fn;
        fn->is_nested     = true;
        fn->nesting_depth = vm->compiler.fn_nesting_depth + 1;
        // #1039: nested functions are implicitly static (not visible
        // outside) -- except when an asm("symbol") label is present; see
        // the matching comment in declare_function_prototype above.
        if (!fn->asm_label)
            fn->is_static = true;
        // #1076: record the parent's locals snapshot the same way a block
        // literal does (parse_blocks.c) so a block literal defined *inside*
        // this nested function can climb past it to capture a variable
        // owned by one of its own ancestors -- see the matching ancestor
        // walk in block_literal() (parse_blocks.c) that now keys off
        // is_nested rather than is_block.
        fn->block_outer_locals = saved_locals;
        // #1081: register `fn` as one of parent_fn's nested_children --
        // compound_stmt() (parse_stmt.c) never appends an AST node for a
        // nested function definition, so this is the only way a block
        // literal's own transitive-capture climb (block_literal(),
        // parse_blocks.c) can reach a variable referenced only inside a
        // nested function defined directly in the block's own body.
        // parent_fn may itself be a block (Obj.is_block) -- that's the
        // exact shape #1081 is about.
        fn->next_nested_sibling    = parent_fn->nested_children;
        parent_fn->nested_children = fn;
    } else {
        fn->parent_fn     = NULL;
        fn->is_nested     = false;
        fn->nesting_depth = 0;
    }

    fn->is_root = !(fn->is_static && fn->is_inline);

    // -Wmissing-declarations / -Wmissing-prototypes for external function
    // definitions.
    if (!vm->compiler.in_type_lookahead && !fn->is_static && !fn->is_nested) {
        bool is_main =
            fn->name && strlen(fn->name) == 4 && !memcmp(fn->name, "main", 4);
        if (!is_main) {
            if ((vm->compiler.warnings & CCCC_WARN_MISSING_DECLARATIONS) &&
                !had_prior_decl && !fn->is_inline)
                warn_tok(vm, ty->name, CCCC_WARN_MISSING_DECLARATIONS,
                         "no previous declaration for '%s'", fn->name);
            if ((vm->compiler.warnings & CCCC_WARN_MISSING_PROTOTYPES) &&
                !had_full_proto)
                warn_tok(vm, ty->name, CCCC_WARN_MISSING_PROTOTYPES,
                         "no previous prototype for '%s'", fn->name);
        }
    }

    vm->compiler.current_fn = fn;
    vm->compiler.locals     = NULL;
    if (is_nested)
        vm->compiler.fn_nesting_depth++;

    enter_scope(vm);

    // K&R declaration-list: type declarations between ')' and '{' that give
    // explicit types to the parameter names.  Update ty->params *before*
    // create_param_lvars so that stack-slot sizes are derived from the correct
    // types (e.g. a double param must get an 8-byte slot, not a 4-byte int
    // slot).
    if ((vm->compiler.warnings & CCCC_WARN_OLD_STYLE_DEFINITION) &&
        !equal(tok, "{") && tok->kind != TK_EOF && is_typename(vm, tok))
        warn_tok(vm, fn->tok, CCCC_WARN_OLD_STYLE_DEFINITION,
                 "old-style (K&R) function definition");
    while (!equal(tok, "{") && tok->kind != TK_EOF && is_typename(vm, tok)) {
        VarAttr knr_attr = {};
        Type   *basety   = declspec(vm, &tok, tok, &knr_attr);
        bool    first    = true;
        for (;;) {
            if (!first)
                tok = skip(vm, tok, ",");
            first         = false;
            Type *decl_ty = declarator(vm, &tok, tok, basety);
            if (decl_ty->name) {
                char *pname = decl_ty->name->loc;
                int   plen  = decl_ty->name->len;
                for (Type *p = ty->params; p; p = p->next) {
                    if (p->name && p->name->len == plen &&
                        !memcmp(p->name->loc, pname, plen)) {
                        Token *saved_name = p->name;
                        Type  *saved_next = p->next;
                        *p                = *decl_ty;
                        p->name           = saved_name;
                        p->next           = saved_next;
                        break;
                    }
                }
            }
            if (!consume(vm, &tok, tok, ","))
                break;
        }
        tok = skip(vm, tok, ";");
    }

    validate_main_signature(vm, fn);
    create_param_lvars(vm, ty->params);

    // Resolve checked-pointer bounds expressions for every checked param
    // (#770/#483) now that ALL params are in scope -- a bound may reference
    // a later parameter, e.g. `count(n)` before `int n`. vm->compiler.locals
    // was reset to NULL immediately before create_param_lvars() above (see
    // the assignment a few lines up), so at this point it holds exactly the
    // params just installed and nothing else.
    for (Obj *p = vm->compiler.locals; p; p = p->next)
        if (p->checked_kind != CHECKED_NONE)
            resolve_checked_bounds(vm, p);

    // For nested functions, create a hidden __static_link parameter
    // This holds a pointer to the parent function's stack frame (bp)
    // IMPORTANT: This must be added AFTER create_param_lvars because new_lvar
    // prepends to the locals list. We want __static_link to be at the HEAD of
    // the list (offset -1) to match where ENT3 spills REG_A0.
    if (is_nested) {
        new_lvar(vm, "__static_link", 13, pointer_to(vm, ty_void));
    }

    // Note: Struct/union returns are handled via return_buffer in codegen.c
    // The hidden parameter approach was incomplete (caller never provided it),
    // so it has been removed to fix variadic functions with struct returns.
    // Type *rty = ty->return_ty;
    // if ((rty->kind == TY_STRUCT || rty->kind == TY_UNION) && rty->size > 16)
    //     new_lvar(vm, "", pointer_to(rty));

    fn->params = vm->compiler.locals;

    if (ty->is_variadic)
        fn->va_area =
            new_lvar(vm, "__va_area__", 11, array_of(vm, ty_char, 136));
    fn->alloca_bottom =
        new_lvar(vm, "__alloca_size__", 15, pointer_to(vm, ty_char));

    // #1043: for a K&R-form definition, is_definition was set from
    // equal(tok, "{") back at :494/:526 -- before the K&R declaration-list
    // loop above ran, when tok was still sitting on that list's leading
    // type, not "{". Re-set it now that the body is confirmed to be next;
    // the bodyless-declaration early-out above (consume(tok, ";")) means
    // only a real definition ever reaches this point, so this can't
    // wrongly mark a prototype.
    fn->is_definition = true;
    tok               = skip(vm, tok, "{");

    // Reset cleanup-scope ancestry for this function body. The ++/-- pairs in
    // compound_stmt balance to NULL, but reset defensively in case of error
    // recovery in a prior function.
    vm->compiler.cur_cleanup_chain = NULL;

    // [https://www.sigbus.info/n1570#6.4.2.2p1] "__func__" is automatically
    // defined as a local variable containing the current function name.
    // Not available before C99 — omitting it causes a natural undeclared-
    // identifier error if used in C89 mode.
    if (vm->compiler.c_std >= CCCC_STD_C99) {
        push_scope(vm, "__func__", 8)->var = new_string_literal(
            vm, fn->name, array_of(vm, ty_char, strlen(fn->name) + 1));

        // [GNU] __FUNCTION__ is yet another name of __func__.
        push_scope(vm, "__FUNCTION__", 12)->var = new_string_literal(
            vm, fn->name, array_of(vm, ty_char, strlen(fn->name) + 1));
    }

    Token *close_brace = NULL;

    // Negative test: body is expected to fail compilation with a specific
    // error. Compile in error-collection mode and absorb all errors regardless
    // of match. A nested setjmp catches fatal error_tok() longjmps so they are
    // treated the same as recoverable errors (#615): the function body is
    // entered, any error terminates it, and the error is counted and matched
    // normally.
    TestFnRecord *neg_rec = find_neg_test_record(vm, fn->name);
    if (neg_rec) {
        bool          old_collect = vm->collect_errors;
        int           pre_count   = vm->error_count;
        CompileError *pre_tail    = vm->errors_tail;
        jmp_buf       neg_jmp_buf;
        jmp_buf      *saved_jmp_buf = vm->error_jmp_buf;
        vm->collect_errors          = true;
        vm->error_jmp_buf           = &neg_jmp_buf;

        // Save the opening '{' position so we can skip past the body if a
        // fatal longjmp fires and leaves tok stranded inside it (#615).
        Token *body_start = tok;

        if (setjmp(neg_jmp_buf) == 0) {
            fn->body = compound_stmt(vm, &tok, tok, &close_brace);
            append_implicit_return(vm, fn,
                                   close_brace ? close_brace : ty->name);
            fn->locals = vm->compiler.locals;
            leave_scope(vm);
            resolve_goto_labels(vm);
            resolve_objsize_queries(vm, fn->body);
            mark_addr_escapes(fn->body);
            propagate_checked_bounds(vm, fn);
            verify_checked_assign_bounds(vm, fn);
            // check_nonnull_flow() runs post-parse now (#688) -- see the
            // loop in parse() -- so a forward-referenced callee's summary
            // is available. This negative-test path nulls fn->body below
            // regardless, so the function is simply skipped by that loop.
        } else {
            // Fatal error_tok() fired inside the function body.  The error has
            // already been collected by error_tok(); we just need to clean up
            // scope. Advance tok past the closing '}' of the function body so
            // the outer parse loop doesn't try to re-parse tokens from inside
            // the body. Note: tok = skip(vm, tok, "{") above already consumed
            // the opening '{', so body_start is the first token *inside* the
            // body — start at depth 1.
            leave_scope(vm);
            int depth = 1;
            for (Token *t = body_start; t && t->kind != TK_EOF; t = t->next) {
                if (equal(t, "{"))
                    depth++;
                else if (equal(t, "}")) {
                    depth--;
                    if (depth == 0) {
                        tok = t->next;
                        break;
                    }
                }
            }
        }

        vm->error_jmp_buf        = saved_jmp_buf;

        CompileError *new_errors = pre_tail ? pre_tail->next : vm->errors;
        int           err_count  = vm->error_count - pre_count;
        if (err_count == 0) {
            neg_rec->neg_passed = 0;
            strncpy(neg_rec->neg_actual, "no error produced",
                    sizeof(neg_rec->neg_actual) - 1);
        } else {
            neg_rec->neg_passed = -1;
            if (new_errors && new_errors->message)
                strncpy(neg_rec->neg_actual, new_errors->message,
                        sizeof(neg_rec->neg_actual) - 1);
            // First check: if error_count operator is set, apply it
            if (neg_rec->error_count_op != CMP_NONE &&
                !apply_cmp_op_i64(neg_rec->error_count_op, err_count,
                                  neg_rec->expect_errors)) {
                snprintf(neg_rec->neg_actual, sizeof(neg_rec->neg_actual),
                         "expected error_count %s %d, got %d",
                         cmp_op_str(neg_rec->error_count_op),
                         neg_rec->expect_errors, err_count);
            } else if (!neg_rec->error_pat) {
                // expect_compile_error = true and no pattern: any error passes
                // (#615)
                neg_rec->neg_passed = 1;
            } else {
                // Second check: error message pattern
                if (neg_rec->error_pat_negate) {
                    // error != "pat": passes if NO error contains the pattern
                    bool matched = false;
                    for (CompileError *e = new_errors; e; e = e->next) {
                        if (e->message &&
                            strstr(e->message, neg_rec->error_pat)) {
                            matched = true;
                            snprintf(neg_rec->neg_actual,
                                     sizeof(neg_rec->neg_actual),
                                     "error unexpectedly matched \"%s\": %s",
                                     neg_rec->error_pat, e->message);
                            break;
                        }
                    }
                    if (!matched)
                        neg_rec->neg_passed = 1;
                } else {
                    for (CompileError *e = new_errors; e; e = e->next) {
                        if (e->message &&
                            strstr(e->message, neg_rec->error_pat)) {
                            neg_rec->neg_passed = 1;
                            strncpy(neg_rec->neg_actual, e->message,
                                    sizeof(neg_rec->neg_actual) - 1);
                            break;
                        }
                    }
                }
            }
        }

        if (pre_tail)
            pre_tail->next = NULL;
        else
            vm->errors = NULL;
        vm->errors_tail    = pre_tail;
        vm->error_count    = pre_count;
        vm->collect_errors = old_collect;

        fn->body = NULL; // suppress codegen — test result is precomputed
    } else {
        fn->body = compound_stmt(vm, &tok, tok, &close_brace);
        append_implicit_return(vm, fn, close_brace ? close_brace : ty->name);
        fn->locals = vm->compiler.locals;
        leave_scope(vm);
        resolve_goto_labels(vm);
        resolve_objsize_queries(vm, fn->body);
        mark_addr_escapes(fn->body);
        // #919: any nested function textually inside `fn` has already run
        // its own mark_nested_captures() call (inside the compound_stmt()
        // above) and set is_captured on whichever of `fn`'s own locals it
        // touches, so propagate_checked_bounds() sees a complete picture.
        propagate_checked_bounds(vm, fn);
        verify_checked_assign_bounds(vm, fn);
        // #836: mark this nested function's captures on whichever enclosing
        // function(s) they belong to, before that/those function(s)'s own
        // codegen runs prepare_local_promotion / prepare_fp_local_promotion.
        if (is_nested)
            mark_nested_captures(fn, fn->body);
        // check_nonnull_flow() runs post-parse now (#688) -- see the loop
        // in parse() -- so a caller of a later-defined function still sees
        // that callee's may-return-null summary.
    }

    // Restore parent function context if this was a nested function
    if (is_nested) {
        vm->compiler.current_fn       = parent_fn;
        vm->compiler.locals           = saved_locals;
        vm->compiler.fn_nesting_depth = saved_nesting_depth;
        vm->compiler.objsize_queries  = saved_objsize_queries;
    } else {
        // CRITICAL: Reset current_fn to NULL for top-level functions!
        // Otherwise the next top-level function will incorrectly think it's
        // nested.
        vm->compiler.current_fn = NULL;
    }

    run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_FUNCTION, fn->name, fn->ty,
                          fn, fn->tok);

    return tok;
}

// #957: fold a redeclaration of an existing global variable (`prev`) into
// it, rather than creating a second Obj. Every rule below only strengthens
// prev's state -- never clears something a prior declaration established --
// because C allows any order of `extern`/definition/tentative redeclarations
// and each one only adds information. Does not touch prev->init_data/rel or
// is_tentative; the caller runs gvar_initializer (which may write init_data)
// and sets/clears is_tentative itself, since those depend on tok, not on
// this declaration's attr/ty alone. Does not check the redefinition case
// either -- the caller must reject `equal(tok,"=") && prev->init_data`
// before calling this, since by the time this runs prev's fields have
// already started changing.
static void merge_global_decl(VirtualMachine *vm, Obj *prev, Type *ty,
                              VarAttr *attr, Token *name_tok) {
    bool is_def_now = !attr->is_extern;

    if (is_def_now)
        prev->is_definition = true;
    if (attr->is_static)
        prev->is_static = true;
    if (attr->is_tls)
        prev->is_tls = true;

    if (attr->is_constexpr) {
        if (attr->is_extern || attr->is_tls)
            error_tok(
                vm, name_tok,
                "constexpr object must be a definition with internal storage");
        prev->is_constexpr = true;
        prev->is_static    = true;
    }

    if (attr->align > prev->align)
        prev->align = attr->align;

    // Definition wins outright; otherwise an incomplete array type adopts a
    // later declaration's complete size (`extern int a[]; extern int a[5];`)
    // so the data-segment allocation loop (which sizes the slot from
    // var->ty->size) gets the real size instead of the first-seen one.
    if (is_def_now || (prev->ty->kind == TY_ARRAY && prev->ty->size < 0 &&
                       ty->kind == TY_ARRAY && ty->size >= 0))
        prev->ty = ty;

    if (ty->asm_label) {
        if (!prev->asm_label)
            prev->asm_label = ty->asm_label;
        else if (strcmp(prev->asm_label, ty->asm_label) != 0)
            error_tok(vm, name_tok, "conflicting asm label for '%s'",
                      prev->name);
    }

    if (ty->checked_kind != CHECKED_NONE) {
        prev->checked_kind        = ty->checked_kind;
        prev->checked_bounds_form = ty->checked_bounds_form;
        resolve_checked_bounds(vm, prev);
    }

    prev->is_maybe_unused |= ty->is_maybe_unused;
    prev->is_deprecated   |= ty->is_deprecated;
    if (!prev->deprecated_msg)
        prev->deprecated_msg = ty->deprecated_msg;

    if (is_def_now)
        prev->tok = name_tok;
}

// True if ty is a VLA, or a pointer/array chain that bottoms out at one
// (e.g. `int (*p)[n]` at file scope). C11 6.7.6.2p4/6.9.2p3: an object with
// variably modified type must have block scope and no linkage -- a file-
// scope VLA is a constraint violation, not something CCCC should silently
// accept (it previously did; `sizeof` on such a global then read an
// unresolved/zero vla_size and crashed the compiler, see the test added
// alongside this check).
static bool type_has_vla(Type *ty) {
    if (!ty)
        return false;
    if (ty->kind == TY_VLA)
        return true;
    if (ty->kind == TY_PTR || ty->kind == TY_ARRAY)
        return type_has_vla(ty->base);
    return false;
}

Token *global_variable(VirtualMachine *vm, Token *tok, Type *basety,
                       VarAttr *attr) {
    bool first = true;

    while (!consume(vm, &tok, tok, ";")) {
        if (!first)
            tok = skip(vm, tok, ",");
        first    = false;

        Type *ty = declarator(vm, &tok, tok, basety);
        if (!ty->name)
            error_tok(vm, ty->name_pos, "variable name omitted");

        char *var_name     = get_ident(vm, ty->name);
        int   var_name_len = (int)ty->name->len;

        if (type_has_vla(ty))
            error_tok(vm, ty->name, "variably modified '%s' at file scope",
                      var_name);

        // C23 auto type inference for global variables
        if (attr->is_auto) {
            if (attr->is_extern)
                error_tok(vm, ty->name, "cannot use 'auto' with 'extern'");
            int decl_depth = count_auto_ptr_depth(ty);
            if (decl_depth < 0)
                error_tok(
                    vm, ty->name,
                    "cannot use 'auto' with array or function declarator");
            if (!equal(tok, "="))
                error_tok(vm, ty->name,
                          "declaration of variable '%.*s' with deduced type "
                          "'auto' requires an initializer",
                          (int)ty->name->len, ty->name->loc);
            if (equal(tok->next, "{"))
                error_tok(vm, tok->next, "cannot use 'auto' with array in C");

            // Probe: parse the initializer expression to infer the type
            Token *eq_tok    = tok;
            Token *probe_tok = tok->next;
            Node  *probe     = assign(vm, &probe_tok, probe_tok);
            add_type(vm, probe);
            Type *deduced = auto_deduced_type(vm, probe->ty);

            if (count_ptr_depth(deduced) != decl_depth) {
                char stars[16] = "";
                for (int i = 0; i < decl_depth && i < 15; i++)
                    stars[i] = '*';
                error_tok(vm, ty->name,
                          "variable '%.*s' with type 'auto%s%s' has "
                          "incompatible initializer",
                          (int)ty->name->len, ty->name->loc,
                          decl_depth > 0 ? " " : "", stars);
            }

            Obj *prev = hashmap_get(&vm->compiler.global_decl_map, var_name);
            Obj *var;
            if (prev) {
                if (prev->init_data)
                    error_tok(vm, ty->name, "redefinition of '%s'", var_name);
                merge_global_decl(vm, prev, deduced, attr, ty->name);
                push_scope(vm, var_name, var_name_len)->var = prev;
                var                                         = prev;
            } else {
                var            = new_gvar(vm, var_name, var_name_len, deduced);
                var->is_static = attr->is_static;
                if (attr->align)
                    var->align = attr->align;
                if (var->checked_kind != CHECKED_NONE)
                    resolve_checked_bounds(vm, var);
                hashmap_put(&vm->compiler.global_decl_map, var_name, var);
            }
            var->is_definition = true;

            // Re-parse from eq_tok to write init_data correctly
            gvar_initializer(vm, &tok, eq_tok->next, var);
            var->is_tentative = false;

            run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_GLOBAL, var->name,
                                  var->ty, var, var->tok);
            continue;
        }

        // For extern declarations, check whether a macro-generated global
        // already exists in macro_globals.  If so, register it in
        // global_decl_map (so later declarations in this TU canonicalize
        // onto it via the ordinary path below) and push it into scope
        // directly so code referencing this name uses the same Obj that
        // will have init_data and the correct data-segment offset in
        // codegen. (Global variable references are offset-based, unlike
        // function calls which are patched by name, so both the scope and
        // codegen must agree on the same Obj.)
        if (attr->is_extern && vm->compiler.macro_globals &&
            !hashmap_get(&vm->compiler.global_decl_map, var_name)) {
            Obj *mg = NULL;
            for (Obj *o = vm->compiler.macro_globals; o; o = o->next) {
                if (!o->is_function && (int)strlen(o->name) == var_name_len &&
                    strncmp(o->name, var_name, var_name_len) == 0) {
                    mg = o;
                    break;
                }
            }
            if (mg) {
                hashmap_put(&vm->compiler.global_decl_map, var_name, mg);
                push_scope(vm, var_name, var_name_len)->var = mg;
                if (equal(tok, "="))
                    gvar_initializer(vm, &tok, tok->next, mg);
                run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_GLOBAL,
                                      mg->name, mg->ty, mg, mg->tok);
                continue;
            }
        }

        // #957: -Wredundant-decls must be evaluated against the pre-merge
        // state (find_var, a scope-visibility question, not a linkage
        // question) -- evaluating it after merge_global_decl below would
        // always see is_definition already folded in and never fire.
        VarScope *previous = find_var(vm, ty->name);
        if (previous && previous->var && !previous->var->is_function &&
            !previous->var->is_definition && attr->is_extern &&
            (vm->compiler.warnings & CCCC_WARN_REDUNDANT_DECLS))
            warn_tok(vm, ty->name, CCCC_WARN_REDUNDANT_DECLS,
                     "redundant redeclaration of '%s'", var_name);

        // #957: canonicalize by name within this translation unit so every
        // reference (offset-based, not name-patched -- see gen_addr) lands
        // on one Obj. See merge_global_decl() for the field-merge rules.
        Obj *prev = hashmap_get(&vm->compiler.global_decl_map, var_name);
        Obj *var;
        if (prev) {
            if (equal(tok, "=") && prev->init_data)
                error_tok(vm, ty->name, "redefinition of '%s'", var_name);
            merge_global_decl(vm, prev, ty, attr, ty->name);
            push_scope(vm, var_name, var_name_len)->var = prev;
            var                                         = prev;
        } else {
            var                = new_gvar(vm, var_name, var_name_len, ty);
            var->is_definition = !attr->is_extern;
            var->is_static     = attr->is_static;
            var->is_tls        = attr->is_tls;
            var->is_constexpr  = attr->is_constexpr;
            if (attr->align)
                var->align = attr->align;
            if (var->checked_kind != CHECKED_NONE)
                resolve_checked_bounds(vm, var);

            if (var->is_constexpr) {
                if (attr->is_extern || attr->is_tls)
                    error_tok(vm, ty->name,
                              "constexpr object must be a definition with "
                              "internal storage");
                var->is_static = true;
            }
            hashmap_put(&vm->compiler.global_decl_map, var_name, var);
        }

        if (equal(tok, "="))
            gvar_initializer(vm, &tok, tok->next, var);
        else if (var->is_constexpr)
            error_tok(vm, ty->name, "constexpr object requires an initializer");
        else if (!attr->is_extern && !attr->is_tls && !var->init_data)
            var->is_tentative = true;

        run_decl_custom_attrs(vm, ty, attr, ATTR_TARGET_GLOBAL, var->name,
                              var->ty, var, var->tok);
    }
    return tok;
}

// Lookahead tokens and returns true if a given token is a start
// of a function definition or declaration.
bool is_function(VirtualMachine *vm, Token *tok, Type *basety) {
    if (equal(tok, ";"))
        return false;

    Type dummy                     = {};
    bool saved_lookahead           = vm->compiler.in_type_lookahead;
    vm->compiler.in_type_lookahead = true;
    Type *ty =
        declarator(vm, &tok, tok, basety ? copy_type(vm, basety) : &dummy);
    vm->compiler.in_type_lookahead = saved_lookahead;
    return ty->kind == TY_FUNC;
}

bool is_function_decl_list(VirtualMachine *vm, Token *tok, Type *basety) {
    Type dummy                     = {};
    bool saved_lookahead           = vm->compiler.in_type_lookahead;
    vm->compiler.in_type_lookahead = true;
    Type *ty =
        declarator(vm, &tok, tok, basety ? copy_type(vm, basety) : &dummy);
    vm->compiler.in_type_lookahead = saved_lookahead;
    return ty->kind == TY_FUNC && equal(tok, ",");
}

// Remove redundant tentative definitions.
static void scan_globals(VirtualMachine *vm) {
    Obj  head;
    Obj *cur = &head;

    for (Obj *var = vm->compiler.globals; var; var = var->next) {
        if (!var->is_tentative) {
            cur = cur->next = var;
            continue;
        }

        // Find another definition of the same identifier.
        Obj *var2 = vm->compiler.globals;
        for (; var2; var2 = var2->next)
            if (var != var2 && var2->is_definition &&
                strlen(var->name) == strlen(var2->name) &&
                strncmp(var->name, var2->name, strlen(var2->name)) == 0)
                break;

        // If there's another definition, the tentative definition
        // is redundant
        if (!var2)
            cur = cur->next = var;
    }

    cur->next            = NULL;
    vm->compiler.globals = head.next;
}

static void warn_unused_globals(VirtualMachine *vm) {
    for (Obj *var = vm->compiler.globals; var; var = var->next) {
        if (!var->is_static || !var->is_definition || var->is_local_symbol ||
            var->is_macro_generated || !var->tok || var->is_used ||
            var->is_maybe_unused)
            continue;

        bool already_checked    = false;
        bool redeclaration_used = false;
        for (Obj *other = vm->compiler.globals; other; other = other->next) {
            if (other == var)
                already_checked = true;
            if (other == var || other->is_function != var->is_function ||
                strcmp(other->name, var->name) != 0)
                continue;
            if (!already_checked)
                redeclaration_used = true;
            if (other->is_used || other->is_maybe_unused)
                redeclaration_used = true;
        }
        if (redeclaration_used)
            continue;

        warn_tok(vm, var->tok, CCCC_WARN_UNUSED, "unused %s '%s'",
                 var->is_function ? "function" : "variable",
                 obj_display_name(var));
    }
}

static void declare_builtin_functions(VirtualMachine *vm) {
    // alloca(size) -> void*
    Type *ty                    = func_type(vm, pointer_to(vm, ty_void));
    ty->params                  = copy_type(vm, ty_int);
    vm->compiler.builtin_alloca = new_gvar(vm, "alloca", 6, ty);
    vm->compiler.builtin_alloca->is_definition = false;
    // Mark with a stable flag: codegen identifies VLA-lowered alloca calls by
    // this flag rather than pointer identity, because declare_builtin_functions
    // re-runs on every parse() (incl. the macro-expansion re-parse) and would
    // otherwise leave AST nodes pointing at a stale builtin_alloca Obj (#588).
    vm->compiler.builtin_alloca->is_builtin_alloca = true;

    // strlen(s) -> long  (private stub; not in global scope so it doesn't
    // conflict with user redeclarations like `int strcmp(const char *s)`)
    Type *strlen_ty             = func_type(vm, ty_long);
    strlen_ty->params           = pointer_to(vm, ty_char);
    vm->compiler.builtin_strlen = new_private_func_obj(vm, "strlen", strlen_ty);

    // strcmp(a, b) -> int  (private stub; same rationale as strlen above)
    Type *strcmp_ty             = func_type(vm, ty_int);
    strcmp_ty->params           = pointer_to(vm, ty_char);
    strcmp_ty->params->next     = pointer_to(vm, ty_char);
    vm->compiler.builtin_strcmp = new_private_func_obj(vm, "strcmp", strcmp_ty);

    // #1144: __builtin_memset/memcpy/memmove/memcmp -- GCC/clang recognise
    // these as ordinary aliases of the libc functions of the same name
    // (minus the __builtin_ prefix), forwarded here the same way
    // __builtin_strlen/__builtin_strcmp already are: a private stub Obj,
    // not in global scope, so it doesn't conflict with (or require) the
    // user's own <string.h> declaration for the VM path. Signatures mirror
    // include/string.h's own memset/memcpy/memmove/memcmp exactly. #1154
    // fixed the matching -c=native/-m/-c=generated gap: serialize_expr.c's
    // ND_VAR case now prints these (and strlen/strcmp above) using their
    // literal __builtin_ spelling, the same way it already did for
    // builtin_alloca, so no native declaration is ever needed either.
    Type *memset_ty               = func_type(vm, pointer_to(vm, ty_void));
    memset_ty->params             = pointer_to(vm, ty_void);
    memset_ty->params->next       = copy_type(vm, ty_int);
    memset_ty->params->next->next = copy_type(vm, ty_long);
    vm->compiler.builtin_memset = new_private_func_obj(vm, "memset", memset_ty);

    // Each `const void *` param below copies ty_void before marking it
    // const (matching the const_char_p pattern just below, __cccc_pc_to_
    // source's file_param) -- pointer_to()'s ->base is the shared ty_void
    // singleton; setting is_const on it directly would corrupt every
    // plain `void` in the rest of the compile (confirmed: without the
    // copy, `void plain_ctor(void)` reserialized as `const void
    // plain_ctor(void)` under -c=native's own ctor/dtor smoke test).
    Type *memcpy_ty         = func_type(vm, pointer_to(vm, ty_void));
    memcpy_ty->params       = pointer_to(vm, ty_void);
    memcpy_ty->params->next = pointer_to(vm, copy_type(vm, ty_void));
    memcpy_ty->params->next->base->is_const = true;
    memcpy_ty->params->next->next           = copy_type(vm, ty_long);
    vm->compiler.builtin_memcpy = new_private_func_obj(vm, "memcpy", memcpy_ty);

    Type *memmove_ty            = func_type(vm, pointer_to(vm, ty_void));
    memmove_ty->params          = pointer_to(vm, ty_void);
    memmove_ty->params->next    = pointer_to(vm, copy_type(vm, ty_void));
    memmove_ty->params->next->base->is_const = true;
    memmove_ty->params->next->next           = copy_type(vm, ty_long);
    vm->compiler.builtin_memmove =
        new_private_func_obj(vm, "memmove", memmove_ty);

    Type *memcmp_ty                   = func_type(vm, ty_int);
    memcmp_ty->params                 = pointer_to(vm, copy_type(vm, ty_void));
    memcmp_ty->params->base->is_const = true;
    memcmp_ty->params->next           = pointer_to(vm, copy_type(vm, ty_void));
    memcmp_ty->params->next->base->is_const = true;
    memcmp_ty->params->next->next           = copy_type(vm, ty_long);
    vm->compiler.builtin_memcmp = new_private_func_obj(vm, "memcmp", memcmp_ty);

    // __cccc_pc_to_name(void *pc) -> const char*
    // Private stub for __builtin_pc_function_name — maps a VM bytecode offset
    // (returned by __builtin_return_address) to the enclosing function name.
    Type *pc_to_name_ty = func_type(vm, pointer_to(vm, copy_type(vm, ty_char)));
    pc_to_name_ty->return_ty->base->is_const = true; // const char *
    pc_to_name_ty->params                    = pointer_to(vm, ty_void);
    vm->compiler.builtin_pc_to_name =
        new_private_func_obj(vm, "__cccc_pc_to_name", pc_to_name_ty);

    // __cccc_pc_to_source(void *pc, const char **file, int *line) -> int
    // Private stub for __builtin_pc_source_location.
    Type *pc_to_src_ty = func_type(vm, ty_int);
    Type *pc_param     = pointer_to(vm, ty_void);
    // const char **:  pointer to (const char *)
    Type *const_char_p           = pointer_to(vm, copy_type(vm, ty_char));
    const_char_p->base->is_const = true;
    Type *file_param             = pointer_to(vm, const_char_p);
    Type *line_param             = pointer_to(vm, ty_int);
    pc_to_src_ty->params         = pc_param;
    pc_param->next               = file_param;
    file_param->next             = line_param;
    vm->compiler.builtin_pc_to_source =
        new_private_func_obj(vm, "__cccc_pc_to_source", pc_to_src_ty);

    // setjmp(jmp_buf) -> int
    // jmp_buf is an array type, but we'll treat it as a pointer for now.
    // The pointee type here (long) is just the VM-side parameter shape;
    // it does not need to (and, under -c=native, must not -- see #1054)
    // agree with the real host jmp_buf ABI. serialize_type.c suppresses the
    // implicit cast to `long *` at each of these four call sites and
    // prints `(void *)` instead, so the emitted C never claims the arg is
    // a `long *` there. jmp_buf itself is include/setjmp.h's
    // `long long[40]` -- sized to cover every supported host's real
    // sizeof(jmp_buf) (see that header's comment).
    Type *setjmp_ty             = func_type(vm, ty_int);
    setjmp_ty->params           = pointer_to(vm, ty_long);
    vm->compiler.builtin_setjmp = new_gvar(vm, "setjmp", 6, setjmp_ty);
    vm->compiler.builtin_setjmp->is_definition = false;

    // longjmp(jmp_buf, int) -> void (noreturn)
    Type *longjmp_ty             = func_type(vm, ty_void);
    longjmp_ty->params           = pointer_to(vm, ty_long);
    longjmp_ty->params->next     = copy_type(vm, ty_int);
    vm->compiler.builtin_longjmp = new_gvar(vm, "longjmp", 7, longjmp_ty);
    vm->compiler.builtin_longjmp->is_definition = false;

    // _setjmp/_longjmp: POSIX variants without signal-mask save/restore.
    // In the cccc VM there is no signal mask, so these are identical builtins.
    vm->compiler.builtin__setjmp = new_gvar(vm, "_setjmp", 7, setjmp_ty);
    vm->compiler.builtin__setjmp->is_definition = false;
    vm->compiler.builtin__longjmp = new_gvar(vm, "_longjmp", 8, longjmp_ty);
    vm->compiler.builtin__longjmp->is_definition = false;

    Type *dlopen_ty             = func_type(vm, pointer_to(vm, ty_void));
    dlopen_ty->params           = pointer_to(vm, ty_char);
    dlopen_ty->params->next     = copy_type(vm, ty_int);
    vm->compiler.builtin_dlopen = new_gvar(vm, "dlopen", 6, dlopen_ty);
    vm->compiler.builtin_dlopen->is_definition = false;

    Type *dlsym_ty             = func_type(vm, pointer_to(vm, ty_void));
    dlsym_ty->params           = pointer_to(vm, ty_void);
    dlsym_ty->params->next     = pointer_to(vm, ty_char);
    vm->compiler.builtin_dlsym = new_gvar(vm, "dlsym", 5, dlsym_ty);
    vm->compiler.builtin_dlsym->is_definition = false;

    Type *dlclose_ty                          = func_type(vm, ty_int);
    dlclose_ty->params                        = pointer_to(vm, ty_void);
    vm->compiler.builtin_dlclose = new_gvar(vm, "dlclose", 7, dlclose_ty);
    vm->compiler.builtin_dlclose->is_definition = false;

    Type *dlerror_ty             = func_type(vm, pointer_to(vm, ty_char));
    vm->compiler.builtin_dlerror = new_gvar(vm, "dlerror", 7, dlerror_ty);
    vm->compiler.builtin_dlerror->is_definition = false;

    // signal(int sig, void (*func)(int)) -> void (*)(int)
    Type *signal_handler_ty   = func_type(vm, ty_void);
    signal_handler_ty->params = copy_type(vm, ty_int);
    Type *signal_ty         = func_type(vm, pointer_to(vm, signal_handler_ty));
    signal_ty->params       = copy_type(vm, ty_int);
    signal_ty->params->next = pointer_to(vm, signal_handler_ty);
    vm->compiler.builtin_signal = new_gvar(vm, "signal", 6, signal_ty);
    vm->compiler.builtin_signal->is_definition = false;

    // raise(int sig) -> int
    Type *raise_ty             = func_type(vm, ty_int);
    raise_ty->params           = copy_type(vm, ty_int);
    vm->compiler.builtin_raise = new_gvar(vm, "raise", 5, raise_ty);
    vm->compiler.builtin_raise->is_definition = false;

    // __cccc_block_copy_impl(void *desc) -> void*
    // Internal helper backing the Block_copy() extension; resolved to the
    // host cfunc registered in the FFI table by register_stdlib_functions.
    // Declared as a global prototype so Block_copy's parser lookup finds it.
    Type *block_copy_ty   = func_type(vm, pointer_to(vm, ty_void));
    block_copy_ty->params = pointer_to(vm, ty_void);
    vm->compiler.builtin_block_copy =
        new_gvar(vm, "__cccc_block_copy_impl", 22, block_copy_ty);
    vm->compiler.builtin_block_copy->is_function   = true;
    vm->compiler.builtin_block_copy->is_definition = false;

    // free(void*) -> void
    // Ensures Block_release always resolves even when <stdlib.h> is not
    // included (#458).  Named "free" so codegen's is_extern_func_name("free")
    // check routes it to MFRE (CCCC_VM_HEAP) or the host free() via FFI.
    // If the TU later declares its own free prototype the parser will find the
    // user declaration in globals first (it's prepended), shadowing this one.
    Type *free_ty                            = func_type(vm, ty_void);
    free_ty->params                          = pointer_to(vm, ty_void);
    vm->compiler.builtin_free                = new_gvar(vm, "free", 4, free_ty);
    vm->compiler.builtin_free->is_function   = true;
    vm->compiler.builtin_free->is_definition = false;
}

// Parse file-scope declarations (typedef | function-definition |
// global-variable | static_assert) from `tok` until TK_EOF, into whatever
// scope and vm->compiler.globals list the caller has already set up. Does
// NOT enter/leave scope, reset globals, or run any post-parse passes -- it
// is the shared unit of work behind parse()'s top-level loop, REPL top-level
// declarations (cc_parse_repl_unit), and demand-driven comptime declaration
// splicing (splice_comptime_decl, #894).
static Token *parse_file_scope_decls(VirtualMachine *vm, Token *tok) {
    while (tok->kind != TK_EOF) {
        // _Static_assert or static_assert (C23) - check before declspec
        if (equal(tok, "_Static_assert") || equal(tok, "static_assert")) {
            Token *decl_tok = tok;
            Node  *cond     = NULL;
            char  *msg      = NULL;
            int    msg_len  = 0;
            tok = static_assert_decl(vm, tok, &cond, &msg, &msg_len);
            // #1098: file scope leaves no Node to stash onto (unlike the
            // block-scope arm in stmt()), so keep it in its own list on
            // Compiler for serialize_program.c to re-emit -- see
            // StaticAssertRecord's own comment.
            StaticAssertRecord *rec     = calloc(1, sizeof(StaticAssertRecord));
            rec->cond                   = cond;
            rec->msg                    = msg;
            rec->msg_len                = msg_len;
            rec->tok                    = decl_tok;
            rec->next                   = vm->compiler.static_asserts;
            vm->compiler.static_asserts = rec;
            continue;
        }

        // File-scope macro calls to non-inline macros are executed pre-parse
        // by cc_execute_inline_macros and their tokens are removed. If the
        // parser still sees one, it had arguments or was missed; skip it.
        // File-scope macro calls are executed pre-parse; skip any that remain.
        if (!vm->compiler.in_macro_mode && tok->kind == TK_IDENT &&
            equal(tok->next, "(")) {
            MacroFn *pm = find_macro_fn(vm, tok);
            if (pm) {
                // Skip the call tokens (was executed pre-parse).
                // Walk to matching ')' respecting nesting.
                tok       = tok->next->next; // after '('
                int depth = 1;
                while (tok && tok->kind != TK_EOF && depth > 0) {
                    if (equal(tok, "("))
                        depth++;
                    else if (equal(tok, ")")) {
                        depth--;
                        if (depth == 0)
                            break;
                    }
                    tok = tok->next;
                }
                tok = skip(vm, tok, ")");
                tok = skip(vm, tok, ";");
                continue;
            }
        }

        VarAttr attr   = {};
        Type   *basety = declspec(vm, &tok, tok, &attr);

        // Typedef
        if (attr.is_typedef) {
            tok = parse_typedef(vm, tok, basety, &attr);
            continue;
        }

        // Function
        if (is_function(vm, tok, basety)) {
            tok = is_function_decl_list(vm, tok, basety)
                      ? function_declaration_list(vm, tok, basety, &attr)
                      : function(vm, tok, basety, &attr);
            continue;
        }

        // File-scope type declaration such as @serialize struct Point { ... };
        if (equal(tok, ";")) {
            if (has_custom_attrs(basety, &attr)) {
                char *name = NULL;
                if (basety->name)
                    name =
                        arena_strndup(vm, basety->name->loc, basety->name->len);
                run_decl_custom_attrs(vm, basety, &attr, ATTR_TARGET_TYPE, name,
                                      basety, NULL, basety->name);
            }
            tok = tok->next;
            continue;
        }

        // Global variable
        tok = global_variable(vm, tok, basety, &attr);
    }
    return tok;
}

// #894: parse an already-materialized, EOF-terminated file-scope declaration
// (produced by the comptime declaration index's splice path in macros.c)
// into the comptime program's own file scope
// (vm->compiler.macro_file_scope), regardless of where in the comptime
// parse the miss that triggered this happened.
//
// Contained: a malformed splice (most plausibly reachable only via
// --comptime-include-all widening the index into an exotic system-header
// declaration cccc can't parse in isolation) degrades to returning false --
// an ordinary "not found" at the use site -- instead of the longjmp/exit an
// uncontained error_tok()/error_at() would perform out of the whole
// compile. Not fully transactional: a declaration that fails partway
// through may leave an inert (never looked up again) partial Obj in
// vm->compiler.globals or a partial scope registration. Accepted as a
// bounded, one-shot cost -- the caller marks the failing entry CD_FAILED
// and never retries it -- rather than adding a full scope/globals
// snapshot-restore for what should be a rare path.
bool cc_parse_splice_range(VirtualMachine *vm, Token *tok) {
    Obj     *saved_locals        = vm->compiler.locals;
    Obj     *saved_current_fn    = vm->compiler.current_fn;
    Scope   *saved_scope         = vm->compiler.scope;
    bool     saved_lookahead     = vm->compiler.in_type_lookahead;
    bool     saved_splice_active = vm->compiler.comptime_splice_active;
    uint64_t saved_warnings      = vm->compiler.warnings;
    uint64_t saved_werror        = vm->compiler.warning_errors;
    int      saved_error_count   = vm->error_count;
    jmp_buf *saved_jmp           = vm->error_jmp_buf;
    jmp_buf  local;

    vm->compiler.locals     = NULL;
    vm->compiler.current_fn = NULL;
    if (vm->compiler.macro_file_scope)
        vm->compiler.scope = vm->compiler.macro_file_scope;
    // #894: a splice can be triggered two ways -- during the comptime
    // *parse* (is_typename/find_tag/primary()'s hooks, in_macro_mode
    // already true) or during comptime *execution*, well after
    // compile_macro_program reset in_macro_mode to false (reflection.c's
    // GetType()/VarRef()/FindGlobal(), via cc_comptime_resolve_type_name/
    // _value_name). Either way, the declaration being spliced in here must
    // itself parse AS IF still inside the comptime parse -- e.g. a struct
    // member naming another not-yet-spliced tag needs find_tag()'s own hook
    // to fire recursively. comptime_splice_active (not in_macro_mode itself
    // -- see its declaration, src/cccc.h) is what those hooks check.
    vm->compiler.comptime_splice_active = true;
    // Several is_typename() call sites probe in a lookahead-suppressed mode
    // (sizeof, cast, _Alignof, _Generic); a splice triggered from inside one
    // must not inherit that suppression, or the spliced declaration itself
    // would silently skip real work.
    vm->compiler.in_type_lookahead = false;
    // Suppress warnings for the duration -- a spliced header declaration
    // (e.g. -Wredundant-decls on a struct also visible via @shared) is not
    // something the user can act on from here.
    vm->compiler.warnings       = 0;
    vm->compiler.warning_errors = 0;
    vm->error_jmp_buf           = &local;

    bool ok;
    if (setjmp(local) == 0) {
        parse_file_scope_decls(vm, tok);
        ok = (vm->error_count == saved_error_count);
    } else {
        ok = false;
    }

    vm->error_jmp_buf                   = saved_jmp;
    vm->compiler.warnings               = saved_warnings;
    vm->compiler.warning_errors         = saved_werror;
    vm->compiler.in_type_lookahead      = saved_lookahead;
    vm->compiler.comptime_splice_active = saved_splice_active;
    vm->compiler.scope                  = saved_scope;
    vm->compiler.current_fn             = saved_current_fn;
    vm->compiler.locals                 = saved_locals;
    return ok;
}

// program = (typedef | function-definition | global-variable)*
Obj *parse(VirtualMachine *vm, Token *tok) {
    // Initialize error recovery placeholder
    vm->compiler.error_var.name = "<error>";
    vm->compiler.error_var.ty   = ty_error;

    // Initialize global scope
    enter_scope(vm);

    // #894: remember the comptime program's own file scope so a
    // demand-driven splice (splice_comptime_decl, below) can land its
    // declaration there even when the miss that triggered it happened deep
    // inside a comptime function body's block scope.
    if (vm->compiler.in_macro_mode)
        vm->compiler.macro_file_scope = vm->compiler.scope;

    declare_builtin_functions(vm);
    vm->compiler.globals = NULL;
    // #957: reset the per-TU global-declaration canonicalization map. Must
    // happen here, not just once at vm init, because main.c reuses one vm
    // across multiple input files (parse() runs once per TU) -- a stale
    // entry from a previous TU would otherwise alias an unrelated Obj by
    // name across TUs, which is exactly the cross-module hazard
    // cc_link_progs (not this map) is responsible for.
    hashmap_deinit(&vm->compiler.global_decl_map);
    memset(&vm->compiler.global_decl_map, 0,
           sizeof(vm->compiler.global_decl_map));

    tok = parse_file_scope_decls(vm, tok);

    for (Obj *var = vm->compiler.globals; var; var = var->next)
        if (var->is_root)
            mark_live(vm, var);

    // Remove redundant tentative definitions.
    scan_globals(vm);
    warn_unused_globals(vm);

    // #688: run the flow-sensitive -Wnonnull/-Wmaybe-nonnull pass here,
    // post-parse over every function, rather than inline as each
    // definition finishes (the old #679/#687 scheme). This is what lets a
    // caller see the may-return-null summary of a callee defined *later*
    // in the translation unit -- summaries must be computed first so every
    // check_nonnull_flow() call below sees a complete picture regardless
    // of source order.
    check_may_return_null_summaries(vm);
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next)
        if (fn->is_function && fn->body)
            check_nonnull_flow(vm, fn);

    return vm->compiler.globals;
}

// #1001: parse() itself is called from two very different contexts --
// once per command-line input file, from main.c's parse loop via
// cc_parse() (linker.c), and once per *synthetic comptime program*, from
// compile_macro_program() (macros.c). The latter deliberately relies on
// the runtime TU's own file scope still being reachable when it does its
// own enter_scope() -- a macro function's body can reference a runtime
// global via the `$identifier` reflect operator (parse.c's primary(),
// resolved through find_var() at parse time), and compile_macro_program
// runs after every real TU has already been parsed (cc_expand_macros,
// called from main.c after cc_link_progs). parse() itself therefore must
// not leave_scope() unconditionally, and neither can cc_parse() do it
// unconditionally after every call -- only *between* two real TUs, never
// after the last one. Exposed here as a separate step main.c's parse loop
// calls directly, once per boundary between two command-line input files
// (never before the first, never after the last), rather than baked into
// parse() or cc_parse() where it would run at the wrong times for either
// caller.
//
// Before this fix, parse() enter_scope()'d once per translation unit with
// no matching leave_scope() at all -- documented at struct Compiler's
// primary_file comment as a known hazard -- so every TU's file scope
// stacked on the previous one and find_var() (parse_core.c) walked straight
// through the whole chain: a second command-line input file could resolve
// a first file's typedef, tag, or file-scope variable with no #include at
// all. This is what let the include-guard half of #1001 go unnoticed for
// so long -- an already-guarded header re-#include'd by a later TU emits
// nothing new, but that TU's own declarations were still visible via the
// leaked scope chain, silently masking the guard leak.
void cc_leave_top_file_scope(VirtualMachine *vm) {
    if (vm->compiler.scope)
        leave_scope(vm);
}

// Exposed parsing functions for K's ast_parse API
Node *cc_parse_expr(VirtualMachine *vm, Token **rest, Token *tok) {
    return expr(vm, rest, tok);
}

Node *cc_parse_assign(VirtualMachine *vm, Token **rest, Token *tok) {
    return assign(vm, rest, tok);
}

Node *cc_parse_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    return stmt(vm, rest, tok);
}

Node *cc_parse_compound_stmt(VirtualMachine *vm, Token **rest, Token *tok) {
    return compound_stmt(vm, rest, tok, NULL);
}

int64_t cc_eval(VirtualMachine *vm, Node *node) {
    return eval(vm, node);
}

double cc_eval_double(VirtualMachine *vm, Node *node) {
    return eval_double(vm, node);
}

void cc_init_parser(VirtualMachine *vm) {
    vm->compiler.error_var.name = "<error>";
    vm->compiler.error_var.ty   = ty_error;
}

// ---------------------------------------------------------------------
// REPL support (ticket #661)
// ---------------------------------------------------------------------
// Parse and classify one top-level unit against the *persistent* global
// scope already installed on vm (see cc_run_repl in src/repl.c, which calls
// parse() once up front on an empty token stream to enter the global scope
// and declare builtins). Unlike parse(), this never enters/leaves scope and
// never resets vm->compiler.globals -- declarations accumulate across calls.
ReplUnitKind cc_parse_repl_unit(VirtualMachine *vm, Token *tok,
                                Node **out_expr) {
    *out_expr = NULL;
    if (!tok || tok->kind == TK_EOF)
        return REPL_UNIT_EMPTY;

    if (is_decl_start(vm, tok)) {
        // Same unit of work as parse()'s top-level loop, minus the
        // scope/globals reset -- a REPL "line" may itself contain more than
        // one declaration (e.g. "int a; int b = a + 1;").
        parse_file_scope_decls(vm, tok);
        return REPL_UNIT_DECL;
    }

    // Bare expression: the one REPL affordance with no equivalent at real
    // file scope. Parse via expr() (assignment + comma operator), consume an
    // optional trailing ';', and require the whole line be consumed.
    Node *n = expr(vm, &tok, tok);
    add_type(vm, n);
    if (equal(tok, ";"))
        tok = tok->next;
    if (tok->kind != TK_EOF)
        error_tok(vm, tok, "unexpected token after expression: '%.*s'",
                  tok->len, tok->loc);
    *out_expr = n;
    return REPL_UNIT_EXPR;
}
