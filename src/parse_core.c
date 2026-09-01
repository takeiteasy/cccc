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

// Scope/state management and AST-node construction helpers shared by every
// stage of the parser: scope push/pop and lookup (variables, tags,
// typedefs, macros), custom-attribute plumbing, and the new_*() node/Obj
// constructors.

#include "./parse_internal.h"

int align_to(int n, int align) {
    return (int)(((long long)n + align - 1) / align * align);
}

// Return the TestFnRecord for this name if it is a negative test, else NULL.
// A negative test has either error_pat or expect_compile_error set.
TestFnRecord *find_neg_test_record(VirtualMachine *vm, const char *name) {
    if (!name)
        return NULL;
    for (TestFnRecord *r = vm->compiler.test_fns; r; r = r->next)
        if ((r->error_pat || r->expect_compile_error) &&
            strcmp(r->name, name) == 0)
            return r;
    return NULL;
}

int align_down(int n, int align) {
    return align_to(n - align + 1, align);
}

void enter_scope(VirtualMachine *vm) {
    Scope *sc = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
    memset(sc, 0, sizeof(Scope));
    sc->next           = vm->compiler.scope;
    vm->compiler.scope = sc;
}

bool type_has_restrict(Type *ty) {
    for (; ty; ty = ty->base) {
        if (ty->is_restrict)
            return true;
    }
    return false;
}

char *obj_display_name(Obj *var) {
    return var->display_name ? var->display_name : var->name;
}

void warn_deprecated_use(VirtualMachine *vm, Token *tok, char *name,
                         char *message) {
    if (message)
        warn_tok(vm, tok, CCCC_WARN_DEPRECATED, "'%s' is deprecated: %s", name,
                 message);
    else
        warn_tok(vm, tok, CCCC_WARN_DEPRECATED, "'%s' is deprecated", name);
}

Type *type_after_deprecated_use(VirtualMachine *vm, Type *ty) {
    Type *copy           = copy_type(vm, ty);
    copy->is_deprecated  = false;
    copy->deprecated_msg = NULL;
    return copy;
}

static void warn_unused_scope(VirtualMachine *vm, Scope *sc) {
    for (VarScopeNode *node = sc->vars; node; node = node->next) {
        Obj *var = node->var;
        if (!var || !var->is_local_symbol || !var->tok || var->is_used ||
            var->is_maybe_unused)
            continue;
        warn_tok(vm, var->tok, CCCC_WARN_UNUSED, "unused %s '%s'",
                 var->is_param ? "parameter" : "variable",
                 obj_display_name(var));
    }
}

void leave_scope(VirtualMachine *vm) {
    warn_unused_scope(vm, vm->compiler.scope);
    hashmap_deinit_borrowed(&vm->compiler.scope->var_map);
    hashmap_deinit_borrowed(&vm->compiler.scope->tag_map);
    vm->compiler.scope = vm->compiler.scope->next;
}

// Find a variable by name.
VarScope *find_var(VirtualMachine *vm, Token *tok) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        if (sc->var_map.buckets) {
            VarScopeNode *node = hashmap_get2(&sc->var_map, tok->loc, tok->len);
            if (node)
                return (VarScope *)node;
        } else {
            for (VarScopeNode *node = sc->vars; node; node = node->next) {
                if (node->name_len == tok->len &&
                    strncmp(node->name, tok->loc, tok->len) == 0)
                    return (VarScope *)node;
            }
        }
    }
    return NULL;
}

void warn_if_shadowing(VirtualMachine *vm, Token *tok) {
    if (!tok || !vm->compiler.current_fn || !vm->compiler.scope)
        return;

    for (Scope *sc = vm->compiler.scope->next; sc; sc = sc->next) {
        VarScopeNode *node =
            sc->var_map.buckets ? hashmap_get2(&sc->var_map, tok->loc, tok->len)
                                : NULL;
        if (!node) {
            for (node = sc->vars; node; node = node->next)
                if (node->name_len == tok->len &&
                    !strncmp(node->name, tok->loc, tok->len))
                    break;
        }
        if (!node)
            continue;
        if (node->var && !node->var->is_function)
            warn_tok(vm, tok, CCCC_WARN_SHADOW,
                     "declaration of '%.*s' shadows an outer variable",
                     tok->len, tok->loc);
        return;
    }
}

// Find a macro function by name
MacroFn *find_macro_fn(VirtualMachine *vm, Token *tok) {
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (pm->is_macro_entry && strlen(pm->name) == tok->len &&
            strncmp(pm->name, tok->loc, tok->len) == 0) {
            return pm;
        }
    }
    return NULL;
}

MacroFn *find_attribute_macro(VirtualMachine *vm, Token *tok) {
    if (vm->compiler.in_macro_mode)
        return NULL;
    if (!tok || tok->kind != TK_IDENT)
        return NULL;
    // Ticket #235: make sure reflection.h's built-in @comptime(attribute(...))
    // handlers (e.g. __cccc_attr_serialize) are registered before searching.
    ensure_reflection_attrs_registered(vm);
    for (MacroFn *pm = vm->compiler.macro_fns; pm; pm = pm->next) {
        if (!pm->is_attribute_handler || !pm->attribute_name)
            continue;
        if (strlen(pm->attribute_name) == tok->len &&
            strncmp(pm->attribute_name, tok->loc, tok->len) == 0)
            return pm;
    }
    return NULL;
}

void append_custom_attr(VirtualMachine *vm, CustomAttrUse **list,
                        Token *name_tok, Node *args, int arg_count) {
    if (!list || !name_tok)
        return;
    CustomAttrUse *use =
        arena_alloc(&vm->compiler.parser_arena, sizeof(CustomAttrUse));
    memset(use, 0, sizeof(CustomAttrUse));
    use->name      = arena_strndup(vm, name_tok->loc, name_tok->len);
    use->tok       = name_tok;
    use->args      = args;
    use->arg_count = arg_count;

    if (!*list) {
        *list = use;
        return;
    }
    CustomAttrUse *tail = *list;
    while (tail->next)
        tail = tail->next;
    tail->next = use;
}

static Token *skip_paren_group(VirtualMachine *vm, Token *tok) {
    tok       = skip(vm, tok, "(");
    int depth = 1;
    while (tok && tok->kind != TK_EOF && depth > 0) {
        if (equal(tok, "("))
            depth++;
        else if (equal(tok, ")"))
            depth--;
        tok = tok->next;
    }
    return tok;
}

static Token *skip_c23_attr_group(Token *tok) {
    if (!equal(tok, "[") || !tok->next || !equal(tok->next, "["))
        return tok;
    tok = tok->next->next;
    while (tok && tok->kind != TK_EOF) {
        if (equal(tok, "]") && tok->next && equal(tok->next, "]"))
            return tok->next->next;
        tok = tok->next;
    }
    return tok;
}

bool is_decl_start(VirtualMachine *vm, Token *tok) {
    for (;;) {
        if (equal(tok, "__attribute__")) {
            Token *p = tok->next;
            if (!p || !equal(p, "(") || !p->next || !equal(p->next, "("))
                return false;
            p = skip_paren_group(vm, p);
            if (!p)
                return false;
            tok = p;
            continue;
        }
        if (equal(tok, "[") && tok->next && equal(tok->next, "[")) {
            tok = skip_c23_attr_group(tok);
            continue;
        }
        if (tok && tok->kind == TK_IDENT && tok->next && equal(tok->next, ":"))
            return false;
        return is_typename(vm, tok);
    }
}

Token *parse_custom_attr_args(VirtualMachine *vm, Token *tok, Node **args,
                              int *arg_count) {
    *args      = NULL;
    *arg_count = 0;
    if (!equal(tok, "("))
        return tok;

    if (vm->compiler.in_type_lookahead)
        return skip_paren_group(vm, tok);

    tok        = tok->next;
    Node  head = {};
    Node *cur  = &head;
    while (!equal(tok, ")")) {
        if (cur != &head)
            tok = skip(vm, tok, ",");
        Node *arg = assign(vm, &tok, tok);
        cur = cur->next = arg;
        (*arg_count)++;
    }
    *args = head.next;
    return tok->next;
}

static void run_custom_attrs(VirtualMachine *vm, CustomAttrUse *attrs,
                             AttrTargetKind kind, char *name, Type *ty,
                             Obj *obj, Token *tok) {
    // Ticket #235: make sure reflection.h's built-in @comptime(attribute(...))
    // handlers (e.g. __cccc_attr_serialize) are registered before searching.
    if (attrs)
        ensure_reflection_attrs_registered(vm);
    for (CustomAttrUse *use = attrs; use; use = use->next) {
        MacroFn *pm = NULL;
        for (MacroFn *m = vm->compiler.macro_fns; m; m = m->next) {
            if (m->is_attribute_handler && m->attribute_name &&
                !strcmp(m->attribute_name, use->name)) {
                pm = m;
                break;
            }
        }
        if (!pm)
            error_tok(vm, use->tok, "undefined custom attribute '%s'",
                      use->name);

        AttrTarget *target =
            arena_alloc(&vm->compiler.parser_arena, sizeof(AttrTarget));
        memset(target, 0, sizeof(AttrTarget));
        target->kind = kind;
        target->name = name;
        target->ty   = ty;
        target->obj  = obj;
        target->tok  = tok ? tok : use->tok;
        cc_execute_attribute_macro(vm, pm, use->tok, target, use->args,
                                   use->arg_count);
    }
}

bool has_custom_attrs(Type *ty, VarAttr *attr) {
    return (ty && ty->custom_attrs) || (attr && attr->custom_attrs);
}

void run_decl_custom_attrs(VirtualMachine *vm, Type *ty, VarAttr *attr,
                           AttrTargetKind kind, char *name, Type *target_ty,
                           Obj *obj, Token *tok) {
    if (attr && attr->custom_attrs)
        run_custom_attrs(vm, attr->custom_attrs, kind, name, target_ty, obj,
                         tok);
    if (ty && ty->custom_attrs)
        run_custom_attrs(vm, ty->custom_attrs, kind, name, target_ty, obj, tok);
}

// Ticket #619: generic programmatic attribute application for AST-generated
// Objs. Parses attr_text as though it appeared in [[attr_text]] in source and
// applies it to fn. Handles mode attrs (test/build/build_target/…), standard
// C23/GNU attrs, and custom @attrs.
void cc_apply_attr_to_fn(VirtualMachine *vm, Obj *fn, const char *attr_text,
                         Token *site_tok) {
    if (!vm || !fn || !fn->name || !attr_text)
        return;

    // cccc::comptime cannot be applied retroactively — the fn is already
    // compiled.
    if (strstr(attr_text, "comptime"))
        error_tok(vm, site_tok,
                  "cccc::comptime cannot be applied via AddAttribute");

    // Synthesize "[[attr_text]]\nvoid fn_name(void);\n" so the mode-attr
    // scanner can find the function name by looking ahead past "]]\n".
    size_t buf_len = strlen(attr_text) + strlen(fn->name) + 32;
    char  *buf     = malloc(buf_len);
    snprintf(buf, buf_len, "[[%s]]\nvoid %s(void);\n", attr_text, fn->name);
    Token *toks = tokenize_string(vm, "<add-attribute>", buf);
    free(buf);

    // Fix up error locations to point at the macro call site.
    if (site_tok) {
        for (Token *t = toks; t && t->kind != TK_EOF; t = t->next) {
            t->file    = site_tok->file;
            t->line_no = site_tok->line_no;
        }
    }

    // Try mode attributes (test/build/build_target/test_setup/test_teardown).
    // emit_scan=true: register records but do NOT re-extract as a comptime
    // function.
    Token *tok_ptr = toks;
    if (try_extract_attr_macro(vm, &tok_ptr, /*emit_scan=*/true))
        return;

    // Try standard C23 / GNU attributes.  Both parsers fill a VarAttr and
    // advance past the attribute syntax; custom @attrs land in
    // VarAttr.custom_attrs.
    VarAttr attr  = {0};
    Token  *after = toks;
    if (equal(after, "[") && after->next && equal(after->next, "["))
        after = c23_attribute_list(vm, after, NULL, &attr);
    else if (equal(after, "__attribute__"))
        after = attribute_list(vm, after, NULL, &attr);

    bool has_std = attr.is_maybe_unused || attr.is_deprecated ||
                   attr.is_noreturn || attr.is_nodiscard || attr.is_pure ||
                   attr.is_func_const || attr.format_style || attr.cleanup_fn ||
                   attr.align > 0;
    bool has_custom = attr.custom_attrs != NULL;

    if (!has_std && !has_custom)
        error_tok(vm, site_tok ? site_tok : toks,
                  "AddAttribute: unrecognized attribute '%s'", attr_text);

    if (has_std) {
        fn->ty = apply_var_attrs_to_type(vm, fn->ty, &attr);
        if (attr.align > 0)
            fn->align = attr.align;
    }
    if (has_custom)
        run_decl_custom_attrs(vm, NULL, &attr, ATTR_TARGET_FUNCTION, fn->name,
                              fn->ty, fn, site_tok);
}

void append_custom_attr_list(CustomAttrUse **dst, CustomAttrUse *src) {
    if (!dst || !src)
        return;
    if (!*dst) {
        *dst = src;
        return;
    }
    CustomAttrUse *tail = *dst;
    while (tail->next)
        tail = tail->next;
    tail->next = src;
}

static Type *find_tag_scan(VirtualMachine *vm, Token *tok) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        if (sc->tag_map.buckets) {
            TagScopeNode *node = hashmap_get2(&sc->tag_map, tok->loc, tok->len);
            if (node)
                return node->ty;
        } else {
            for (TagScopeNode *node = sc->tags; node; node = node->next) {
                if (node->name_len == tok->len &&
                    strncmp(node->name, tok->loc, tok->len) == 0)
                    return node->ty;
            }
        }
    }
    return NULL;
}

Type *find_tag(VirtualMachine *vm, Token *tok) {
    Type *ty = find_tag_scan(vm, tok);
    if (ty)
        return ty;

    // #894: on a miss during the comptime parse, ask the demand-driven
    // declaration index to splice a matching struct/union/enum tag in, then
    // retry once. Note find_tag_in_current_scope (below) -- the redefinition
    // check -- is deliberately NOT hooked; splicing must never make a fresh
    // declaration look like a collision with something not actually there
    // yet.
    if ((vm->compiler.in_macro_mode || vm->compiler.comptime_splice_active) &&
        tok->kind == TK_IDENT && cc_comptime_resolve_tag(vm, tok))
        return find_tag_scan(vm, tok);

    return NULL;
}

Type *find_tag_in_current_scope(VirtualMachine *vm, Token *tok) {
    Scope *sc = vm->compiler.scope;
    if (!sc)
        return NULL;
    if (sc->tag_map.buckets) {
        TagScopeNode *node = hashmap_get2(&sc->tag_map, tok->loc, tok->len);
        return node ? node->ty : NULL;
    }
    for (TagScopeNode *node = sc->tags; node; node = node->next)
        if (node->name_len == tok->len &&
            strncmp(node->name, tok->loc, tok->len) == 0)
            return node->ty;
    return NULL;
}

VarScope *find_var_in_current_scope(VirtualMachine *vm, char *name,
                                    int name_len) {
    Scope *sc = vm->compiler.scope;
    if (!sc)
        return NULL;
    if (sc->var_map.buckets) {
        VarScopeNode *node = hashmap_get2(&sc->var_map, name, name_len);
        return node ? (VarScope *)node : NULL;
    }
    for (VarScopeNode *node = sc->vars; node; node = node->next)
        if (node->name_len == name_len &&
            strncmp(node->name, name, name_len) == 0)
            return (VarScope *)node;
    return NULL;
}

Node *new_node(VirtualMachine *vm, NodeKind kind, Token *tok) {
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = kind;
    node->tok  = tok;
    return node;
}

Node *new_binary(VirtualMachine *vm, NodeKind kind, Node *lhs, Node *rhs,
                 Token *tok) {
    Node *node = new_node(vm, kind, tok);
    node->lhs  = lhs;
    node->rhs  = rhs;
    return node;
}

Node *new_unary(VirtualMachine *vm, NodeKind kind, Node *expr, Token *tok) {
    Node *node = new_node(vm, kind, tok);
    node->lhs  = expr;
    return node;
}

Node *new_num(VirtualMachine *vm, int64_t val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val  = val;
    node->ty   = ty_int;
    return node;
}

Node *new_long(VirtualMachine *vm, int64_t val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val  = val;
    node->ty   = ty_long;
    return node;
}

Node *new_ulong(VirtualMachine *vm, long val, Token *tok) {
    Node *node = new_node(vm, ND_NUM, tok);
    node->val  = val;
    node->ty   = ty_ulong;
    return node;
}

Node *new_complex_node(VirtualMachine *vm, Node *real, Node *imag, Type *ty,
                       Token *tok) {
    Node *node = new_node(vm, ND_COMPLEX, tok);
    node->lhs  = real;
    node->rhs  = imag;
    node->ty   = ty;
    return node;
}

Node *new_var_node(VirtualMachine *vm, Obj *var, Token *tok) {
    Node *node = new_node(vm, ND_VAR, tok);
    node->var  = var;
    return node;
}

Node *new_vla_ptr(VirtualMachine *vm, Obj *var, Token *tok) {
    Node *node = new_node(vm, ND_VLA_PTR, tok);
    node->var  = var;
    return node;
}

Node *new_cast(VirtualMachine *vm, Node *expr, Type *ty) {
    add_type(vm, expr);
    Node *node = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    memset(node, 0, sizeof(Node));
    node->kind = ND_CAST;
    node->tok  = expr->tok;
    node->lhs  = expr;
    node->ty   = copy_type(vm, ty);
    return node;
}

// #1155: VarScope (parse_internal.h) is a prefix-layout view of
// VarScopeNode (cccc.h) -- every allocation below allocates a VarScopeNode
// and hands back a VarScope* cast of it. This pins that invariant at
// compile time so the two can never drift again the way #1095 let them
// (see VarScopeNode's own doc comment for what that corrupted).
static_assert(offsetof(VarScopeNode, name) == sizeof(VarScope),
              "VarScope must be a layout prefix of VarScopeNode (they are "
              "cast between; see VarScopeNode's doc comment in cccc.h)");

VarScope *push_scope(VirtualMachine *vm, char *name, int name_len) {
    VarScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(VarScopeNode));
    memset(node, 0, sizeof(VarScopeNode));
    node->name               = name;
    node->name_len           = name_len;
    node->next               = vm->compiler.scope->vars;
    vm->compiler.scope->vars = node;
    hashmap_put2_borrowed(&vm->compiler.scope->var_map, name, name_len, node);
    return (VarScope *)node;
}

// decl_tok is the token the name/tag was declared at, used to tell whether
// this declaration came from an #include (see TypeNameRecord.from_include in
// cccc.h). NULL is treated as "not from an include" (the synthetic-token
// callers, if any are added later, should prefer always_emit instead).
void record_type_name(VirtualMachine *vm, Type *ty, char *name, int name_len,
                      bool is_tag, Token *decl_tok) {
    if (!ty || !name || name_len <= 0)
        return;

    TypeNameRecord *rec =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TypeNameRecord));
    memset(rec, 0, sizeof(TypeNameRecord));
    rec->ty       = ty;
    rec->name     = name;
    rec->name_len = name_len;
    rec->owner_fn = vm->compiler.current_fn;
    rec->is_tag   = is_tag;
    // #896: a type declared in a file whose contents (directly or
    // transitively) use cccc-only routing syntax is never treated as
    // from_include -- serialize_program.c's native-backend re-emission filter
    // suppresses the raw #include of such a file, so its own definition
    // has to be serialized normally instead of relying on that #include
    // to supply it to the downstream compiler.
    // #1006: was `decl_tok->file != vm->compiler.primary_file`, which only
    // ever names input_files[0] (cc_preprocess/linker.c pin primary_file to
    // the *first* input file forever) -- so a typedef/struct/enum written
    // in input_files[1..N] (a non-primary translation unit) was wrongly
    // treated as header-supplied and its definition dropped from
    // -c=native/-m output, even though nothing actually supplies it (its
    // own TU's directives were never replayed either -- see the matching
    // preprocess.c fix). cc_file_is_command_line_input() is the same test
    // #1002 already established for serialize_program.c's function passes.
    // #1034: __builtin_quote's Quote() wrapper (reflection.c) tokenizes its
    // template string under the fixed pseudo-filename "<quote>" -- unlike
    // tokenize_private_header()'s "<implicit-reflection.h>"/"<building.h>"/
    // "<testing.h>" (a real on-disk header, just re-tokenized under a tag
    // instead of resolved by path; genuinely from_include, must stay
    // suppressed here) there is no backing file at all for a Quote()d
    // template -- no downstream #include could ever supply this
    // definition. A struct/union/enum whose tag is only ever declared
    // inside a Quote()d template (the file-scope ND_BLOCK splice path,
    // macros.c) was wrongly treated as from_include and its definition
    // suppressed entirely, leaving every reference to the tag an
    // incomplete-type error. Matched by exact name, not a "<...>" prefix
    // (tried first; wrongly caught tokenize_private_header's real headers
    // too and undid #892's AttrTarget/opaque-handle-collision fix).
    bool is_quote_pseudo_file = decl_tok && decl_tok->file &&
                                decl_tok->file->name &&
                                strcmp(decl_tok->file->name, "<quote>") == 0;
    rec->from_include =
        decl_tok && decl_tok->file && !is_quote_pseudo_file &&
        !cc_file_is_command_line_input(vm, decl_tok->file->name) &&
        !cc_file_is_cccc_only(vm, decl_tok->file->name);
    rec->file_path = decl_tok && decl_tok->file ? decl_tok->file->name : NULL;
    rec->next      = vm->compiler.type_names;
    vm->compiler.type_names = rec;
}

// #1010: marks the most-recently-created TypeNameRecord for `ty` as having
// been recorded at a real definition (see TypeNameRecord.defines_type,
// cccc.h). Only meaningful called immediately after the record_type_name()
// call it's marking -- a no-op if type_names' head isn't that record (e.g.
// record_type_name() itself declined to create one, ty/name were NULL).
void mark_last_type_name_as_definition(VirtualMachine *vm, Type *ty) {
    if (vm->compiler.type_names && vm->compiler.type_names->ty == ty)
        vm->compiler.type_names->defines_type = true;
}

Initializer *new_initializer(VirtualMachine *vm, Type *ty, bool is_flexible) {
    Initializer *init =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Initializer));
    memset(init, 0, sizeof(Initializer));
    init->ty = ty;

    if (ty->kind == TY_ARRAY) {
        if (is_flexible && ty->size < 0) {
            init->is_flexible = true;
            return init;
        }

        init->children = arena_alloc(&vm->compiler.parser_arena,
                                     ty->array_len * sizeof(Initializer *));
        memset(init->children, 0, ty->array_len * sizeof(Initializer *));
        for (int i = 0; i < ty->array_len; i++)
            init->children[i] = new_initializer(vm, ty->base, false);
        return init;
    }

    // GNU vector_size vector (TY_VECTOR): one child per lane, keyed on
    // vec_len like the TY_ARRAY case above (tracker #713).
    if (ty->kind == TY_VECTOR) {
        init->children = arena_alloc(&vm->compiler.parser_arena,
                                     ty->vec_len * sizeof(Initializer *));
        memset(init->children, 0, ty->vec_len * sizeof(Initializer *));
        for (int i = 0; i < ty->vec_len; i++)
            init->children[i] = new_initializer(vm, ty->base, false);
        return init;
    }

    // VLA initialization: treat like flexible array - will be sized during
    // parsing
    if (ty->kind == TY_VLA) {
        init->is_flexible = true;
        return init;
    }

    if (ty->kind == TY_STRUCT || ty->kind == TY_UNION) {
        // Count the number of struct members.
        int len = 0;
        for (Member *mem = ty->members; mem; mem = mem->next)
            len++;

        init->children = arena_alloc(&vm->compiler.parser_arena,
                                     len * sizeof(Initializer *));
        memset(init->children, 0, len * sizeof(Initializer *));

        for (Member *mem = ty->members; mem; mem = mem->next) {
            if (is_flexible && ty->is_flexible && !mem->next) {
                Initializer *child = arena_alloc(&vm->compiler.parser_arena,
                                                 sizeof(Initializer));
                memset(child, 0, sizeof(Initializer));
                child->ty                = mem->ty;
                child->is_flexible       = true;
                init->children[mem->idx] = child;
            } else {
                init->children[mem->idx] = new_initializer(vm, mem->ty, false);
            }
        }
        return init;
    }

    return init;
}

Obj *new_var(VirtualMachine *vm, char *name, int name_len, Type *ty) {
    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name      = name;
    var->ty        = ty;
    var->asm_label = ty->asm_label;
    var->align     = ty->align;
    var->tok       = ty->name;
    var->display_name =
        ty->name ? arena_strndup(vm, ty->name->loc, ty->name->len) : name;
    var->is_maybe_unused = ty->is_maybe_unused;
    var->is_deprecated   = ty->is_deprecated;
    var->is_noreturn     = ty->is_noreturn;
    var->is_pure         = ty->is_pure;
    var->is_func_const   = ty->is_func_const;
    var->deprecated_msg  = ty->deprecated_msg;
    var->is_constructor  = ty->is_constructor;
    var->is_destructor   = ty->is_destructor;
    var->init_priority   = ty->init_priority;
    if (var->is_constructor || var->is_destructor) {
        // Reachable only via the attribute (gen() walks prog for
        // is_constructor/ is_destructor, not through any call site) — keep it
        // live under DCE.
        var->is_live = true;
        var->is_root = true;
    }
    if (ty->cleanup_fn) {
        var->cleanup_fn = ty->cleanup_fn;
        // Mark cleanup fn as reachable so the liveness pass keeps it.
        ty->cleanup_fn->is_live = true;
        ty->cleanup_fn->is_root = true;
    }
    // Checked C-style checked-pointer transport (#770/#482-484), same
    // pattern as cleanup_fn above. The bounds expression itself (if any) is
    // resolved later by resolve_checked_bounds() -- it reads the still-
    // unresolved token spans straight off var->ty->checked_bounds_arg1/arg2
    // rather than needing its own copy here.
    var->checked_kind                   = ty->checked_kind;
    var->checked_bounds_form            = ty->checked_bounds_form;
    push_scope(vm, name, name_len)->var = var;
    return var;
}

Obj *new_lvar(VirtualMachine *vm, char *name, int name_len, Type *ty) {
    warn_if_shadowing(vm, ty->name);
    Obj *var             = new_var(vm, name, name_len, ty);
    var->is_local        = true;
    var->is_local_symbol = ty->name && name_len > 0;
    var->next            = vm->compiler.locals;
    vm->compiler.locals  = var;
    return var;
}

Obj *new_gvar(VirtualMachine *vm, char *name, int name_len, Type *ty) {
    Obj *var           = new_var(vm, name, name_len, ty);
    var->next          = vm->compiler.globals;
    var->is_static     = true;
    var->is_definition = true;
    // #1250: every global built while compiling the comptime program (in
    // particular one spliced in on demand by comptime_index_splice's
    // CDK_OBJECT branch, src/macros.c) lands storage in the comptime
    // program's own data segment, not the runtime translation unit's -- see
    // is_macro_program_global's comment, src/cccc.h.
    var->is_macro_program_global = vm->compiler.in_macro_mode;
    vm->compiler.globals         = var;
    return var;
}

// Side-effect check for a checked-pointer bounds expression (#770/#483):
// bounds are re-evaluated at every checked access a checked pointer
// participates in (see the design note in man/SAFETY.md), so an expression
// with side effects would run once per access rather than once -- e.g.
// count(i++) would increment i on every dereference, not once. Recurses
// through every expression-level node kind reachable from assign()'s
// grammar (bounds expressions are parsed with assign(), never a full
// statement); flags assignment (compound assignment and ++/-- both lower to
// ND_ASSIGN, see to_assign()), function calls, and the atomic op kinds.
// Also recurses into ->then/->els (#949): ND_COND (the ternary `cond ? then
// : els`) stores its two branches there, separately from ->lhs/->rhs, so
// `c ? f() : g()` was missed entirely before this and reported
// side-effect-free even though evaluating it can call f() or g(). Every
// other child-bearing field reachable from this grammar (->body, ->init,
// ->inc, cas_*, atomic_expr) belongs to a node kind the switch above
// already returns true for, or to a statement kind only reachable via
// ND_STMT_EXPR, which also returns true -- so lhs/rhs/cond/then/els/args is
// the complete set.
bool node_has_side_effects(Node *n) {
    if (!n)
        return false;
    switch (n->kind) {
        case ND_ASSIGN:
        case ND_FUNCALL:
        case ND_CAS:
        case ND_EXCH:
        case ND_ALOAD:
        case ND_ASTORE:
        case ND_FENCE:
        case ND_STMT_EXPR: // GNU statement expression; may contain anything
            return true;
        default:
            break;
    }
    if (node_has_side_effects(n->lhs) || node_has_side_effects(n->rhs) ||
        node_has_side_effects(n->cond) || node_has_side_effects(n->then) ||
        node_has_side_effects(n->els))
        return true;
    for (Node *a = n->args; a; a = a->next)
        if (node_has_side_effects(a))
            return true;
    return false;
}

// Shared classifier for "is this node a statement, not an expression" --
// used wherever a comptime macro's returned Node* has to be checked against
// the syntactic position it landed in (transform_node's ND_MACRO_CALL/
// ND_EXPR_STMT handling in macros.c, cc_quote_expand_lazy's want_stmt wrap in
// reflection.c). In statement position such a node splices in directly; in
// expression position it is a compile error, and it must never reach codegen
// as a bare expression (gen_expr has no arm for any of these kinds and would
// abort with an opaque "unsupported expression node kind N").
//
// Invariant: this set is exactly the node kinds gen_stmt() handles in
// src/codegen_stmt.c -- a kind that codegen treats as a statement is one
// this classifier must recognise. Keep the two in sync: adding a statement
// kind to gen_stmt() means adding it here too.
//
// Not included, on purpose: ND_LABEL_VAL (`&&label`), which sits right next
// to ND_LABEL in the NodeKind enum but is an *expression* producing a label
// address.
bool node_is_stmt_kind(Node *n) {
    if (!n)
        return false;
    switch (n->kind) {
        case ND_RETURN:
        case ND_IF:
        case ND_FOR:
        case ND_DO:
        case ND_SWITCH:
        case ND_CASE:
        case ND_BLOCK:
        case ND_GOTO:
        case ND_GOTO_EXPR:
        case ND_LABEL:
        case ND_EXPR_STMT:
        case ND_ASM:
            return true;
        default:
            return false;
    }
}

// Resolves a checked pointer's bounds expression(s) from the raw token
// span(s) captured at attribute-parse time (Type.checked_bounds_arg1/arg2 --
// see its comment for why resolution is deferred) into real Node* ASTs, once
// `var` sits in a scope where anything its bounds reference is visible.
// Call sites: for parameters, once after create_param_lvars() finishes ALL
// params of the function (a bound may reference a LATER parameter, e.g.
// `count(n)` before `int n`); for locals/globals, immediately after
// new_lvar()/new_gvar() (anything a bound references must already be
// declared, same as ordinary C). Never called at all for a prototype-only
// declaration -- function() returns before opening a body scope, and a
// prototype has no accesses to check -- so its bounds token span is left
// permanently unresolved, which is correct, not an error (see #488 for the
// caller-side checking that would eventually consume it).
//
// checked_bounds_lo/checked_bounds_hi's stored meaning depends on the form:
//   CB_COUNT/CB_BYTE_COUNT: hi = resolved element/byte count `n`; lo is left
//                            NULL -- the base address is the checked
//                            pointer's own live value at each checked
//                            access, recomputed fresh by codegen (#484's
//                            per-access re-evaluation), not fixed here.
//   CB_RANGE:                lo/hi = resolved absolute bound expressions.
//   CB_UNKNOWN/CB_NONE:      neither set; no runtime check is ever emitted
//                            (CB_UNKNOWN is the bounds(unknown) escape
//                            hatch: the checked type is recorded, but
//                            trusted rather than enforced).
//
// Shared by resolve_checked_bounds() (Obj: locals/params/globals) and
// resolve_member_checked_bounds() (#921: struct/union members) -- the only
// difference between the two callers is *which scope* is active when this
// runs (a real variable scope vs. the synthetic placeholder scope
// resolve_member_checked_bounds() sets up), which is entirely the caller's
// concern; this function just re-parses token spans with assign() in
// whatever scope happens to be current. `form`/`arg1`/`arg2` are read
// straight off `ty` rather than requiring an Obj, so a member can call this
// with `mem->ty` directly. Returns immediately (leaving *out_lo/*out_hi
// untouched) for CB_NONE/CB_UNKNOWN -- callers are expected to have already
// skipped those forms via their own cheaper check, this is just a second
// line of defense.
static void resolve_bounds_tokens(VirtualMachine *vm, Type *ty, Node **out_lo,
                                  Node **out_hi) {
    if (ty->checked_bounds_form == CB_NONE ||
        ty->checked_bounds_form == CB_UNKNOWN)
        return;

    Token *tok;

    if (ty->checked_bounds_form == CB_RANGE) {
        tok      = ty->checked_bounds_arg1;
        Node *lo = assign(vm, &tok, tok);
        add_type(vm, lo);
        if (node_has_side_effects(lo))
            error_tok(vm, ty->checked_bounds_arg1,
                      "checked-pointer bounds expression must not have side "
                      "effects -- it is re-evaluated at every access");

        tok      = ty->checked_bounds_arg2;
        Node *hi = assign(vm, &tok, tok);
        add_type(vm, hi);
        if (node_has_side_effects(hi))
            error_tok(vm, ty->checked_bounds_arg2,
                      "checked-pointer bounds expression must not have side "
                      "effects -- it is re-evaluated at every access");

        *out_lo = lo;
        *out_hi = hi;
        return;
    }

    // CB_COUNT / CB_BYTE_COUNT
    tok     = ty->checked_bounds_arg1;
    Node *n = assign(vm, &tok, tok);
    add_type(vm, n);
    if (node_has_side_effects(n))
        error_tok(vm, ty->checked_bounds_arg1,
                  "checked-pointer bounds expression must not have side "
                  "effects -- it is re-evaluated at every access");
    *out_hi = n;
}

void resolve_checked_bounds(VirtualMachine *vm, Obj *var) {
    resolve_bounds_tokens(vm, var->ty, &var->checked_bounds_lo,
                          &var->checked_bounds_hi);
}

// Walks a resolved member-bounds template (see Member.checked_bounds_lo/hi's
// comment) validating every ND_VAR it contains. Called once per member
// bounds expression, right after resolve_bounds_tokens() -- separate from
// that function because this check is member-bounds-specific (an ordinary
// Obj-rooted bounds expression can reference any in-scope local/param/
// global, so this validation would be wrong there). A sibling field
// reference resolves to a checked_self_member placeholder (legal); a global
// resolves to a non-local Obj (legal, same as today's Obj-rooted bounds); a
// LOCAL leaking in from whatever scope struct_members() happens to be
// nested in (e.g. a local struct definition) is rejected -- the struct type
// can outlive that local, so trusting it would be unsound. A bit-field
// sibling is rejected outright rather than supported: nothing downstream
// (compute_checked_bounds's ND_MEMBER substitution) extracts a bit-field's
// value, and doing so would need to duplicate the bit-extraction codegen
// path for no real benefit -- v1 chooses to reject, not to add that.
static void check_member_bounds_template(VirtualMachine *vm, Node *n,
                                         Token *tok) {
    if (!n)
        return;
    if (n->kind == ND_VAR) {
        Obj *v = n->var;
        if (v->checked_self_member) {
            if (v->checked_self_member->is_bitfield)
                error_tok(
                    vm, tok,
                    "a checked-pointer bounds expression cannot reference "
                    "a bit-field member");
        } else if (v->is_local) {
            error_tok(vm, tok,
                      "checked-pointer bounds on a struct member may only "
                      "reference sibling members or globals");
        }
    }
    check_member_bounds_template(vm, n->lhs, tok);
    check_member_bounds_template(vm, n->rhs, tok);
    check_member_bounds_template(vm, n->cond, tok);
    check_member_bounds_template(vm, n->then, tok);
    check_member_bounds_template(vm, n->els, tok);
}

// Resolves checked-pointer bounds expressions on struct/union members
// (#770/#483/#921) once the full member list of a struct/union is known --
// a bound may name a LATER sibling field, e.g. `count(n)` on a member that
// textually precedes `int n`, the same reason parameter bounds are resolved
// only after create_param_lvars() finishes all params (see
// resolve_checked_bounds()'s call site for parameters). Builds a synthetic
// scope populated with one throwaway placeholder Obj per named member (see
// Obj.checked_self_member's comment) so resolve_bounds_tokens()'s ordinary
// assign()-based re-parse resolves sibling-field identifiers to those
// placeholders instead of erroring "undeclared identifier" -- struct scope
// isn't a variable scope, so this manufactures one just for the resolve.
void resolve_member_checked_bounds(VirtualMachine *vm, Member *members) {
    bool any = false;
    for (Member *mem = members; mem; mem = mem->next)
        if (mem->name && mem->ty->checked_bounds_form != CB_NONE &&
            mem->ty->checked_bounds_form != CB_UNKNOWN) {
            any = true;
            break;
        }
    if (!any)
        return;

    enter_scope(vm);
    for (Member *mem = members; mem; mem = mem->next) {
        if (!mem->name)
            continue;
        Obj *placeholder = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
        memset(placeholder, 0, sizeof(Obj));
        placeholder->name = arena_strndup(vm, mem->name->loc, mem->name->len);
        placeholder->display_name                           = placeholder->name;
        placeholder->ty                                     = mem->ty;
        placeholder->checked_self_member                    = mem;
        push_scope(vm, mem->name->loc, mem->name->len)->var = placeholder;
    }
    for (Member *mem = members; mem; mem = mem->next) {
        if (!mem->name || mem->ty->checked_bounds_form == CB_NONE ||
            mem->ty->checked_bounds_form == CB_UNKNOWN)
            continue;
        resolve_bounds_tokens(vm, mem->ty, &mem->checked_bounds_lo,
                              &mem->checked_bounds_hi);
        check_member_bounds_template(vm, mem->checked_bounds_lo, mem->name);
        check_member_bounds_template(vm, mem->checked_bounds_hi, mem->name);
    }
    leave_scope(vm);
}

// Create a function Obj that is NOT in the global scope or globals list.
// Used for __builtin_strlen/__builtin_strcmp stubs so they don't interfere
// with user redeclarations of strlen/strcmp.
Obj *new_private_func_obj(VirtualMachine *vm, const char *name, Type *ty) {
    Obj *var = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
    memset(var, 0, sizeof(Obj));
    var->name          = (char *)name;
    var->display_name  = (char *)name;
    var->ty            = ty;
    var->is_function   = true;
    var->is_definition = false;
    return var;
}

Obj *new_implicit_function(VirtualMachine *vm, Token *tok) {
    Type *ty           = func_type(vm, ty_int);
    ty->is_variadic    = true;

    Scope *saved_scope = vm->compiler.scope;
    while (vm->compiler.scope->next)
        vm->compiler.scope = vm->compiler.scope->next;
    Obj *fn = new_gvar(vm, arena_strndup(vm, tok->loc, tok->len), tok->len, ty);
    vm->compiler.scope = saved_scope;

    fn->is_function    = true;
    fn->is_definition  = false;
    fn->is_static      = false;
    fn->is_implicit    = true;
    return fn;
}

char *new_unique_name(VirtualMachine *vm) {
    return arena_format(vm, ".L..%d", vm->compiler.unique_name_counter++);
}

Obj *new_anon_gvar(VirtualMachine *vm, Type *ty) {
    char *name = new_unique_name(vm);
    return new_gvar(vm, name, strlen(name), ty);
}

Obj *new_string_literal(VirtualMachine *vm, char *p, Type *ty) {
    Obj *var               = new_anon_gvar(vm, ty);
    var->init_data         = p;
    var->is_string_literal = true;
    return var;
}

char *get_ident(VirtualMachine *vm, Token *tok) {
    if (tok->kind != TK_IDENT)
        error_tok(vm, tok, "expected an identifier, found '%.*s'", tok->len,
                  tok->loc);
    char *s = arena_alloc(&vm->compiler.parser_arena, tok->len + 1);
    memcpy(s, tok->loc, tok->len);
    s[tok->len] = '\0';
    return s;
}

// Error recovery helper: Skip to end of statement (semicolon or closing brace)
Token *skip_to_stmt_end(VirtualMachine *vm, Token *tok) {
    int paren_depth = 0, brace_depth = 0;

    while (tok->kind != TK_EOF) {
        if (equal(tok, "("))
            paren_depth++;
        if (equal(tok, ")") && paren_depth > 0)
            paren_depth--;
        if (equal(tok, "{"))
            brace_depth++;
        if (equal(tok, "}")) {
            if (brace_depth > 0) {
                brace_depth--;
            } else {
                return tok; // Found unmatched closing brace
            }
        }

        // Only treat semicolon as end if we're at same nesting level
        if (paren_depth == 0 && brace_depth == 0 && equal(tok, ";"))
            return tok->next;

        tok = tok->next;
    }
    return tok;
}

// Error recovery helper: Skip to next declarator boundary
Token *skip_to_decl_boundary(VirtualMachine *vm, Token *tok) {
    int paren_depth = 0;

    while (tok->kind != TK_EOF) {
        if (equal(tok, "("))
            paren_depth++;
        if (equal(tok, ")") && paren_depth > 0)
            paren_depth--;

        if (paren_depth == 0) {
            if (equal(tok, ","))
                return tok->next; // Next declarator
            if (equal(tok, ";"))
                return tok->next; // End of declaration
            if (equal(tok, "{"))
                return tok;       // Function body start
        }

        tok = tok->next;
    }
    return tok;
}

Type *find_typedef(VirtualMachine *vm, Token *tok) {
    if (tok->kind == TK_IDENT) {
        VarScope *sc = find_var(vm, tok);
        if (sc)
            return sc->type_def;
    }
    return NULL;
}

void push_tag_scope(VirtualMachine *vm, Token *tok, Type *ty) {
    TagScopeNode *node =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TagScopeNode));
    node->name               = tok->loc;
    node->name_len           = tok->len;
    node->ty                 = ty;
    node->next               = vm->compiler.scope->tags;
    vm->compiler.scope->tags = node;
    hashmap_put2_borrowed(&vm->compiler.scope->tag_map, tok->loc, tok->len,
                          node);
    record_type_name(vm, ty, tok->loc, tok->len, true, tok);
}
