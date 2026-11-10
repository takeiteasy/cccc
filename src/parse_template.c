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

// `@template(T...)` and `@inst(Name, Type...)`. The preprocessor turns the two
// into the identifiers `__cccc_template` and `__cccc_inst`. A template is kept
// as unparsed tokens; the first `@inst` of an argument list clones them under a
// mangled name and parses the clone at file scope with each parameter bound to
// its argument type.

#include "./parse_internal.h"

#define TMPL_MAX_DEPTH 64

static Token *skip_parens(Token *tok) {
    int depth = 0;
    for (; tok && tok->kind != TK_EOF; tok = tok->next) {
        if (equal(tok, "("))
            depth++;
        else if (equal(tok, ")") && --depth == 0)
            return tok->next;
    }
    return tok;
}

static Token *match_brace(Token *open) {
    int depth = 0;
    for (Token *t = open; t && t->kind != TK_EOF; t = t->next) {
        if (equal(t, "{"))
            depth++;
        else if (equal(t, "}") && --depth == 0)
            return t;
    }
    return NULL;
}

// Steps over `__attribute__((...))` and `[[...]]` groups.
static Token *skip_attr_groups(Token *tok) {
    for (;;) {
        if (equal(tok, "__attribute__") && equal(tok->next, "(")) {
            tok = skip_parens(tok->next);
        } else if (equal(tok, "[") && equal(tok->next, "[")) {
            tok = tok->next->next;
            while (tok->kind != TK_EOF &&
                   !(equal(tok, "]") && equal(tok->next, "]")))
                tok = tok->next;
            if (tok->kind != TK_EOF)
                tok = tok->next->next;
        } else {
            return tok;
        }
    }
}

static bool is_linkage_kw(Token *tok) {
    return equal(tok, "static") || equal(tok, "inline") ||
           equal(tok, "__inline") || equal(tok, "__inline__") ||
           equal(tok, "_Noreturn");
}

static bool is_typeof_kw(Token *tok) {
    return equal(tok, "typeof") || equal(tok, "__typeof__") ||
           equal(tok, "__typeof") || equal(tok, "typeof_unqual") ||
           equal(tok, "_Alignas");
}

// Classifies the declaration at `start`. Returns 0 when it is not a template,
// 1 with `t` filled in when it is, and -1 with `err`/`err_tok` set when it is a
// malformed one. Allocates only the parameter array.
int cc_template_scan(VirtualMachine *vm, Token *start, Template *t,
                     const char **err, Token **err_tok) {
    memset(t, 0, sizeof(*t));
    t->start = start;

    Token *p = start;
    for (;;) {
        Token *q = skip_attr_groups(p);
        if (q != p) {
            p = q;
            continue;
        }
        if (equal(p, "typedef") || equal(p, "extern")) {
            Token *probe = p;
            while (probe->kind != TK_EOF && !equal(probe, ";") &&
                   !equal(probe, "{") && !equal(probe, "__cccc_template"))
                probe = probe->next;
            if (!equal(probe, "__cccc_template"))
                return 0;
            *err     = "a template cannot be typedef or extern";
            *err_tok = p;
            return -1;
        }
        if (is_linkage_kw(p)) {
            p = p->next;
            continue;
        }
        break;
    }
    if (!equal(p, "__cccc_template"))
        return 0;

    t->marker = p;
    p         = p->next;
    if (!equal(p, "(")) {
        *err     = "expected '(' after @template";
        *err_tok = p;
        return -1;
    }
    Token *close  = skip_parens(p);
    t->marker_end = close;

    int n         = 0;
    for (Token *a = p->next; a != close->next && !equal(a, ")"); a = a->next) {
        if (equal(a, ","))
            continue;
        n++;
    }
    t->params =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Token *) * (n + 1));
    for (Token *a = p->next; !equal(a, ")");) {
        if (a->kind != TK_IDENT) {
            *err     = "@template parameters must be type names; constant "
                       "parameters are not supported";
            *err_tok = a;
            return -1;
        }
        for (int i = 0; i < t->nparams; i++)
            if (t->params[i]->len == a->len &&
                !strncmp(t->params[i]->loc, a->loc, a->len)) {
                *err     = "duplicate template parameter";
                *err_tok = a;
                return -1;
            }
        t->params[t->nparams++] = a;
        a                       = a->next;
        if (equal(a, ","))
            a = a->next;
        else if (!equal(a, ")")) {
            *err     = "expected ',' or ')' in @template parameter list";
            *err_tok = a;
            return -1;
        }
    }
    if (t->nparams == 0) {
        *err     = "@template needs at least one parameter";
        *err_tok = t->marker;
        return -1;
    }

    p = t->marker_end;
    for (;;) {
        Token *q = skip_attr_groups(p);
        if (q != p) {
            p = q;
            continue;
        }
        if (is_linkage_kw(p)) {
            p = p->next;
            continue;
        }
        break;
    }

    if (equal(p, "struct") || equal(p, "union")) {
        t->kind = equal(p, "struct") ? TMPL_STRUCT : TMPL_UNION;
        p       = skip_attr_groups(p->next);
        if (p->kind != TK_IDENT) {
            *err     = "a template struct or union needs a tag name";
            *err_tok = p;
            return -1;
        }
        t->name_tok = p;
        p           = skip_attr_groups(p->next);
        if (!equal(p, "{")) {
            *err     = "a template must be a definition";
            *err_tok = p;
            return -1;
        }
        Token *end = match_brace(p);
        if (!end || !equal(end->next, ";")) {
            *err     = "a template struct or union cannot declare variables";
            *err_tok = end ? end->next : p;
            return -1;
        }
        t->end = end->next->next;
        return 1;
    }
    if (equal(p, "enum")) {
        *err     = "enum templates are not supported";
        *err_tok = p;
        return -1;
    }

    Token *prev = NULL, *name = NULL, *q = p;
    while (q->kind != TK_EOF) {
        Token *g = skip_attr_groups(q);
        if (g != q) {
            q = g;
            continue;
        }
        if (is_typeof_kw(q) && equal(q->next, "(")) {
            q    = skip_parens(q->next);
            prev = NULL;
            continue;
        }
        if (equal(q, "(")) {
            name = prev;
            q    = skip_parens(q);
            break;
        }
        if (equal(q, ";") || equal(q, "{") || equal(q, "=")) {
            *err = "@template must be a struct, union or function definition";
            *err_tok = q;
            return -1;
        }
        prev = q;
        q    = q->next;
    }
    if (!name || name->kind != TK_IDENT) {
        *err     = "@template must be a struct, union or function definition";
        *err_tok = p;
        return -1;
    }
    while (q->kind != TK_EOF) {
        Token *g = skip_attr_groups(q);
        if (g == q)
            break;
        q = g;
    }
    if (!equal(q, "{")) {
        *err     = "a template must be a definition";
        *err_tok = q;
        return -1;
    }
    Token *end = match_brace(q);
    if (!end) {
        *err     = "unterminated template body";
        *err_tok = q;
        return -1;
    }
    t->kind      = TMPL_FUNC;
    t->name_tok  = name;
    t->body_open = q;
    t->end       = end->next;
    return 1;
}

// Capture arm of parse_file_scope_decls: records a `@template` definition and
// returns the token after it, or NULL when `tok` starts something else.
Token *cc_template_capture(VirtualMachine *vm, Token *tok) {
    Template    scan;
    const char *err     = NULL;
    Token      *err_tok = NULL;
    int         r       = cc_template_scan(vm, tok, &scan, &err, &err_tok);
    if (r < 0)
        error_tok(vm, err_tok, "%s", err);
    if (r == 0)
        return NULL;
    if (vm->compiler.in_macro_mode)
        error_tok(vm, scan.marker,
                  "@template is not supported in comptime code");

    Template *t = arena_alloc(&vm->compiler.parser_arena, sizeof(Template));
    *t          = scan;
    t->name     = arena_strndup(vm, t->name_tok->loc, t->name_tok->len);
    t->name_len = t->name_tok->len;
    if (hashmap_get2(&vm->compiler.templates, t->name, t->name_len))
        error_tok(vm, t->name_tok, "redefinition of template '%s'", t->name);
    hashmap_put2(&vm->compiler.templates, t->name, t->name_len, t);
    return t->end;
}

Template *cc_find_template(VirtualMachine *vm, Token *name) {
    if (!vm->compiler.saw_templates || !name || name->kind != TK_IDENT)
        return NULL;
    return hashmap_get2(&vm->compiler.templates, name->loc, name->len);
}

// Reports a stray `@template` the capture arm did not consume.
void cc_template_misplaced(VirtualMachine *vm, Token *tok) {
    error_tok(vm, tok,
              "@template is only allowed on a file-scope struct, union or "
              "function definition");
}

// ---- mangling -------------------------------------------------------------

static char *mangle_type(VirtualMachine *vm, Type *ty, Token *site);

static char *mangle_tag(VirtualMachine *vm, Token *tag, const char *kw,
                        Token *site) {
    if (!tag)
        error_tok(vm, site,
                  "template argument must be a named type; give the %s a tag",
                  kw);
    return arena_format(vm, "%s_%.*s", kw, (int)tag->len, tag->loc);
}

static char *mangle_base(VirtualMachine *vm, Type *ty, Token *site) {
    const char *u = ty->is_unsigned ? "unsigned_" : "";
    switch (ty->kind) {
        case TY_VOID:
            return "void";
        case TY_BOOL:
            return "bool";
        case TY_CHAR:
            return arena_format(vm, "%schar", u);
        case TY_SHORT:
            return arena_format(vm, "%sshort", u);
        case TY_INT:
            return arena_format(vm, "%sint", u);
        case TY_LONG:
            return arena_format(vm, "%s%s", u,
                                ty->is_long_long ? "long_long" : "long");
        case TY_FLOAT:
            return "float";
        case TY_DOUBLE:
            return "double";
        case TY_LDOUBLE:
            return "long_double";
        case TY_FLOAT16:
            return "_Float16";
        case TY_DECIMAL32:
            return "_Decimal32";
        case TY_DECIMAL64:
            return "_Decimal64";
        case TY_DECIMAL128:
            return "_Decimal128";
        case TY_NULLPTR_T:
            return "nullptr_t";
        case TY_BITINT:
            return arena_format(vm, "%s_BitInt%d",
                                ty->is_unsigned ? "unsigned__" : "",
                                ty->bit_width);
        case TY_COMPLEX:
            return arena_format(vm, "complex_%s",
                                mangle_type(vm, ty->base, site));
        case TY_VECTOR:
            return arena_format(vm, "vec%d_%s", ty->size,
                                mangle_type(vm, ty->base, site));
        case TY_ENUM:
            return mangle_tag(vm, type_agg(ty)->enum_tag, "enum", site);
        case TY_STRUCT:
            return mangle_tag(vm, type_agg(ty)->struct_tag, "struct", site);
        case TY_UNION:
            return mangle_tag(vm, type_agg(ty)->struct_tag, "union", site);
        case TY_PTR:
            return arena_format(vm, "%s_ptr", mangle_type(vm, ty->base, site));
        case TY_ARRAY:
            if (ty->array_len < 0 || ty->is_flexible)
                return arena_format(vm, "%s_arr",
                                    mangle_type(vm, ty->base, site));
            return arena_format(vm, "%s_arr%d", mangle_type(vm, ty->base, site),
                                ty->array_len);
        case TY_FUNC: {
            char *s = arena_format(vm, "fn_%s_of",
                                   mangle_type(vm, ty->return_ty, site));
            int   n = 0;
            for (Type *p = ty->params; p; p = p->next, n++)
                s = arena_format(vm, "%s_%s", s, mangle_type(vm, p, site));
            if (!n && !ty->is_variadic)
                s = arena_format(vm, "%s_void", s);
            return arena_format(vm, "%s%s_end", s,
                                ty->is_variadic ? "_va" : "");
        }
        case TY_VLA:
            error_tok(vm, site,
                      "a variable-length array cannot be a template argument");
        case TY_BLOCK:
            error_tok(vm, site, "a block type cannot be a template argument");
        default:
            error_tok(vm, site, "this type cannot be a template argument");
    }
}

static char *mangle_type(VirtualMachine *vm, Type *ty, Token *site) {
    if (ty->checked_kind != CHECKED_NONE)
        error_tok(vm, site,
                  "a checked-pointer type cannot be a template argument");
    if (ty->addr_space != AS_NONE)
        error_tok(vm, site,
                  "an address-space type cannot be a template argument");

    char *s = mangle_base(vm, ty, site);
    if (ty->kind == TY_PTR) {
        if (ty->is_const)
            s = arena_format(vm, "%s_const", s);
        if (ty->is_volatile)
            s = arena_format(vm, "%s_volatile", s);
        if (ty->is_restrict)
            s = arena_format(vm, "%s_restrict", s);
        if (ty->is_atomic)
            s = arena_format(vm, "%s_atomic", s);
        return s;
    }
    if (ty->is_atomic)
        s = arena_format(vm, "atomic_%s", s);
    if (ty->is_restrict)
        s = arena_format(vm, "restrict_%s", s);
    if (ty->is_volatile)
        s = arena_format(vm, "volatile_%s", s);
    if (ty->is_const)
        s = arena_format(vm, "const_%s", s);
    return s;
}

// ---- instantiation --------------------------------------------------------

static Token *synth_token(VirtualMachine *vm, const char *text) {
    Token *t = tokenize_string(vm, "<template>", arena_strdup(vm, text));
    convert_pp_tokens(vm, t);
    Token *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    token_assign(vm, copy, t);
    copy->next = NULL;
    return copy;
}

static Token *copy_tok(VirtualMachine *vm, Token *src) {
    Token *copy = arena_alloc(&vm->compiler.parser_arena, sizeof(Token));
    token_assign(vm, copy, src);
    copy->next = NULL;
    return copy;
}

static bool has_linkage_static(Template *t) {
    for (Token *p = t->start; p != t->name_tok && p->kind != TK_EOF;
         p        = p->next)
        if (equal(p, "static"))
            return true;
    return false;
}

// The template's tokens up to `limit` (NULL: all of them) under `mangled`,
// the `@template(...)` marker removed and the chain ended by `tail` (a
// synthesised ";" for a prototype) and an EOF.
static Token *clone_template(VirtualMachine *vm, Template *t,
                             const char *mangled, Token *limit, bool prototype,
                             ExpansionFrame *frame) {
    Token  head = {};
    Token *cur  = &head;
    if (t->kind == TMPL_FUNC && !has_linkage_static(t))
        cur = cur->next = synth_token(vm, "static");

    Token *prev = NULL, *prev2 = NULL;
    for (Token *p                                              = t->start;
         p != (limit ? limit : t->end) && p->kind != TK_EOF; p = p->next) {
        if (p == t->marker) {
            p = t->marker_end;
            if (p == (limit ? limit : t->end))
                break;
        }
        Token *c = cur = cur->next     = copy_tok(vm, p);
        tok_rare_mut(vm, c)->expansion = frame;
        bool rename = p->kind == TK_IDENT && p->len == (size_t)t->name_len &&
                      !strncmp(p->loc, t->name, t->name_len);
        if (rename) {
            if (t->kind == TMPL_FUNC)
                rename =
                    !(prev && (equal(prev, ".") || equal(prev, "->") ||
                               equal(prev, "struct") || equal(prev, "union") ||
                               equal(prev, "enum"))) &&
                    !(prev && prev2 && equal(prev, "(") &&
                      equal(prev2, "__cccc_inst"));
            else
                rename = prev &&
                         (equal(prev, "struct") || equal(prev, "union")) &&
                         !(prev2 && equal(prev2, "__cccc_inst"));
        }
        if (rename) {
            c->loc = (char *)mangled;
            c->len = strlen(mangled);
        }
        prev2 = prev;
        prev  = p;
    }
    if (prototype)
        cur = cur->next = synth_token(vm, ";");
    Token *eof = synth_token(vm, "");
    cur->next  = eof;
    return head.next;
}

typedef struct {
    Scope *scope;
    Obj   *current_fn, *locals;
    Node  *gotos, *labels, *current_switch;
    void  *deferred_unused_labels, *objsize_queries, *omp_frames;
    char  *brk_label, *cont_label;
    int    brk_cleanup_depth, cont_cleanup_depth, cleanup_scope_depth;
    CleanupChainNode *cur_cleanup_chain;
    int          fn_nesting_depth, unevaluated, dead_code_depth, proto_params;
    CheckedScope checked_scope_attr;
    bool         in_param_default, in_const_gvar_init, in_bitfield_width,
        in_type_lookahead, in_generic_assoc;
    Obj                *initializing_var;
    AddrSpace          *addr_space_sink;
    PendingMemberAttrs *pma, *pma_tail;
    PendingMethod      *pm, *pm_tail;
    PendingParamAttrs  *ppa;
    Scope              *bindings, *anchor;
} TmplSnapshot;

#define TMPL_FIELDS(X)                                                         \
    X(scope, scope)                                                            \
    X(current_fn, current_fn)                                                  \
    X(locals, locals)                                                          \
    X(gotos, gotos)                                                            \
    X(labels, labels)                                                          \
    X(current_switch, current_switch)                                          \
    X(deferred_unused_labels, deferred_unused_labels)                          \
    X(objsize_queries, objsize_queries)                                        \
    X(omp_frames, omp_frames)                                                  \
    X(brk_label, brk_label)                                                    \
    X(cont_label, cont_label)                                                  \
    X(brk_cleanup_depth, brk_cleanup_depth)                                    \
    X(cont_cleanup_depth, cont_cleanup_depth)                                  \
    X(cleanup_scope_depth, cleanup_scope_depth)                                \
    X(cur_cleanup_chain, cur_cleanup_chain)                                    \
    X(fn_nesting_depth, fn_nesting_depth)                                      \
    X(unevaluated, unevaluated)                                                \
    X(dead_code_depth, dead_code_depth)                                        \
    X(proto_params, proto_params)                                              \
    X(checked_scope_attr, checked_scope_attr)                                  \
    X(in_param_default, in_param_default)                                      \
    X(in_const_gvar_init, in_const_gvar_init)                                  \
    X(in_bitfield_width, in_bitfield_width)                                    \
    X(in_type_lookahead, in_type_lookahead)                                    \
    X(in_generic_assoc, in_generic_assoc)                                      \
    X(initializing_var, initializing_var)                                      \
    X(addr_space_sink, addr_space_sink)                                        \
    X(pma, pending_member_attrs)                                               \
    X(pma_tail, pending_member_attrs_tail)                                     \
    X(pm, pending_methods)                                                     \
    X(pm_tail, pending_methods_tail)                                           \
    X(ppa, pending_param_attrs)                                                \
    X(bindings, tmpl_bindings)                                                 \
    X(anchor, tmpl_anchor)

static void snapshot_save(VirtualMachine *vm, TmplSnapshot *s) {
    Compiler *c = &vm->compiler;
#define X(f, cf) s->f = c->cf;
    TMPL_FIELDS(X)
#undef X
}

static void snapshot_restore(VirtualMachine *vm, const TmplSnapshot *s) {
    Compiler *c = &vm->compiler;
#define X(f, cf) c->cf = s->f;
    TMPL_FIELDS(X)
#undef X
}

static void snapshot_clear(VirtualMachine *vm) {
    Compiler    *c = &vm->compiler;
    TmplSnapshot zero;
    memset(&zero, 0, sizeof(zero));
#define X(f, cf) c->cf = zero.f;
    TMPL_FIELDS(X)
#undef X
}

TemplateInstance *instantiate_template(VirtualMachine *vm, Template *t,
                                       Type **args, int nargs, Token *site) {
    if (nargs != t->nparams)
        error_tok(vm, site, "template '%s' takes %d type argument%s, %d given",
                  t->name, t->nparams, t->nparams == 1 ? "" : "s", nargs);

    char *mangled = t->name;
    for (int i = 0; i < nargs; i++)
        mangled =
            arena_format(vm, "%s__%s", mangled, mangle_type(vm, args[i], site));

    TemplateInstance *inst = hashmap_get(&vm->compiler.template_insts, mangled);
    if (inst) {
        if (inst->state == TI_FAILED)
            error_tok(vm, site, "instantiation of '%s' failed earlier",
                      mangled);
        return inst;
    }
    if (vm->compiler.tmpl_depth >= TMPL_MAX_DEPTH)
        error_tok(vm, site, "template instantiation depth exceeds %d",
                  TMPL_MAX_DEPTH);

    inst = arena_alloc(&vm->compiler.parser_arena, sizeof(*inst));
    memset(inst, 0, sizeof(*inst));
    inst->tmpl    = t;
    inst->mangled = mangled;
    inst->state   = TI_IN_PROGRESS;
    hashmap_put(&vm->compiler.template_insts, mangled, inst);

    ExpansionFrame *frame =
        arena_alloc(&vm->compiler.parser_arena, sizeof(ExpansionFrame));
    memset(frame, 0, sizeof(*frame));
    frame->kind        = EXPANSION_TEMPLATE_INST;
    frame->name        = t->name;
    frame->target_desc = mangled;
    frame->call_tok    = site;
    frame->parent      = tok_rare(site)->expansion;

    // The prototype or `struct M;` is declared first so the body can refer to
    // the instance (recursion, `struct M *next`).
    Token *forward;
    if (t->kind == TMPL_FUNC) {
        forward = clone_template(vm, t, mangled, t->body_open, true, frame);
    } else {
        forward = tokenize_string(
            vm, "<template>",
            arena_format(vm, "%s %s;",
                         t->kind == TMPL_UNION ? "union" : "struct", mangled));
        convert_pp_tokens(vm, forward);
    }
    Token *whole    = clone_template(vm, t, mangled, NULL, false, frame);

    Scope *bindings = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
    memset(bindings, 0, sizeof(Scope));

    TmplSnapshot snap;
    snapshot_save(vm, &snap);
    jmp_buf  jb;
    jmp_buf *saved_jb = vm->error_jmp_buf;
    vm->error_jmp_buf = &jb;
    if (setjmp(jb)) {
        snapshot_restore(vm, &snap);
        vm->compiler.tmpl_depth--;
        inst->state       = TI_FAILED;
        vm->error_jmp_buf = saved_jb;
        longjmp(*saved_jb, 1);
    }

    snapshot_clear(vm);
    vm->compiler.scope = vm->compiler.runtime_file_scope;
    for (int i = 0; i < nargs; i++) {
        vm->compiler.scope = bindings;
        VarScope *vs = push_scope(vm, t->params[i]->loc, t->params[i]->len);
        vm->compiler.scope = vm->compiler.runtime_file_scope;
        bool tagged = args[i]->kind == TY_STRUCT || args[i]->kind == TY_UNION ||
                      args[i]->kind == TY_ENUM;
        vs->type_def = tagged ? args[i] : copy_type(vm, args[i]);
    }
    vm->compiler.tmpl_bindings = bindings;
    vm->compiler.tmpl_anchor   = vm->compiler.runtime_file_scope;
    vm->compiler.tmpl_depth++;

    cc_parse_file_scope_chain(vm, forward);
    Token *name_probe = synth_token(vm, mangled);
    if (t->kind == TMPL_FUNC) {
        VarScope *vs = scope_find_var(vm->compiler.runtime_file_scope, mangled,
                                      strlen(mangled));
        inst->fn     = vs ? vs->var : NULL;
    } else {
        inst->ty = find_tag(vm, name_probe);
    }
    cc_parse_file_scope_chain(vm, whole);
    if (t->kind == TMPL_FUNC && !inst->fn) {
        VarScope *vs = scope_find_var(vm->compiler.runtime_file_scope, mangled,
                                      strlen(mangled));
        inst->fn     = vs ? vs->var : NULL;
    }
    if (t->kind != TMPL_FUNC && !inst->ty)
        inst->ty = find_tag(vm, name_probe);

    vm->compiler.tmpl_depth--;
    snapshot_restore(vm, &snap);
    vm->error_jmp_buf = saved_jb;
    inst->state       = TI_DONE;
    return inst;
}

// Parses `( Name , Type {, Type} )` after the `__cccc_inst` marker at `tok` and
// returns the (possibly new) instance.
TemplateInstance *cc_parse_template_inst(VirtualMachine *vm, Token **rest,
                                         Token *tok) {
    Token *site = tok;
    tok         = skip(vm, tok->next, "(");
    Template *t = cc_find_template(vm, tok);
    if (!t)
        error_tok(vm, tok, "'%.*s' is not a template", (int)tok->len, tok->loc);
    tok = tok->next;

    Type *args[16];
    int   n = 0;
    while (equal(tok, ",")) {
        if (n == 16)
            error_tok(vm, tok, "too many template arguments");
        args[n++] = typename(vm, &tok, tok->next);
    }
    *rest = skip(vm, tok, ")");
    return instantiate_template(vm, t, args, n, site);
}

// `@inst(Name, Types...)` where a type is expected.
Type *cc_template_inst_type(VirtualMachine *vm, Token **rest, Token *tok) {
    TemplateInstance *inst = cc_parse_template_inst(vm, rest, tok);
    if (inst->tmpl->kind == TMPL_FUNC)
        error_tok(vm, tok, "template '%s' is a function, not a type",
                  inst->tmpl->name);
    return inst->ty;
}

// File-scope `@inst(Name, Types...);` instantiates without using the result.
// Returns the token after the `;`, or NULL when `tok` starts something else.
Token *cc_template_force_inst(VirtualMachine *vm, Token *tok) {
    if (!equal(tok, "__cccc_inst") || !equal(tok->next, "("))
        return NULL;
    Token *after = skip_parens(tok->next);
    if (!equal(after, ";"))
        return NULL;
    cc_parse_template_inst(vm, &after, tok);
    return skip(vm, after, ";");
}
