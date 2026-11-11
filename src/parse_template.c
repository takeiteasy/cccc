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
        if (!eq && t->nparams && t->params[t->nparams - 1].def) {
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
        t->kind = TMPL_ENUM;
        p       = skip_attr_groups(p->next);
        if (p->kind != TK_IDENT) {
            *err     = "a template enum needs a tag name";
            *err_tok = p;
            return -1;
        }
        t->name_tok = p;
        p           = skip_attr_groups(p->next);
        if (equal(p, ":"))
            while (p->kind != TK_EOF && !equal(p, "{") && !equal(p, ";"))
                p = p->next;
        if (!equal(p, "{")) {
            *err     = "a template must be a definition";
            *err_tok = p;
            return -1;
        }
        Token *end = match_brace(p);
        if (!end || !equal(end->next, ";")) {
            *err     = "a template enum cannot declare variables";
            *err_tok = end ? end->next : p;
            return -1;
        }
        t->end = end->next->next;
        return 1;
    }
    if (equal(p, "typedef"))
        return scan_typedef_template(p, t, err, err_tok);

    Declarator d = {};
    Token     *q = scan_declarator(p, true, &d, err, err_tok);
    if (!q)
        return -1;
    if (!d.name || !d.params) {
        *err = "@template must be a struct, union, enum, typedef or function "
               "definition";
        *err_tok = q->kind == TK_EOF ? p : q;
        return -1;
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
    t->name_tok  = d.name;
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
              "@template is only allowed on a file-scope struct, union, enum, "
              "typedef or function definition");
}

// ---- mangling -------------------------------------------------------------

// The instance name under construction. It starts on the stack and moves to
// the arena only if it outgrows that, so a repeat `@inst` allocates nothing.
typedef struct {
    VirtualMachine *vm;
    char           *p;
    size_t          len, cap;
    char            init[256];
} MangleBuf;

static void mb_init(MangleBuf *mb, VirtualMachine *vm) {
    mb->vm      = vm;
    mb->p       = mb->init;
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

static void mangle_tag(MangleBuf *mb, Token *tag, const char *kw, Token *site) {
    if (!tag)
        error_tok(mb->vm, site,
                  "template argument must be a named type; give the %s a tag",
                  kw);
    mb_puts(mb, kw);
    mb_puts(mb, "_");
    mb_putn(mb, tag->loc, tag->len);
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
            return mangle_tag(mb, type_agg(ty)->enum_tag, "enum", site);
        case TY_STRUCT:
            return mangle_tag(mb, type_agg(ty)->struct_tag, "struct", site);
        case TY_UNION:
            return mangle_tag(mb, type_agg(ty)->struct_tag, "union", site);
        case TY_PTR:
            mangle_type(mb, ty->base, site);
            return mb_puts(mb, "_ptr");
        case TY_ARRAY:
            mangle_type(mb, ty->base, site);
            if (ty->array_len < 0 || ty->is_flexible)
                return mb_puts(mb, "_arr");
            mb_puts(mb, "_arr");
            return mb_putu(mb, ty->array_len);
        case TY_FUNC: {
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
            return;
        }
        case TY_VLA:
            error_tok(mb->vm, site,
                      "a variable-length array cannot be a template argument");
        case TY_BLOCK:
            error_tok(mb->vm, site,
                      "a block type cannot be a template argument");
        default:
            error_tok(mb->vm, site, "this type cannot be a template argument");
    }
}

static void mangle_type(MangleBuf *mb, Type *ty, Token *site) {
    if (ty->checked_kind != CHECKED_NONE)
        error_tok(mb->vm, site,
                  "a checked-pointer type cannot be a template argument");
    if (ty->addr_space != AS_NONE)
        error_tok(mb->vm, site,
                  "an address-space type cannot be a template argument");

    if (ty->kind == TY_PTR) {
        mangle_base(mb, ty, site);
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

static bool has_linkage_static(Template *t) {
    for (Token *p = t->start; p != t->name_tok && p->kind != TK_EOF;
         p        = p->next)
        if (equal(p, "static"))
            return true;
    return false;
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

    int     nenum = 0;
    Token **enums =
        t->kind == TMPL_ENUM ? enumerator_names(vm, t, &nenum) : NULL;

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
            bool member = prev && (equal(prev, ".") || equal(prev, "->"));
            bool inst_arg =
                prev && prev2 && equal(prev, "(") && equal(prev2, "__cccc_inst");
            switch (t->kind) {
                case TMPL_FUNC:
                    rename = !(prev && (member || equal(prev, "struct") ||
                                        equal(prev, "union") ||
                                        equal(prev, "enum"))) &&
                             !inst_arg;
                    break;
                case TMPL_TYPEDEF:
                    rename = !member && !inst_arg;
                    break;
                case TMPL_ENUM:
                    rename = prev && equal(prev, "enum") && !inst_arg;
                    break;
                default:
                    rename = prev &&
                             (equal(prev, "struct") || equal(prev, "union")) &&
                             !inst_arg;
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

// The tokens `[from, to)` as a NUL-terminated chain ending in EOF.
static Token *clone_range(VirtualMachine *vm, Token *from, Token *to) {
    Token  head = {};
    Token *cur  = &head;
    for (Token *p = from; p != to; p = p->next)
        cur = cur->next = copy_tok(vm, p);
    cur->next = synth_token(vm, "");
    return head.next;
}

static char *range_text(VirtualMachine *vm, Token *from, Token *to) {
    char *s = "";
    for (Token *p = from; p != to; p = p->next)
        s = arena_format(vm, "%s%s%.*s", s, *s ? " " : "", (int)p->len, p->loc);
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

    bool want_fn = pty->base->kind == TY_FUNC;
    bool is_fn   = sym->ty->kind == TY_FUNC;
    if (want_fn != is_fn)
        error_tok(vm, site,
                  want_fn ? "'%s' is an object but the template parameter "
                            "points to a function"
                          : "'%s' is a function but the template parameter "
                            "points to an object",
                  sym->name);
    Type *have = sym->ty->kind == TY_ARRAY ? sym->ty->base : sym->ty;
    if (pty->base->kind != TY_VOID) {
        Type *a = copy_type(vm, pty->base), *b = copy_type(vm, have);
        a->is_const = a->is_volatile = b->is_const = b->is_volatile = false;
        if (!is_compatible(a, b))
            error_tok(vm, site,
                      "template argument '%s' does not match the parameter's "
                      "pointer type",
                      sym->name);
    }
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
    Token *toks = clone_range(vm, p->type_start, p->decl_end);
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
    if (is_integer(ty)) {
        arg->val          = eval(vm, arg->expr);
        arg->val_unsigned = aty && aty->is_unsigned;
        if (!const_fits(ty, arg->val, arg->val_unsigned))
            error_tok(
                vm, site, "template argument %s does not fit '%s'",
                arg->val_unsigned
                    ? arena_format(vm, "%llu", (unsigned long long)arg->val)
                    : arena_format(vm, "%lld", (long long)arg->val),
                range_text(vm, p->type_start, p->name));
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
static void bind_param(VirtualMachine *vm, Template *t, int i, Type *ty,
                       TemplateArg *arg, Scope *bindings, Obj **hidden) {
    TemplateParam *p     = &t->params[i];
    Node          *value = NULL;
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
static void default_arg(VirtualMachine *vm, Template *t, int i,
                        TemplateArg *out) {
    TemplateParam *p    = &t->params[i];
    Token         *toks = clone_range(vm, p->def, p->def_end);
    Token         *rest;
    memset(out, 0, sizeof(*out));
    if (p->type_start)
        parse_const_arg(vm, &rest, toks, out);
    else
        out->ty = typename(vm, &rest, toks);
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

#define TMPL_STACK_PARAMS 16

// Per-parameter state of one instantiation.
typedef struct {
    Type *ty;
    Obj  *hidden; // the constexpr object of a bound float or pointer parameter
    bool  bound;
} ParamState;

// Binds parameter `i` once, creating the bindings scope on first use.
static void bind_state(VirtualMachine *vm, Template *t, int i,
                       ParamState *state, TemplateArg *args, Scope **bindings) {
    if (state[i].bound)
        return;
    if (!*bindings) {
        *bindings = arena_alloc(&vm->compiler.parser_arena, sizeof(Scope));
        memset(*bindings, 0, sizeof(Scope));
        vm->compiler.tmpl_bindings = *bindings;
    }
    bind_param(vm, t, i, state[i].ty, &args[i], *bindings, &state[i].hidden);
    state[i].bound = true;
}

TemplateInstance *instantiate_template(VirtualMachine *vm, Template *t,
                                       TemplateArg *args, int nargs,
                                       Token *site) {
    int required = t->nparams;
    while (required > 0 && t->params[required - 1].def)
        required--;
    if (nargs < required || nargs > t->nparams) {
        if (required == t->nparams)
            error_tok(vm, site, "template '%s' takes %d argument%s, %d given",
                      t->name, t->nparams, t->nparams == 1 ? "" : "s", nargs);
        error_tok(vm, site, "template '%s' takes %d to %d arguments, %d given",
                  t->name, required, t->nparams, nargs);
    }
    // A repeat @inst keeps its state on the stack; only a new instance moves
    // its arguments to the arena.
    TemplateArg  stack_args[TMPL_STACK_PARAMS];
    ParamState   stack_state[TMPL_STACK_PARAMS];
    TemplateArg *given = args;
    ParamState  *state = stack_state;
    if (t->nparams <= TMPL_STACK_PARAMS) {
        args = stack_args;
    } else {
        args  = arena_alloc(&vm->compiler.parser_arena,
                            sizeof(TemplateArg) * t->nparams);
        state = arena_alloc(&vm->compiler.parser_arena,
                            sizeof(ParamState) * t->nparams);
    }
    memcpy(args, given, sizeof(TemplateArg) * nargs);
    memset(state, 0, sizeof(ParamState) * t->nparams);

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
    for (int i = 0; i < t->nparams; i++) {
        TemplateParam *p = &t->params[i];
        if (i >= nargs)
            default_arg(vm, t, i, &args[i]);
        state[i].ty = p->type_start ? param_type(vm, t, i) : args[i].ty;
        if (p->type_start)
            param_value(vm, t, i, state[i].ty, &args[i], site);
        mb_puts(&mb, "__");
        mangle_arg(&mb, t, i, state[i].ty, &args[i], site);
        if (p->needed)
            bind_state(vm, t, i, state, args, &bindings);
    }

    TemplateInstance *found = hashmap_get(&vm->compiler.template_insts, mb.p);
    if (found) {
        for (int i = 0; i < t->nparams; i++) {
            const Obj *x = found->args[i].sym, *y = args[i].sym;
            if (t->params[i].type_start && (!x != !y || (x && strcmp(x->name, y->name))))
                error_tok(vm, site,
                          "cannot name the instance: '%s' is also the name of "
                          "a different instance",
                          mb.p);
        }
        snapshot_restore(vm, &snap);
        vm->error_jmp_buf = saved_jb;
        if (found->state == TI_FAILED)
            error_tok(vm, site, "instantiation of '%s' failed earlier", mb.p);
        return found;
    }
    if (vm->compiler.tmpl_depth >= TMPL_MAX_DEPTH)
        error_tok(vm, site, "template instantiation depth exceeds %d",
                  TMPL_MAX_DEPTH);

    for (int i = 0; i < t->nparams; i++)
        bind_state(vm, t, i, state, args, &bindings);
    char *mangled = arena_strndup(vm, mb.p, (int)mb.len);
    args          = memcpy(arena_alloc(&vm->compiler.parser_arena,
                                       sizeof(TemplateArg) * t->nparams),
                           args, sizeof(TemplateArg) * t->nparams);

    inst = arena_alloc(&vm->compiler.parser_arena, sizeof(*inst));
    memset(inst, 0, sizeof(*inst));
    inst->tmpl    = t;
    inst->mangled = mangled;
    inst->state   = TI_IN_PROGRESS;
    inst->args    = args;
    hashmap_put(&vm->compiler.template_insts, mangled, inst);
    vm->compiler.tmpl_depth++;

    for (int i = 0; i < t->nparams; i++)
        if (state[i].hidden) {
            state[i].hidden->name = arena_format(
                vm, "__cccc_tp_%s__%.*s", mangled, (int)t->params[i].name->len,
                t->params[i].name->loc);
            link_gvar(vm, state[i].hidden);
        }

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
        forward = clone_template(vm, t, mangled, t->body_open, true, frame);
    } else if (t->kind == TMPL_STRUCT || t->kind == TMPL_UNION) {
        forward = tokenize_string(
            vm, "<template>",
            arena_format(vm, "%s %s;",
                         t->kind == TMPL_UNION ? "union" : "struct", mangled));
        convert_pp_tokens(vm, forward);
    }
    Token *whole = clone_template(vm, t, mangled, NULL, false, frame);

    Token *name_probe = synth_token(vm, mangled);
    if (forward) {
        cc_parse_file_scope_chain(vm, forward);
        inst_lookup(vm, inst, name_probe);
    }
    cc_parse_file_scope_chain(vm, whole);
    if (!inst->fn && !inst->ty)
        inst_lookup(vm, inst, name_probe);

    vm->compiler.tmpl_depth--;
    snapshot_restore(vm, &snap);
    vm->error_jmp_buf = saved_jb;
    inst->state       = TI_DONE;
    return inst;
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

    TemplateArg args[16];
    int         n = 0;
    while (equal(tok, ",")) {
        if (n == 16)
            error_tok(vm, tok, "too many template arguments");
        memset(&args[n], 0, sizeof(args[n]));
        if (n < t->nparams && t->params[n].type_start) {
            parse_const_arg(vm, &tok, tok->next, &args[n]);
        } else {
            args[n].ty = typename(vm, &tok, tok->next);
        }
        n++;
    }
    *rest = skip(vm, tok, ")");
    return instantiate_template(vm, t, args, n, site);
}

// `@inst(Types...) Name` where a type is expected.
Type *cc_template_inst_type(VirtualMachine *vm, Token **rest, Token *tok) {
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
