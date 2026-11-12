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

// `@template(T, int N...)` and `@inst(Arg...) Name` (also `Name@(Arg...)` and
// bare `@inst Name`). The preprocessor turns them into the identifier
// `__cccc_template` and the call-like `__cccc_inst ( Name, Arg... )`. A
// template (struct, union, enum, typedef or function) is kept as unparsed
// tokens; the first `@inst` of an argument list clones them under a mangled
// name and parses the clone at file scope with each type parameter bound to its
// argument type and each constant parameter to an enumerator-style constant.

#include "./parse_internal.h"

#define TMPL_MAX_DEPTH 64
#define TMPL_MAX_ARGS  16

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
        if ((equal(tok, "__attribute__") || equal(tok, "__attribute")) &&
            equal(tok->next, "(")) {
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

// A type specifier written with parentheses: `typeof(x)`, `_Atomic(T)`,
// `_BitInt(N)` or an `@inst` (`__cccc_inst ( Name, Arg... )`).
static bool is_type_paren_kw(Token *tok) {
    return (is_typeof_kw(tok) && !equal(tok, "_Alignas")) ||
           equal(tok, "_Atomic") || equal(tok, "_BitInt") ||
           equal(tok, "__cccc_inst");
}

static bool is_type_spec_kw(Token *tok) {
    static const char *const kw[] = {
        "void",        "_Bool",    "bool",       "char",       "short",
        "int",         "long",     "signed",     "unsigned",   "float",
        "double",      "_Complex", "_Imaginary", "__int128",   "__int128_t",
        "__uint128_t", "_Float16", "_Decimal32", "_Decimal64", "_Decimal128",
        "__auto_type",
    };
    for (size_t i = 0; i < sizeof kw / sizeof *kw; i++)
        if (equal(tok, kw[i]))
            return true;
    return false;
}

typedef struct {
    Token *name;    // the last identifier outside brackets and parameter lists
    Token *def_tag; // the tag of a struct, union or enum defined in the type
    Token *params;  // the last parameter list
} Declarator;

// Walks a template's declaration from `q`. The name is the last identifier
// outside brace bodies, brackets, parameter lists and attributes. An identifier
// before any type specifier is the type (`Foo` in `Foo (Fn)(T)`), so a `(`
// after it opens a grouped declarator. After the declarator's name or a `)` a
// `(` opens a parameter list unless it starts a grouped declarator.
// Returns the token that ends it: `;` for a typedef, and for a function the
// `{` of its body or the `;` or `=` that shows it has none. NULL on an error.
static Token *scan_declarator(Token *q, bool is_func, Declarator *d,
                              const char **err, Token **err_tok) {
    Token *prev      = NULL;
    bool   have_type = false;
    while (q->kind != TK_EOF && !equal(q, ";") &&
           !(is_func && (equal(q, "=") || (d->name && equal(q, "{"))))) {
        Token *g = skip_attr_groups(q);
        if (g != q) {
            q = g;
            continue;
        }
        if (equal(q, "_Alignas") && equal(q->next, "(")) {
            q = skip_parens(q->next);
            continue;
        }
        if (is_type_paren_kw(q) && equal(q->next, "(")) {
            q         = skip_parens(q->next);
            have_type = true;
            prev      = NULL;
            continue;
        }
        if (equal(q, "struct") || equal(q, "union") || equal(q, "enum")) {
            Token *tag = skip_attr_groups(q->next);
            have_type  = true;
            if (tag->kind == TK_IDENT) {
                Token *after = skip_attr_groups(tag->next);
                if (equal(after, "{") || (equal(q, "enum") && equal(after, ":")))
                    d->def_tag = tag;
                q    = tag->next;
                prev = NULL;
                continue;
            }
        }
        if (is_type_spec_kw(q))
            have_type = true;
        else if (q->kind == TK_IDENT && !have_type) {
            have_type = true;
            prev      = NULL;
            q         = q->next;
            continue;
        }
        if (equal(q, "{")) {
            Token *end = match_brace(q);
            if (!end) {
                *err     = "unterminated template body";
                *err_tok = q;
                return NULL;
            }
            q = end->next;
            continue;
        }
        if (equal(q, "[")) {
            int depth = 0;
            for (; q->kind != TK_EOF; q = q->next) {
                if (equal(q, "["))
                    depth++;
                else if (equal(q, "]") && --depth == 0)
                    break;
            }
            if (q->kind != TK_EOF)
                q = q->next;
            continue;
        }
        if (equal(q, ",")) {
            *err     = is_func ? "@template must be a struct, union, enum, "
                                 "typedef or function definition"
                               : "a typedef template declares one name";
            *err_tok = q;
            return NULL;
        }
        if (equal(q, "(")) {
            bool grouped = !prev || !(prev->kind == TK_IDENT || equal(prev, ")")) ||
                           equal(q->next, "*") || equal(q->next, "^") ||
                           equal(q->next, "(");
            if (!grouped) {
                d->params = q;
                q         = skip_parens(q);
                continue;
            }
        }
        if (q->kind == TK_IDENT)
            d->name = q;
        prev = q;
        q    = q->next;
    }
    return q;
}

// `@template(...) typedef ...;` declares one name.
static int scan_typedef_template(Token *p, Template *t, const char **err,
                                 Token **err_tok) {
    Declarator d = {};
    Token     *q = scan_declarator(p->next, false, &d, err, err_tok);
    if (!q)
        return -1;
    Token *name = d.name, *def_tag = d.def_tag;
    if (!equal(q, ";") || !name) {
        *err     = "a typedef template must declare a name";
        *err_tok = q;
        return -1;
    }
    if (def_tag && (def_tag->len != name->len ||
                    strncmp(def_tag->loc, name->loc, name->len))) {
        *err     = "the tag in a typedef template must be omitted or match "
                   "the typedef name";
        *err_tok = def_tag;
        return -1;
    }
    t->kind     = TMPL_TYPEDEF;
    t->name_tok = name;
    t->end      = q->next;
    return 1;
}

// Parses the parameter list between `p` (the `(`) and `close`, the token after
// its `)`, into `t`. Returns 0, or -1 with `err`/`err_tok` set.
static int scan_params(VirtualMachine *vm, Token *p, Token *close, Template *t,
                       const char **err, Token **err_tok) {
    int n         = 0;
    for (Token *a = p->next; a != close->next && !equal(a, ")"); a = a->next)
        n++;
    t->params = arena_alloc(&vm->compiler.parser_arena,
                            sizeof(TemplateParam) * (n + 1));
    for (Token *a = p->next; !equal(a, ")");) {
        Token *last = NULL, *eq = NULL;
        int    depth = 0;
        Token *e     = a;
        for (; e->kind != TK_EOF; e = e->next) {
            if (equal(e, "("))
                depth++;
            else if (equal(e, ")") && depth-- == 0)
                break;
            else if (equal(e, ",") && depth == 0)
                break;
            else if (equal(e, "=") && depth == 0 && !eq)
                eq = e;
            if (!eq)
                last = e;
        }
        Token *decl_end = eq ? eq : e;
        Token *dots     = NULL;
        int    dd       = 0;
        for (Token *q = a; q != decl_end && !dots; q = q->next) {
            if (equal(q, "("))
                dd++;
            else if (equal(q, ")"))
                dd--;
            else if (equal(q, "...") &&
                     (dd == 0 || (dd == 1 && q->next->kind == TK_IDENT &&
                                  equal(q->next->next, ")"))))
                dots = q;
        }
        if (dots) {
            if (eq) {
                *err     = "a template parameter pack cannot have a default";
                *err_tok = eq;
                return -1;
            }
            if (dots == a) {
                *err     = "expected a parameter before '...' in @template";
                *err_tok = dots;
                return -1;
            }
            if (dots->next == decl_end) {
                // `Ts...`: the name is the one token before the dots.
                if (a->next != dots || a->kind != TK_IDENT) {
                    *err     = "expected a type parameter name before '...'";
                    *err_tok = dots;
                    return -1;
                }
                last = a;
            }
        }
        if (t->nparams && t->params[t->nparams - 1].is_pack) {
            *err     = "a template parameter pack must be the last parameter";
            *err_tok = a;
            return -1;
        }
        if (last && equal(last, ")")) {
            // `int (*F)(int)`: the name is the identifier in `( * F )`.
            Token *found = NULL;
            for (Token *q = a; !found && q != decl_end; q = q->next) {
                if (!equal(q, "(") || !equal(q->next, "*"))
                    continue;
                Token *n = q->next->next;
                while (equal(n, "const") || equal(n, "volatile") ||
                       equal(n, "restrict"))
                    n = n->next;
                if (equal(n, "..."))
                    n = n->next;
                if (n->kind == TK_IDENT && equal(n->next, ")"))
                    found = n;
            }
            last = found;
        }
        if (!last || last->kind != TK_IDENT) {
            *err     = "expected a parameter name in @template";
            *err_tok = e;
            return -1;
        }
        if (eq && eq->next == e) {
            *err     = "expected a default value in @template";
            *err_tok = eq;
            return -1;
        }
        for (int i = 0; i < t->nparams; i++)
            if (t->params[i].name->len == last->len &&
                !strncmp(t->params[i].name->loc, last->loc, last->len)) {
                *err     = "duplicate template parameter";
                *err_tok = last;
                return -1;
            }
        if (!eq && !dots && t->nparams && t->params[t->nparams - 1].def) {
            *err     = arena_format(vm,
                                    "template parameter '%.*s' needs a default "
                                    "after a defaulted parameter",
                                    (int)last->len, last->loc);
            *err_tok = last;
            return -1;
        }
        t->params[t->nparams].name       = last;
        t->params[t->nparams].type_start = last == a ? NULL : a;
        t->params[t->nparams].decl_end   = decl_end;
        t->params[t->nparams].def        = eq ? eq->next : NULL;
        t->params[t->nparams].def_end    = eq ? e : NULL;
        t->params[t->nparams].is_pack    = dots != NULL;
        t->params[t->nparams].dots       = dots && last != a ? dots : NULL;
        t->nparams++;
        a = e;
        if (equal(a, ","))
            a = a->next;
        else if (!equal(a, ")")) {
            *err     = "expected ',' or ')' in @template parameter list";
            *err_tok = a;
            return -1;
        }
    }
    for (int i = 0; i < t->nparams; i++) {
        TemplateParam *d    = &t->params[i];
        Token         *prev = NULL;
        for (Token *k = d->def; d->def && k != d->def_end;
             prev = k, k = k->next) {
            if (k->kind != TK_IDENT ||
                (prev && (equal(prev, "struct") || equal(prev, "union") ||
                          equal(prev, "enum") || equal(prev, ".") ||
                          equal(prev, "->"))))
                continue;
            for (int j = i; j < t->nparams; j++)
                if (t->params[j].name->len == k->len &&
                    !strncmp(t->params[j].name->loc, k->loc, k->len)) {
                    *err = arena_format(
                        vm, "default of '%.*s' names %s parameter '%.*s'",
                        (int)d->name->len, d->name->loc,
                        j == i ? "its own" : "later", (int)k->len, k->loc);
                    *err_tok = k;
                    return -1;
                }
        }
    }
    for (int i = 1; i < t->nparams; i++) {
        TemplateParam *d = &t->params[i];
        for (int pass = 0; pass < 2; pass++) {
            Token *from = pass ? d->def : d->type_start;
            Token *to   = pass ? d->def_end : d->decl_end;
            Token *prev = NULL;
            for (Token *k = from; from && k != to; prev = k, k = k->next) {
                if (k->kind != TK_IDENT ||
                    (prev && (equal(prev, "struct") || equal(prev, "union") ||
                              equal(prev, "enum") || equal(prev, ".") ||
                              equal(prev, "->"))))
                    continue;
                for (int j = 0; j < i; j++)
                    if (t->params[j].name->len == k->len &&
                        !strncmp(t->params[j].name->loc, k->loc, k->len)) {
                        t->params[j].needed  = true;
                        d->dependent        |= pass == 0;
                    }
            }
        }
    }
    if (t->nparams == 0) {
        *err     = "@template needs at least one parameter";
        *err_tok = t->marker;
        return -1;
    }
    return 0;
}

// Classifies the declaration at `start`. Returns 0 when it is not a template
// (or, with `special`, a specialization), 1 with `t` filled in when it is, and
// -1 with `err`/`err_tok` set when it is a malformed one. Allocates only the
// parameter array; a specialization's argument list is left unparsed.
static int template_scan(VirtualMachine *vm, Token *start, Template *t,
                         const char **err, Token **err_tok, bool special) {
    const char *marker_kw = special ? "__cccc_specialize" : "__cccc_template";
    const char *what      = special ? "specialization" : "template";
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
                   !equal(probe, "{") && !equal(probe, marker_kw))
                probe = probe->next;
            if (!equal(probe, marker_kw))
                return 0;
            *err = arena_format(vm, "a %s cannot be typedef or extern", what);
            *err_tok = p;
            return -1;
        }
        if (is_linkage_kw(p)) {
            p = p->next;
            continue;
        }
        break;
    }
    if (!equal(p, marker_kw))
        return 0;

    t->marker = p;
    p         = p->next;
    if (!equal(p, "(")) {
        *err     = special ? "expected '(' after @specialize"
                           : "expected '(' after @template";
        *err_tok = p;
        return -1;
    }
    Token *close  = skip_parens(p);
    t->marker_end = close;

    if (!special && scan_params(vm, p, close, t, err, err_tok) < 0)
        return -1;

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
        if (!special && !t->pattern && equal(p, "__cccc_specialize") &&
            equal(p->next, "(")) {
            t->pattern     = p;
            t->pattern_end = skip_parens(p->next);
            p              = t->pattern_end;
            continue;
        }
        break;
    }

    if (equal(p, "struct") || equal(p, "union")) {
        t->kind = equal(p, "struct") ? TMPL_STRUCT : TMPL_UNION;
        p       = skip_attr_groups(p->next);
        if (p->kind != TK_IDENT) {
            *err =
                arena_format(vm, "a %s struct or union needs a tag name", what);
            *err_tok = p;
            return -1;
        }
        t->name_tok = p;
        p           = skip_attr_groups(p->next);
        if (!equal(p, "{")) {
            *err     = arena_format(vm, "a %s must be a definition", what);
            *err_tok = p;
            return -1;
        }
        Token *end  = match_brace(p);
        Token *semi = end ? skip_attr_groups(end->next) : NULL;
        if (!semi || !equal(semi, ";")) {
            *err = arena_format(
                vm, "a %s struct or union cannot declare variables", what);
            *err_tok = semi ? semi : p;
            return -1;
        }
        t->end = semi->next;
        return 1;
    }
    if (equal(p, "enum")) {
        t->kind = TMPL_ENUM;
        p       = skip_attr_groups(p->next);
        if (p->kind != TK_IDENT) {
            *err     = arena_format(vm, "a %s enum needs a tag name", what);
            *err_tok = p;
            return -1;
        }
        t->name_tok = p;
        p           = skip_attr_groups(p->next);
        if (equal(p, ":"))
            while (p->kind != TK_EOF && !equal(p, "{") && !equal(p, ";"))
                p = p->next;
        if (!equal(p, "{")) {
            *err     = arena_format(vm, "a %s must be a definition", what);
            *err_tok = p;
            return -1;
        }
        Token *end  = match_brace(p);
        Token *semi = end ? skip_attr_groups(end->next) : NULL;
        if (!semi || !equal(semi, ";")) {
            *err = arena_format(vm, "a %s enum cannot declare variables", what);
            *err_tok = semi ? semi : p;
            return -1;
        }
        t->end = semi->next;
        return 1;
    }
    if (equal(p, "typedef"))
        return scan_typedef_template(p, t, err, err_tok);

    Declarator d = {};
    Token     *q = scan_declarator(p, true, &d, err, err_tok);
    if (!q)
        return -1;
    if (!d.name || !d.params) {
        *err     = arena_format(vm,
                                "@%s must be a struct, union, enum, typedef or "
                                "function definition",
                                special ? "specialize" : "template");
        *err_tok = q->kind == TK_EOF ? p : q;
        return -1;
    }
    if (!equal(q, "{")) {
        *err     = arena_format(vm, "a %s must be a definition", what);
        *err_tok = q;
        return -1;
    }
    Token *end = match_brace(q);
    if (!end) {
        *err     = arena_format(vm, "unterminated %s body", what);
        *err_tok = q;
        return -1;
    }
    t->kind      = TMPL_FUNC;
    t->name_tok  = d.name;
    t->body_open = q;
    t->end       = end->next;
    return 1;
}

int cc_template_scan(VirtualMachine *vm, Token *start, Template *t,
                     const char **err, Token **err_tok) {
    return template_scan(vm, start, t, err, err_tok, false);
}

static void register_partial(VirtualMachine *vm, Template *pt);

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
    if (t->pattern) {
        register_partial(vm, t);
        return t->end;
    }
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

// Reports a stray `@template` or `@specialize` the capture arm did not consume.
void cc_template_misplaced(VirtualMachine *vm, Token *tok) {
    error_tok(vm, tok,
              "@%s is only allowed on a file-scope struct, union, enum, "
              "typedef or function definition",
              equal(tok, "__cccc_specialize") ? "specialize" : "template");
}

// ---- mangling -------------------------------------------------------------

// The instance name under construction. It starts on the stack and moves to
// the arena only if it outgrows that, so a repeat `@inst` allocates nothing.
typedef struct {
    VirtualMachine *vm;
    char           *p;
    size_t          len, cap;
    bool local; // an argument is, or is built from, a block-scope type
    char            init[256];
} MangleBuf;

static void mb_init(MangleBuf *mb, VirtualMachine *vm) {
    mb->vm      = vm;
    mb->p       = mb->init;
    mb->local   = false;
    mb->len     = 0;
    mb->cap     = sizeof mb->init;
    mb->init[0] = 0;
}

static void mb_putn(MangleBuf *mb, const char *s, size_t n) {
    if (mb->len + n + 1 > mb->cap) {
        size_t cap = mb->cap * 2;
        while (cap < mb->len + n + 1)
            cap *= 2;
        char *grown = arena_alloc(&mb->vm->compiler.parser_arena, cap);
        memcpy(grown, mb->p, mb->len);
        mb->p   = grown;
        mb->cap = cap;
    }
    memcpy(mb->p + mb->len, s, n);
    mb->len        += n;
    mb->p[mb->len]  = 0;
}

static void mb_puts(MangleBuf *mb, const char *s) {
    mb_putn(mb, s, strlen(s));
}

static void mb_putu(MangleBuf *mb, unsigned long long v) {
    char text[24];
    int  n = snprintf(text, sizeof text, "%llu", v);
    mb_putn(mb, text, n);
}

static void mangle_type(MangleBuf *mb, Type *ty, Token *site);

// The names of the functions around `fn`, outermost first (`f_g` for a GNU
// nested `g` in `f`).
static void mb_put_fn_chain(MangleBuf *mb, Obj *fn) {
    Obj *chain[16];
    int  n = 0;
    for (Obj *o = fn; o && n < 16; o = obj_fn(o)->parent_fn)
        if (!o->is_block && !o->is_omp_region && !o->is_omp_task)
            chain[n++] = o;
    while (n--) {
        mb_puts(mb, chain[n]->name);
        if (n)
            mb_puts(mb, "_");
    }
}

// An aggregate is named by its tag (`struct_S`), else by the typedef that names
// it (`typedef_P`). A block-scope aggregate also takes `__in_<function>`, and
// `__N` from the second same-spelled one in that function, in declaration
// order.
static void mangle_tag(MangleBuf *mb, Type *ty, Token *tag, const char *kw,
                       Token *site) {
    bool tagged = tag;
    for (Type *t = ty; !tag && t; t = t->origin)
        tag = type_agg(t)->typedef_name;
    if (!tag)
        error_tok(mb->vm, site,
                  "template argument must be a named type; give the %s a tag "
                  "or a typedef name",
                  kw);
    size_t start = mb->len;
    mb_puts(mb, tagged ? kw : "typedef");
    mb_puts(mb, "_");
    mb_putn(mb, tag->loc, tag->len);

    Type *root = ty;
    while (root->origin)
        root = root->origin;
    const TypeAgg *agg  = type_agg(root);
    mb->local          |= agg->block_scope || agg->local_inst;
    if (!agg->block_scope)
        return;
    if (!agg->scope_fn || !agg->scope_ord)
        error_tok(mb->vm, site,
                  "a %s declared outside a function body cannot be a template "
                  "argument",
                  kw);
    mb_puts(mb, "__in_");
    mb_put_fn_chain(mb, agg->scope_fn);
    if (agg->scope_ord > 1) {
        mb_puts(mb, "__");
        mb_putu(mb, agg->scope_ord);
    }
    HashMap *owners = &mb->vm->compiler.template_aggs;
    Type    *owner  = hashmap_get2(owners, mb->p + start, mb->len - start);
    if (!owner)
        hashmap_put2(owners, mb->p + start, mb->len - start, root);
    else if (owner != root)
        error_tok(mb->vm, site,
                  "cannot name the instance: '%s' is also the name of a "
                  "different instance",
                  mb->p);
}

static const char *addr_space_spelling(AddrSpace space) {
    switch (space) {
        case AS_GLOBAL:
            return "global";
        case AS_LOCAL:
            return "local";
        case AS_CONSTANT:
            return "constant";
        case AS_GENERIC:
            return "generic";
        default:
            return "private";
    }
}

static const char *checked_spelling(CheckedKind kind) {
    switch (kind) {
        case CHECKED_SINGLE:
            return "_single";
        case CHECKED_ARRAY:
            return "_array";
        case CHECKED_NTARRAY:
            return "_ntarray";
        default:
            return "";
    }
}

static Type *root_aggregate(Type *ty) {
    while (ty && (ty->kind == TY_PTR || ty->kind == TY_ARRAY))
        ty = ty->base;
    if (!ty || (ty->kind != TY_STRUCT && ty->kind != TY_UNION &&
                ty->kind != TY_ENUM))
        return NULL;
    while (ty->origin)
        ty = ty->origin;
    return ty;
}

static void mangle_signature(MangleBuf *mb, Type *ty, Token *site) {
    mb_puts(mb, "fn_");
    mangle_type(mb, ty->return_ty, site);
    mb_puts(mb, "_of");
    int n = 0;
    for (Type *p = ty->params; p; p = p->next, n++) {
        mb_puts(mb, "_");
        mangle_type(mb, p, site);
    }
    if (!n && !ty->is_variadic)
        mb_puts(mb, "_void");
    mb_puts(mb, ty->is_variadic ? "_va_end" : "_end");
}

static void mangle_base(MangleBuf *mb, Type *ty, Token *site) {
    const char *u = ty->is_unsigned ? "unsigned_" : "";
    switch (ty->kind) {
        case TY_VOID:
            return mb_puts(mb, "void");
        case TY_BOOL:
            return mb_puts(mb, "bool");
        case TY_CHAR:
            mb_puts(mb, u);
            return mb_puts(mb, "char");
        case TY_SHORT:
            mb_puts(mb, u);
            return mb_puts(mb, "short");
        case TY_INT:
            mb_puts(mb, u);
            return mb_puts(mb, "int");
        case TY_LONG:
            mb_puts(mb, u);
            return mb_puts(mb, ty->is_long_long ? "long_long" : "long");
        case TY_FLOAT:
            return mb_puts(mb, "float");
        case TY_DOUBLE:
            return mb_puts(mb, "double");
        case TY_LDOUBLE:
            return mb_puts(mb, "long_double");
        case TY_FLOAT16:
            return mb_puts(mb, "_Float16");
        case TY_DECIMAL32:
            return mb_puts(mb, "_Decimal32");
        case TY_DECIMAL64:
            return mb_puts(mb, "_Decimal64");
        case TY_DECIMAL128:
            return mb_puts(mb, "_Decimal128");
        case TY_NULLPTR_T:
            return mb_puts(mb, "nullptr_t");
        case TY_BITINT:
            mb_puts(mb, ty->is_unsigned ? "unsigned__" : "");
            mb_puts(mb, "_BitInt");
            return mb_putu(mb, ty->bit_width);
        case TY_COMPLEX:
            mb_puts(mb, "complex_");
            return mangle_type(mb, ty->base, site);
        case TY_VECTOR:
            mb_puts(mb, "vec");
            mb_putu(mb, ty->size);
            mb_puts(mb, "_");
            return mangle_type(mb, ty->base, site);
        case TY_ENUM:
            return mangle_tag(mb, ty, type_agg(ty)->enum_tag, "enum", site);
        case TY_STRUCT:
            return mangle_tag(mb, ty, type_agg(ty)->struct_tag, "struct", site);
        case TY_UNION:
            return mangle_tag(mb, ty, type_agg(ty)->struct_tag, "union", site);
        case TY_PTR:
            mangle_type(mb, ty->base, site);
            return mb_puts(mb, "_ptr");
        case TY_ARRAY:
            mangle_type(mb, ty->base, site);
            mb_puts(mb, "_arr");
            if (ty->array_len >= 0 && !ty->is_flexible)
                mb_putu(mb, ty->array_len);
            if (ty->checked_kind == CHECKED_ARRAY)
                return mb_puts(mb, "_checked");
            if (ty->checked_kind == CHECKED_NTARRAY)
                return mb_puts(mb, "_ntchecked");
            return;
        case TY_FUNC:
            return mangle_signature(mb, ty, site);
        case TY_BLOCK:
            mangle_signature(mb, ty, site);
            return mb_puts(mb, "_block");
        case TY_VLA:
            error_tok(mb->vm, site,
                      "a variable-length array cannot be a template argument");
        default:
            error_tok(mb->vm, site, "this type cannot be a template argument");
    }
}

static void mangle_type(MangleBuf *mb, Type *ty, Token *site) {
    if (ty->kind == TY_PTR) {
        mangle_base(mb, ty, site);
        mb_puts(mb, checked_spelling(ty->checked_kind));
        if (ty->addr_space != AS_NONE) {
            mb_puts(mb, "_");
            mb_puts(mb, addr_space_spelling(ty->addr_space));
        }
        if (ty->is_const)
            mb_puts(mb, "_const");
        if (ty->is_volatile)
            mb_puts(mb, "_volatile");
        if (ty->is_restrict)
            mb_puts(mb, "_restrict");
        if (ty->is_atomic)
            mb_puts(mb, "_atomic");
        return;
    }
    if (ty->addr_space != AS_NONE) {
        mb_puts(mb, addr_space_spelling(ty->addr_space));
        mb_puts(mb, "_");
    }
    if (ty->is_const)
        mb_puts(mb, "const_");
    if (ty->is_volatile)
        mb_puts(mb, "volatile_");
    if (ty->is_restrict)
        mb_puts(mb, "restrict_");
    if (ty->is_atomic)
        mb_puts(mb, "atomic_");
    mangle_base(mb, ty, site);
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

// The enumerator name tokens of an enum template.
static Token **enumerator_names(VirtualMachine *vm, Template *t, int *count) {
    Token *open = t->name_tok;
    while (!equal(open, "{"))
        open = open->next;
    int n = 0;
    for (Token *p = open->next; p != t->end && p->kind != TK_EOF; p = p->next)
        n++;
    Token **names =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Token *) * (n + 1));
    *count    = 0;
    int depth = 0;
    for (Token *p = open->next; p->kind != TK_EOF;) {
        p = skip_attr_groups(p);
        if (p->kind == TK_IDENT)
            names[(*count)++] = p;
        for (; p->kind != TK_EOF; p = p->next) {
            if (equal(p, "(") || equal(p, "[") || equal(p, "{"))
                depth++;
            else if (equal(p, ")") || equal(p, "]"))
                depth--;
            else if (equal(p, "}") && depth-- == 0)
                return names;
            else if (equal(p, ",") && depth == 0)
                break;
        }
        if (p->kind != TK_EOF)
            p = p->next;
    }
    return names;
}

static bool is_enumerator(Token **names, int count, Token *p) {
    for (int i = 0; i < count; i++)
        if (names[i]->len == p->len && !strncmp(names[i]->loc, p->loc, p->len))
            return true;
    return false;
}

// ---- parameter packs ------------------------------------------------------
//
// A template whose last parameter is a pack (`Ts...`, `int... Ns`) has each
// element bound under `__cccc_pk_<pack>_<k>`. expand_packs() rewrites the
// cloned tokens so the body only names those elements:
//   sizeof...(Ts)        the length
//   Ts...[k]  xs...[k]   element k
//   Ts... xs             a declaration per element: `T0 xs0, T1 xs1`
//   f(xs)...             the pattern once per element, comma separated
//   (xs + ...)           a fold over the elements
// A pattern may hold expansions and folds of its own: g(h(xs, xs...)...).
// `t.v...` over an instance's member pack is left alone here and expanded by
// cc_expand_member_packs() once the type of `t` is known.

static bool tmpl_has_pack(const Template *t) {
    return t->nparams && t->params[t->nparams - 1].is_pack;
}

typedef enum {
    DOTS_NONE,
    DOTS_PENDING,
    DOTS_SIZEOF,
    DOTS_INDEX,
    DOTS_FOLD,
    DOTS_ITEM,
    DOTS_OTHER // C varargs, GNU case and designator ranges
} DotsKind;

typedef struct {
    const char *name;
    int         len;
    bool        declared; // `Ts... xs`: its elements are xs0, xs1, ...
    bool        member;   // `t.v`: a member pack of an instance, see mpack
    int         n;        // its elements
    const char *key;      // member: the object's spelling and the name
} Pack;

typedef struct {
    int  op;     // the operator token
    int  a, b;   // the pattern, [a, b)
    int  ia, ib; // a binary fold's initial value, [ia, ib)
    bool right, binary;
} FoldShape;

typedef struct {
    VirtualMachine *vm;
    Token         **toks;
    int             ntoks, n;      // n: elements of a template pack
    bool            defer_members; // leave `t.v...` for the parser
    bool            copy_all;      // never relink the input tokens
    int            *mpack;      // by token: the member pack it names, else -1
    int            *match, *encl;
    DotsKind       *kind;
    int            *item_dots;  // by item start: the item's `...`, else -1
    int            *fold_close; // by `(`: its `)` when it is a fold, else -1
    int            *fold_dots;
    Pack           *packs;
    int             npacks;
    Token         **out;
    int             nout, cap;
    Token          *comma, *semi, *lp, *rp;
} Expander;

static bool tok_is_open(Token *t) {
    return equal(t, "(") || equal(t, "[") || equal(t, "{");
}

static bool tok_is_close(Token *t) {
    return equal(t, ")") || equal(t, "]") || equal(t, "}");
}

static bool brackets_pair(Token *open, Token *close) {
    return (equal(open, "(") && equal(close, ")")) ||
           (equal(open, "[") && equal(close, "]")) ||
           (equal(open, "{") && equal(close, "}"));
}

static bool is_fold_op(Token *t) {
    static const char *ops[] = {
        "+",  "-",  "*",  "/",  "%",  "^",  "&",   "|",   "<<", ">>", "&&",
        "||", ",",  "==", "!=", "<",  ">",  "<=",  ">=",  "=",  "+=", "-=",
        "*=", "/=", "%=", "^=", "&=", "|=", "<<=", ">>=", NULL};
    for (int i = 0; ops[i]; i++)
        if (equal(t, ops[i]))
            return true;
    return false;
}

static void ex_put(Expander *ex, Token *tok) {
    if (ex->nout == ex->cap) {
        int     ncap = ex->cap ? ex->cap * 2 : 64;
        Token **grown =
            arena_alloc(&ex->vm->compiler.parser_arena, sizeof(Token *) * ncap);
        if (ex->nout)
            memcpy(grown, ex->out, sizeof(Token *) * ex->nout);
        ex->out = grown;
        ex->cap = ncap;
    }
    ex->out[ex->nout++] = tok;
}

static void ex_punct(Expander *ex, Token *proto) {
    ex_put(ex, copy_tok(ex->vm, proto));
}

static void ex_text(Expander *ex, const char *text) {
    Token *t =
        tokenize_string(ex->vm, "<template>", arena_strdup(ex->vm, text));
    convert_pp_tokens(ex->vm, t);
    for (; t->kind != TK_EOF; t = t->next)
        ex_put(ex, copy_tok(ex->vm, t));
}

// The pack that token `i` names, else -1.
static int ex_pack_at(Expander *ex, int i) {
    Token *t = ex->toks[i];
    if (t->kind != TK_IDENT)
        return -1;
    if (ex->mpack && ex->mpack[i] >= 0)
        return ex->mpack[i];
    if (i > 0) {
        Token *p = ex->toks[i - 1];
        if (equal(p, ".") || equal(p, "->") || equal(p, "struct") ||
            equal(p, "union") || equal(p, "enum"))
            return -1;
    }
    for (int k = 0; k < ex->npacks; k++)
        if (!ex->packs[k].member && ex->packs[k].len == (int)t->len &&
            !strncmp(ex->packs[k].name, t->loc, t->len))
            return k;
    return -1;
}

// True when `[from, to)` holds `. name` or `-> name`.
static bool ex_member_access(Expander *ex, int from, int to) {
    for (int i = from + 1; i < to; i++)
        if (ex->toks[i]->kind == TK_IDENT &&
            (equal(ex->toks[i - 1], ".") || equal(ex->toks[i - 1], "->")))
            return true;
    return false;
}

static bool ex_has_pack(Expander *ex, int from, int to) {
    for (int i = from; i < to; i++)
        if (ex_pack_at(ex, i) >= 0)
            return true;
    return false;
}

// The first token of the list item that holds the `...` at `d`.
static int ex_item_start(Expander *ex, int d) {
    int i = d - 1;
    while (i >= 0) {
        Token *t = ex->toks[i];
        if (tok_is_close(t))
            i = ex->match[i] - 1;
        else if (tok_is_open(t) || equal(t, ",") || equal(t, ";"))
            break;
        else
            i--;
    }
    return i + 1;
}

// The token after the list item that holds the `...` at `d`: its `,`, `;` or
// closing bracket.
static int ex_item_end(Expander *ex, int d) {
    int i = d + 1;
    while (i < ex->ntoks) {
        Token *t = ex->toks[i];
        if (tok_is_open(t))
            i = ex->match[i] + 1;
        else if (tok_is_close(t) || equal(t, ",") || equal(t, ";"))
            break;
        else
            i++;
    }
    return i;
}

// Like ex_has_pack, but skips the items and folds that have their own `...`:
// their packs belong to them, not to an enclosing pattern.
static bool ex_has_free_pack(Expander *ex, int from, int to) {
    for (int i = from; i < to;) {
        if (ex->item_dots[i] >= 0)
            i = ex_item_end(ex, ex->item_dots[i]);
        else if (ex->fold_close[i] >= 0)
            i = ex->fold_close[i] + 1;
        else if (ex_pack_at(ex, i) >= 0)
            return true;
        else
            i++;
    }
    return false;
}

static Token *ex_elem(Expander *ex, int pack, int k, Token *like) {
    const Pack *p = &ex->packs[pack];
    Token      *c = copy_tok(ex->vm, like);
    c->loc =
        p->declared || p->member
            ? arena_format(ex->vm, "%.*s%d", p->len, p->name, k)
            : arena_format(ex->vm, "__cccc_pk_%.*s_%d", p->len, p->name, k);
    c->len = strlen(c->loc);
    return c;
}

static void ex_range(Expander *ex, int lo, int hi, int k);

// The length of the packs a pattern in `[from, to)` expands; they must agree.
static int ex_count(Expander *ex, int from, int to, Token *site) {
    int n = -1;
    for (int i = from; i < to;) {
        if (ex->item_dots[i] >= 0) {
            i = ex_item_end(ex, ex->item_dots[i]);
            continue;
        }
        if (ex->fold_close[i] >= 0) {
            i = ex->fold_close[i] + 1;
            continue;
        }
        int pk = ex_pack_at(ex, i++);
        if (pk < 0)
            continue;
        if (n >= 0 && ex->packs[pk].n != n)
            error_tok(ex->vm, site,
                      "the packs in one pattern have different lengths (%d "
                      "and %d)",
                      n, ex->packs[pk].n);
        n = ex->packs[pk].n;
    }
    return n >= 0 ? n : ex->n;
}

// The constant index of pack `pk` in `[from, to)`.
static int ex_index(Expander *ex, int pk, int from, int to, Token *site) {
    VirtualMachine *vm   = ex->vm;
    Token           head = {};
    Token          *cur  = &head;
    int             base = ex->nout;
    ex_range(ex, from, to, -1);
    for (int i = base; i < ex->nout; i++)
        cur = cur->next = copy_tok(vm, ex->out[i]);
    ex->nout  = base;
    cur->next = synth_token(vm, "");
    Token *rest;
    Node  *expr =
        cc_expand_const_macro_calls(vm, conditional(vm, &rest, head.next));
    if (rest->kind != TK_EOF)
        error_tok(vm, rest, "unexpected token in the pack index");
    add_type(vm, expr);
    if (!is_integer(expr->ty) || !is_const_expr(vm, expr))
        error_tok(vm, site, "a pack index must be an integer constant");
    int64_t v = eval(vm, expr);
    int     n = ex->packs[pk].n;
    if (v < 0 || v >= n)
        error_tok(vm, site, "pack index %lld is out of range for %d element%s",
                  (long long)v, n, n == 1 ? "" : "s");
    return (int)v;
}

static bool ex_fold_shape(Expander *ex, int o, int d, int c, FoldShape *f) {
    memset(f, 0, sizeof(*f));
    bool lop = d > o + 1 && is_fold_op(ex->toks[d - 1]);
    bool rop = d + 1 < c && is_fold_op(ex->toks[d + 1]);
    if (d == o + 1 && rop) { // ( ... op E )
        f->op = d + 1;
        f->a  = d + 2;
        f->b  = c;
        return ex_has_free_pack(ex, f->a, f->b);
    }
    if (d == c - 1 && lop) { // ( E op ... )
        f->op    = d - 1;
        f->a     = o + 1;
        f->b     = d - 1;
        f->right = true;
        return ex_has_free_pack(ex, f->a, f->b);
    }
    if (lop && rop) { // ( E op ... op I ) or ( I op ... op E )
        Token *l = ex->toks[d - 1], *r = ex->toks[d + 1];
        if (l->len != r->len || strncmp(l->loc, r->loc, l->len))
            return false;
        bool pl = ex_has_free_pack(ex, o + 1, d - 1);
        bool pr = ex_has_free_pack(ex, d + 2, c);
        if (pl && pr)
            error_tok(ex->vm, ex->toks[d],
                      "only one side of a binary fold can hold a parameter "
                      "pack");
        if (!pl && !pr)
            return false;
        f->binary = true;
        f->op     = d - 1;
        f->right  = pl;
        f->a      = pl ? o + 1 : d + 2;
        f->b      = pl ? d - 1 : c;
        f->ia     = pl ? d + 2 : o + 1;
        f->ib     = pl ? c : d - 1;
        return true;
    }
    return false;
}

static void ex_operand(Expander *ex, int a, int b, int k) {
    ex_punct(ex, ex->lp);
    ex_range(ex, a, b, k);
    ex_punct(ex, ex->rp);
}

// Replaces the parenthesised fold at `o` by explicitly nested parentheses.
static void ex_fold(Expander *ex, int o, int k) {
    FoldShape f;
    ex_fold_shape(ex, o, ex->fold_dots[o], ex->fold_close[o], &f);
    Token *op = ex->toks[f.op];
    int    n  = ex_count(ex, f.a, f.b, op);
    if (n == 0) {
        if (f.binary)
            ex_operand(ex, f.ia, f.ib, k);
        else if (equal(op, "&&"))
            ex_text(ex, "(1)");
        else if (equal(op, "||"))
            ex_text(ex, "(0)");
        else if (equal(op, ","))
            ex_text(ex, "((void)0)");
        else
            error_tok(ex->vm, op,
                      "a fold over '%.*s' of an empty pack needs an initial "
                      "value",
                      (int)op->len, op->loc);
        return;
    }
    int opens = f.binary ? n : n - 1;
    if (!f.right) {
        for (int i = 0; i < opens; i++)
            ex_punct(ex, ex->lp);
        if (f.binary)
            ex_operand(ex, f.ia, f.ib, k);
        else
            ex_operand(ex, f.a, f.b, 0);
        for (int j = f.binary ? 0 : 1; j < n; j++) {
            ex_punct(ex, op);
            ex_operand(ex, f.a, f.b, j);
            ex_punct(ex, ex->rp);
        }
        return;
    }
    for (int j = 0; j < opens; j++) {
        ex_punct(ex, ex->lp);
        ex_operand(ex, f.a, f.b, j);
        ex_punct(ex, op);
    }
    if (f.binary)
        ex_operand(ex, f.ia, f.ib, k);
    else
        ex_operand(ex, f.a, f.b, n - 1);
    for (int i = 0; i < opens; i++)
        ex_punct(ex, ex->rp);
}

// Expands the list item that starts at `s`; returns the index to resume at.
static int ex_item(Expander *ex, int s) {
    int d            = ex->item_dots[s];
    ex->item_dots[s] = -1;
    int  e           = ex_item_end(ex, d);
    bool decl        = d + 1 < e;
    if (decl && ex->toks[d + 1]->kind != TK_IDENT)
        error_tok(ex->vm, ex->toks[d + 1], "expected a name after '...'");
    bool semi = e < ex->ntoks && equal(ex->toks[e], ";");
    int  n    = ex_count(ex, s, d, ex->toks[d]);
    if (n == 0) {
        if (semi || (e < ex->ntoks && equal(ex->toks[e], ",")))
            return e + 1;
        if (s > 0 && ex->nout && ex->out[ex->nout - 1] == ex->toks[s - 1] &&
            equal(ex->toks[s - 1], ","))
            ex->nout--;
        return e;
    }
    int pk = -1;
    if (decl)
        for (int i = 0; i < ex->npacks; i++)
            if (ex->packs[i].declared &&
                ex->packs[i].len == (int)ex->toks[d + 1]->len &&
                !strncmp(ex->packs[i].name, ex->toks[d + 1]->loc,
                         ex->packs[i].len))
                pk = i;
    for (int k = 0; k < n; k++) {
        if (k)
            ex_punct(ex, semi ? ex->semi : ex->comma);
        ex_range(ex, s, d, k);
        if (decl) {
            ex_put(ex, ex_elem(ex, pk, k, ex->toks[d + 1]));
            ex_range(ex, d + 2, e, k);
        }
    }
    ex->item_dots[s] = d; // a nested item expands again per outer element
    return e;
}

// Emits `[lo, hi)` with every pack expanded. `k` is the element a bare pack
// name stands for inside a pattern, and -1 outside one.
static void ex_range(Expander *ex, int lo, int hi, int k) {
    VirtualMachine *vm = ex->vm;
    for (int i = lo; i < hi;) {
        Token *tok = ex->toks[i];
        if (ex->item_dots[i] >= 0) {
            i = ex_item(ex, i);
            continue;
        }
        if (ex->fold_close[i] >= 0) {
            ex_fold(ex, i, k);
            i = ex->fold_close[i] + 1;
            continue;
        }
        if (equal(tok, "sizeof") && i + 1 < hi &&
            ex->kind[i + 1] == DOTS_SIZEOF) {
            int close = ex->match[i + 2];
            int pack  = ex_pack_at(ex, close - 1);
            ex_put(ex,
                   synth_token(vm, arena_format(vm, "%d", ex->packs[pack].n)));
            i = close + 1;
            continue;
        }
        int pk = ex_pack_at(ex, i);
        if (pk >= 0) {
            if (i + 1 < hi && ex->kind[i + 1] == DOTS_INDEX) {
                int close = ex->match[i + 2];
                ex_put(ex, ex_elem(ex, pk, ex_index(ex, pk, i + 3, close, tok),
                                   tok));
                i = close + 1;
                continue;
            }
            if (k < 0)
                error_tok(vm, tok,
                          "parameter pack '%.*s' must be expanded with '...'",
                          (int)tok->len, tok->loc);
            ex_put(ex, ex_elem(ex, pk, k, tok));
            i++;
            continue;
        }
        ex_put(ex, k < 0 && !ex->copy_all ? tok : copy_tok(vm, tok));
        i++;
    }
}

// Splits the chain from `from` up to `to` (NULL: its EOF) into the Expander's
// token arrays; returns the token it stopped at.
static Token *ex_setup(Expander *ex, VirtualMachine *vm, Token *from,
                       Token *to) {
    int count = 0;
    for (Token *p = from; p != to && p->kind != TK_EOF; p = p->next)
        count++;
    Arena *a    = &vm->compiler.parser_arena;

    ex->vm      = vm;
    ex->ntoks   = count;
    ex->toks    = arena_alloc(a, sizeof(Token *) * (count + 1));
    Token *stop = from;
    for (int i = 0; i < count; i++, stop = stop->next)
        ex->toks[i] = stop;
    ex->match      = arena_alloc(a, sizeof(int) * (count + 1));
    ex->encl       = arena_alloc(a, sizeof(int) * (count + 1));
    ex->kind       = arena_alloc(a, sizeof(DotsKind) * (count + 1));
    ex->item_dots  = arena_alloc(a, sizeof(int) * (count + 1));
    ex->fold_close = arena_alloc(a, sizeof(int) * (count + 1));
    ex->fold_dots  = arena_alloc(a, sizeof(int) * (count + 1));
    ex->packs      = arena_alloc(a, sizeof(Pack) * (count + 1));
    memset(ex->kind, 0, sizeof(DotsKind) * (count + 1));
    for (int i = 0; i < count; i++)
        ex->match[i] = ex->item_dots[i] = ex->fold_close[i] = -1;

    int *stack = arena_alloc(a, sizeof(int) * (count + 1));
    int  sp    = 0;
    for (int i = 0; i < count; i++) {
        Token *tok  = ex->toks[i];
        ex->encl[i] = sp ? stack[sp - 1] : -1;
        if (tok_is_open(tok)) {
            stack[sp++] = i;
        } else if (tok_is_close(tok)) {
            if (!sp || !brackets_pair(ex->toks[stack[sp - 1]], tok))
                error_tok(vm, tok, "unbalanced '%.*s' in a template body",
                          (int)tok->len, tok->loc);
            int o        = stack[--sp];
            ex->match[o] = i;
            ex->match[i] = o;
            ex->encl[i]  = sp ? stack[sp - 1] : -1;
        }
    }
    if (sp)
        error_tok(
            vm, ex->toks[stack[sp - 1]], "unbalanced '%.*s' in a template body",
            (int)ex->toks[stack[sp - 1]]->len, ex->toks[stack[sp - 1]]->loc);
    return stop;
}

// What each `...` is, once the packs are known.
static void ex_classify(Expander *ex) {
    VirtualMachine *vm    = ex->vm;
    int             count = ex->ntoks;
    int            *cnt =
        arena_alloc(&vm->compiler.parser_arena, sizeof(int) * (count + 1));
    memset(cnt, 0, sizeof(int) * (count + 1));

    for (int d = 0; d < count; d++) {
        if (!equal(ex->toks[d], "..."))
            continue;
        if (d > 0 && equal(ex->toks[d - 1], "sizeof")) {
            int  close = d + 1 < count && equal(ex->toks[d + 1], "(")
                             ? ex->match[d + 1]
                             : -1;
            bool ok =
                close > d + 2 && ex_pack_at(ex, close - 1) >= 0 &&
                (close == d + 3 || (ex->mpack && ex->mpack[close - 1] >= 0));
            if (ok)
                ex->kind[d] = DOTS_SIZEOF;
            else if (ex->defer_members && close > d + 2 &&
                     ex_member_access(ex, d + 2, close))
                ex->kind[d] = DOTS_OTHER;
            else
                error_tok(vm, ex->toks[d], "expected 'sizeof...(pack)'");
        } else if (d > 0 && d + 1 < count && equal(ex->toks[d + 1], "[") &&
                   ex_pack_at(ex, d - 1) >= 0) {
            ex->kind[d] = DOTS_INDEX;
        } else {
            ex->kind[d] = DOTS_PENDING;
            if (ex->encl[d] >= 0)
                cnt[ex->encl[d]]++;
        }
    }
    for (int d = 0; d < count; d++) {
        if (ex->kind[d] != DOTS_PENDING)
            continue;
        int       o = ex->encl[d];
        FoldShape f;
        if (o >= 0 && equal(ex->toks[o], "(") && cnt[o] == 1 &&
            ex_fold_shape(ex, o, d, ex->match[o], &f)) {
            ex->kind[d]       = DOTS_FOLD;
            ex->fold_close[o] = ex->match[o];
            ex->fold_dots[o]  = d;
            continue;
        }
        int s = ex_item_start(ex, d);
        if (s == d) {
            ex->kind[d] = DOTS_OTHER;
            continue;
        }
        if (ex->item_dots[s] >= 0)
            error_tok(vm, ex->toks[d],
                      "a list item can hold only one pack expansion");
        if (!ex_has_free_pack(ex, s, d)) {
            Token *next = d + 1 < count ? ex->toks[d + 1] : NULL;
            if (!(ex->defer_members && ex_member_access(ex, s, d)) &&
                (!next || equal(next, ",") || equal(next, ")") ||
                 equal(next, ";")))
                error_tok(vm, ex->toks[d],
                          "the pattern before '...' contains no parameter "
                          "pack");
            ex->kind[d] = DOTS_OTHER;
            continue;
        }
        ex->kind[d]      = DOTS_ITEM;
        ex->item_dots[s] = d;
    }

    ex->comma = synth_token(vm, ",");
    ex->semi  = synth_token(vm, ";");
    ex->lp    = synth_token(vm, "(");
    ex->rp    = synth_token(vm, ")");
}

// Links the expanded tokens, which end in `stop`; returns the new chain.
static Token *ex_finish(Expander *ex, Token *stop) {
    if (!ex->nout)
        return stop;
    for (int i = 0; i + 1 < ex->nout; i++)
        ex->out[i]->next = ex->out[i + 1];
    ex->out[ex->nout - 1]->next = stop;
    return ex->out[0];
}

// Rewrites the cloned chain `chain` of template `t`, whose pack has `n`
// elements; returns the new chain, which ends in the same EOF. The packs a
// struct declares directly in its body go to `out_members`.
static Token *expand_packs(VirtualMachine *vm, Template *t, Token *chain, int n,
                           MemberPack **out_members) {
    Expander ex = {};
    ex.n        = n;
    Token *eof  = ex_setup(&ex, vm, chain, NULL);
    if (!ex.ntoks)
        return chain;
    ex.defer_members        = true;

    const TemplateParam *pp = &t->params[t->nparams - 1];
    ex.packs[ex.npacks++]   = (Pack){pp->name->loc, (int)pp->name->len, false};

    // Packs declared by `Pattern... name`.
    for (int d = 1; d + 1 < ex.ntoks; d++) {
        if (!equal(ex.toks[d], "...") || equal(ex.toks[d - 1], "sizeof") ||
            ex.toks[d + 1]->kind != TK_IDENT || ex_pack_at(&ex, d + 1) >= 0)
            continue;
        if (!ex_has_pack(&ex, ex_item_start(&ex, d), d))
            continue;
        Token *name           = ex.toks[d + 1];
        ex.packs[ex.npacks++] = (Pack){name->loc, (int)name->len, true};
        int o                 = ex.encl[d];
        if (out_members && o >= 0 && equal(ex.toks[o], "{") && ex.encl[o] < 0) {
            MemberPack *mp =
                arena_alloc(&vm->compiler.parser_arena, sizeof(*mp));
            mp->name     = name->loc;
            mp->len      = (int)name->len;
            mp->count    = n;
            mp->next     = *out_members;
            *out_members = mp;
        }
    }
    for (int k = 0; k < ex.npacks; k++)
        ex.packs[k].n = n;

    ex_classify(&ex);
    ex_range(&ex, 0, ex.ntoks, -1);
    return ex_finish(&ex, eof);
}

// ---- member packs -----------------------------------------------------------
//
// `Ts... v;` in an instance's struct declares v0, v1, ...; the instance's type
// keeps the pack (TypeAgg.member_packs). `t.v...` is expanded by
// cc_expand_member_packs() when the statement is about to be parsed, because
// only then is the type of `t` known.

static bool is_stmt_head(Token *t) {
    return equal(t, "if") || equal(t, "while") || equal(t, "for") ||
           equal(t, "switch");
}

// The token that ends the statement starting at `tok`: its `;`, or the `{`
// opening a nested block or the `}` closing the enclosing one.
static Token *stmt_range_end(Token *tok) {
    Token *prev = NULL, *paren_prev = NULL;
    bool   head_paren = false;
    int    depth      = 0;
    for (Token *p = tok; p->kind != TK_EOF; prev = p, p = p->next) {
        if (depth == 0) {
            if (equal(p, ";") || equal(p, "}") || equal(p, ")") ||
                equal(p, "]"))
                return p;
            if (equal(p, "{")) {
                bool block = p == tok || equal(prev, "else") ||
                             equal(prev, "do") || equal(prev, ":") ||
                             (equal(prev, ")") && head_paren);
                if (block)
                    return p;
            }
            if (equal(p, "("))
                paren_prev = prev;
        }
        if (tok_is_open(p)) {
            depth++;
        } else if (tok_is_close(p)) {
            if (--depth == 0 && equal(p, ")"))
                head_paren = paren_prev && (is_stmt_head(paren_prev) ||
                                            paren_prev->kind == TK_IDENT);
        }
    }
    return tok;
}

// The first token of the postfix chain that ends before the `.` or `->` at
// `dot`, else -1.
static int ex_object_start(Expander *ex, int dot) {
    int j = dot - 1;
    while (j >= 0) {
        Token *t = ex->toks[j];
        if (t->kind == TK_IDENT) {
            if (j >= 1 &&
                (equal(ex->toks[j - 1], ".") || equal(ex->toks[j - 1], "->"))) {
                j -= 2;
                continue;
            }
            return j;
        }
        if (!equal(t, ")") && !equal(t, "]"))
            return -1;
        int open = ex->match[j];
        if (equal(t, "]")) {
            j = open - 1;
            continue;
        }
        int before = open - 1;
        if (before < 0)
            return open;
        Token *b = ex->toks[before];
        if (equal(b, ")") && ex->match[before] > 0 &&
            is_stmt_head(ex->toks[ex->match[before] - 1]))
            return open;
        if (b->kind != TK_IDENT && !equal(b, ")") && !equal(b, "]"))
            return open;
        j = before;
    }
    return -1;
}

// Makes the member name at `i` (after `.` or `->`) a pack when the object
// before it has a member pack of that name.
static void ex_bind_member(Expander *ex, int i) {
    VirtualMachine *vm    = ex->vm;
    int             dot   = i - 1;
    int             start = ex_object_start(ex, dot);
    if (start < 0)
        return;
    Token *lead = ex->toks[start];
    if (lead->kind == TK_IDENT && !find_var(vm, lead))
        return;

    Token  head = {};
    Token *cur  = &head;
    for (int k = start; k < dot; k++)
        cur = cur->next = copy_tok(vm, ex->toks[k]);
    cur->next = synth_token(vm, "");
    Token *rest;
    Node  *obj = conditional(vm, &rest, head.next);
    if (rest->kind != TK_EOF)
        return;
    add_type(vm, obj);
    Type *root = obj->ty ? root_aggregate(obj->ty) : NULL;
    if (!root)
        return;
    Token      *name = ex->toks[i];
    MemberPack *mp   = type_agg(root)->member_packs;
    while (mp &&
           !(mp->len == name->len && !strncmp(mp->name, name->loc, mp->len)))
        mp = mp->next;
    if (!mp)
        return;

    const char *key = "";
    for (int k = start; k < dot; k++)
        key = arena_format(vm, "%s%.*s ", key, (int)ex->toks[k]->len,
                           ex->toks[k]->loc);
    key    = arena_format(vm, "%s%.*s", key, (int)name->len, name->loc);
    int pk = 0;
    while (pk < ex->npacks &&
           !(ex->packs[pk].member && !strcmp(ex->packs[pk].key, key)))
        pk++;
    if (pk == ex->npacks)
        ex->packs[ex->npacks++] =
            (Pack){name->loc, (int)name->len, false, true, mp->count, key};
    ex->mpack[i] = pk;
}

Token *cc_expand_member_packs(VirtualMachine *vm, Token *tok) {
    Token *end  = stmt_range_end(tok);
    bool   dots = false;
    for (Token *p = tok; p != end && !dots; p = p->next)
        dots = equal(p, "...");
    if (!dots)
        return tok;

    Expander ex = {};
    ex_setup(&ex, vm, tok, end);
    ex.copy_all = true;
    ex.mpack =
        arena_alloc(&vm->compiler.parser_arena, sizeof(int) * (ex.ntoks + 1));
    for (int i = 0; i < ex.ntoks; i++)
        ex.mpack[i] = -1;

    bool access = false;
    for (int d = 0; d < ex.ntoks; d++) {
        if (!equal(ex.toks[d], "..."))
            continue;
        int e = ex_item_end(&ex, d);
        for (int i = ex_item_start(&ex, d) + 1; i < e; i++) {
            if (ex.toks[i]->kind != TK_IDENT || ex.mpack[i] >= 0 ||
                !(equal(ex.toks[i - 1], ".") || equal(ex.toks[i - 1], "->")))
                continue;
            access = true;
            ex_bind_member(&ex, i);
        }
    }
    if (!access)
        return tok;
    ex_classify(&ex);
    if (!ex.npacks)
        return tok;
    ex_range(&ex, 0, ex.ntoks, -1);
    return ex_finish(&ex, end);
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
// synthesised ";" for a prototype) and an EOF. With `internal`, a function
// instance is `static`.
static Token *clone_template(VirtualMachine *vm, Template *t,
                             const char *mangled, Token *limit, bool prototype,
                             bool internal, ExpansionFrame *frame, int npack,
                             MemberPack **out_members) {
    Token  head = {};
    Token *cur  = &head;
    if (internal && t->kind == TMPL_FUNC && !has_linkage_static(t))
        cur = cur->next = synth_token(vm, "static");
    int     nenum = 0;
    Token **enums =
        t->kind == TMPL_ENUM ? enumerator_names(vm, t, &nenum) : NULL;

    Token *prev = NULL, *prev2 = NULL;
    Token *tag_kw = NULL, *tag_slot = NULL;
    for (Token *p                                              = t->start;
         p != (limit ? limit : t->end) && p->kind != TK_EOF; p = p->next) {
        if (p == t->marker) {
            p = t->marker_end;
            if (p == (limit ? limit : t->end))
                break;
        }
        if (p == t->pattern) {
            p = t->pattern_end;
            if (p == (limit ? limit : t->end))
                break;
        }
        Token *c = cur = cur->next     = copy_tok(vm, p);
        tok_rare_mut(vm, c)->expansion = frame;
        if (equal(p, "struct") || equal(p, "union") || equal(p, "enum")) {
            tag_kw   = p;
            tag_slot = skip_attr_groups(p->next);
        }
        bool rename = p->kind == TK_IDENT && p->len == (size_t)t->name_len &&
                      !strncmp(p->loc, t->name, t->name_len);
        if (rename) {
            bool member = prev && (equal(prev, ".") || equal(prev, "->"));
            bool inst_arg =
                prev && prev2 && equal(prev, "(") && equal(prev2, "__cccc_inst");
            bool is_tag = p == tag_slot;
            switch (t->kind) {
                case TMPL_FUNC:
                    rename = !(member || is_tag) && !inst_arg;
                    break;
                case TMPL_TYPEDEF:
                    rename = !member && !inst_arg;
                    break;
                case TMPL_ENUM:
                    rename = is_tag && equal(tag_kw, "enum") && !inst_arg;
                    break;
                default:
                    rename = is_tag && !equal(tag_kw, "enum") && !inst_arg;
            }
        }
        if (rename) {
            c->loc = (char *)mangled;
            c->len = strlen(mangled);
        } else if (enums && p->kind == TK_IDENT && is_enumerator(enums, nenum, p) &&
                   !(prev && (equal(prev, ".") || equal(prev, "->")))) {
            char *renamed = arena_format(vm, "%s__%.*s", mangled, (int)p->len, p->loc);
            c->loc        = renamed;
            c->len        = strlen(renamed);
        }
        prev2 = prev;
        prev  = p;
    }
    if (prototype)
        cur = cur->next = synth_token(vm, ";");
    Token *eof = synth_token(vm, "");
    cur->next  = eof;
    return npack >= 0 ? expand_packs(vm, t, head.next, npack, out_members)
                      : head.next;
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
    Template           *pattern;
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
    X(anchor, tmpl_anchor)                                                     \
    X(pattern, tmpl_pattern)

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

// The tokens `[from, to)` less `skip` as a chain ending in EOF.
static Token *clone_range_skip(VirtualMachine *vm, Token *from, Token *to,
                               Token *skip) {
    Token  head = {};
    Token *cur  = &head;
    for (Token *p = from; p != to; p = p->next)
        if (p != skip)
            cur = cur->next = copy_tok(vm, p);
    cur->next = synth_token(vm, "");
    return head.next;
}

static Token *clone_range(VirtualMachine *vm, Token *from, Token *to) {
    return clone_range_skip(vm, from, to, NULL);
}

static char *range_text(VirtualMachine *vm, Token *from, Token *to,
                        Token *skip) {
    char *s = "";
    for (Token *p = from; p != to; p = p->next)
        if (p != skip)
            s = arena_format(vm, "%s%s%.*s", s, *s ? " " : "", (int)p->len,
                             p->loc);
    return s;
}

static bool const_fits(Type *ty, int64_t v, bool v_unsigned) {
    int  bits   = ty->kind == TY_BOOL     ? 1
                  : ty->kind == TY_BITINT ? ty->bit_width
                                          : ty->size * 8;
    bool is_uns = ty->kind == TY_BOOL || ty->is_unsigned;
    bool neg    = !v_unsigned && v < 0;
    if (is_uns)
        return !neg && (bits >= 64 || (uint64_t)v < (1ULL << bits));
    if (neg)
        return bits >= 64 || v >= -(int64_t)(1ULL << (bits - 1));
    return (uint64_t)v <=
           (bits >= 64 ? (uint64_t)INT64_MAX : (1ULL << (bits - 1)) - 1);
}

static bool pointer_kind_matches(const Obj *sym, Type *pty) {
    return (pty->base->kind == TY_FUNC) == (sym->ty->kind == TY_FUNC);
}

// Whether `sym` points to a type compatible with parameter type `pty`'s
// pointee, ignoring top-level qualifiers.
static bool pointer_target_compatible(VirtualMachine *vm, const Obj *sym,
                                      Type *pty) {
    if (pty->base->kind == TY_VOID)
        return true;
    Type *have = sym->ty->kind == TY_ARRAY ? sym->ty->base : sym->ty;
    Type *a = copy_type(vm, pty->base), *b = copy_type(vm, have);
    a->is_const = a->is_volatile = b->is_const = b->is_volatile = false;
    return is_compatible(a, b);
}

static bool is_float_kind(Type *ty) {
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE;
}

// "integer", "floating" or "pointer": what a constant of type `ty` can bind.
static const char *const_kind(Type *ty) {
    return is_integer(ty)      ? "integer"
           : is_float_kind(ty) ? "floating"
                               : "pointer";
}

// Whether constant `arg` is a value that variable type `ty` can take: the same
// kind (integer, floating or pointer), and a value it represents exactly. An
// argument without a type is an integer (an array length).
static bool arg_fits(VirtualMachine *vm, Type *ty, const TemplateArg *arg) {
    Type *cty = arg->cty;
    if (is_integer(ty))
        return (!cty || is_integer(cty)) &&
               const_fits(ty, arg->val, arg->val_unsigned);
    if (is_float_kind(ty))
        return cty && is_float_kind(cty) &&
               (ty->kind == TY_DOUBLE || (double)(float)arg->fval == arg->fval);
    return cty && cty->kind == TY_PTR &&
           (!arg->sym || (pointer_kind_matches(arg->sym, ty) &&
                          pointer_target_compatible(vm, arg->sym, ty)));
}

// The pointer a constant argument names: a function, a file-scope or static
// object (or an array, as its first element), or null (NULL). `expr` is the
// argument and `pty` the parameter's pointer type.
static Obj *pointer_arg(VirtualMachine *vm, Node *expr, Type *pty, Token *site) {
    Node *n = expr;
    for (;;) {
        add_type(vm, n);
        if (n->kind == ND_CAST)
            n = n->lhs;
        else if (n->kind == ND_VAR && n->var->is_constexpr &&
                 n->var->ty->kind == TY_PTR && constexpr_expr_for_node(n))
            n = constexpr_expr_for_node(n); // a pointer parameter of another template
        else
            break;
    }
    if (n->ty->kind == TY_NULLPTR_T ||
        (is_integer(n->ty) && is_const_expr(vm, n) && eval(vm, n) == 0))
        return NULL;
    if (is_integer(n->ty))
        error_tok(vm, site,
                  "pointer template argument must be a function, the address "
                  "of an object, or null");

    Node *target = n->kind == ND_ADDR ? n->lhs : n;
    add_type(vm, target);
    if (target->kind != ND_VAR || (n->kind != ND_ADDR && !target->var->is_function &&
                                    target->var->ty->kind != TY_ARRAY))
        error_tok(vm, site,
                  "pointer template argument must name a function or the "
                  "address of a named object");
    Obj *sym = target->var;
    if (sym->is_string_literal)
        error_tok(vm, site,
                  "a string literal cannot be a template argument; pass a "
                  "named 'static const char x[]' as 'x'");
    if (sym->is_local || sym->is_local_symbol || sym->is_compound_literal ||
        sym->is_tls || sym->is_param)
        error_tok(vm, site,
                  "pointer template argument '%s' must be a function or an "
                  "object with file-scope or static storage",
                  sym->display_name ? sym->display_name : sym->name);

    if (!pointer_kind_matches(sym, pty))
        error_tok(vm, site,
                  pty->base->kind == TY_FUNC
                      ? "'%s' is an object but the template parameter "
                        "points to a function"
                      : "'%s' is a function but the template parameter "
                        "points to an object",
                  sym->name);
    if (!pointer_target_compatible(vm, sym, pty))
        error_tok(vm, site,
                  "template argument '%s' does not match the parameter's "
                  "pointer type",
                  sym->name);
    warn_implicit_conversion(vm, expr, pty, site);
    return sym;
}

// The value that initializes a pointer parameter's object.
static Node *pointer_value(VirtualMachine *vm, Obj *sym, Type *pty, Token *tok) {
    Node *n;
    if (!sym)
        n = new_num(vm, 0, tok);
    else {
        n = new_var_node(vm, sym, tok);
        if (sym->ty->kind != TY_FUNC && sym->ty->kind != TY_ARRAY)
            n = new_unary(vm, ND_ADDR, n, tok);
    }
    return new_cast(vm, n, pty);
}

// The type of constant parameter `i`, parsed from its declaration so it can
// name the type parameters bound before it. A type that names no earlier
// parameter is the same for every instance and is kept on the template.
static Type *param_type(VirtualMachine *vm, Template *t, int i) {
    TemplateParam *p = &t->params[i];
    if (p->ty)
        return p->ty;
    Token *rest;
    Token *toks = clone_range_skip(vm, p->type_start, p->decl_end, p->dots);
    Type  *ty   = declspec(vm, &rest, toks, NULL);
    ty          = declarator(vm, &rest, rest, ty);
    if (rest->kind != TK_EOF || !ty->name || ty->name->len != p->name->len ||
        strncmp(ty->name->loc, p->name->loc, p->name->len))
        error_tok(vm, p->name, "malformed constant template parameter '%.*s'",
                  (int)p->name->len, p->name->loc);
    if (ty->kind == TY_LDOUBLE)
        error_tok(vm, p->name,
                  "constant template parameter '%.*s' cannot have type "
                  "long double",
                  (int)p->name->len, p->name->loc);
    if (is_integer(ty)) {
        if (ty->size > 8 || (ty->kind == TY_BITINT && ty->bit_width > 64))
            error_tok(vm, p->name,
                      "constant template parameter '%.*s' must have an "
                      "integer type of at most 64 bits",
                      (int)p->name->len, p->name->loc);
    } else if (ty->kind != TY_FLOAT && ty->kind != TY_DOUBLE &&
               ty->kind != TY_PTR) {
        error_tok(vm, p->name,
                  "constant template parameter '%.*s' must have an "
                  "integer, float, double or pointer type",
                  (int)p->name->len, p->name->loc);
    }
    if (!p->dependent)
        p->ty = ty;
    return ty;
}

// Checks constant argument `arg` against parameter `i` of type `ty` and takes
// its value (`val`, `fval` or `sym`).
static void param_value(VirtualMachine *vm, Template *t, int i, Type *ty,
                        TemplateArg *arg, Token *site) {
    TemplateParam *p = &t->params[i];
    add_type(vm, arg->expr);
    Type *aty = arg->expr->ty;
    arg->cty  = ty;
    if (is_integer(ty)) {
        arg->val          = eval(vm, arg->expr);
        arg->val_unsigned = aty && aty->is_unsigned;
        if (!const_fits(ty, arg->val, arg->val_unsigned))
            error_tok(
                vm, site, "template argument %s does not fit '%s'",
                arg->val_unsigned
                    ? arena_format(vm, "%llu", (unsigned long long)arg->val)
                    : arena_format(vm, "%lld", (long long)arg->val),
                range_text(vm, p->type_start, p->name, p->dots));
    } else if (ty->kind == TY_PTR) {
        arg->sym = pointer_arg(vm, arg->expr, ty, site);
    } else {
        if (!aty || !is_numeric(aty) || is_complex(aty))
            error_tok(vm, site,
                      "template argument for '%.*s' must be a real number",
                      (int)p->name->len, p->name->loc);
        double v = eval_double(vm, arg->expr);
        if (isnan(v))
            error_tok(vm, site, "NaN is not a valid template argument");
        if (ty->kind == TY_FLOAT) {
            float f = (float)v;
            if (isinf(f) && !isinf(v))
                error_tok(vm, site, "template argument %s does not fit 'float'",
                          arena_format(vm, "%g", v));
            v = f;
        }
        arg->fval = v;
    }
}

// Binds parameter `i` in `bindings` to `arg`. A float or pointer parameter is
// bound to a constexpr object kept in `*hidden` until the instance has its
// name.
static void bind_param_as(VirtualMachine *vm, TemplateParam *p, Type *ty,
                          TemplateArg *arg, Scope *bindings, Obj **hidden) {
    Node *value = NULL;
    if (p->type_start) {
        ty = copy_type(vm, ty);
        if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE) {
            value                         = new_node(vm, ND_NUM, p->name);
            node_lit_mut(vm, value)->fval = arg->fval;
            value->ty = ty->kind == TY_FLOAT ? ty_float : ty_double;
        } else if (ty->kind == TY_PTR) {
            value = pointer_value(vm, arg->sym, ty, p->name);
        }
    }
    vm->compiler.scope = bindings;
    if (value) {
        Type *cty    = copy_type(vm, ty);
        cty->is_const = true;
        Obj *obj     = new_var(vm, arena_strndup(vm, p->name->loc, p->name->len),
                               (int)p->name->len, cty);
        obj->is_static     = true;
        obj->is_definition = true;
        obj->is_constexpr  = true;
        obj->is_maybe_unused = true;
        cc_finalize_const_gvar(vm, obj, value);
        *hidden = obj;
        vm->compiler.scope = vm->compiler.runtime_file_scope;
        return;
    }
    VarScope *vs       = push_scope(vm, p->name->loc, p->name->len);
    vm->compiler.scope = vm->compiler.runtime_file_scope;
    if (p->type_start) {
        vs->enum_ty  = ty;
        vs->enum_val = arg->val;
    } else {
        bool tagged  = ty->kind == TY_STRUCT || ty->kind == TY_UNION ||
                       ty->kind == TY_ENUM;
        vs->type_def = tagged ? ty : copy_type(vm, ty);
    }
}

static void bind_param(VirtualMachine *vm, Template *t, int i, Type *ty,
                       TemplateArg *arg, Scope *bindings, Obj **hidden) {
    bind_param_as(vm, &t->params[i], ty, arg, bindings, hidden);
}

// The shortest decimal that reads back as `v` in the parameter's type,
// spelled as an identifier: `n` for the sign, `p` for the point and `m` for a
// negative exponent (1.5 is `1p5`, -0.25 is `n0p25`, 1e-5 is `1em05`).
static void float_name(MangleBuf *mb, Type *ty, double v) {
    if (isinf(v))
        return mb_puts(mb, v < 0 ? "ninf" : "inf");
    bool   f32 = ty->kind == TY_FLOAT;
    double a   = fabs(v);
    char   sci[64];
    int    digits = 1;
    for (; digits < (f32 ? 9 : 17); digits++) {
        snprintf(sci, sizeof sci, "%.*e", digits - 1, a);
        if (f32 ? strtof(sci, NULL) == (float)a : strtod(sci, NULL) == a)
            break;
    }
    snprintf(sci, sizeof sci, "%.*e", digits - 1, a);
    int  exp10 = a == 0 ? 0 : atoi(strchr(sci, 'e') + 1);
    char text[64];
    if (exp10 >= -4 && exp10 < 15)
        snprintf(text, sizeof text, "%.*f",
                 digits - 1 - exp10 > 0 ? digits - 1 - exp10 : 0, a);
    else
        memcpy(text, sci, sizeof sci);

    char  out[80];
    char *w = out;
    if (signbit(v))
        *w++ = 'n';
    for (const char *r = text; *r; r++) {
        if (*r == '.')
            *w++ = 'p';
        else if (*r == '-')
            *w++ = 'm';
        else if (*r != '+')
            *w++ = *r;
    }
    mb_putn(mb, out, w - out);
}

// A constant argument is spelled by the first enumerator of its enum type with
// that value, `true`/`false` for bool, else by its number (`n` prefix when
// negative).
static void mangle_arg(MangleBuf *mb, Template *t, int i, Type *ty,
                       const TemplateArg *arg, Token *site) {
    if (!t->params[i].type_start)
        return mangle_type(mb, arg->ty, site);
    if (ty->kind == TY_BOOL)
        return mb_puts(mb, arg->val ? "true" : "false");
    if (ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE)
        return float_name(mb, ty, arg->fval);
    if (ty->kind == TY_PTR)
        return mb_puts(mb, arg->sym ? arg->sym->name : "null");
    bool neg = !arg->val_unsigned && arg->val < 0;
    char num[24];
    snprintf(num, sizeof num, neg ? "n%llu" : "%llu",
             neg ? -(unsigned long long)arg->val
                 : (unsigned long long)arg->val);
    if (ty->kind == TY_ENUM) {
        for (EnumConstant *ec = type_agg(ty)->enum_constants; ec; ec = ec->next)
            if (ec->value == arg->val)
                return mb_puts(mb, ec->name);
        for (EnumConstant *ec = type_agg(ty)->enum_constants; ec; ec = ec->next)
            if (!strcmp(ec->name, num))
                error_tok(mb->vm, site,
                          "cannot name the instance: enumerator '%s' clashes "
                          "with the number %s",
                          num, num);
    }
    mb_puts(mb, num);
}

// Parses a constant template argument at `tok` into `out`. Its value is taken
// in bind_param, once the parameter's type is known.
static void parse_const_arg(VirtualMachine *vm, Token **rest, Token *tok,
                            TemplateArg *out) {
    out->expr = cc_expand_const_macro_calls(vm, conditional(vm, rest, tok));
}

// Fills argument `i` from its default. Earlier parameters are already bound,
// so the default may name them.
//
// A checked pointer's count()/bounds() may name locals of the `@inst` site, so
// an argument carries only the checked kind: bounds stay per declaration.
static Type *strip_arg_bounds(VirtualMachine *vm, Type *ty) {
    if (!ty || (ty->kind != TY_PTR && ty->kind != TY_ARRAY))
        return ty;
    Type *base = strip_arg_bounds(vm, ty->base);
    if (base == ty->base && type_chk(ty)->form == CB_NONE)
        return ty;
    Type *copy = copy_type(vm, ty);
    copy->base = base;
    if (type_chk(ty)->form != CB_NONE) {
        TypeChecked *chk = type_chk_mut(vm, copy);
        chk->form        = CB_NONE;
        chk->arg1        = NULL;
        chk->arg2        = NULL;
    }
    return copy;
}

static void default_arg(VirtualMachine *vm, Template *t, int i,
                        TemplateArg *out) {
    TemplateParam *p    = &t->params[i];
    Token         *toks = clone_range(vm, p->def, p->def_end);
    Token         *rest;
    memset(out, 0, sizeof(*out));
    if (p->type_start)
        parse_const_arg(vm, &rest, toks, out);
    else
        out->ty = strip_arg_bounds(vm, typename(vm, &rest, toks));
    if (rest->kind != TK_EOF)
        error_tok(vm, rest, "unexpected token in the default of '%.*s'",
                  (int)p->name->len, p->name->loc);
}

// Records the instance's function, tag type or typedef once declared.
static void inst_lookup(VirtualMachine *vm, TemplateInstance *inst,
                        Token *name_probe) {
    switch (inst->tmpl->kind) {
        case TMPL_FUNC: {
            VarScope *vs = scope_find_var(vm->compiler.runtime_file_scope,
                                          inst->mangled, strlen(inst->mangled));
            inst->fn     = vs ? vs->var : NULL;
            break;
        }
        case TMPL_TYPEDEF:
            inst->ty = find_typedef(vm, name_probe);
            break;
        default:
            inst->ty = find_tag(vm, name_probe);
    }
}

// Tags a function instance so the linker can merge the copies of it that
// several translation units each instantiate.
static void mark_instance(VirtualMachine *vm, TemplateInstance *inst,
                          Template *t) {
    if (!inst->fn)
        return;
    inst->fn->is_tmpl_inst                = true;
    obj_fn_mut(vm, inst->fn)->tmpl_origin = t->start;
}

// Names the constexpr object of a bound float or pointer parameter (element
// `elem` of a pack, or -1) after the instance, and links it.
static void link_hidden(VirtualMachine *vm, Obj *obj, const char *mangled,
                        const TemplateParam *p, int elem) {
    if (!obj)
        return;
    obj->name = elem < 0 ? arena_format(vm, "__cccc_tp_%s__%.*s", mangled,
                                        (int)p->name->len, p->name->loc)
                         : arena_format(vm, "__cccc_tp_%s__%.*s_%d", mangled,
                                        (int)p->name->len, p->name->loc, elem);
    link_gvar(vm, obj);
}

#define TMPL_STACK_PARAMS 16

// Per-parameter state of one instantiation.
typedef struct {
    Type *ty;
    Obj  *hidden; // the constexpr object of a bound float or pointer parameter
    bool  bound;
} ParamState;

static void ensure_bindings(VirtualMachine *vm, Scope **bindings) {
    if (*bindings)
        return;
    *bindings = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
    memset(*bindings, 0, sizeof(Scope));
    vm->compiler.tmpl_bindings = *bindings;
}

// Binds parameter `i` once, creating the bindings scope on first use.
static void bind_state(VirtualMachine *vm, Template *t, int i,
                       ParamState *state, TemplateArg *args, Scope **bindings) {
    if (state[i].bound)
        return;
    ensure_bindings(vm, bindings);
    bind_param(vm, t, i, state[i].ty, &args[i], *bindings, &state[i].hidden);
    state[i].bound = true;
}

// Binds `n` elements of `pack` as `__cccc_pk_<name>_<k>`. A constant pack's
// elements have type `const_ty`; with `hidden`, element k's float or pointer
// object is stored in `hidden[k]`.
static void bind_pack_elems(VirtualMachine *vm, TemplateParam *pack,
                            const TemplateArg *els, int n, Type *const_ty,
                            Scope *bindings, Obj **hidden) {
    for (int k = 0; k < n; k++) {
        TemplateParam element = *pack;
        element.name    = synth_token(vm, arena_format(vm, "__cccc_pk_%.*s_%d",
                                                       (int)pack->name->len,
                                                       pack->name->loc, k));
        TemplateArg arg    = els[k];
        Obj        *unused = NULL;
        bind_param_as(vm, &element, pack->type_start ? const_ty : arg.ty, &arg,
                      bindings, hidden ? &hidden[k] : &unused);
    }
}

// Binds the elements `[fixed, total)` of the primary's pack.
static void bind_pack(VirtualMachine *vm, Template *t, int fixed, int total,
                      ParamState *state, TemplateArg *args, Scope **bindings) {
    if (total == fixed)
        return;
    ensure_bindings(vm, bindings);
    Obj *hidden[total - fixed];
    memset(hidden, 0, sizeof hidden);
    bind_pack_elems(vm, &t->params[fixed], args + fixed, total - fixed,
                    state[fixed].ty, *bindings, hidden);
    for (int k = 0; k < total - fixed; k++)
        state[fixed + k].hidden = hidden[k];
}

// ---- partial specialization -----------------------------------------------

// The arguments a pack variable took: `n` of them, then, when the argument
// list is another pattern with an open tail, every argument that tail stands
// for (`open_ty` is the type of each, `open_var` the variable of a constant
// pack). `vars` is the argument pattern's constant variables, if any.
typedef struct {
    const TemplateArg *els;
    int                n;
    const int         *vars;
    bool               open;
    Type              *open_ty;
    int                open_var;
} PackBind;

// What a partial specialization's variables stood for in one match. A constant
// is a value or, when two patterns are compared, a variable of the other. A
// pack variable is the slice of arguments it took.
typedef struct {
    Type              *ty[TMPL_MAX_ARGS];
    TemplateArg        cv[TMPL_MAX_ARGS];  // a constant variable's value
    int                var[TMPL_MAX_ARGS]; // or the argument pattern's variable
    bool               set[TMPL_MAX_ARGS];
    bool               is_var[TMPL_MAX_ARGS];
    PackBind           pk[TMPL_MAX_ARGS];
} Deduced;

// A match of `pt`'s pattern. When two partials are compared, `arg_pt` is the
// one whose pattern stands in for the arguments.
typedef struct {
    VirtualMachine *vm;
    Template       *pt;
    const Template *arg_pt;
    Deduced        *d;
    Token          *site;
} Matcher;

// A constant argument: a value, or variable `var` of the argument pattern.
typedef struct {
    const TemplateArg *arg;
    int                var;
} ConstArg;

// The parameter of `t` that entry `i` of an argument list belongs to.
static int param_at(const Template *t, int i) {
    return i < t->nparams ? i : t->nparams - 1;
}

static Type *origin_root(Type *ty) {
    while (ty->origin)
        ty = ty->origin;
    return ty;
}

// The variable of `pt` that placeholder type `ty` stands for, or -1.
static int pattern_var(const Template *pt, Type *ty) {
    ty = origin_root(ty);
    for (int k = 0; k < pt->nparams; k++)
        if (pt->vars[k] == ty)
            return k;
    return -1;
}

static InstPat *inst_pat_of(const Template *pt, Type *ty) {
    ty = origin_root(ty);
    for (InstPat *ip = pt ? pt->inst_pats : NULL; ip; ip = ip->next)
        if (ip->ty == ty)
            return ip;
    return NULL;
}

static ArrPat *arr_pat_of(const Template *pt, Type *ty) {
    ty = origin_root(ty);
    for (ArrPat *ap = pt ? pt->arr_pats : NULL; ap; ap = ap->next)
        if (ap->ty == ty)
            return ap;
    return NULL;
}

static bool pattern_has_var(const Template *pt, Type *ty) {
    if (!ty)
        return false;
    if (pattern_var(pt, ty) >= 0 || inst_pat_of(pt, ty))
        return true;
    switch (ty->kind) {
        case TY_ARRAY:
            if (arr_pat_of(pt, ty))
                return true;
            return pattern_has_var(pt, ty->base);
        case TY_PTR:
            return pattern_has_var(pt, ty->base);
        case TY_FUNC:
        case TY_BLOCK:
            if (pattern_has_var(pt, ty->return_ty))
                return true;
            for (Type *p = ty->params; p; p = p->next)
                if (pattern_has_var(pt, p))
                    return true;
            return false;
        default:
            return false;
    }
}

static void pattern_mark_vars(const Template *pt, Type *ty, bool *used,
                              bool tails) {
    if (!ty)
        return;
    int k = pattern_var(pt, ty);
    if (k >= 0) {
        used[k] = true;
        return;
    }
    InstPat *ip = inst_pat_of(pt, ty);
    if (ip) {
        for (int i = 0; i < ip->pl.n; i++) {
            if (!ip->tmpl->params[param_at(ip->tmpl, i)].type_start)
                pattern_mark_vars(pt, ip->pl.pat[i].ty, used, tails);
            else if (ip->pl.pat_var[i] >= 0)
                used[ip->pl.pat_var[i]] = true;
        }
        if (tails && ip->pl.tail >= 0)
            used[ip->pl.tail] = true;
        return;
    }
    switch (ty->kind) {
        case TY_ARRAY: {
            ArrPat *ap = arr_pat_of(pt, ty);
            if (ap)
                used[ap->var] = true;
        }
            // fallthrough
        case TY_PTR:
            return pattern_mark_vars(pt, ty->base, used, tails);
        case TY_FUNC:
        case TY_BLOCK:
            pattern_mark_vars(pt, ty->return_ty, used, tails);
            for (Type *p = ty->params; p; p = p->next)
                pattern_mark_vars(pt, p, used, tails);
            return;
        default:
            return;
    }
}

// Two arguments are one type when they name the same instance.
static bool same_arg_type(VirtualMachine *vm, Type *a, Type *b, Token *site) {
    MangleBuf x, y;
    mb_init(&x, vm);
    mb_init(&y, vm);
    mangle_type(&x, a, site);
    mangle_type(&y, b, site);
    return !strcmp(x.p, y.p) && root_aggregate(a) == root_aggregate(b);
}

static bool quals_within(const Type *pat, const Type *arg) {
    return (!pat->is_const || arg->is_const) &&
           (!pat->is_volatile || arg->is_volatile) &&
           (!pat->is_restrict || arg->is_restrict) &&
           (!pat->is_atomic || arg->is_atomic);
}

static bool quals_equal(const Type *a, const Type *b) {
    return a->is_const == b->is_const && a->is_volatile == b->is_volatile &&
           a->is_restrict == b->is_restrict && a->is_atomic == b->is_atomic;
}

static bool same_const(const TemplateArg *a, const TemplateArg *b) {
    return a->val == b->val && !memcmp(&a->fval, &b->fval, sizeof(double)) &&
           (a->sym == b->sym ||
            (a->sym && b->sym && !strcmp(a->sym->name, b->sym->name)));
}

// Binds constant variable `k` to `c`; a variable that is already bound must be
// bound to the same value.
static bool bind_const(Matcher *m, int k, ConstArg c) {
    Deduced *d = m->d;
    if (c.var < 0 && !arg_fits(m->vm, param_type(m->vm, m->pt, k), c.arg))
        return false;
    if (d->set[k]) {
        if (d->is_var[k] != (c.var >= 0))
            return false;
        return c.var >= 0 ? d->var[k] == c.var : same_const(&d->cv[k], c.arg);
    }
    d->set[k]    = true;
    d->is_var[k] = c.var >= 0;
    d->var[k]    = c.var;
    d->cv[k]     = *c.arg;
    return true;
}

static bool match_type(Matcher *m, Type *pat, Type *arg);

// Whether two argument lists bound to pack variable `k` are one list: the same
// length, each element the same type or value, and the same open tail.
static bool same_pack(Matcher *m, int k, const PackBind *a, const PackBind *b) {
    if (a->n != b->n || a->open != b->open)
        return false;
    bool is_const = m->pt->params[k].type_start;
    for (int i = 0; i < a->n; i++) {
        int av = a->vars ? a->vars[i] : -1, bv = b->vars ? b->vars[i] : -1;
        if (!is_const) {
            if (!same_arg_type(m->vm, a->els[i].ty, b->els[i].ty, m->site))
                return false;
        } else if (av >= 0 || bv >= 0) {
            if (av != bv)
                return false;
        } else if (!same_const(&a->els[i], &b->els[i])) {
            return false;
        }
    }
    if (!a->open)
        return true;
    if (is_const)
        return a->open_var == b->open_var;
    return a->open_ty && b->open_ty &&
           same_arg_type(m->vm, a->open_ty, b->open_ty, m->site);
}

// Binds `pl`'s pack variable to the arguments `[pl->n, nargs)`. A constant
// pack's elements must fit its type; when `apl` is the argument pattern, an
// element that is one of its variables fits any. With an element pattern, the
// variable is bound to what each argument matched. A variable that is already
// bound (it appears twice) must be bound to the same list.
static bool bind_tail(Matcher *m, const PatList *pl, const TemplateArg *args,
                      int nargs, const PatList *apl) {
    int      k = pl->tail, from = pl->n;
    Deduced *d    = m->d;
    bool     open = apl && apl->tail >= 0;
    if (m->pt->params[k].type_start)
        for (int i = from; i < nargs; i++)
            if (!(apl && apl->pat_var[i] >= 0) &&
                !arg_fits(m->vm, param_type(m->vm, m->pt, k), &args[i]))
                return false;
    const TemplateArg *els     = args + from;
    Type              *open_ty = NULL;
    if (pl->tail_pat) {
        TemplateArg *own =
            arena_alloc(&m->vm->compiler.parser_arena,
                        sizeof(TemplateArg) * (nargs - from + 1));
        for (int i = from; i < nargs; i++) {
            d->ty[k] = NULL;
            if (!match_type(m, pl->tail_pat, args[i].ty))
                return false;
            own[i - from].ty = d->ty[k];
        }
        // Another pattern's open tail stands for arguments of its own.
        if (open) {
            d->ty[k] = NULL;
            Type *other =
                apl->tail_pat ? apl->tail_pat : m->arg_pt->vars[apl->tail];
            if (!match_type(m, pl->tail_pat, other))
                return false;
            open_ty = d->ty[k];
        }
        d->ty[k] = NULL;
        els      = own;
    } else if (open && !m->pt->params[k].type_start) {
        open_ty = apl->tail_pat ? apl->tail_pat : m->arg_pt->vars[apl->tail];
    }
    PackBind got = {els,  nargs - from, apl ? apl->pat_var + from : NULL,
                    open, open_ty,      open ? apl->tail : -1};
    if (d->set[k])
        return same_pack(m, k, &d->pk[k], &got);
    d->set[k] = true;
    d->pk[k]  = got;
    return true;
}

// Matches the `nargs` arguments of template `t` against `pl`. `apl` is the
// argument list's own pattern when `args` is one, whose variables the
// constant arguments then are; its pack variable makes the list open-ended.
static bool match_args(Matcher *m, const Template *t, const PatList *pl,
                       const TemplateArg *args, int nargs, const PatList *apl) {
    if (pl->tail < 0 ? (apl && apl->tail >= 0) || nargs != pl->n
                     : nargs < pl->n)
        return false;
    for (int i = 0; i < pl->n; i++) {
        int pi = param_at(t, i);
        if (!t->params[pi].type_start) {
            if (!match_type(m, pl->pat[i].ty, args[i].ty))
                return false;
            continue;
        }
        ConstArg c = {&args[i], apl ? apl->pat_var[i] : -1};
        if (pl->pat_var[i] < 0) {
            if (c.var >= 0 || !same_const(&pl->pat[i], &args[i]))
                return false;
        } else if (!bind_const(m, pl->pat_var[i], c)) {
            return false;
        }
    }
    return pl->tail < 0 || bind_tail(m, pl, args, nargs, apl);
}

// Matches `arg` against the instance pattern `ip`: another pattern naming the
// same template, or an instance of it made earlier.
static bool match_inst(Matcher *m, Type *pat, InstPat *ip, Type *arg) {
    if (!quals_equal(pat, arg) || pat->addr_space != arg->addr_space)
        return false;
    InstPat *other = inst_pat_of(m->arg_pt, arg);
    if (other)
        return other->tmpl == ip->tmpl &&
               match_args(m, ip->tmpl, &ip->pl, other->pl.pat, other->pl.n,
                          &other->pl);
    if ((arg->kind != TY_STRUCT && arg->kind != TY_UNION &&
         arg->kind != TY_ENUM) ||
        (m->arg_pt && pattern_var(m->arg_pt, arg) >= 0))
        return false;
    Type  *root = origin_root(arg);
    Token *tag  = arg->kind == TY_ENUM ? type_agg(root)->enum_tag
                                       : type_agg(root)->struct_tag;
    if (!tag)
        return false;
    TemplateInstance *in =
        hashmap_get2(&m->vm->compiler.template_insts, tag->loc, tag->len);
    if (!in || in->tmpl != ip->tmpl || !in->ty || origin_root(in->ty) != root)
        return false;
    return match_args(m, ip->tmpl, &ip->pl, in->args, in->nargs, NULL);
}

// Matches `arg` against pattern type `pat`, binding variables in `m->d`. A
// variable written with qualifiers (`const T`) binds the argument without them.
static bool match_type(Matcher *m, Type *pat, Type *arg) {
    VirtualMachine *vm = m->vm;
    int             k  = pattern_var(m->pt, pat);
    if (k >= 0) {
        Deduced *d = m->d;
        if (!quals_within(pat, arg))
            return false;
        Type *bound = arg;
        if (pat->is_const || pat->is_volatile || pat->is_restrict ||
            pat->is_atomic) {
            bound               = copy_type(vm, arg);
            bound->is_const    &= !pat->is_const;
            bound->is_volatile &= !pat->is_volatile;
            bound->is_restrict &= !pat->is_restrict;
            bound->is_atomic   &= !pat->is_atomic;
        }
        if (d->ty[k])
            return same_arg_type(vm, d->ty[k], bound, m->site);
        d->ty[k] = bound;
        return true;
    }
    InstPat *ip = inst_pat_of(m->pt, pat);
    if (ip)
        return match_inst(m, pat, ip, arg);
    if (!pattern_has_var(m->pt, pat))
        return same_arg_type(vm, pat, arg, m->site);
    if (pat->kind != arg->kind || !quals_equal(pat, arg) ||
        pat->addr_space != arg->addr_space)
        return false;
    switch (pat->kind) {
        case TY_PTR:
            return pat->checked_kind == arg->checked_kind &&
                   match_type(m, pat->base, arg->base);
        case TY_ARRAY: {
            ArrPat *ap    = arr_pat_of(m->pt, pat);
            ArrPat *other = arr_pat_of(m->arg_pt, arg);
            if (ap) {
                TemplateArg len = {.val = arg->array_len};
                ConstArg    c   = {&len, -1};
                if (other)
                    c.var = other->var;
                else if (arg->is_flexible || arg->array_len < 0)
                    return false;
                return pat->checked_kind == arg->checked_kind &&
                       bind_const(m, ap->var, c) &&
                       match_type(m, pat->base, arg->base);
            }
            return !other && pat->array_len == arg->array_len &&
                   pat->is_flexible == arg->is_flexible &&
                   pat->checked_kind == arg->checked_kind &&
                   match_type(m, pat->base, arg->base);
        }
        case TY_FUNC:
        case TY_BLOCK: {
            if (pat->is_variadic != arg->is_variadic ||
                !match_type(m, pat->return_ty, arg->return_ty))
                return false;
            Type *p = pat->params, *a = arg->params;
            for (; p && a; p = p->next, a = a->next)
                if (!match_type(m, p, a))
                    return false;
            return !p && !a;
        }
        default:
            return false;
    }
}

// Whether the instance with `args` is one that partial specialization `pt`
// covers; `d` receives the values of its variables.
static bool match_partial(VirtualMachine *vm, Template *pt,
                          const TemplateArg *args, int nargs, Deduced *d,
                          Token *site) {
    memset(d, 0, sizeof(*d));
    Matcher m = {vm, pt, NULL, d, site};
    return match_args(&m, pt->primary, &pt->pl, args, nargs, NULL);
}

// Whether every instance covered by `a` is also covered by `b`.
static bool pattern_covers(VirtualMachine *vm, Template *b, Template *a,
                           Token *site) {
    Deduced d = {};
    Matcher m = {vm, b, a, &d, site};
    return match_args(&m, b->primary, &b->pl, a->pl.pat, a->pl.n, &a->pl);
}

static bool more_specialised(VirtualMachine *vm, Template *a, Template *b,
                             Token *site) {
    return pattern_covers(vm, b, a, site) && !pattern_covers(vm, a, b, site);
}

// The most specialised partial specialization of `t` covering `args`, with its
// variables in `out`; NULL when none does.
static Template *select_partial(VirtualMachine *vm, Template *t,
                                const TemplateArg *args, int nargs,
                                Deduced *out, Token *site,
                                const char *mangled) {
    bool    any = false;
    Deduced d;
    for (Template *c = t->partials; c; c = c->next_partial) {
        if (!match_partial(vm, c, args, nargs, &d, site))
            continue;
        any       = true;
        bool best = true;
        for (Template *o = t->partials; o && best; o = o->next_partial) {
            Deduced od;
            best = o == c || !match_partial(vm, o, args, nargs, &od, site) ||
                   more_specialised(vm, c, o, site);
        }
        if (best) {
            match_partial(vm, c, args, nargs, out, site);
            return c;
        }
    }
    if (any)
        error_tok(vm, site,
                  "ambiguous partial specializations of '%s' for '%s'", t->name,
                  mangled);
    return NULL;
}

// With `def`, registers it as the hand-written definition of the argument list
// instead of instantiating; the first `@inst` of that list parses it.
static TemplateInstance *instantiate(VirtualMachine *vm, Template *t,
                                     TemplateArg *args, int nargs, Token *site,
                                     Template *def) {
    bool pack     = tmpl_has_pack(t);
    int  fixed    = t->nparams - pack; // parameters before the pack
    int  required = fixed;
    while (required > 0 && t->params[required - 1].def)
        required--;
    if (nargs < required || (!pack && nargs > t->nparams)) {
        if (pack)
            error_tok(vm, site,
                      "template '%s' takes at least %d argument%s, %d given",
                      t->name, required, required == 1 ? "" : "s", nargs);
        if (required == t->nparams)
            error_tok(vm, site, "template '%s' takes %d argument%s, %d given",
                      t->name, t->nparams, t->nparams == 1 ? "" : "s", nargs);
        error_tok(vm, site, "template '%s' takes %d to %d arguments, %d given",
                  t->name, required, t->nparams, nargs);
    }
    int total = pack ? (nargs > fixed ? nargs : fixed)
                     : t->nparams; // arguments after the defaults
    // A repeat @inst keeps its state on the stack; only a new instance moves
    // its arguments to the arena.
    TemplateArg  stack_args[TMPL_STACK_PARAMS];
    ParamState   stack_state[TMPL_STACK_PARAMS];
    TemplateArg *given = args;
    ParamState  *state = stack_state;
    if (total <= TMPL_STACK_PARAMS) {
        args = stack_args;
    } else {
        args = arena_alloc(&vm->compiler.parser_arena,
                           sizeof(TemplateArg) * total);
        state =
            arena_alloc(&vm->compiler.parser_arena, sizeof(ParamState) * total);
    }
    memcpy(args, given, sizeof(TemplateArg) * nargs);
    memset(state, 0, sizeof(ParamState) * total);

    TemplateInstance *volatile inst = NULL;
    Scope       *bindings           = NULL;

    TmplSnapshot snap;
    snapshot_save(vm, &snap);
    jmp_buf  jb;
    jmp_buf *saved_jb = vm->error_jmp_buf;
    vm->error_jmp_buf = &jb;
    if (setjmp(jb)) {
        snapshot_restore(vm, &snap);
        vm->error_jmp_buf = saved_jb;
        if (inst) {
            vm->compiler.tmpl_depth--;
            inst->state = TI_FAILED;
        }
        longjmp(*saved_jb, 1);
    }

    snapshot_clear(vm);
    vm->compiler.scope       = vm->compiler.runtime_file_scope;
    vm->compiler.tmpl_anchor = vm->compiler.runtime_file_scope;

    // The name is built from the arguments alone. A parameter is bound (made
    // visible to the template's later types and defaults) only when one of
    // them names it; the rest are bound if the instance turns out to be new.
    MangleBuf mb;
    mb_init(&mb, vm);
    mb_puts(&mb, t->name);
    for (int i = 0; i < total; i++) {
        int            pi = i < t->nparams ? i : t->nparams - 1;
        TemplateParam *p  = &t->params[pi];
        if (i >= nargs)
            default_arg(vm, t, i, &args[i]);
        state[i].ty = p->type_start ? param_type(vm, t, pi) : args[i].ty;
        if (p->type_start)
            param_value(vm, t, pi, state[i].ty, &args[i], site);
        mb_puts(&mb, "__");
        mangle_arg(&mb, t, pi, state[i].ty, &args[i], site);
        if (p->needed && !p->is_pack)
            bind_state(vm, t, i, state, args, &bindings);
    }

    TemplateInstance *found = hashmap_get(&vm->compiler.template_insts, mb.p);
    if (def && found)
        error_tok(vm, site,
                  found->state == TI_SPECIALIZED
                      ? "redefinition of specialization '%s'"
                      : "specialization of '%s' after its first use",
                  mb.p);
    if (def) {
        TemplateInstance *spec =
            arena_alloc(&vm->compiler.parser_arena, sizeof(*spec));
        memset(spec, 0, sizeof(*spec));
        spec->tmpl    = t;
        spec->mangled = arena_strndup(vm, mb.p, (int)mb.len);
        spec->state   = TI_SPECIALIZED;
        spec->spec    = def;
        spec->nargs   = total;
        spec->args    = memcpy(arena_alloc(&vm->compiler.parser_arena,
                                           sizeof(TemplateArg) * (total + 1)),
                               args, sizeof(TemplateArg) * total);
        hashmap_put(&vm->compiler.template_insts, spec->mangled, spec);
        snapshot_restore(vm, &snap);
        vm->error_jmp_buf = saved_jb;
        return spec;
    }
    bool from_spec = found && found->state == TI_SPECIALIZED;
    if (found) {
        bool clash = found->tmpl != t || found->nargs != total;
        for (int i = 0; !clash && i < total; i++) {
            int        pi = i < t->nparams ? i : t->nparams - 1;
            const Obj *x = found->args[i].sym, *y = args[i].sym;
            Type      *xa = root_aggregate(found->args[i].ty);
            Type      *ya = root_aggregate(args[i].ty);
            clash         = (t->params[pi].type_start &&
                             (!x != !y || (x && strcmp(x->name, y->name)))) ||
                            xa != ya;
        }
        if (clash)
            error_tok(vm, site,
                      "cannot name the instance: '%s' is also the name of "
                      "a different instance",
                      mb.p);
        if (!from_spec) {
            snapshot_restore(vm, &snap);
            vm->error_jmp_buf = saved_jb;
            if (found->state == TI_FAILED)
                error_tok(vm, site, "instantiation of '%s' failed earlier",
                          mb.p);
            return found;
        }
    }
    if (vm->compiler.tmpl_depth >= TMPL_MAX_DEPTH)
        error_tok(vm, site, "template instantiation depth exceeds %d",
                  TMPL_MAX_DEPTH);

    // The instance is private to this file when a block-scope type is among
    // its arguments, so the linker never merges it with another file's.
    bool      local = mb.local && !from_spec;
    char     *mangled;
    Template *impl  = NULL; // the definition cloned: t, or a partial of it
    int       npack = -1;   // elements of the impl's pack, -1 without one
    if (from_spec) {
        // The definition is plain C: the parameter names are not visible.
        mangled                    = found->mangled;
        inst                       = found;
        inst->state                = TI_IN_PROGRESS;
        vm->compiler.tmpl_bindings = NULL;
        t                          = found->spec;
    } else {
        Deduced deduced;
        impl = t->partials
                   ? select_partial(vm, t, args, total, &deduced, site, mb.p)
                   : NULL;
        if (impl) {
            // The partial's variables replace the primary's parameters.
            bindings = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
            memset(bindings, 0, sizeof(Scope));
            vm->compiler.tmpl_bindings = bindings;
            for (int k = 0; k < impl->nparams; k++) {
                TemplateParam *p = &impl->params[k];
                if (p->is_pack) {
                    npack        = deduced.pk[k].n;
                    Obj **hidden = arena_alloc(&vm->compiler.parser_arena,
                                               sizeof(Obj *) * (npack + 1));
                    memset(hidden, 0, sizeof(Obj *) * (npack + 1));
                    bind_pack_elems(vm, p, deduced.pk[k].els, npack,
                                    p->type_start ? param_type(vm, impl, k)
                                                  : NULL,
                                    bindings, hidden);
                    for (int e = 0; e < npack; e++)
                        link_hidden(vm, hidden[e], mb.p, p, e);
                    continue;
                }
                TemplateArg var    = {};
                Type       *var_ty = deduced.ty[k];
                if (p->type_start) {
                    var    = deduced.cv[k];
                    var_ty = param_type(vm, impl, k);
                }
                Obj *hidden = NULL;
                bind_param(vm, impl, k, var_ty, &var, bindings, &hidden);
                link_hidden(vm, hidden, mb.p, p, -1);
            }
        } else {
            for (int i = 0; i < fixed; i++)
                bind_state(vm, t, i, state, args, &bindings);
            if (pack) {
                bind_pack(vm, t, fixed, total, state, args, &bindings);
                npack = total - fixed;
            }
        }
        mangled = arena_strndup(vm, mb.p, (int)mb.len);
        args    = memcpy(arena_alloc(&vm->compiler.parser_arena,
                                     sizeof(TemplateArg) * (total + 1)),
                         args, sizeof(TemplateArg) * total);

        inst    = arena_alloc(&vm->compiler.parser_arena, sizeof(*inst));
        memset(inst, 0, sizeof(*inst));
        inst->tmpl    = t;
        inst->mangled = mangled;
        inst->state   = TI_IN_PROGRESS;
        inst->args    = args;
        inst->nargs   = total;
        hashmap_put(&vm->compiler.template_insts, mangled, inst);
        inst->next_inst = t->insts;
        t->insts        = inst;
    }
    if (!impl)
        impl = t;
    vm->compiler.tmpl_depth++;

    for (int i = 0; i < total && !from_spec && impl == t; i++)
        link_hidden(vm, state[i].hidden, mangled, &t->params[param_at(t, i)],
                    i < fixed ? -1 : i - fixed);

    ExpansionFrame *frame =
        arena_alloc(&vm->compiler.parser_arena, sizeof(ExpansionFrame));
    memset(frame, 0, sizeof(*frame));
    frame->kind        = EXPANSION_TEMPLATE_INST;
    frame->name        = t->name;
    frame->target_desc = mangled;
    frame->call_tok    = site;
    frame->parent      = tok_rare(site)->expansion;

    // The prototype or `struct M;` is declared first so the body can refer to
    // the instance (recursion, `struct M *next`). Enums and typedefs have none.
    Token *forward = NULL;
    if (t->kind == TMPL_FUNC) {
        forward = clone_template(vm, impl, mangled, impl->body_open, true,
                                 local, frame, npack, NULL);
    } else if (t->kind == TMPL_STRUCT || t->kind == TMPL_UNION) {
        forward = tokenize_string(
            vm, "<template>",
            arena_format(vm, "%s %s;",
                         t->kind == TMPL_UNION ? "union" : "struct", mangled));
        convert_pp_tokens(vm, forward);
    }
    MemberPack *member_packs = NULL;
    bool        aggregate = t->kind == TMPL_STRUCT || t->kind == TMPL_UNION ||
                            t->kind == TMPL_TYPEDEF;
    if (aggregate && tmpl_has_pack(t))
        vm->compiler.saw_variadic_aggregate = true;
    Token *whole = clone_template(vm, impl, mangled, NULL, false, local, frame,
                                  npack, aggregate ? &member_packs : NULL);

    Token *name_probe = synth_token(vm, mangled);
    if (forward) {
        cc_parse_file_scope_chain(vm, forward);
        inst_lookup(vm, inst, name_probe);
        mark_instance(vm, inst, impl);
    }
    cc_parse_file_scope_chain(vm, whole);
    if (!inst->fn && !inst->ty)
        inst_lookup(vm, inst, name_probe);
    mark_instance(vm, inst, impl);
    if (member_packs && inst->ty) {
        Type *root                           = root_aggregate(inst->ty);
        type_agg_mut(vm, root)->member_packs = member_packs;
    }
    if (local && inst->ty && t->kind != TMPL_TYPEDEF) {
        Type *root = inst->ty;
        while (root->origin)
            root = root->origin;
        type_agg_mut(vm, root)->local_inst = true;
    }

    vm->compiler.tmpl_depth--;
    snapshot_restore(vm, &snap);
    vm->error_jmp_buf = saved_jb;
    inst->state       = TI_DONE;
    return inst;
}

TemplateInstance *instantiate_template(VirtualMachine *vm, Template *t,
                                       TemplateArg *args, int nargs,
                                       Token *site) {
    return instantiate(vm, t, args, nargs, site, NULL);
}

// Parses argument `n` of `t` at `tok`: a type, or a constant expression where
// the template's parameter is a constant.
static void parse_inst_arg(VirtualMachine *vm, Template *t, int n, Token **rest,
                           Token *tok, TemplateArg *out) {
    memset(out, 0, sizeof(*out));
    int pi = n < t->nparams ? n : (tmpl_has_pack(t) ? t->nparams - 1 : n);
    if (pi < t->nparams && t->params[pi].type_start)
        parse_const_arg(vm, rest, tok, out);
    else
        out->ty = strip_arg_bounds(vm, typename(vm, rest, tok));
}

// The arguments in the list that starts at `tok` (its first `,`, or the `)`
// that ends it): one per depth-0 comma.
static int count_inst_args(Token *tok) {
    int n = 0, depth = 0;
    for (Token *p = tok; p->kind != TK_EOF; p = p->next) {
        if (tok_is_open(p))
            depth++;
        else if (tok_is_close(p)) {
            if (depth-- == 0)
                break;
        } else if (depth == 0 && equal(p, ","))
            n++;
    }
    return n;
}

static TemplateArg *alloc_args(VirtualMachine *vm, TemplateArg *stack, int n) {
    if (n <= TMPL_STACK_PARAMS)
        return stack;
    return arena_alloc(&vm->compiler.parser_arena, sizeof(TemplateArg) * n);
}

// Parses the rewritten `( Name {, Arg} )` after the `__cccc_inst` marker at
// `tok` and returns the (possibly new) instance. Each argument is a type or a
// constant expression according to the template's parameter at that position.
TemplateInstance *cc_parse_template_inst(VirtualMachine *vm, Token **rest,
                                         Token *tok) {
    Token *site = tok;
    tok         = skip(vm, tok->next, "(");
    Template *t = cc_find_template(vm, tok);
    if (!t)
        error_tok(vm, tok, "'%.*s' is not a template", (int)tok->len, tok->loc);
    tok = tok->next;

    TemplateArg  stack[TMPL_STACK_PARAMS];
    TemplateArg *args = alloc_args(vm, stack, count_inst_args(tok));
    int          n    = 0;
    while (equal(tok, ",")) {
        parse_inst_arg(vm, t, n, &tok, tok->next, &args[n]);
        n++;
    }
    *rest = skip(vm, tok, ")");
    return instantiate_template(vm, t, args, n, site);
}

// `@inst(Types...) Name` where a type is expected.
static Type *pattern_inst_type(VirtualMachine *vm, Token **rest, Token *tok);

Type *cc_template_inst_type(VirtualMachine *vm, Token **rest, Token *tok) {
    if (vm->compiler.tmpl_pattern) {
        Type *placeholder = pattern_inst_type(vm, rest, tok);
        if (placeholder)
            return placeholder;
    }
    TemplateInstance *inst = cc_parse_template_inst(vm, rest, tok);
    if (inst->tmpl->kind == TMPL_FUNC)
        error_tok(vm, tok, "template '%s' is a function, not a type",
                  inst->tmpl->name);
    return inst->ty;
}

// File-scope `@inst(Types...) Name;` instantiates without using the result.
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

int cc_specialize_scan(VirtualMachine *vm, Token *start, Template *t,
                       const char **err, Token **err_tok) {
    return template_scan(vm, start, t, err, err_tok, true);
}

static const char *kind_name(TemplateKind kind) {
    switch (kind) {
        case TMPL_STRUCT:
            return "struct";
        case TMPL_UNION:
            return "union";
        case TMPL_ENUM:
            return "enum";
        case TMPL_TYPEDEF:
            return "typedef";
        default:
            return "function";
    }
}

// Capture arm of parse_file_scope_decls: records `@specialize(Args) decl` as
// the definition of that argument list and returns the token after it, or NULL
// when `tok` starts something else.
Token *cc_specialize_capture(VirtualMachine *vm, Token *tok) {
    Template    scan;
    const char *err     = NULL;
    Token      *err_tok = NULL;
    int         r       = cc_specialize_scan(vm, tok, &scan, &err, &err_tok);
    if (r < 0)
        error_tok(vm, err_tok, "%s", err);
    if (r == 0)
        return NULL;
    if (vm->compiler.in_macro_mode)
        error_tok(vm, scan.marker,
                  "@specialize is not supported in comptime code");

    Template *t = cc_find_template(vm, scan.name_tok);
    if (!t)
        error_tok(vm, scan.name_tok, "'%.*s' is not a template",
                  (int)scan.name_tok->len, scan.name_tok->loc);
    if (t->kind != scan.kind)
        error_tok(vm, scan.name_tok, "'%s' is a %s template, not a %s", t->name,
                  kind_name(t->kind), kind_name(scan.kind));

    Token       *a = scan.marker->next->next;
    TemplateArg  stack[TMPL_STACK_PARAMS];
    TemplateArg *args = alloc_args(vm, stack, count_inst_args(a) + 1);
    int          n    = 0;
    while (!equal(a, ")")) {
        parse_inst_arg(vm, t, n, &a, a, &args[n]);
        n++;
        if (equal(a, ","))
            a = a->next;
        else if (!equal(a, ")"))
            error_tok(vm, a, "expected ',' or ')' in @specialize");
    }

    Template *def = arena_alloc(&vm->compiler.parser_arena, sizeof(Template));
    *def          = scan;
    def->name     = t->name;
    def->name_len = t->name_len;
    instantiate(vm, t, args, n, scan.marker, def);
    return scan.end;
}

// The end of the pattern argument starting at `a`: its `,` or the closing `)`.
static Token *pattern_arg_end(Token *a) {
    int depth = 0;
    for (; a->kind != TK_EOF; a = a->next) {
        if (equal(a, "(") || equal(a, "[") || equal(a, "{"))
            depth++;
        else if (equal(a, ")") || equal(a, "]") || equal(a, "}")) {
            if (depth-- == 0)
                break;
        } else if (depth == 0 && equal(a, ","))
            break;
    }
    return a;
}

static bool default_names_param(const Template *t, int i) {
    const TemplateParam *d = &t->params[i];
    for (Token *k = d->def; k != d->def_end; k = k->next)
        if (k->kind == TK_IDENT)
            for (int j = 0; j < i; j++)
                if (t->params[j].name->len == k->len &&
                    !strncmp(t->params[j].name->loc, k->loc, k->len))
                    return true;
    return false;
}

// Stores the exact value of constant argument `i` of `t` in a pattern.
static void pattern_const(VirtualMachine *vm, Template *t, int i,
                          TemplateArg *arg, Token *site) {
    if (t->params[i].dependent)
        error_tok(vm, site,
                  "a partial specialization cannot give a value for '%.*s': "
                  "its type depends on another parameter",
                  (int)t->params[i].name->len, t->params[i].name->loc);
    param_value(vm, t, i, param_type(vm, t, i), arg, site);
}

// The constant variable of `pt` that `tok` names, or -1.
static int const_var_index(const Template *pt, const Token *tok) {
    for (int k = 0; k < pt->nparams; k++)
        if (pt->params[k].type_start && !pt->params[k].is_pack &&
            pt->params[k].name->len == tok->len &&
            !strncmp(pt->params[k].name->loc, tok->loc, tok->len))
            return k;
    return -1;
}

// The pack variable of `pt` that `tok` names, or -1.
static int pack_var_index(const Template *pt, const Token *tok) {
    for (int k = 0; k < pt->nparams; k++)
        if (pt->params[k].is_pack && pt->params[k].name->len == tok->len &&
            !strncmp(pt->params[k].name->loc, tok->loc, tok->len))
            return k;
    return -1;
}

// The pack variable that `ty` names, or -1. A template has one pack.
static int pattern_pack_var(const Template *pt, Type *ty) {
    bool used[TMPL_MAX_ARGS] = {};
    pattern_mark_vars(pt, ty, used, false);
    for (int k = 0; k < pt->nparams; k++)
        if (used[k] && pt->params[k].is_pack)
            return k;
    return -1;
}

// The pack variable that the item `[a, e)` of a pattern list is, written
// `Ts...` or, with `*elem` set to the pattern each element must match,
// `Ts *...`; -1 for any other item. A pack variable used any other way is an
// error.
static int pattern_tail(VirtualMachine *vm, Template *pt, Template *t, Token *a,
                        Token *e, Type **elem) {
    *elem = NULL;
    if (a == e)
        return -1;
    int k = a->kind == TK_IDENT ? pack_var_index(pt, a) : -1;
    Token *last = a;
    while (last->next != e)
        last = last->next;
    if (!equal(last, "...")) {
        if (k >= 0 && a->next == e && !pt->pat_elem)
            error_tok(vm, a,
                      "parameter pack '%.*s' must be expanded with '...'",
                      (int)a->len, a->loc);
        return -1;
    }
    if (pt->pat_elem)
        error_tok(vm, last, "a pack expansion cannot be nested in another");
    if (!equal(e, ")"))
        error_tok(vm, last,
                  "a pack in a pattern is written alone as the last item, "
                  "'Ts...'");
    if (k < 0 || a->next != last) {
        Token *rest;
        pt->pat_elem = true;
        Type *ty     = typename(vm, &rest, a);
        pt->pat_elem = false;
        if (rest != last)
            error_tok(vm, rest, "unexpected token in the pattern");
        k = pattern_pack_var(pt, ty);
        if (k < 0)
            error_tok(vm, a, "the pattern before '...' names no pack variable");
        *elem = ty;
    }
    if (!tmpl_has_pack(t))
        error_tok(vm, a,
                  "cannot expand pack '%.*s' into template '%s', which is "
                  "not variadic",
                  (int)pt->params[k].name->len, pt->params[k].name->loc,
                  t->name);
    if (!t->params[t->nparams - 1].type_start != !pt->params[k].type_start)
        error_tok(vm, a, "pack '%.*s' is a %s pack, but '%s' takes %s there",
                  (int)pt->params[k].name->len, pt->params[k].name->loc,
                  pt->params[k].type_start ? "constant" : "type", t->name,
                  pt->params[k].type_start ? "types" : "constants");
    const TemplateParam *tp = &t->params[t->nparams - 1];
    if (tp->type_start && !tp->dependent &&
        strcmp(const_kind(param_type(vm, pt, k)),
               const_kind(param_type(vm, t, t->nparams - 1))))
        error_tok(vm, a,
                  "pack '%.*s' holds %s constants, but '%s' takes %s ones",
                  (int)pt->params[k].name->len, pt->params[k].name->loc,
                  const_kind(param_type(vm, pt, k)), t->name,
                  const_kind(param_type(vm, t, t->nparams - 1)));
    return k;
}

// Parses the arguments of template `t` in a pattern of `pt`, from `a` to the
// closing `)`, which it returns, into `pl`.
static Token *parse_pattern_args(VirtualMachine *vm, Template *pt, Template *t,
                                 Token *a, Token *site, PatList *pl) {
    int cap = count_inst_args(a) + 1;
    if (cap < t->nparams)
        cap = t->nparams;
    pl->pat =
        arena_alloc(&vm->compiler.parser_arena, sizeof(TemplateArg) * cap);
    pl->pat_var = arena_alloc(&vm->compiler.parser_arena, sizeof(int) * cap);
    memset(pl->pat, 0, sizeof(TemplateArg) * cap);
    for (int i = 0; i < cap; i++)
        pl->pat_var[i] = -1;
    pl->tail             = -1;
    pl->tail_pat         = NULL;

    TemplateArg *pat     = pl->pat;
    int         *pat_var = pl->pat_var;
    int          given   = 0;
    while (!equal(a, ")")) {
        Token *e = pattern_arg_end(a);
        if (e->kind == TK_EOF)
            error_tok(vm, a, "unterminated @specialize");
        pl->tail = pattern_tail(vm, pt, t, a, e, &pl->tail_pat);
        if (pl->tail >= 0) {
            a = e;
            break;
        }
        if (given == t->nparams && !tmpl_has_pack(t))
            error_tok(vm, a, "template '%s' takes %d argument%s, more given",
                      t->name, t->nparams, t->nparams == 1 ? "" : "s");
        int pi = param_at(t, given);
        int bare =
            a->next == e && a->kind == TK_IDENT ? const_var_index(pt, a) : -1;
        Token *rest = NULL;
        if (t->params[pi].type_start) {
            if (bare >= 0) {
                if (t->params[pi].dependent)
                    error_tok(vm, a,
                              "'%.*s' cannot bind '%.*s': its type depends on "
                              "another parameter",
                              (int)a->len, a->loc, (int)t->params[pi].name->len,
                              t->params[pi].name->loc);
                Type *var_ty = param_type(vm, pt, bare);
                Type *par_ty = param_type(vm, t, pi);
                if (strcmp(const_kind(var_ty), const_kind(par_ty)))
                    error_tok(vm, a,
                              "'%.*s' has %s type and cannot bind '%.*s', "
                              "which has %s type",
                              (int)a->len, a->loc, const_kind(var_ty),
                              (int)t->params[pi].name->len,
                              t->params[pi].name->loc, const_kind(par_ty));
                pat_var[given] = bare;
            } else {
                parse_const_arg(vm, &rest, a, &pat[given]);
                if (rest != e)
                    error_tok(vm, rest, "unexpected token in the pattern");
                pattern_const(vm, t, pi, &pat[given], a);
            }
        } else {
            if (bare >= 0)
                error_tok(vm, a, "constant '%.*s' cannot be a type argument",
                          (int)a->len, a->loc);
            parse_inst_arg(vm, t, given, &rest, a, &pat[given]);
            if (rest != e)
                error_tok(vm, rest, "unexpected token in the pattern");
            int pk = pt->pat_elem ? -1 : pattern_pack_var(pt, pat[given].ty);
            if (pk >= 0)
                error_tok(
                    vm, a, "parameter pack '%.*s' must be expanded with '...'",
                    (int)pt->params[pk].name->len, pt->params[pk].name->loc);
        }
        given++;
        a = equal(e, ",") ? e->next : e;
    }
    pl->n     = given;
    bool pack = tmpl_has_pack(t);
    int  fixed = t->nparams - pack;
    int  required = fixed;
    while (pack && required > 0 && t->params[required - 1].def)
        required--;
    if (pack && given < required)
        error_tok(vm, site,
                  "template '%s' takes at least %d argument%s, %d given",
                  t->name, required, required == 1 ? "" : "s", given);
    if (pack && pl->tail >= 0) {
        // The pack variable also takes the defaulted parameters it stands at.
        for (int i = given; i < fixed; i++)
            if (!t->params[i].type_start != !t->params[fixed].type_start)
                error_tok(vm, site,
                          "pack variable '%.*s' cannot stand for '%.*s', %s "
                          "parameter",
                          (int)pt->params[pl->tail].name->len,
                          pt->params[pl->tail].name->loc,
                          (int)t->params[i].name->len, t->params[i].name->loc,
                          t->params[i].type_start ? "a constant" : "a type");
        return a;
    }
    int end = pack ? fixed : t->nparams;
    for (int i = given; i < end; i++) {
        TemplateParam *p = &t->params[i];
        if (!p->def)
            error_tok(vm, site, "template '%s' takes %d argument%s, %d given",
                      t->name, i, i == 1 ? "" : "s", given);
        if (default_names_param(t, i))
            error_tok(vm, site,
                      "partial specialization must give argument '%.*s': its "
                      "default names an earlier parameter",
                      (int)p->name->len, p->name->loc);
        default_arg(vm, t, i, &pat[i]);
        if (p->type_start)
            pattern_const(vm, t, i, &pat[i], site);
    }
    if (end > given)
        pl->n = end;
    return a;
}

// `Name@(Args)` in a pattern: a placeholder type when an argument names a
// variable, else NULL and an ordinary instance.
static Type *pattern_inst_type(VirtualMachine *vm, Token **rest, Token *tok) {
    Template *pt   = vm->compiler.tmpl_pattern;
    Token    *name = skip(vm, tok->next, "(");
    Template *t    = cc_find_template(vm, name);
    if (!t)
        return NULL;
    PatList pl;
    Token  *a        = equal(name->next, ",") ? name->next->next : name->next;
    Token  *close    = parse_pattern_args(vm, pt, t, a, tok, &pl);

    bool    mentions = pl.tail >= 0;
    for (int i = 0; i < pl.n; i++)
        mentions |= t->params[param_at(t, i)].type_start
                        ? pl.pat_var[i] >= 0
                        : pattern_has_var(pt, pl.pat[i].ty);
    if (!mentions)
        return NULL;
    if (t->kind == TMPL_FUNC || t->kind == TMPL_TYPEDEF)
        error_tok(vm, name,
                  "a partial specialization pattern cannot name an instance "
                  "of %s template '%s'",
                  kind_name(t->kind), t->name);

    Type *ph                         = struct_type(vm);
    ph->size                         = 1;
    type_agg_mut(vm, ph)->struct_tag = t->name_tok;
    ph->name                         = t->name_tok;
    InstPat *ip   = arena_alloc(&vm->compiler.parser_arena, sizeof(*ip));
    *ip           = (InstPat){pt->inst_pats, ph, t, pl};
    pt->inst_pats = ip;
    *rest         = skip(vm, close, ")");
    return ph;
}

// The constant variable that the array length at `tok` is, or -1 when it names
// none; a variable inside a larger expression is an error.
int cc_template_pattern_length_var(VirtualMachine *vm, Token *tok) {
    Template *pt    = vm->compiler.tmpl_pattern;
    Token    *named = NULL;
    int       var = -1, depth = 0;
    for (Token *q = tok; q->kind != TK_EOF; q = q->next) {
        if (equal(q, "(") || equal(q, "[") || equal(q, "{"))
            depth++;
        else if (equal(q, ")") || equal(q, "]") || equal(q, "}")) {
            if (depth-- == 0)
                break;
        } else if (q->kind == TK_IDENT && !named) {
            var = const_var_index(pt, q);
            if (var >= 0)
                named = q;
        }
    }
    if (!named)
        return -1;
    if (named != tok || !equal(tok->next, "]"))
        error_tok(vm, named,
                  "'%.*s' must be the whole array length in a pattern",
                  (int)named->len, named->loc);
    if (!is_integer(param_type(vm, pt, var)))
        error_tok(vm, named,
                  "'%.*s' must have an integer type to be an array "
                  "length",
                  (int)named->len, named->loc);
    return var;
}

void cc_template_pattern_array(VirtualMachine *vm, Type *arr, int var) {
    Template *pt = vm->compiler.tmpl_pattern;
    ArrPat   *ap = arena_alloc(&vm->compiler.parser_arena, sizeof(*ap));
    *ap          = (ArrPat){pt->arr_pats, arr, var};
    pt->arr_pats = ap;
}

// Registers `@template(V...) @specialize(Pattern...) <definition>` as a partial
// specialization of the template it names. Each V is a variable the pattern
// binds; the pattern is parsed once, with every type variable standing for a
// placeholder type.
static void register_partial(VirtualMachine *vm, Template *pt) {
    Token    *site = pt->pattern;
    Template *t    = cc_find_template(vm, pt->name_tok);
    if (!t)
        error_tok(vm, pt->name_tok, "'%.*s' is not a template",
                  (int)pt->name_tok->len, pt->name_tok->loc);
    if (t->kind != pt->kind)
        error_tok(vm, pt->name_tok, "'%s' is a %s template, not a %s", t->name,
                  kind_name(t->kind), kind_name(pt->kind));
    if (vm->compiler.in_macro_mode)
        error_tok(vm, site, "@specialize is not supported in comptime code");
    if (pt->nparams > TMPL_MAX_ARGS)
        error_tok(vm, pt->params[TMPL_MAX_ARGS].name,
                  "a partial specialization takes at most %d parameters",
                  TMPL_MAX_ARGS);
    for (int k = 0; k < pt->nparams; k++) {
        TemplateParam *p = &pt->params[k];
        if (p->def)
            error_tok(vm, p->name,
                      "a partial specialization parameter cannot have a "
                      "default");
        if (p->dependent)
            error_tok(vm, p->name,
                      "the type of '%.*s' cannot name another parameter of a "
                      "partial specialization",
                      (int)p->name->len, p->name->loc);
    }

    pt->primary = t;
    pt->vars =
        arena_alloc(&vm->compiler.parser_arena, sizeof(Type *) * pt->nparams);
    memset(pt->vars, 0, sizeof(Type *) * pt->nparams);

    TmplSnapshot snap;
    snapshot_save(vm, &snap);
    jmp_buf  jb;
    jmp_buf *saved_jb = vm->error_jmp_buf;
    vm->error_jmp_buf = &jb;
    if (setjmp(jb)) {
        snapshot_restore(vm, &snap);
        vm->error_jmp_buf = saved_jb;
        longjmp(*saved_jb, 1);
    }
    snapshot_clear(vm);
    vm->compiler.scope       = vm->compiler.runtime_file_scope;
    vm->compiler.tmpl_anchor = vm->compiler.runtime_file_scope;
    Scope *bindings = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
    memset(bindings, 0, sizeof(Scope));
    vm->compiler.tmpl_bindings = bindings;

    for (int k = 0; k < pt->nparams; k++) {
        if (pt->params[k].type_start) {
            param_type(vm, pt, k);
            continue;
        }
        Type *ph                         = struct_type(vm);
        ph->size                         = 1;
        type_agg_mut(vm, ph)->struct_tag = pt->params[k].name;
        ph->name                         = pt->params[k].name;
        pt->vars[k]                      = ph;
        Obj *unused                      = NULL;
        bind_param(vm, pt, k, ph, NULL, bindings, &unused);
    }

    vm->compiler.tmpl_pattern = pt;
    PatList *pl               = &pt->pl;
    parse_pattern_args(vm, pt, t, pt->pattern->next->next, site, pl);

    // Generic: variables standing alone, as the primary's own parameters do.
    bool used[TMPL_MAX_ARGS] = {};
    bool generic = !tmpl_has_pack(t) ||
                   (pl->tail >= 0 && !pl->tail_pat && pl->n <= t->nparams - 1);
    if (pl->tail >= 0)
        used[pl->tail] = true;
    for (int i = 0; i < pl->n; i++) {
        if (t->params[param_at(t, i)].type_start) {
            if (pl->pat_var[i] < 0)
                generic = false;
            else
                used[pl->pat_var[i]] = true;
            continue;
        }
        pattern_mark_vars(pt, pl->pat[i].ty, used, true);
        int k = pattern_var(pt, pl->pat[i].ty);
        if (k < 0 || pt->vars[k] != pl->pat[i].ty)
            generic = false;
    }
    if (pl->tail_pat) {
        bool elem[TMPL_MAX_ARGS] = {};
        pattern_mark_vars(pt, pl->tail_pat, elem, true);
        for (int k = 0; k < pt->nparams; k++)
            if (elem[k] && !used[k])
                error_tok(vm, pt->params[k].name,
                          "partial specialization parameter '%.*s' is not "
                          "deduced by the pattern",
                          (int)pt->params[k].name->len,
                          pt->params[k].name->loc);
    }
    for (int k = 0; k < pt->nparams; k++)
        if (!used[k])
            error_tok(vm, pt->params[k].name,
                      "partial specialization parameter '%.*s' is not "
                      "deduced by the pattern",
                      (int)pt->params[k].name->len, pt->params[k].name->loc);
    if (generic) {
        for (int i = 0; i < pl->n && generic; i++)
            for (int j = 0; j < i; j++)
                if (!t->params[param_at(t, i)].type_start ==
                        !t->params[param_at(t, j)].type_start &&
                    (t->params[param_at(t, i)].type_start
                         ? pl->pat_var[i] == pl->pat_var[j]
                         : pattern_var(pt, pl->pat[i].ty) ==
                               pattern_var(pt, pl->pat[j].ty)))
                    generic = false;
        if (generic)
            error_tok(vm, site,
                      "a partial specialization must be more specific than "
                      "the primary template");
    }

    for (TemplateInstance *in = t->insts; in; in = in->next_inst) {
        Deduced d;
        if (match_partial(vm, pt, in->args, in->nargs, &d, site))
            error_tok(vm, site,
                      "partial specialization of '%s' after '%s' was "
                      "instantiated",
                      t->name, in->mangled);
    }
    for (Template *o = t->partials; o; o = o->next_partial)
        if (pattern_covers(vm, o, pt, site) && pattern_covers(vm, pt, o, site))
            error_tok(vm, site,
                      "redefinition of partial specialization of '%s'",
                      t->name);

    pt->next_partial = t->partials;
    t->partials      = pt;
    snapshot_restore(vm, &snap);
    vm->error_jmp_buf = saved_jb;
}
