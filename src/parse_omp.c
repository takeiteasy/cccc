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

// `#pragma omp` under -fopenmp. The preprocessor hands the directive over as
// `__cccc_omp ( <directive tokens> )` (splice_omp_pragma, preprocess.c); the
// parser lowers it to the one-thread form OpenMP allows: private, firstprivate,
// lastprivate and reduction variables become shadow locals declared by
// re-tokenised snippets, and the directive rides on the resulting ND_BLOCK as
// Node.omp. With omp_threaded (-c=native, -m) the directives are lowered onto
// the runtime in src/shims/omp/ instead ("Threaded lowering" below).

#include "./parse_internal.h"
#include <ctype.h>
#include <stdarg.h>

static char *tok_text(VirtualMachine *vm, Token *tok) {
    return arena_format(vm, "%.*s", tok->len, tok->loc);
}

static bool is_word(Token *tok, const char *word) {
    return (tok->kind == TK_IDENT || tok->kind == TK_KEYWORD) &&
           equal(tok, (char *)word);
}

// The memory order an `atomic` clause names, numbered like __ATOMIC_*, or -1.
static int atomic_order_word(Token *tok) {
    static const char *const names[] = {"relaxed", NULL,      "acquire",
                                        "release", "acq_rel", "seq_cst"};
    for (int i = 0; i < 6; i++)
        if (names[i] && is_word(tok, names[i]))
            return i;
    return -1;
}

// The order a failed compare-and-swap or the load of a retry loop uses.
static int omp_load_order(int order) {
    return order == 4 ? 2 : order == 3 ? 0 : order;
}

static Node *snippet(VirtualMachine *vm, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static Node *snippet(VirtualMachine *vm, const char *fmt, ...) {
    va_list ap;
    va_start(ap, fmt);
    char buf[4096];
    int  len = vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len < 0 || len >= (int)sizeof(buf))
        error("OpenMP clause expression is too long");
    char  *src = arena_strdup(vm, buf);

    Token *tok = tokenize_string(vm, "<omp>", src);
    convert_pp_tokens(vm, tok);
    if (is_decl_start(vm, tok)) {
        VarAttr attr   = {};
        Type   *basety = declspec(vm, &tok, tok, &attr);
        return declaration(vm, &tok, tok, basety, &attr);
    }
    return stmt(vm, &tok, tok);
}

typedef struct {
    Node *head;
    Node *cur;
} OmpBody;

static Node *single_stmt(Node *block) {
    if (block->kind == ND_BLOCK && block->body && !block->body->next)
        return block->body;
    return block;
}

static void body_push(VirtualMachine *vm, OmpBody *body, Node *node) {
    add_type(vm, node);
    body->cur = body->cur->next = node;
}

static Obj *clause_var(VirtualMachine *vm, Token *tok, const char *clause) {
    VarScope *sc = tok->kind == TK_IDENT ? find_var(vm, tok) : NULL;
    if (!sc || !sc->var)
        error_tok(vm, tok, "'%.*s' in '%s' clause is not a variable", tok->len,
                  tok->loc, clause);
    if (vm->compiler.omp_frames)
        sc = omp_note_use(vm, tok, sc);
    return sc->var;
}

// ---- default(none) ----
//
// A frame is active while the body of a default(none) directive is parsed.
// Identifier resolution (omp_note_use) records every variable declared outside
// the frame's scope that no clause of the directive names, and the directive
// reports the first one when it ends. Checking at resolution covers the
// threaded lowering too, where nested regions live in outlined functions.

// `#pragma omp section`, which ends a block while a sections body is parsed.
static bool section_marker(Token *tok) {
    return equal(tok, "__cccc_omp") && equal(tok->next, "(") &&
           is_word(tok->next->next, "section") &&
           equal(tok->next->next->next, ")");
}

bool omp_at_section(VirtualMachine *vm, Token *tok) {
    return vm->compiler.omp_sections && section_marker(tok);
}

static const char *omp_directive_word(OmpKind kind) {
    return kind == OMP_TASK ? "task" : "parallel";
}

static bool names_var(OmpDirective *d, Obj *var) {
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->orig == var)
            return true;
    return false;
}

// True when the lookup of `tok` finds its variable at or inside `boundary`.
static bool found_inside(VirtualMachine *vm, Token *tok, Scope *boundary) {
    bool passed = false;
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        if (scope_find_var(sc, tok->loc, tok->len))
            return !passed;
        if (sc == boundary)
            passed = true;
    }
    return false;
}

static void record_violation(VirtualMachine *vm, OmpFrame *f, Token *tok,
                             Obj *var) {
    OmpViolation *v     = arena_alloc(&vm->compiler.parser_arena, sizeof(*v));
    v->var              = var;
    v->tok              = tok;
    OmpViolation **tail = &f->violations;
    while (*tail)
        tail = &(*tail)->next;
    *tail = v;
}

static void task_body_push(VirtualMachine *vm, OmpFrame *f, Node *node) {
    body_push(vm, f->body, node);
}

// The outlined parallel region directly inside `fn` that the parse is in, or
// NULL when the parse is not inside OpenMP regions only.
static Obj *region_child_of(VirtualMachine *vm, Obj *fn) {
    for (Obj *cur = vm->compiler.current_fn; cur; cur = cur->parent_fn) {
        bool outlined = cur->is_omp_region || cur->is_omp_task;
        if (cur->parent_fn == fn)
            return outlined ? cur : NULL;
        if (!outlined)
            return NULL;
    }
    return NULL;
}

// A task copies an outer automatic into its own variable on first use: always
// under default(firstprivate), and by default when the variable is not shared
// by the enclosing parallel region.
static bool task_privatises(VirtualMachine *vm, OmpFrame *f, Token *tok,
                            Obj *var) {
    OmpDirective *d = f->dir;
    if (d->kind != OMP_TASK || d->default_shared || !var->is_local ||
        var->is_static)
        return false;
    if (f->fn != vm->compiler.current_fn && !region_child_of(vm, f->fn))
        return false;
    return d->default_firstprivate || d->default_private || !f->parallel ||
           found_inside(vm, tok, f->parallel);
}

// Array levels of `ty`, outermost first, down to the innermost variable-length
// one. Every extent in that prefix travels in the task payload.
static int vla_copy_dims(Type *ty) {
    int dims = 0, depth = 0;
    for (; ty->kind == TY_ARRAY || ty->kind == TY_VLA; ty = ty->base) {
        depth++;
        if (ty->kind == TY_VLA)
            dims = depth;
    }
    return dims;
}

// `[0]` repeated `n` times: the element type after `n` levels of an array.
static char *zero_subscripts(VirtualMachine *vm, int n) {
    char *s = arena_strdup(vm, "");
    for (int i = 0; i < n; i++)
        s = arena_format(vm, "%s[0]", s);
    return s;
}

// Declares the task's own variable `name`. A copy is read out of the payload
// the spawning code fills in; a private variable is only declared. A
// variable-length array's extents are also read from the payload, so the task
// never reads them from the spawning function.
static void task_copy_decl(VirtualMachine *vm, OmpDirective *d, OmpBody *b,
                           const char *name, Type *ty, bool copy) {
    int  dims = vla_copy_dims(ty);
    bool vla  = dims > 0;
    int  id   = d->omp_id;
    if ((copy || vla) && !d->task_data_decl) {
        body_push(vm, b,
                  snippet(vm,
                          "char *__omp%d_dt = (char *)__cccc_omp_task_data();",
                          id));
        d->task_data_decl = true;
    }
    if (!copy && !vla) {
        body_push(vm, b, snippet(vm, "typeof_unqual(%s) %s;", name, name));
        return;
    }
    OmpTaskCopy *c = arena_alloc(&vm->compiler.parser_arena, sizeof(*c));
    memset(c, 0, sizeof(*c));
    c->name = arena_strdup(vm, name);
    c->off  = d->task_size;
    c->dims = dims;
    c->copy = copy;
    d->task_size += vla ? (dims + 1) * (int)sizeof(unsigned long) : ty->size;
    OmpTaskCopy **tail = &d->copies;
    while (*tail)
        tail = &(*tail)->next;
    *tail = c;
    if (vla) {
        char *extents = arena_strdup(vm, "");
        for (int i = 0; i < dims; i++)
            extents =
                arena_format(vm, "%s[__omp%d_%s_vs[%d]]", extents, id, name, i);
        body_push(vm, b,
                  snippet(vm, "unsigned long __omp%d_%s_vs[%d];", id, name,
                          dims + 1));
        body_push(vm, b,
                  snippet(vm,
                          "__builtin_memcpy(__omp%d_%s_vs, __omp%d_dt + %d, "
                          "sizeof(__omp%d_%s_vs));",
                          id, name, id, c->off, id, name));
        body_push(vm, b,
                  snippet(vm, "typeof_unqual(%s%s) %s%s;", name,
                          zero_subscripts(vm, dims), name, extents));
        if (copy)
            body_push(vm, b,
                      snippet(vm,
                              "__builtin_memcpy(%s, __omp%d_dt + "
                              "__omp%d_%s_vs[%d], sizeof(%s));",
                              name, id, id, name, dims, name));
        return;
    }
    body_push(vm, b, snippet(vm, "typeof_unqual(%s) %s;", name, name));
    body_push(vm, b,
              snippet(vm, "__builtin_memcpy(&%s, __omp%d_dt + %d, sizeof(%s));",
                      name, id, c->off, name));
}

static void make_task_copy(VirtualMachine *vm, OmpFrame *f, Token *tok,
                           Obj *var) {
    int       id            = vm->compiler.unique_name_counter++;
    char     *name          = tok_text(vm, tok);
    Scope    *saved_scope   = vm->compiler.scope;
    OmpFrame *saved_frames  = vm->compiler.omp_frames;
    Obj      *saved_fn      = vm->compiler.current_fn;
    Obj      *saved_locals  = vm->compiler.locals;
    int       saved_depth   = vm->compiler.fn_nesting_depth;
    // From inside a nested region the copy still belongs to the task's
    // function; the region sees it through its snapshot of that function's
    // locals.
    Obj *child = saved_fn != f->fn ? region_child_of(vm, f->fn) : NULL;
    if (child) {
        vm->compiler.current_fn       = f->fn;
        vm->compiler.locals           = child->block_outer_locals;
        vm->compiler.fn_nesting_depth = f->fn->nesting_depth;
    }
    vm->compiler.scope      = f->boundary;
    vm->compiler.omp_frames = NULL;
    if (f->dir->task_outlined) {
        task_copy_decl(vm, f->dir, f->body, name, var->ty,
                       !f->dir->default_private);
    } else {
        task_body_push(vm, f,
                       snippet(vm, "typeof_unqual(%s) *__omp_%d_%s = &%s;",
                               name, id, name, name));
        task_body_push(vm, f, snippet(vm, "typeof_unqual(%s) %s;", name, name));
        if (!f->dir->default_private)
            task_body_push(vm, f,
                           snippet(vm,
                                   "__builtin_memcpy(&%s, __omp_%d_%s, "
                                   "sizeof(%s));",
                                   name, id, name, name));
    }
    if (child) {
        child->block_outer_locals     = vm->compiler.locals;
        vm->compiler.current_fn       = saved_fn;
        vm->compiler.locals           = saved_locals;
        vm->compiler.fn_nesting_depth = saved_depth;
    }
    vm->compiler.scope      = saved_scope;
    vm->compiler.omp_frames = saved_frames;
    (void)var;
}

// Frames are visited outermost first, so a variable a nested task copies is
// the enclosing task's copy.
static VarScope *note_frames(VirtualMachine *vm, Token *tok, VarScope *sc,
                             OmpFrame *f) {
    if (f->next)
        sc = note_frames(vm, tok, sc, f->next);
    Obj *var = sc->var;
    if (found_inside(vm, tok, f->boundary) || names_var(f->dir, var))
        return sc;
    if (f->dir->default_none) {
        record_violation(vm, f, tok, var);
        return sc;
    }
    if (task_privatises(vm, f, tok, var)) {
        make_task_copy(vm, f, tok, var);
        return find_var(vm, tok);
    }
    return sc;
}

VarScope *omp_note_use(VirtualMachine *vm, Token *tok, VarScope *sc) {
    if (!sc || !sc->var || sc->var->is_function || sc->var->is_tls ||
        !strncmp(tok->loc, "__omp", 5) || !strncmp(tok->loc, "__cccc", 6))
        return sc;
    return note_frames(vm, tok, sc, vm->compiler.omp_frames);
}

// A loop variable of a worksharing loop is private to it, so it never needs a
// clause.
static void exempt_loop_var(VirtualMachine *vm, Obj *var) {
    for (OmpFrame *f = vm->compiler.omp_frames; f; f = f->next)
        for (OmpViolation **v = &f->violations; *v;) {
            if ((*v)->var == var)
                *v = (*v)->next;
            else
                v = &(*v)->next;
        }
}

static void exempt_loop_vars(VirtualMachine *vm, Node *loop, int depth) {
    for (int level = 0; loop && level < depth; level++) {
        Node *init = loop->kind == ND_FOR ? node_ctl(loop)->init : NULL;
        if (init && init->kind == ND_EXPR_STMT && init->lhs &&
            init->lhs->kind == ND_ASSIGN && init->lhs->lhs &&
            init->lhs->lhs->kind == ND_VAR)
            exempt_loop_var(vm, init->lhs->lhs->var);
        loop = loop->kind == ND_FOR ? single_stmt(node_ctl(loop)->then) : NULL;
    }
}

// Scope of the innermost enclosing parallel directive, if it is still open.
static Scope *parallel_scope(VirtualMachine *vm) {
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next)
        if (sc == vm->compiler.omp_parallel)
            return sc;
    return NULL;
}

static void frame_push(VirtualMachine *vm, OmpFrame *f, OmpDirective *d,
                       void *body) {
    // A frame left behind by an aborted parse has a scope that no longer
    // exists.
    while (vm->compiler.omp_frames) {
        bool live = false;
        for (Scope *sc = vm->compiler.scope; sc && !live; sc = sc->next)
            live = sc == vm->compiler.omp_frames->boundary;
        if (live)
            break;
        vm->compiler.omp_frames = vm->compiler.omp_frames->next;
    }
    f->dir                  = d;
    f->boundary             = vm->compiler.scope;
    f->parallel             = parallel_scope(vm);
    f->fn                   = vm->compiler.current_fn;
    f->body                 = body;
    f->next                 = vm->compiler.omp_frames;
    vm->compiler.omp_frames = f;
}

static void frame_pop(VirtualMachine *vm, OmpFrame *f) {
    vm->compiler.omp_frames = f->next;
    if (f->violations)
        error_tok(vm, f->violations->tok,
                  "'%.*s' not specified in enclosing '%s'",
                  f->violations->tok->len, f->violations->tok->loc,
                  omp_directive_word(f->dir->kind));
}

static const char *const reduction_ops[] = {"+", "-",  "*",  "&",   "|",
                                            "^", "&&", "||", "max", "min"};

static char *reduction_op(VirtualMachine *vm, Token **rest, Token *tok) {
    for (size_t i = 0; i < sizeof(reduction_ops) / sizeof(*reduction_ops);
         i++) {
        if (equal(tok, (char *)reduction_ops[i])) {
            *rest = tok->next;
            return (char *)reduction_ops[i];
        }
    }
    error_tok(vm, tok, "unsupported OpenMP reduction operator '%.*s'", tok->len,
              tok->loc);
    return "+";
}

static void add_clause(VirtualMachine *vm, OmpDirective *d, OmpClauseKind kind,
                       Obj *var, Token *name, char *op) {
    OmpClause *c     = arena_alloc(&vm->compiler.parser_arena, sizeof(*c));
    c->kind          = kind;
    c->name          = tok_text(vm, name);
    c->orig          = var;
    c->reduction_op  = op;
    OmpClause **tail = &d->clauses;
    while (*tail)
        tail = &(*tail)->next;
    *tail = c;
}

// Token range [from, to) as source text.
static char *tokens_text(VirtualMachine *vm, Token *from, Token *to) {
    char *out = arena_strdup(vm, "");
    for (Token *t = from; t != to; t = t->next) {
        if (t->kind == TK_EOF)
            break;
        out = arena_format(vm, "%s%s%.*s", out, t == from ? "" : " ", t->len,
                           t->loc);
    }
    return out;
}

static bool is_relop(Token *tok) {
    return equal(tok, "<") || equal(tok, "<=") || equal(tok, ">") ||
           equal(tok, ">=") || equal(tok, "!=");
}

// First token at nesting depth 0 that is ";", a closing ")", or (with
// stop_relop) a relational operator.
static Token *scan_expr_end(Token *tok, bool stop_relop) {
    int depth = 0;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (equal(tok, "(") || equal(tok, "[") || equal(tok, "{"))
            depth++;
        else if (equal(tok, ")") || equal(tok, "]") || equal(tok, "}")) {
            if (depth == 0)
                return tok;
            depth--;
        } else if (depth == 0 &&
                   (equal(tok, ";") || (stop_relop && is_relop(tok))))
            return tok;
    }
    return tok;
}

// Variable list of a data-sharing clause, after its "(".
static Token *var_list(VirtualMachine *vm, OmpDirective *d, OmpClauseKind kind,
                       const char *clause, char *op, Token *tok) {
    do {
        Obj *var = clause_var(vm, tok, clause);
        if (kind == OMP_CLAUSE_REDUCTION &&
            (!is_numeric(var->ty) ||
             (strchr("&|^", op[0]) && op[1] == '\0' && !is_integer(var->ty))))
            error_tok(vm, tok,
                      "reduction operator '%s' does not apply to '%.*s'", op,
                      tok->len, tok->loc);
        if (kind == OMP_CLAUSE_COPYIN && !var->is_tls)
            error_tok(vm, tok, "'%.*s' in 'copyin' clause is not threadprivate",
                      tok->len, tok->loc);
        add_clause(vm, d, kind, var, tok, op);
        tok = tok->next;
    } while (consume(vm, &tok, tok, ","));
    return skip(vm, tok, ")");
}

// linear(list [: step]), after its "(".
static Token *linear_list(VirtualMachine *vm, OmpDirective *d, Token *tok) {
    do {
        Obj *var = clause_var(vm, tok, "linear");
        if (!is_integer(var->ty) && var->ty->kind != TY_PTR)
            error_tok(vm, tok,
                      "'%.*s' in 'linear' clause must be an integer or a "
                      "pointer",
                      tok->len, tok->loc);
        add_clause(vm, d, OMP_CLAUSE_LINEAR, var, tok, NULL);
        tok = tok->next;
    } while (consume(vm, &tok, tok, ","));
    char *step = "1";
    if (consume(vm, &tok, tok, ":")) {
        Token *end = scan_expr_end(tok, false);
        step       = tokens_text(vm, tok, end);
        tok        = end;
    }
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->kind == OMP_CLAUSE_LINEAR && !c->step)
            c->step = step;
    return skip(vm, tok, ")");
}

typedef enum {
    CL_DATA     = 1 << 0, // private/firstprivate/shared/reduction/default
    CL_PARALLEL = 1 << 1, // num_threads/if/proc_bind
    CL_LOOP     = 1 << 2, // schedule/collapse/ordered
    CL_NOWAIT   = 1 << 3,
    CL_SIMD     = 1 << 4, // simdlen/safelen
    CL_FIRSTPRIVATE_ONLY = 1 << 5,  // single: private/firstprivate only
    CL_LASTPRIVATE       = 1 << 6,
    CL_COPYPRIVATE       = 1 << 7,
    CL_DEFAULT           = 1 << 8,  // default(shared|none)
    CL_TASK_DATA         = 1 << 9,  // task: private/firstprivate/shared
    CL_TASK_EXPR         = 1 << 10, // task: if/final/priority
    CL_TASK_FLAG         = 1 << 11, // task: untied/mergeable
    CL_DEPEND            = 1 << 12,
    CL_LINEAR            = 1 << 13,
    CL_COPYIN            = 1 << 14,
    CL_DOACROSS          = 1 << 15, // ordered: depend/doacross sink and source
} ClauseSet;

static int clauses_allowed(OmpKind kind) {
    switch (kind) {
        case OMP_PARALLEL:
            return CL_DATA | CL_PARALLEL | CL_DEFAULT | CL_COPYIN;
        case OMP_FOR:
            return CL_DATA | CL_LOOP | CL_NOWAIT | CL_LASTPRIVATE | CL_LINEAR;
        case OMP_PARALLEL_FOR:
            return CL_DATA | CL_PARALLEL | CL_LOOP | CL_LASTPRIVATE |
                   CL_DEFAULT | CL_LINEAR | CL_COPYIN;
        case OMP_SINGLE:
            return CL_FIRSTPRIVATE_ONLY | CL_NOWAIT | CL_COPYPRIVATE;
        case OMP_SIMD:
            return CL_SIMD | CL_LOOP | CL_LINEAR;
        case OMP_SECTIONS:
            return CL_DATA | CL_NOWAIT | CL_LASTPRIVATE;
        case OMP_PARALLEL_SECTIONS:
            return CL_DATA | CL_PARALLEL | CL_LASTPRIVATE | CL_DEFAULT |
                   CL_COPYIN;
        case OMP_TASK:
            return CL_TASK_DATA | CL_DEFAULT | CL_TASK_EXPR | CL_TASK_FLAG |
                   CL_DEPEND;
        case OMP_TASKWAIT:
            return CL_DEPEND;
        case OMP_ORDERED:
            return CL_DOACROSS;
        default:
            return 0;
    }
}

static Token *skip_balanced_parens(VirtualMachine *vm, Token *tok) {
    tok       = skip(vm, tok, "(");
    int depth = 1;
    while (depth) {
        if (tok->kind == TK_EOF)
            error_tok(vm, tok, "unterminated OpenMP clause");
        if (equal(tok, "("))
            depth++;
        else if (equal(tok, ")"))
            depth--;
        tok = tok->next;
    }
    return tok;
}

static Token *directive_name(VirtualMachine *vm, OmpDirective *d, Token *tok) {
    Token *name = tok;
    if (is_word(tok, "parallel")) {
        tok = tok->next;
        if (equal(tok, "for")) {
            d->kind = OMP_PARALLEL_FOR;
            tok     = tok->next;
            if (is_word(tok, "simd"))
                tok = tok->next;
        } else if (is_word(tok, "sections")) {
            d->kind = OMP_PARALLEL_SECTIONS;
            tok     = tok->next;
        } else {
            d->kind = OMP_PARALLEL;
        }
    } else if (equal(tok, "for")) {
        d->kind = OMP_FOR;
        tok     = tok->next;
        if (is_word(tok, "simd"))
            tok = tok->next;
    } else if (is_word(tok, "critical")) {
        d->kind = OMP_CRITICAL;
        tok     = tok->next;
        if (equal(tok, "(")) {
            if (tok->next->kind != TK_IDENT)
                error_tok(vm, tok->next, "expected critical section name");
            d->critical_name = tok_text(vm, tok->next);
            tok              = skip(vm, tok->next->next, ")");
        }
    } else if (is_word(tok, "atomic")) {
        d->kind = OMP_ATOMIC;
        tok     = tok->next;
        Token *fail = NULL, *order_tok = NULL;
        int    fail_order = -1, hints = 0;
        d->atomic_order = -1;
        for (;;) {
            int order = atomic_order_word(tok);
            if (order >= 0) {
                if (order_tok)
                    error_tok(vm, tok, "too many memory order clauses");
                order_tok       = tok;
                d->atomic_order = order;
            } else if (is_word(tok, "read")) {
                d->atomic_kind = 1;
            } else if (is_word(tok, "write")) {
                d->atomic_kind = 2;
            } else if (is_word(tok, "update")) {
            } else if (is_word(tok, "capture")) {
                d->atomic_kind = 3;
            } else if (is_word(tok, "compare")) {
                d->atomic_compare = true;
            } else if (is_word(tok, "weak")) {
                d->atomic_weak = true;
            } else if (is_word(tok, "hint")) {
                Token *hint = tok;
                tok         = skip(vm, tok->next, "(");
                Node *n     = conditional(vm, &tok, tok);
                add_type(vm, n);
                if (!is_const_expr(vm, n))
                    error_tok(vm, hint,
                              "expected a constant integer expression in "
                              "'hint' clause");
                int64_t v  = eval(vm, n);
                hints     |= v & 15;
                if (v < 0 || v > 15 || (hints & 3) == 3 || (hints & 12) == 12)
                    error_tok(vm, hint, "invalid sync hint value");
                tok = skip(vm, tok, ")");
                consume(vm, &tok, tok, ",");
                continue;
            } else if (is_word(tok, "fail")) {
                fail = tok;
                tok  = skip(vm, tok->next, "(");
                fail_order = atomic_order_word(tok);
                if (fail_order != 0 && fail_order != 2 && fail_order != 5)
                    error_tok(vm, tok,
                              "expected 'seq_cst', 'acquire' or 'relaxed'");
                tok = skip(vm, tok->next, ")");
                consume(vm, &tok, tok, ",");
                continue;
            } else {
                break;
            }
            tok = tok->next;
            consume(vm, &tok, tok, ",");
        }
        if (d->atomic_compare && (d->atomic_kind == 1 || d->atomic_kind == 2))
            error_tok(vm, name,
                      "'compare' clause is incompatible with 'read' or "
                      "'write' clauses");
        if (fail && !d->atomic_compare)
            error_tok(vm, fail, "'fail' clause requires 'compare' clause");
        if (d->atomic_weak && !d->atomic_compare)
            error_tok(vm, name, "'weak' clause requires 'compare' clause");
        // No clause is relaxed; an acq_rel read acquires and an acq_rel write
        // releases.
        int order = d->atomic_order < 0 ? 0 : d->atomic_order;
        if (d->atomic_kind == 1 && order == 3)
            error_tok(vm, order_tok,
                      "'#pragma omp atomic read' incompatible with 'release' "
                      "clause");
        if (d->atomic_kind == 2 && order == 2)
            error_tok(vm, order_tok,
                      "'#pragma omp atomic write' incompatible with "
                      "'acquire' clause");
        if (order == 4 && d->atomic_kind == 1)
            order = 2;
        else if (order == 4 && d->atomic_kind == 2)
            order = 3;
        d->atomic_order = order;
        d->atomic_fail  = fail_order >= 0 ? fail_order : omp_load_order(order);
    } else if (is_word(tok, "barrier")) {
        d->kind = OMP_BARRIER;
        tok     = tok->next;
    } else if (is_word(tok, "single")) {
        d->kind = OMP_SINGLE;
        tok     = tok->next;
    } else if (is_word(tok, "master")) {
        d->kind = OMP_MASTER;
        tok     = tok->next;
    } else if (is_word(tok, "masked")) {
        d->kind = OMP_MASKED;
        tok     = tok->next;
    } else if (is_word(tok, "simd")) {
        d->kind = OMP_SIMD;
        tok     = tok->next;
    } else if (is_word(tok, "sections")) {
        d->kind = OMP_SECTIONS;
        tok     = tok->next;
    } else if (is_word(tok, "section")) {
        error_tok(vm, name,
                  "'#pragma omp section' may only be used in '#pragma omp "
                  "sections' construct");
    } else if (is_word(tok, "task")) {
        d->kind = OMP_TASK;
        tok     = tok->next;
    } else if (is_word(tok, "taskwait")) {
        d->kind = OMP_TASKWAIT;
        tok     = tok->next;
    } else if (is_word(tok, "taskgroup")) {
        d->kind = OMP_TASKGROUP;
        tok     = tok->next;
    } else if (is_word(tok, "taskyield")) {
        d->kind = OMP_TASKYIELD;
        tok     = tok->next;
    } else if (is_word(tok, "ordered")) {
        d->kind = OMP_ORDERED;
        tok     = tok->next;
    } else {
        error_tok(vm, name, "unsupported OpenMP directive '%.*s'", name->len,
                  name->loc);
    }
    return tok;
}

// True when d has a clause of `kind`, on the variable `name` if non-NULL.
static bool has_clause(OmpDirective *d, const char *name, OmpClauseKind kind) {
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->kind == kind && (!name || !strcmp(c->name, name)))
            return true;
    return false;
}

// firstprivate and lastprivate on one variable share a single shadow, owned by
// whichever of the two is listed first.
static bool owns_shadow(OmpDirective *d, OmpClause *c) {
    if (c->kind != OMP_CLAUSE_FIRSTPRIVATE && c->kind != OMP_CLAUSE_LASTPRIVATE)
        return true;
    for (OmpClause *p = d->clauses; p != c; p = p->next)
        if (!strcmp(p->name, c->name) && (p->kind == OMP_CLAUSE_FIRSTPRIVATE ||
                                          p->kind == OMP_CLAUSE_LASTPRIVATE))
            return false;
    return true;
}

// schedule([modifier[, modifier]:] kind[, chunk]), after its "(". The
// monotonic, nonmonotonic and simd modifiers are accepted and ignored.
static Token *schedule_clause(VirtualMachine *vm, OmpDirective *d,
                              OmpBody *body, Token *name, Token *tok) {
    while (is_word(tok, "monotonic") || is_word(tok, "nonmonotonic") ||
           is_word(tok, "simd")) {
        tok = tok->next;
        if (equal(tok, ":")) {
            tok = tok->next;
            break;
        }
        tok = skip(vm, tok, ",");
    }
    static const struct {
        const char *word;
        int         kind;
    } kinds[]       = {{"static", 1},
                       {"dynamic", 2},
                       {"guided", 3},
                       {"auto", 4},
                       {"runtime", 0x10}};
    Token *kind_tok = tok;
    for (size_t i = 0; i < sizeof(kinds) / sizeof(*kinds); i++)
        if (is_word(tok, kinds[i].word))
            d->sched_kind = kinds[i].kind;
    if (!d->sched_kind)
        error_tok(vm, kind_tok, "unknown schedule kind '%.*s'", kind_tok->len,
                  kind_tok->loc);
    tok = tok->next;
    if (equal(tok, ",")) {
        if (d->sched_kind == 4 || d->sched_kind == 0x10)
            error_tok(vm, name, "schedule(%.*s) takes no chunk size",
                      kind_tok->len, kind_tok->loc);
        tok = tok->next;
        if (vm->compiler.omp_threaded) {
            Token *end     = scan_expr_end(tok, false);
            d->sched_chunk = tokens_text(vm, tok, end);
            tok            = end;
        } else {
            Node *e = expr(vm, &tok, tok);
            body_push(vm, body, new_unary(vm, ND_EXPR_STMT, e, name));
        }
    }
    return skip(vm, tok, ")");
}

// A sink offset: an optional `+ c` or `- c` after a name, c a constant.
static long sink_offset(VirtualMachine *vm, Token **rest, Token *tok) {
    if (!equal(tok, "+") && !equal(tok, "-")) {
        *rest = tok;
        return 0;
    }
    bool  neg = equal(tok, "-");
    Node *n   = conditional(vm, &tok, tok->next);
    long  c   = (long)eval(vm, n);
    *rest     = tok;
    return neg ? -c : c;
}

// depend(source), depend(sink: vec), doacross(source:) and doacross(sink: vec),
// after the clause name.
static Token *doacross_clause(VirtualMachine *vm, OmpDirective *d, Token *name,
                              Token *tok) {
    bool is_depend = is_word(name, "depend");
    tok            = skip(vm, tok, "(");
    if (is_word(tok, "source")) {
        tok = tok->next;
        if (!is_depend) {
            tok = skip(vm, tok, ":");
            if (is_word(tok, "omp_cur_iteration"))
                tok = tok->next;
        }
        if (d->source)
            error_tok(vm, name, "'source' appears more than once");
        d->source = true;
        return skip(vm, tok, ")");
    }
    if (!is_word(tok, "sink"))
        error_tok(vm, tok, "expected 'sink' or 'source'");
    OmpSink *s = arena_alloc(&vm->compiler.parser_arena, sizeof(*s));
    memset(s, 0, sizeof(*s));
    s->tok = name;
    tok    = skip(vm, tok->next, ":");
    if (is_word(tok, "omp_cur_iteration")) {
        if (is_depend)
            error_tok(vm, tok, "'omp_cur_iteration' needs the doacross clause");
        s->prev   = true;
        s->n      = 1;
        s->off[0] = sink_offset(vm, &tok, tok->next);
    } else {
        do {
            if (s->n == 8)
                error_tok(vm, tok, "a 'sink' vector is limited to 8 elements");
            if (tok->kind != TK_IDENT)
                error_tok(vm, tok, "expected a loop variable in 'sink'");
            s->name[s->n] = tok_text(vm, tok);
            s->off[s->n]  = sink_offset(vm, &tok, tok->next);
            s->n++;
        } while (consume(vm, &tok, tok, ","));
    }
    OmpSink **tail = &d->sinks;
    while (*tail)
        tail = &(*tail)->next;
    *tail = s;
    return skip(vm, tok, ")");
}

// The end of one `depend` list item: the next top-level `,` or `)`.
static Token *depend_item_end(Token *tok) {
    int depth = 0;
    for (; tok->kind != TK_EOF; tok = tok->next) {
        if (equal(tok, "(") || equal(tok, "[") || equal(tok, "{"))
            depth++;
        else if (equal(tok, ")") || equal(tok, "]") || equal(tok, "}")) {
            if (depth == 0)
                return tok;
            depth--;
        } else if (depth == 0 && equal(tok, ","))
            return tok;
    }
    return tok;
}

// The address a `depend` item stands for. An array section `a[lo:len]` is the
// address of `a[lo]`; two items are the same dependence when the addresses
// match.
static char *depend_address(VirtualMachine *vm, Token *from, Token *to) {
    int depth = 0;
    for (Token *t = from; t != to; t = t->next) {
        if (equal(t, "[") && depth == 0) {
            int    inner = 0, ternary = 0;
            Token *colon = NULL, *close = NULL;
            for (Token *u = t->next; u != to && !close; u = u->next) {
                if (equal(u, "(") || equal(u, "[") || equal(u, "{"))
                    inner++;
                else if (equal(u, ")") || equal(u, "]") || equal(u, "}")) {
                    if (inner-- == 0)
                        close = u;
                } else if (inner == 0 && equal(u, "?"))
                    ternary++;
                else if (inner == 0 && equal(u, ":") && !colon) {
                    if (ternary)
                        ternary--;
                    else
                        colon = u;
                }
            }
            if (!close)
                error_tok(vm, t, "unterminated array section");
            if (colon) {
                if (close->next != to)
                    error_tok(vm, close->next,
                              "only a one-dimensional array section is "
                              "supported in 'depend'");
                char *lo =
                    colon == t->next ? "0" : tokens_text(vm, t->next, colon);
                return arena_format(vm, "&(%s[%s])", tokens_text(vm, from, t),
                                    lo);
            }
            t = close;
            continue;
        }
        if (equal(t, "(") || equal(t, "{"))
            depth++;
        else if (equal(t, ")") || equal(t, "}"))
            depth--;
    }
    return arena_format(vm, "&(%s)", tokens_text(vm, from, to));
}

// depend([in|out|inout|mutexinoutset|inoutset] : list), after the clause name.
// Only a threaded task acts on it; the VM runs tasks in order.
static Token *depend_clause(VirtualMachine *vm, OmpDirective *d, Token *tok) {
    tok = skip(vm, tok, "(");
    if (is_word(tok, "iterator"))
        error_tok(vm, tok,
                  "the 'iterator' modifier of 'depend' is not supported");
    if (is_word(tok, "depobj"))
        error_tok(vm, tok, "'depend(depobj: ...)' is not supported");
    bool write;
    if (is_word(tok, "in"))
        write = false;
    else if (is_word(tok, "out") || is_word(tok, "inout") ||
             is_word(tok, "mutexinoutset") || is_word(tok, "inoutset"))
        write = true;
    else
        error_tok(vm, tok,
                  "expected a dependence type: in, out, inout, "
                  "mutexinoutset or inoutset");
    tok = skip(vm, tok->next, ":");
    do {
        Token *end = depend_item_end(tok);
        if (end == tok)
            error_tok(vm, tok, "expected a list item in 'depend'");
        if (vm->compiler.omp_threaded && d->kind == OMP_TASK) {
            OmpDepend *dep =
                arena_alloc(&vm->compiler.parser_arena, sizeof(*dep));
            memset(dep, 0, sizeof(*dep));
            dep->addr        = depend_address(vm, tok, end);
            dep->write       = write;
            OmpDepend **tail = &d->depends;
            while (*tail)
                tail = &(*tail)->next;
            *tail = dep;
        }
        tok = end;
    } while (consume(vm, &tok, tok, ","));
    return skip(vm, tok, ")");
}

// Parses the clauses up to the wrapper's closing ")" and appends the
// expressions that must still be evaluated once (num_threads, if) to body.
static Token *clauses(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                      Token *tok) {
    int allowed = clauses_allowed(d->kind);
    d->collapse = 1;

    while (!equal(tok, ")")) {
        Token *name = tok;
        if (tok->kind != TK_IDENT && tok->kind != TK_KEYWORD)
            error_tok(vm, tok, "expected OpenMP clause");
        tok          = tok->next;

        bool is_data = is_word(name, "private") ||
                       is_word(name, "firstprivate") ||
                       is_word(name, "shared") || is_word(name, "reduction");
        if (is_data) {
            bool simple =
                is_word(name, "private") || is_word(name, "firstprivate");
            bool ok = (allowed & CL_DATA) ||
                      ((allowed & CL_FIRSTPRIVATE_ONLY) && simple) ||
                      ((allowed & CL_TASK_DATA) &&
                       (simple || is_word(name, "shared")));
            if (!ok)
                error_tok(vm, name, "'%.*s' clause is not allowed here",
                          name->len, name->loc);
            tok = skip(vm, tok, "(");
            if (is_word(name, "private"))
                tok = var_list(vm, d, OMP_CLAUSE_PRIVATE, "private", NULL, tok);
            else if (is_word(name, "firstprivate"))
                tok = var_list(vm, d, OMP_CLAUSE_FIRSTPRIVATE, "firstprivate",
                               NULL, tok);
            else if (is_word(name, "shared"))
                tok = var_list(vm, d, OMP_CLAUSE_SHARED, "shared", NULL, tok);
            else {
                char *op = reduction_op(vm, &tok, tok);
                tok      = skip(vm, tok, ":");
                tok =
                    var_list(vm, d, OMP_CLAUSE_REDUCTION, "reduction", op, tok);
            }
        } else if (is_word(name, "default") && (allowed & CL_DEFAULT)) {
            tok = skip(vm, tok, "(");
            if (is_word(tok, "none"))
                d->default_none = true;
            else if (is_word(tok, "shared"))
                d->default_shared = true;
            else if (d->kind == OMP_TASK && is_word(tok, "firstprivate"))
                d->default_firstprivate = true;
            else if (d->kind == OMP_TASK && is_word(tok, "private"))
                d->default_private = true;
            else
                error_tok(vm, tok, "expected 'shared' or 'none'");
            tok = skip(vm, tok->next, ")");
        } else if ((equal(name, "if") || is_word(name, "final") ||
                    is_word(name, "priority")) &&
                   (allowed & CL_TASK_EXPR)) {
            tok = skip(vm, tok, "(");
            if (vm->compiler.omp_threaded) {
                if (equal(name, "if") && is_word(tok, "task") &&
                    equal(tok->next, ":"))
                    tok = tok->next->next;
                Token *end = scan_expr_end(tok, false);
                char  *txt = tokens_text(vm, tok, end);
                if (equal(name, "if"))
                    d->if_text = txt;
                else if (is_word(name, "final"))
                    d->final_text = txt;
                else
                    d->priority_text = txt;
                tok = skip(vm, end, ")");
            } else {
                Node *e = expr(vm, &tok, tok);
                tok     = skip(vm, tok, ")");
                body_push(vm, body, new_unary(vm, ND_EXPR_STMT, e, name));
            }
        } else if ((is_word(name, "untied") || is_word(name, "mergeable")) &&
                   (allowed & CL_TASK_FLAG)) {
        } else if ((is_word(name, "depend") || is_word(name, "doacross")) &&
                   (allowed & CL_DOACROSS)) {
            tok = doacross_clause(vm, d, name, tok);
        } else if (is_word(name, "depend") && (allowed & CL_DEPEND)) {
            tok = depend_clause(vm, d, tok);
        } else if ((is_word(name, "num_threads") || equal(name, "if")) &&
                   (allowed & CL_PARALLEL)) {
            tok = skip(vm, tok, "(");
            if (vm->compiler.omp_threaded) {
                Token *end = scan_expr_end(tok, false);
                char  *txt = tokens_text(vm, tok, end);
                if (equal(name, "if"))
                    d->if_text = txt;
                else
                    d->num_threads_text = txt;
                tok = skip(vm, end, ")");
            } else {
                Node *e = expr(vm, &tok, tok);
                tok     = skip(vm, tok, ")");
                body_push(vm, body, new_unary(vm, ND_EXPR_STMT, e, name));
            }
        } else if (is_word(name, "proc_bind") && (allowed & CL_PARALLEL)) {
            tok = skip_balanced_parens(vm, tok);
        } else if (is_word(name, "schedule") && (allowed & CL_LOOP)) {
            tok = schedule_clause(vm, d, body, name, skip(vm, tok, "("));
        } else if (is_word(name, "collapse") && (allowed & CL_LOOP)) {
            tok         = skip(vm, tok, "(");
            Node *n     = conditional(vm, &tok, tok);
            tok         = skip(vm, tok, ")");
            d->collapse = (int)eval(vm, n);
            if (d->collapse < 1)
                error_tok(vm, name, "collapse argument must be positive");
        } else if (is_word(name, "ordered") && (allowed & CL_LOOP)) {
            if (d->ordered || d->ordered_n)
                error_tok(vm, name, "'ordered' appears more than once");
            if (equal(tok, "(")) {
                tok          = skip(vm, tok, "(");
                Node *n      = conditional(vm, &tok, tok);
                tok          = skip(vm, tok, ")");
                d->ordered_n = (int)eval(vm, n);
                if (d->ordered_n < 1)
                    error_tok(vm, name, "ordered argument must be positive");
                if (d->ordered_n > 8)
                    error_tok(vm, name, "ordered is limited to 8 loops");
            } else {
                d->ordered = true;
            }
        } else if (is_word(name, "nowait") && (allowed & CL_NOWAIT)) {
            d->nowait = true;
        } else if ((is_word(name, "simdlen") || is_word(name, "safelen")) &&
                   (allowed & CL_SIMD)) {
            tok = skip_balanced_parens(vm, tok);
        } else if (is_word(name, "lastprivate") && (allowed & CL_LASTPRIVATE)) {
            tok = skip(vm, tok, "(");
            tok = var_list(vm, d, OMP_CLAUSE_LASTPRIVATE, "lastprivate", NULL,
                           tok);
        } else if (is_word(name, "copyprivate") && (allowed & CL_COPYPRIVATE)) {
            tok = skip(vm, tok, "(");
            tok = var_list(vm, d, OMP_CLAUSE_COPYPRIVATE, "copyprivate", NULL,
                           tok);
        } else if (is_word(name, "copyin") && (allowed & CL_COPYIN)) {
            tok = skip(vm, tok, "(");
            tok = var_list(vm, d, OMP_CLAUSE_COPYIN, "copyin", NULL, tok);
        } else if (is_word(name, "linear") && (allowed & CL_LINEAR)) {
            tok = skip(vm, tok, "(");
            tok = linear_list(vm, d, tok);
        } else {
            error_tok(vm, name, "'%.*s' clause is not allowed here", name->len,
                      name->loc);
        }
        consume(vm, &tok, tok, ",");
    }
    if (d->ordered_n && d->ordered_n < d->collapse)
        error_tok(vm, tok, "ordered(%d) is smaller than collapse(%d)",
                  d->ordered_n, d->collapse);
    if (d->nowait && has_clause(d, NULL, OMP_CLAUSE_COPYPRIVATE))
        error_tok(vm, tok, "'copyprivate' cannot be combined with 'nowait'");
    return tok;
}

static const char *identity(VirtualMachine *vm, const char *op,
                            const char *name) {
    if (!strcmp(op, "*") || !strcmp(op, "&&"))
        return "1";
    if (!strcmp(op, "&"))
        return arena_format(vm, "~(typeof_unqual(%s))0", name);
    return "0";
}

// Declarations for the shadow copies; the pointers capture the originals
// before the shadow of the same name hides them.
static void capture_pointers(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                             int id) {
    for (OmpClause *c = d->clauses; c; c = c->next) {
        if (c->kind == OMP_CLAUSE_SHARED || c->kind == OMP_CLAUSE_PRIVATE ||
            c->kind == OMP_CLAUSE_COPYPRIVATE || !owns_shadow(d, c))
            continue;
        body_push(vm, body,
                  snippet(vm, "typeof_unqual(%s) *__omp_%d_%s = &%s;", c->name,
                          id, c->name, c->name));
        if (c->kind == OMP_CLAUSE_LINEAR && c->snap) {
            body_push(vm, body,
                      snippet(vm, "typeof_unqual(%s) __omp_%d_%s_base = %s;",
                              c->name, id, c->name, c->name));
            body_push(vm, body,
                      snippet(vm, "long __omp_%d_%s_step = (long)(%s);", id,
                              c->name, c->step));
        } else if (c->snap) {
            body_push(vm, body,
                      snippet(vm, "typeof_unqual(%s) __omp_%d_%s_snap;",
                              c->name, id, c->name));
            body_push(vm, body,
                      snippet(vm,
                              "__builtin_memcpy(&__omp_%d_%s_snap, &%s, "
                              "sizeof(%s));",
                              id, c->name, c->name, c->name));
        }
    }
}

static bool copyin_follows(OmpClause *c) {
    for (c = c->next; c; c = c->next)
        if (c->kind == OMP_CLAUSE_COPYIN)
            return true;
    return false;
}

// Where a firstprivate copy reads the original from.
static const char *first_source(VirtualMachine *vm, OmpClause *c, int id) {
    return c->snap ? arena_format(vm, "&__omp_%d_%s_snap", id, c->name)
                   : arena_format(vm, "__omp_%d_%s", id, c->name);
}

// A variable read at region entry and written back at its end races unless the
// original is read first: firstprivate with lastprivate, and linear.
static bool reads_and_writes_back(OmpDirective *d, OmpClause *c) {
    return c->kind == OMP_CLAUSE_LINEAR ||
           ((c->kind == OMP_CLAUSE_FIRSTPRIVATE ||
             c->kind == OMP_CLAUSE_LASTPRIVATE) &&
            has_clause(d, c->name, OMP_CLAUSE_FIRSTPRIVATE) &&
            has_clause(d, c->name, OMP_CLAUSE_LASTPRIVATE));
}

static bool has_race_prone_clause(OmpDirective *d) {
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (reads_and_writes_back(d, c))
            return true;
    return false;
}

// A parallel region reads the originals in the parent, before the fork.
static void mark_snapshots(OmpDirective *d) {
    for (OmpClause *c = d->clauses; c; c = c->next)
        c->snap = reads_and_writes_back(d, c);
}

static void shadow_decls(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                         int id) {
    for (OmpClause *c = d->clauses; c; c = c->next) {
        switch (c->kind) {
            case OMP_CLAUSE_SHARED:
            case OMP_CLAUSE_COPYPRIVATE:
                break;
            case OMP_CLAUSE_LASTPRIVATE:
                if (!owns_shadow(d, c))
                    break;
                body_push(
                    vm, body,
                    snippet(vm, "typeof_unqual(%s) %s;", c->name, c->name));
                if (has_clause(d, c->name, OMP_CLAUSE_FIRSTPRIVATE))
                    body_push(
                        vm, body,
                        snippet(vm, "__builtin_memcpy(&%s, %s, sizeof(%s));",
                                c->name, first_source(vm, c, id), c->name));
                break;
            case OMP_CLAUSE_LINEAR:
                // The step is evaluated before the private copy hides the
                // names it may use.
                if (!c->snap) {
                    body_push(vm, body,
                              snippet(vm,
                                      "typeof_unqual(%s) __omp_%d_%s_base = "
                                      "*__omp_%d_%s;",
                                      c->name, id, c->name, id, c->name));
                    body_push(vm, body,
                              snippet(vm, "long __omp_%d_%s_step = (long)(%s);",
                                      id, c->name, c->step));
                }
                body_push(
                    vm, body,
                    snippet(vm, "typeof_unqual(%s) %s;", c->name, c->name));
                break;
            case OMP_CLAUSE_COPYIN:
                // The primary thread's copy is read by every other thread, and
                // the primary thread must not change it first.
                if (vm->compiler.omp_threaded) {
                    body_push(vm, body,
                              snippet(vm,
                                      "if (__cccc_omp_thread_num() != 0) "
                                      "__builtin_memcpy(&%s, __omp_%d_%s, "
                                      "sizeof(%s));",
                                      c->name, id, c->name, c->name));
                    if (!copyin_follows(c))
                        body_push(vm, body,
                                  snippet(vm, "__cccc_omp_barrier();"));
                }
                break;
            case OMP_CLAUSE_PRIVATE:
                body_push(
                    vm, body,
                    snippet(vm, "typeof_unqual(%s) %s;", c->name, c->name));
                break;
            case OMP_CLAUSE_FIRSTPRIVATE:
                if (!owns_shadow(d, c))
                    break;
                body_push(
                    vm, body,
                    snippet(vm, "typeof_unqual(%s) %s;", c->name, c->name));
                body_push(vm, body,
                          snippet(vm, "__builtin_memcpy(&%s, %s, sizeof(%s));",
                                  c->name, first_source(vm, c, id), c->name));
                break;
            case OMP_CLAUSE_REDUCTION: {
                bool extremum = !strcmp(c->reduction_op, "max") ||
                                !strcmp(c->reduction_op, "min");
                // max/min are idempotent, so the shadow can start at the
                // original value.
                const char *init =
                    extremum ? arena_format(vm, "*__omp_%d_%s", id, c->name)
                             : identity(vm, c->reduction_op, c->name);
                if (extremum && vm->compiler.omp_threaded) {
                    // Other threads may be merging into the original.
                    body_push(
                        vm, body,
                        snippet(vm, "typeof_unqual(%s) %s;", c->name, c->name));
                    body_push(vm, body,
                              snippet(vm, "__cccc_omp_reduce_lock();"));
                    body_push(vm, body, snippet(vm, "%s = %s;", c->name, init));
                    body_push(vm, body,
                              snippet(vm, "__cccc_omp_reduce_unlock();"));
                    break;
                }
                body_push(vm, body,
                          snippet(vm, "typeof_unqual(%s) %s = %s;", c->name,
                                  c->name, init));
                break;
            }
        }
    }
}

// Linear variables are set from an iteration counter at the top of the loop
// body; the serial lowering counts the iterations itself.
static void linear_prologue(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                            int id) {
    if (has_clause(d, NULL, OMP_CLAUSE_LINEAR))
        body_push(vm, body, snippet(vm, "long __omp_%d_k = 0;", id));
}

static int shadows(VirtualMachine *vm, OmpDirective *d, OmpBody *body) {
    int id = vm->compiler.unique_name_counter++;
    capture_pointers(vm, d, body, id);
    shadow_decls(vm, d, body, id);
    linear_prologue(vm, d, body, id);
    return id;
}

static char *linear_assign(VirtualMachine *vm, OmpClause *c, int id,
                           const char *iteration) {
    return arena_format(vm,
                        "%s = (typeof_unqual(%s))(__omp_%d_%s_base + "
                        "(long)(%s) * __omp_%d_%s_step);",
                        c->name, c->name, id, c->name, iteration, id, c->name);
}

// The innermost loop of a collapse(depth) nest.
static Node *innermost_loop(Node *loop, int depth) {
    for (int level = 1; level < depth; level++)
        loop = single_stmt(node_ctl(loop)->then);
    return loop;
}

// Prepends the linear assignments to the body of the serial loop.
static void linear_inject(VirtualMachine *vm, OmpDirective *d, int id,
                          Node *loop, Token *tok) {
    Node *inner = innermost_loop(loop, d->collapse);
    if (inner->kind != ND_FOR)
        error_tok(vm, tok, "'linear' needs a for loop");
    Node    head = {};
    OmpBody it   = {&head, &head};
    body_push(vm, &it, snippet(vm, "long __omp_%d_it = __omp_%d_k++;", id, id));
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->kind == OMP_CLAUSE_LINEAR)
            body_push(
                vm, &it,
                snippet(vm, "%s",
                        linear_assign(vm, c, id,
                                      arena_format(vm, "__omp_%d_it", id))));
    body_push(vm, &it, node_ctl(inner)->then);
    Node *block = new_node(vm, ND_BLOCK, tok);
    block->body = head.next;
    node_ctl_mut(vm, inner)->then = block;
}

static void merges(VirtualMachine *vm, OmpDirective *d, OmpBody *body, int id) {
    bool locked = false;
    for (OmpClause *c = d->clauses; c; c = c->next) {
        bool serial = !vm->compiler.omp_threaded || d->kind == OMP_SIMD;
        if (c->kind == OMP_CLAUSE_LINEAR) {
            if (serial)
                body_push(vm, body,
                          snippet(vm,
                                  "if (__omp_%d_k > 0) __builtin_memcpy("
                                  "__omp_%d_%s, &%s, sizeof(%s));",
                                  id, id, c->name, c->name, c->name));
            continue;
        }
        if (c->kind == OMP_CLAUSE_LASTPRIVATE && vm->compiler.omp_threaded)
            continue;
        if (c->kind == OMP_CLAUSE_REDUCTION && vm->compiler.omp_threaded &&
            !locked) {
            body_push(vm, body, snippet(vm, "__cccc_omp_reduce_lock();"));
            locked = true;
        }
        if (c->kind == OMP_CLAUSE_LASTPRIVATE) {
            body_push(vm, body,
                      snippet(vm,
                              "__builtin_memcpy(__omp_%d_%s, &%s, sizeof(%s));",
                              id, c->name, c->name, c->name));
            continue;
        }
        if (c->kind != OMP_CLAUSE_REDUCTION)
            continue;
        const char *op = c->reduction_op;
        if (!strcmp(op, "max") || !strcmp(op, "min"))
            body_push(vm, body,
                      snippet(vm, "if (%s %c *__omp_%d_%s) *__omp_%d_%s = %s;",
                              c->name, op[1] == 'a' ? '>' : '<', id, c->name,
                              id, c->name, c->name));
        else
            body_push(vm, body,
                      snippet(vm, "*__omp_%d_%s = *__omp_%d_%s %s %s;", id,
                              c->name, id, c->name, strcmp(op, "-") ? op : "+",
                              c->name));
    }
    if (locked)
        body_push(vm, body, snippet(vm, "__cccc_omp_reduce_unlock();"));
}


static void check_loop(VirtualMachine *vm, Node *loop, int depth, Token *tok) {
    for (int level = 0; level < depth; level++) {
        if (!loop || loop->kind != ND_FOR || !node_ctl(loop)->init ||
            !node_ctl(loop)->cond || !node_ctl(loop)->inc)
            error_tok(vm, tok,
                      "the statement after '#pragma omp for' must be a for "
                      "loop in canonical form");
        if (level + 1 < depth)
            loop = single_stmt(node_ctl(loop)->then);
    }
}

// The blocks of `{ [section] stmts (section stmts)* }`, one per section.
static Node **sections_body(VirtualMachine *vm, Token **rest, Token *tok,
                            int *count) {
    tok = skip(vm, tok, "{");
    if (section_marker(tok))
        tok = tok->next->next->next->next;
    int    cap  = 4;
    Node **secs = arena_alloc(&vm->compiler.parser_arena, cap * sizeof(*secs));
    int    n    = 0;
    char  *saved_brk  = vm->compiler.brk_label;
    char  *saved_cont = vm->compiler.cont_label;
    vm->compiler.omp_sections++;
    for (;;) {
        // break and continue cannot leave a section.
        vm->compiler.brk_label  = NULL;
        vm->compiler.cont_label = NULL;
        Token *close;
        Node  *block = compound_stmt(vm, &tok, tok, &close);
        if (n == cap) {
            Node **grown = arena_alloc(&vm->compiler.parser_arena,
                                       2 * cap * sizeof(*grown));
            memcpy(grown, secs, n * sizeof(*secs));
            secs  = grown;
            cap  *= 2;
        }
        secs[n++] = block;
        if (equal(close, "}"))
            break;
        tok = tok->next->next->next->next;
    }
    vm->compiler.omp_sections--;
    vm->compiler.brk_label  = saved_brk;
    vm->compiler.cont_label = saved_cont;
    *rest                   = tok;
    *count                  = n;
    return secs;
}

// ---- Threaded lowering (-c=native, -m, -c=generated) ----
//
// A parallel region becomes a nested function (an Obj with is_omp_region); the
// serializer turns the call to it into a __cccc_pool_run() fork and captures
// the enclosing locals by reference through the nested-function environment,
// which is OpenMP's `shared`. Worksharing, single, critical and atomic are
// lowered in place onto the runtime in src/shims/omp/.

// Declares a runtime function, or re-binds the Obj an earlier declaration
// created: a block-scope declaration dies with its scope.
Obj *declare_runtime_fn(VirtualMachine *vm, const char *name,
                        const char *proto) {
    Obj *existing = NULL;
    for (Obj *o = vm->compiler.globals; o && !existing; o = o->next)
        if (o->is_function && !strcmp(o->name, name))
            existing = o;
    if (existing) {
        push_scope(vm, (char *)name, strlen(name))->var = existing;
        return existing;
    }
    Token  *tok  = tokenize_string(vm, "<runtime>", proto);
    VarAttr attr = {};
    convert_pp_tokens(vm, tok);
    Type *base        = declspec(vm, &tok, tok, &attr);
    Type *ty          = declarator(vm, &tok, tok, base);
    Obj  *fn          = new_gvar(vm, (char *)name, strlen(name), ty);
    fn->is_function   = true;
    fn->is_definition = false;
    fn->is_static     = false;
    fn->is_root       = true;
    push_scope(vm, fn->name, strlen(fn->name))->var = fn;
    return fn;
}

// Declares the runtime entry points. A block-scope declaration dies with its
// scope, so a later directive re-binds the Obj the first one created instead
// of declaring a second function of the same name.
static void runtime_decls(VirtualMachine *vm) {
    static const struct {
        const char *name, *proto;
    } rt[] = {
        {"__cccc_omp_barrier", "void __cccc_omp_barrier(void);"},
        {"__cccc_omp_thread_num", "int __cccc_omp_thread_num(void);"},
        {"__cccc_omp_static_range",
         "void __cccc_omp_static_range(long, long *, long *);"},
        {"__cccc_omp_single_begin", "int __cccc_omp_single_begin(void);"},
        {"__cccc_omp_copyprivate_set",
         "void __cccc_omp_copyprivate_set(void *);"},
        {"__cccc_omp_copyprivate_get",
         "void *__cccc_omp_copyprivate_get(void);"},
        {"__cccc_omp_reduce_lock", "void __cccc_omp_reduce_lock(void);"},
        {"__cccc_omp_reduce_unlock", "void __cccc_omp_reduce_unlock(void);"},
        {"__cccc_omp_critical_enter",
         "void *__cccc_omp_critical_enter(void **, const char *);"},
        {"__cccc_omp_critical_exit", "void __cccc_omp_critical_exit(void *);"},
        {"__cccc_omp_loop_begin", "void __cccc_omp_loop_begin(int *, long *);"},
        {"__cccc_omp_loop_next",
         "int __cccc_omp_loop_next(int, long, long, long *, long *, long *);"},
        {"__cccc_omp_loop_end", "void __cccc_omp_loop_end(int, long);"},
        {"__cccc_omp_ordered_start", "void __cccc_omp_ordered_start(long);"},
        {"__cccc_omp_ordered_begin", "void __cccc_omp_ordered_begin(void);"},
        {"__cccc_omp_ordered_chunk_end",
         "void __cccc_omp_ordered_chunk_end(long);"},
        {"__cccc_omp_ordered_loop_end",
         "void __cccc_omp_ordered_loop_end(long);"},
        {"__cccc_omp_doacross_init",
         "void __cccc_omp_doacross_init(long, long, int, long);"},
        {"__cccc_omp_doacross_fini", "void __cccc_omp_doacross_fini(void);"},
        {"__cccc_omp_doacross_post", "void __cccc_omp_doacross_post(long);"},
        {"__cccc_omp_doacross_done", "void __cccc_omp_doacross_done(long);"},
        {"__cccc_omp_doacross_wait", "void __cccc_omp_doacross_wait(long);"},
        {"__cccc_omp_task_alloc", "void *__cccc_omp_task_alloc(long);"},
        {"__cccc_omp_task_stage",
         "void __cccc_omp_task_stage(void *, int, void **, int);"},
        {"__cccc_omp_task_data", "void *__cccc_omp_task_data(void);"},
        {"__cccc_omp_task_drain", "void __cccc_omp_task_drain(void);"},
        {"__cccc_omp_taskwait", "void __cccc_omp_taskwait(void);"},
        {"__cccc_omp_taskgroup_begin",
         "void *__cccc_omp_taskgroup_begin(void);"},
        {"__cccc_omp_taskgroup_end", "void __cccc_omp_taskgroup_end(void *);"},
    };
    for (size_t i = 0; i < sizeof(rt) / sizeof(*rt); i++)
        declare_runtime_fn(vm, rt[i].name, rt[i].proto);
    vm->compiler.omp_used = true;
}

OutlineRegion outline_begin(VirtualMachine *vm, Token *tok, const char *name) {
    OutlineRegion r = {};
    r.parent    = vm->compiler.current_fn;
    if (!r.parent)
        error_tok(vm, tok, "'#pragma omp' must be inside a function");
    r.saved_depth   = vm->compiler.fn_nesting_depth;
    r.saved_queries = vm->compiler.objsize_queries;
    r.saved_brk     = vm->compiler.brk_label;
    r.saved_cont    = vm->compiler.cont_label;
    r.saved_switch  = vm->compiler.current_switch;
    r.saved_gotos   = vm->compiler.gotos;
    r.saved_labels  = vm->compiler.labels;
    r.saved_chain   = vm->compiler.cur_cleanup_chain;
    r.saved_checked = vm->compiler.checked_scope_attr;

    Obj *fn = new_gvar(vm, (char *)name, strlen(name), func_type(vm, ty_void));
    fn->is_function = true;
    fn->is_definition              = true;
    fn->is_static                  = true;
    fn->is_root                    = true;
    fn->is_nested                  = true;
    fn->is_macro_generated         = r.parent->is_macro_generated;
    fn->tok                        = tok;
    fn->parent_fn                  = r.parent;
    fn->nesting_depth              = r.saved_depth + 1;
    fn->block_outer_locals         = vm->compiler.locals;
    fn->next_nested_sibling        = r.parent->nested_children;
    r.parent->nested_children      = fn;
    r.fn                           = fn;

    vm->compiler.current_fn        = fn;
    vm->compiler.locals            = NULL;
    vm->compiler.fn_nesting_depth  = r.saved_depth + 1;
    vm->compiler.objsize_queries   = NULL;
    vm->compiler.brk_label         = NULL;
    vm->compiler.cont_label        = NULL;
    vm->compiler.current_switch    = NULL;
    vm->compiler.cur_cleanup_chain = NULL;
    enter_scope(vm);
    new_lvar(vm, "__static_link", 13, pointer_to(vm, ty_void));
    fn->params = vm->compiler.locals;
    fn->alloca_bottom =
        new_lvar(vm, "__alloca_size__", 15, pointer_to(vm, ty_char));
    return r;
}

void outline_end(VirtualMachine *vm, OutlineRegion *r, Node *body) {
    Obj *fn    = r->fn;
    fn->body   = body;
    fn->locals = vm->compiler.locals;
    leave_scope(vm);
    // A goto must stay inside the region: its labels are matched here, before
    // the enclosing function's own resolution could bind them to an outer one.
    cc_match_goto_labels(vm, vm->compiler.gotos, r->saved_gotos,
                         vm->compiler.labels, r->saved_labels, false, true);
    resolve_objsize_queries(vm, fn->body);
    mark_addr_escapes(fn->body, fn);
    checked_tail_passes(vm, fn);
    mark_nested_captures(fn, fn->body);

    vm->compiler.current_fn         = r->parent;
    vm->compiler.locals             = fn->block_outer_locals;
    vm->compiler.fn_nesting_depth   = r->saved_depth;
    vm->compiler.objsize_queries    = r->saved_queries;
    vm->compiler.brk_label          = r->saved_brk;
    vm->compiler.cont_label         = r->saved_cont;
    vm->compiler.current_switch     = r->saved_switch;
    vm->compiler.cur_cleanup_chain  = r->saved_chain;
    vm->compiler.checked_scope_attr = r->saved_checked;
}

static OutlineRegion region_begin(VirtualMachine *vm, Token *tok, int id,
                                  Obj *nt_var) {
    OutlineRegion r =
        outline_begin(vm, tok, arena_format(vm, "__omp_region_%d", id));
    r.fn->is_omp_region = true;
    r.fn->omp_nt_var    = nt_var;
    return r;
}

static void bad_loop(VirtualMachine *vm, Token *tok) {
    error_tok(vm, tok,
              "the statement after '#pragma omp for' must be a for loop in "
              "canonical form");
}

// True when `text` mentions the identifier `name`.
static bool mentions(const char *text, const char *name) {
    size_t n = strlen(name);
    for (const char *p = text; (p = strstr(p, name)); p++) {
        bool before =
            p == text || !(isalnum((unsigned char)p[-1]) || p[-1] == '_');
        bool after = !(isalnum((unsigned char)p[n]) || p[n] == '_');
        if (before && after)
            return true;
    }
    return false;
}

static Token *loop_header(VirtualMachine *vm, Token *tok, OmpLoopLevel *lv) {
    Token *start = tok;
    tok          = skip(vm, tok, "for");
    tok          = skip(vm, tok, "(");
    Token *name  = tok;
    if (is_typename(vm, tok)) {
        VarAttr attr = {};
        Type   *base = declspec(vm, &tok, tok, &attr);
        lv->ty       = declarator(vm, &tok, tok, base);
        name         = lv->ty->name;
        lv->declared = true;
    } else {
        tok = tok->next;
    }
    if (!name || name->kind != TK_IDENT)
        bad_loop(vm, start);
    lv->name = tok_text(vm, name);
    if (!lv->declared) {
        VarScope *sc = find_var(vm, name);
        if (!sc || !sc->var)
            bad_loop(vm, start);
        lv->ty = sc->var->ty;
    }
    lv->ptr = lv->ty->kind == TY_PTR;
    if (!is_integer(lv->ty) &&
        !(lv->ptr && lv->ty->base->kind != TY_VOID &&
          lv->ty->base->kind != TY_FUNC && lv->ty->base->size > 0))
        error_tok(vm, name,
                  "the loop variable of '#pragma omp for' must be an integer "
                  "or a pointer to an object type with -c=native");
    tok      = skip(vm, tok, "=");
    Token *e = scan_expr_end(tok, false);
    lv->lb   = tokens_text(vm, tok, e);
    tok      = skip(vm, e, ";");

    // Condition: `v rel ub` or `ub rel v`.
    if (tok->kind == TK_IDENT && equal(tok, lv->name) && is_relop(tok->next)) {
        lv->rel = tok_text(vm, tok->next);
        e       = scan_expr_end(tok->next->next, false);
        lv->ub  = tokens_text(vm, tok->next->next, e);
        tok     = e;
    } else {
        e = scan_expr_end(tok, true);
        if (!is_relop(e) || !equal(e->next, lv->name))
            bad_loop(vm, start);
        lv->ub  = tokens_text(vm, tok, e);
        char *r = tok_text(vm, e);
        lv->rel = !strcmp(r, "<")    ? ">"
                  : !strcmp(r, "<=") ? ">="
                  : !strcmp(r, ">")  ? "<"
                  : !strcmp(r, ">=") ? "<="
                                     : r;
        tok     = e->next->next;
    }
    tok = skip(vm, tok, ";");

    // Increment.
    lv->step = "1";
    if (equal(tok, "++") || equal(tok, "--")) {
        lv->up = equal(tok, "++");
        if (!equal(tok->next, lv->name))
            bad_loop(vm, start);
        tok = tok->next->next;
    } else {
        if (!equal(tok, lv->name))
            bad_loop(vm, start);
        tok = tok->next;
        if (equal(tok, "++") || equal(tok, "--")) {
            lv->up = equal(tok, "++");
            tok    = tok->next;
        } else if (equal(tok, "+=") || equal(tok, "-=")) {
            lv->up   = equal(tok, "+=");
            e        = scan_expr_end(tok->next, false);
            lv->step = tokens_text(vm, tok->next, e);
            tok      = e;
        } else if (equal(tok, "=")) {
            tok = tok->next;
            if (equal(tok, lv->name) &&
                (equal(tok->next, "+") || equal(tok->next, "-"))) {
                lv->up   = equal(tok->next, "+");
                e        = scan_expr_end(tok->next->next, false);
                lv->step = tokens_text(vm, tok->next->next, e);
                tok      = e;
            } else {
                e           = scan_expr_end(tok, false);
                Token *prev = NULL, *last = NULL, *before = NULL;
                for (Token *t = tok; t != e; t = t->next) {
                    before = prev;
                    prev   = last;
                    last   = t;
                    (void)before;
                }
                if (!last || !prev || !equal(last, lv->name) ||
                    !equal(prev, "+"))
                    bad_loop(vm, start);
                lv->up   = true;
                lv->step = tokens_text(vm, tok, prev);
                tok      = e;
            }
        } else {
            bad_loop(vm, start);
        }
    }
    bool ok = lv->up ? (!strcmp(lv->rel, "<") || !strcmp(lv->rel, "<=") ||
                        !strcmp(lv->rel, "!="))
                     : (!strcmp(lv->rel, ">") || !strcmp(lv->rel, ">=") ||
                        !strcmp(lv->rel, "!="));
    if (!ok)
        bad_loop(vm, start);
    return skip(vm, tok, ")");
}

static char *iter_count(VirtualMachine *vm, int id, int j, OmpLoopLevel *lv) {
    char *lb   = arena_format(vm, "__omp%d_lb%d", id, j);
    char *ub   = arena_format(vm, "__omp%d_ub%d", id, j);
    char *st   = arena_format(vm, "__omp%d_st%d", id, j);
    char *from = lv->up ? lb : ub, *to = lv->up ? ub : lb;
    char *span = arena_format(vm, "%s - %s", to, from);
    if (!strcmp(lv->rel, "!="))
        return arena_format(vm, "(%s) / %s", span, st);
    bool  incl  = !strcmp(lv->rel, "<=") || !strcmp(lv->rel, ">=");
    char *guard = arena_format(vm, "%s %s %s", lb, lv->rel, ub);
    if (incl)
        return arena_format(vm, "%s ? (%s) / %s + 1 : 0", guard, span, st);
    return arena_format(vm, "%s ? (%s + %s - 1) / %s : 0", guard, span, st, st);
}

// The loop statement a snippet parsed to, which may sit in a block.
static Node *first_loop(Node *n) {
    while (n && n->kind != ND_FOR)
        n = n->kind == ND_BLOCK ? n->body : NULL;
    return n;
}

// A clause expression is evaluated once, outside the region, so it reads the
// original of a variable the directive also privatises. The directive's own
// default(none) does not apply to it.
static Node *outer_snippet(VirtualMachine *vm, OmpDirective *d,
                           const char *text) {
    OmpFrame *saved = vm->compiler.omp_frames;
    if (saved && saved->dir == d)
        vm->compiler.omp_frames = saved->next;
    Node *n                 = snippet(vm, "%s", text);
    vm->compiler.omp_frames = saved;
    return n;
}

static void chunk_decl(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                       int id) {
    body_push(
        vm, body,
        outer_snippet(vm, d,
                      arena_format(vm, "long __omp%d_cs = (long)(%s);", id,
                                   d->sched_chunk ? d->sched_chunk : "0")));
}

// Reads the headers of the `ordered(n)` nest without consuming them.
static void ordered_levels(VirtualMachine *vm, OmpDirective *d, Token *tok) {
    int n = d->ordered_n;
    d->levels =
        arena_alloc(&vm->compiler.parser_arena, sizeof(OmpLoopLevel) * n);
    memset(d->levels, 0, sizeof(OmpLoopLevel) * n);
    Token *start = tok;
    for (int j = 0; j < n; j++) {
        while (j && equal(tok, "{"))
            tok = tok->next;
        if (!equal(tok, "for"))
            error_tok(vm, tok,
                      "ordered(%d) needs %d perfectly nested for loops", n, n);
        tok = loop_header(vm, tok, &d->levels[j]);
        for (int i = 0; i < j; i++)
            if (mentions(d->levels[j].lb, d->levels[i].name) ||
                mentions(d->levels[j].ub, d->levels[i].name) ||
                mentions(d->levels[j].step, d->levels[i].name))
                error_tok(vm, start,
                          "ordered loops must be rectangular: the bounds of an "
                          "inner loop cannot use '%s'",
                          d->levels[i].name);
    }
}

// The loop variable of level j as a long: an offset from the base pointer for
// a pointer variable.
static char *level_long(VirtualMachine *vm, int id, int j, OmpLoopLevel *lv) {
    if (lv->ptr)
        return arena_format(vm, "(%s - __omp%d_p%d)", lv->name, id, j);
    return arena_format(vm, "(long)(%s)", lv->name);
}

// The value of level j's loop variable at `offset` past the bound it starts at.
static char *level_value(VirtualMachine *vm, int id, int j, OmpLoopLevel *lv,
                         const char *offset) {
    char sign = lv->up ? '+' : '-';
    if (lv->ptr)
        return arena_format(vm, "__omp%d_p%d + (__omp%d_lb%d %c (%s))", id, j,
                            id, j, sign, offset);
    return arena_format(vm, "(typeof_unqual(%s))(__omp%d_lb%d %c (%s))",
                        lv->name, id, j, sign, offset);
}

// The iteration number of level j's loop variable, counted from the first.
static char *level_index(VirtualMachine *vm, OmpDirective *loop, int j) {
    OmpLoopLevel *lv = &loop->levels[j];
    int           id = loop->omp_id;
    char         *v  = level_long(vm, id, j, lv);
    if (lv->up)
        return arena_format(vm, "(%s - __omp%d_lb%d) / __omp%d_st%d", v, id, j,
                            id, j);
    return arena_format(vm, "(__omp%d_lb%d - %s) / __omp%d_st%d", id, j, v, id,
                        j);
}

// The position of the current iteration in the flattened iteration space.
static char *current_flat(VirtualMachine *vm, OmpDirective *loop) {
    char *flat = level_index(vm, loop, 0);
    for (int j = 1; j < loop->ordered_n; j++)
        flat = arena_format(vm, "(%s) * __omp%d_n%d + %s", flat, loop->omp_id,
                            j, level_index(vm, loop, j));
    return flat;
}

// True when a constant step does not divide the offset.
static bool step_misses(OmpLoopLevel *lv, long delta) {
    char *end;
    long  st = strtol(lv->step, &end, 0);
    return !*end && st > 0 && delta % st;
}

// Warns about a sink that waits on the current or a later iteration, or on one
// the loop never runs, and reports whether it must be dropped.
static bool sink_dropped(VirtualMachine *vm, OmpDirective *loop, OmpSink *s) {
    const char *why = NULL;
    if (s->prev) {
        if (s->off[0] >= 0)
            why = "waits for the current or a later iteration";
    } else {
        int first = -1;
        for (int j = 0; j < s->n && first < 0; j++)
            if (s->off[j])
                first = j;
        long ahead = first < 0                ? 0
                     : loop->levels[first].up ? s->off[first]
                                              : -s->off[first];
        if (first < 0 || ahead > 0)
            why = "waits for the current or a later iteration";
        for (int j = 0; j < s->n && !why; j++)
            if (step_misses(&loop->levels[j], s->off[j]))
                why = "refers to an iteration the loop never runs";
    }
    if (why)
        warn_tok(vm, s->tok, CCCC_WARN_OPENMP, "'sink' %s; ignored", why);
    return why != NULL;
}

// Checks an `ordered` with sink/source clauses against its loop.
static void doacross_check(VirtualMachine *vm, OmpDirective *d,
                           OmpDirective *loop, Token *start) {
    bool in_loop = loop &&
                   (loop->kind == OMP_FOR || loop->kind == OMP_PARALLEL_FOR) &&
                   loop->ordered_n;
    if (!in_loop)
        error_tok(vm, start,
                  "'ordered' with 'sink' or 'source' must be closely nested "
                  "inside a loop region with an 'ordered(n)' clause");
    if (d->source && d->sinks)
        error_tok(vm, start,
                  "'source' and 'sink' cannot be on the same 'ordered' "
                  "directive");
    for (OmpSink *s = d->sinks; s; s = s->next) {
        if (s->prev)
            continue;
        if (s->n != loop->ordered_n)
            error_tok(vm, s->tok,
                      "'sink' vector has %d element(s), but the loop has "
                      "ordered(%d)",
                      s->n, loop->ordered_n);
        for (int j = 0; j < s->n; j++)
            if (strcmp(s->name[j], loop->levels[j].name))
                error_tok(vm, s->tok,
                          "element %d of the 'sink' vector must be '%s'", j + 1,
                          loop->levels[j].name);
    }
}

// Lowers sink and source: a sink waits for the iteration it names, if the loop
// runs one; a source posts the current iteration.
static void doacross_lower(VirtualMachine *vm, OmpDirective *d,
                           OmpDirective *loop, OmpBody *body) {
    int id = loop->omp_id;
    for (OmpSink *s = d->sinks; s; s = s->next) {
        if (sink_dropped(vm, loop, s) || !vm->compiler.omp_threaded)
            continue;
        if (s->prev) {
            body_push(vm, body,
                      snippet(vm,
                              "{ long __da_f = %s + (%ld); if (__da_f >= 0) "
                              "__cccc_omp_doacross_wait(__da_f); }",
                              current_flat(vm, loop), s->off[0]));
            continue;
        }
        char *text =
            arena_format(vm, "{ long __da_f = 0, __da_i; int __da_ok = 1;");
        for (int j = 0; j < s->n; j++) {
            long delta = loop->levels[j].up ? s->off[j] : -s->off[j];
            if (delta)
                text = arena_format(
                    vm,
                    "%s if ((%ld) %% __omp%d_st%d) __da_ok = 0; __da_i = %s + "
                    "(%ld) / __omp%d_st%d; if (__da_i < 0 || __da_i >= "
                    "__omp%d_n%d) __da_ok = 0; __da_f = __da_f * __omp%d_n%d + "
                    "__da_i;",
                    text, delta, id, j, level_index(vm, loop, j), delta, id, j,
                    id, j, id, j);
            else
                text =
                    arena_format(vm, "%s __da_f = __da_f * __omp%d_n%d + %s;",
                                 text, id, j, level_index(vm, loop, j));
        }
        body_push(vm, body,
                  snippet(vm,
                          "%s if (__da_ok) __cccc_omp_doacross_wait(__da_f); }",
                          text));
    }
    if (d->source && vm->compiler.omp_threaded)
        body_push(vm, body,
                  snippet(vm, "__cccc_omp_doacross_post(%s);",
                          current_flat(vm, loop)));
}

// The worksharing loop. `tok` is on the `for`; the body statement follows the
// collapsed headers. Shadows have already been declared by the caller.
static Token *threaded_for(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                           int id, Token *tok, bool with_barrier) {
    Token *for_tok = tok;
    if (d->collapse > 8)
        error_tok(vm, tok, "collapse is limited to 8 loops with -c=native");
    OmpLoopLevel lv[8] = {};
    int          m     = d->collapse;
    for (int j = 0; j < m; j++) {
        if (!equal(tok, "for"))
            error_tok(vm, tok,
                      "collapse(%d) needs %d perfectly nested for loops", m, m);
        tok = loop_header(vm, tok, &lv[j]);
        for (int i = 0; i < j; i++)
            if (mentions(lv[j].lb, lv[i].name) ||
                mentions(lv[j].ub, lv[i].name) ||
                mentions(lv[j].step, lv[i].name))
                error_tok(vm, for_tok,
                          "collapsed loops must be rectangular: the bounds "
                          "of an inner loop cannot use '%s'",
                          lv[i].name);
    }

    int nlev = d->ordered_n > m ? d->ordered_n : m;
    for (int j = m; j < nlev; j++)
        lv[j] = d->levels[j];

    for (int j = 0; j < nlev; j++) {
        if (lv[j].ptr) {
            body_push(vm, body,
                      snippet(vm, "typeof_unqual(&*(%s)) __omp%d_p%d = (%s);",
                              lv[j].lb, id, j, lv[j].lb));
            body_push(vm, body,
                      snippet(vm,
                              "long __omp%d_lb%d = 0, __omp%d_ub%d = (%s) - "
                              "__omp%d_p%d, __omp%d_st%d = (long)(%s);",
                              id, j, id, j, lv[j].ub, id, j, id, j,
                              lv[j].step));
        } else {
            body_push(vm, body,
                      snippet(vm,
                              "long __omp%d_lb%d = (long)(%s), __omp%d_ub%d = "
                              "(long)(%s), __omp%d_st%d = (long)(%s);",
                              id, j, lv[j].lb, id, j, lv[j].ub, id, j,
                              lv[j].step));
        }
        body_push(vm, body,
                  snippet(vm, "long __omp%d_n%d = %s;", id, j,
                          iter_count(vm, id, j, &lv[j])));
    }
    char *total = arena_format(vm, "__omp%d_n0", id);
    for (int j = 1; j < m; j++)
        total = arena_format(vm, "%s * __omp%d_n%d", total, id, j);
    body_push(vm, body, snippet(vm, "long __omp%d_n = %s;", id, total));
    if (d->ordered_n) {
        char *inner = "1";
        for (int j = m; j < nlev; j++)
            inner = arena_format(vm, "%s * __omp%d_n%d", inner, id, j);
        body_push(vm, body, snippet(vm, "long __omp%d_in = %s;", id, inner));
    }
    body_push(vm, body,
              snippet(vm,
                      "long __omp%d_b, __omp%d_e, __omp%d_c = 0, __omp%d_ch = "
                      "__omp%d_cs;",
                      id, id, id, id, id));
    body_push(vm, body,
              snippet(vm, "int __omp%d_sk = %d, __omp%d_last = 0;", id,
                      d->sched_kind ? d->sched_kind : 1, id));
    body_push(vm, body,
              snippet(vm, "__cccc_omp_loop_begin(&__omp%d_sk, &__omp%d_ch);",
                      id, id));
    if (d->ordered_n)
        body_push(vm, body,
                  snippet(vm,
                          "__cccc_omp_doacross_init(__omp%d_n, __omp%d_in, "
                          "__omp%d_sk, __omp%d_ch);",
                          id, id, id, id));

    // Loop variables: declared by the header, or private copies of existing
    // ones.
    for (int j = 0; j < m; j++) {
        if (lv[j].declared)
            new_lvar(vm, lv[j].name, strlen(lv[j].name), lv[j].ty);
        else
            body_push(
                vm, body,
                snippet(vm, "typeof_unqual(%s) %s;", lv[j].name, lv[j].name));
    }

    // The body is parsed before the chunk loop exists, so it gets labels of
    // its own: `continue` ends the iteration, `break` is not allowed.
    char *saved_brk        = vm->compiler.brk_label;
    char *saved_cont       = vm->compiler.cont_label;
    char *saved_omp_brk    = vm->compiler.omp_loop_brk;
    int   saved_brk_cld    = vm->compiler.brk_cleanup_depth;
    int   saved_cont_cld   = vm->compiler.cont_cleanup_depth;
    char *body_cont        = new_unique_name(vm);
    vm->compiler.brk_label = vm->compiler.omp_loop_brk = new_unique_name(vm);
    vm->compiler.cont_label                            = body_cont;
    vm->compiler.brk_cleanup_depth  = vm->compiler.cleanup_scope_depth;
    vm->compiler.cont_cleanup_depth = vm->compiler.cleanup_scope_depth;
    vm->compiler.omp_assoc_loops    = nlev - m;
    Node *inner                     = stmt(vm, &tok, tok);
    vm->compiler.omp_assoc_loops    = 0;
    vm->compiler.brk_label          = saved_brk;
    vm->compiler.cont_label         = saved_cont;
    vm->compiler.omp_loop_brk       = saved_omp_brk;
    vm->compiler.brk_cleanup_depth  = saved_brk_cld;
    vm->compiler.cont_cleanup_depth = saved_cont_cld;

    body_push(vm, body, snippet(vm, "long __omp%d_k;", id));
    Node *loop =
        d->ordered_n
            ? snippet(vm,
                      "for (__omp%d_k = __omp%d_b; __omp%d_k < "
                      "__omp%d_e; __cccc_omp_doacross_done(__omp%d_k), "
                      "__omp%d_k++) ;",
                      id, id, id, id, id, id)
            : snippet(vm,
                      "for (__omp%d_k = __omp%d_b; __omp%d_k < "
                      "__omp%d_e; __omp%d_k++) ;",
                      id, id, id, id, id);
    Node *for_node = first_loop(loop);
    if (!for_node)
        error_tok(vm, for_tok, "internal error: omp loop shape");
    node_ctl_mut(vm, for_node)->cont_label = body_cont;

    Node    head = {};
    OmpBody it   = {&head, &head};
    body_push(vm, &it, snippet(vm, "long __omp%d_i = __omp%d_k;", id, id));
    for (int j = m - 1; j >= 0; j--) {
        body_push(vm, &it,
                  snippet(vm, "%s = %s;", lv[j].name,
                          level_value(vm, id, j, &lv[j],
                                      arena_format(vm,
                                                   "(__omp%d_i %% __omp%d_n%d) "
                                                   "* __omp%d_st%d",
                                                   id, id, j, id, j))));
        if (j)
            body_push(vm, &it,
                      snippet(vm, "__omp%d_i /= __omp%d_n%d;", id, id, j));
    }
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->kind == OMP_CLAUSE_LINEAR)
            body_push(
                vm, &it,
                snippet(vm, "%s",
                        linear_assign(vm, c, id,
                                      arena_format(vm, "__omp%d_k", id))));
    body_push(vm, &it, inner);
    Node *iter_block = new_node(vm, ND_BLOCK, for_tok);
    iter_block->body = head.next;
    node_ctl_mut(vm, for_node)->then = iter_block;

    // One pass of the outer loop is one chunk; a static loop without a chunk
    // size has a single one.
    Node *chunks = snippet(vm,
                           "while (__cccc_omp_loop_next(__omp%d_sk, "
                           "__omp%d_ch, __omp%d_n, &__omp%d_c, &__omp%d_b, "
                           "&__omp%d_e)) ;",
                           id, id, id, id, id, id);
    Node *chunks_node = first_loop(chunks);
    if (!chunks_node)
        error_tok(vm, for_tok, "internal error: omp loop shape");
    Node    chead = {};
    OmpBody cb    = {&chead, &chead};
    if (d->ordered)
        body_push(vm, &cb,
                  snippet(vm, "__cccc_omp_ordered_start(__omp%d_b);", id));
    body_push(vm, &cb, loop);
    if (d->ordered)
        body_push(vm, &cb,
                  snippet(vm, "__cccc_omp_ordered_chunk_end(__omp%d_e);", id));
    body_push(vm, &cb,
              snippet(vm,
                      "__omp%d_last |= __omp%d_b < __omp%d_e && __omp%d_e == "
                      "__omp%d_n;",
                      id, id, id, id, id));
    Node *chunk_block = new_node(vm, ND_BLOCK, for_tok);
    chunk_block->body = chead.next;
    node_ctl_mut(vm, chunks_node)->then = chunk_block;
    body_push(vm, body, chunks);
    if (d->ordered_n)
        body_push(vm, body, snippet(vm, "__cccc_omp_doacross_fini();"));
    body_push(
        vm, body,
        snippet(vm, "__cccc_omp_loop_end(__omp%d_sk, __omp%d_n);", id, id));
    if (d->ordered)
        body_push(vm, body,
                  snippet(vm, "__cccc_omp_ordered_loop_end(__omp%d_n);", id));

    // lastprivate: the thread that ran the sequentially last iteration copies
    // its values back; a loop variable ends one step past the last iteration.
    bool any_last = false;
    for (OmpClause *c = d->clauses; c; c = c->next)
        any_last |=
            c->kind == OMP_CLAUSE_LASTPRIVATE || c->kind == OMP_CLAUSE_LINEAR;
    if (any_last) {
        char *copy = arena_strdup(vm, "");
        for (OmpClause *c = d->clauses; c; c = c->next) {
            if (c->kind != OMP_CLAUSE_LASTPRIVATE &&
                c->kind != OMP_CLAUSE_LINEAR)
                continue;
            for (int j = 0; j < m; j++)
                if (!strcmp(lv[j].name, c->name))
                    copy = arena_format(
                        vm, "%s %s = %s;", copy, c->name,
                        level_value(vm, id, j, &lv[j],
                                    arena_format(vm,
                                                 "__omp%d_n%d * __omp%d_st%d",
                                                 id, j, id, j)));
            copy = arena_format(
                vm, "%s __builtin_memcpy(__omp_%d_%s, &%s, sizeof(%s));", copy,
                id, c->name, c->name, c->name);
        }
        body_push(vm, body, snippet(vm, "if (__omp%d_last) { %s }", id, copy));
    }
    if (with_barrier && !d->nowait)
        body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
    return tok;
}

// `atomic` statement forms: x op= e; x++; x = x op e; and the read, write
// and capture variants. The statement is parsed from tokens because the AST
// has already lost the operator by the time it exists.
typedef struct {
    char *x, *v, *op, *e;
    bool  pre, post, reversed;
    bool  swap; // `x = e`: the new value is e, not an update of x
    bool  old;  // capture: v gets the value before the update
    // compare: `x == e` stores d; `x < e` / `x > e` store e
    char *cmp, *d, *r; // r: the `r = x == e;` success flag
    bool  x_left;      // x is the left operand of `<` / `>`
    bool  v_on_fail;   // `else { v = x; }`: v gets x only when nothing is stored
} OmpAtomic;

static bool is_update_op(Token *t) {
    static const char *const ops[] = {
        "+=", "-=", "*=", "/=", "&=", "|=", "^=", "<<=", ">>="};
    for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); i++)
        if (equal(t, (char *)ops[i]))
            return true;
    return false;
}

// Binding strength of the binary operator `t` (higher binds tighter; 0 for
// `?:`, assignment and `,`), or -1 when `t` is not one.
static int binary_prec(Token *t) {
    static const struct {
        const char *op;
        int         prec;
    } ops[] = {{"*", 10}, {"/", 10}, {"%", 10}, {"+", 9}, {"-", 9},  {"<<", 8},
               {">>", 8}, {"<", 7},  {"<=", 7}, {">", 7}, {">=", 7}, {"==", 6},
               {"!=", 6}, {"&", 5},  {"^", 4},  {"|", 3}, {"&&", 2}, {"||", 1},
               {"?", 0},  {":", 0},  {",", 0},  {"=", 0}};
    for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); i++)
        if (equal(t, (char *)ops[i].op))
            return ops[i].prec;
    return is_update_op(t) ? 0 : -1;
}

// The operators `x = x op e` may use.
static int update_op_prec(Token *t) {
    static const char *const ops[] = {"+", "-", "*",  "/", "&",
                                      "^", "|", "<<", ">>"};
    for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); i++)
        if (equal(t, (char *)ops[i]))
            return binary_prec(t);
    return -1;
}

static bool ends_operand(Token *t) {
    return t->kind != TK_PUNCT || equal(t, ")") || equal(t, "]");
}

// Whether `[s, end)` is one operand of a binary operator of strength `prec`:
// no top-level operator of that strength or looser (looser than it when
// `left`, since equal strength groups to the left).
static bool binds_tighter(Token *s, Token *end, int prec, bool left) {
    int    depth = 0;
    Token *prev = NULL, *prev2 = NULL;
    for (Token *t = s; t != end; prev2 = prev, prev = t, t = t->next) {
        if (equal(t, "(") || equal(t, "[") || equal(t, "{")) {
            depth++;
        } else if (equal(t, ")") || equal(t, "]") || equal(t, "}")) {
            depth--;
        } else if (depth == 0 && prev) {
            bool binary = ends_operand(prev) ||
                          ((equal(prev, "++") || equal(prev, "--")) && prev2 &&
                           ends_operand(prev2));
            int  p      = binary ? binary_prec(t) : -1;
            if (p >= 0 && (left ? p < prec : p <= prec))
                return false;
        }
    }
    return true;
}

static bool atomic_update_form(VirtualMachine *vm, Token *s, Token *end,
                               OmpAtomic *a) {
    if (equal(s, "++") || equal(s, "--")) {
        a->pre = true;
        a->op  = equal(s, "++") ? "+" : "-";
        a->x   = tokens_text(vm, s->next, end);
        return true;
    }
    int    depth = 0;
    Token *p     = s;
    for (; p != end; p = p->next) {
        if (equal(p, "(") || equal(p, "["))
            depth++;
        else if (equal(p, ")") || equal(p, "]"))
            depth--;
        else if (depth == 0 && (equal(p, "=") || equal(p, "++") ||
                                equal(p, "--") || is_update_op(p)))
            break;
    }
    if (p == end)
        return false;
    a->x = tokens_text(vm, s, p);
    if (equal(p, "++") || equal(p, "--")) {
        a->post = true;
        a->op   = equal(p, "++") ? "+" : "-";
        return true;
    }
    if (is_update_op(p)) {
        a->op                    = tok_text(vm, p);
        a->op[strlen(a->op) - 1] = '\0';
        a->e                     = tokens_text(vm, p->next, end);
        return true;
    }
    // x = x op e  /  x = e op x
    Token *rhs = p->next;
    int    nx  = 0;
    for (Token *t = s; t != p; t = t->next)
        nx++;
    bool   lead = true;
    Token *t1 = s, *t2 = rhs;
    for (int i = 0; i < nx; i++, t1 = t1->next, t2 = t2->next)
        if (t2 == end || t1->len != t2->len ||
            memcmp(t1->loc, t2->loc, t1->len))
            lead = false;
    if (lead && t2 != end) {
        int prec = update_op_prec(t2);
        if (prec < 0 || !binds_tighter(t2->next, end, prec, false))
            return false;
        a->op = tok_text(vm, t2);
        a->e  = tokens_text(vm, t2->next, end);
        return true;
    }
    Token *last = rhs;
    int    n    = 0;
    for (Token *t = rhs; t != end; t = t->next, n++)
        last = t;
    Token *tail = rhs;
    for (int i = 0; i < n - nx; i++)
        tail = tail->next;
    bool match = n > nx;
    {
        Token *a1 = tail, *b1 = s;
        for (int i = 0; match && i < nx; i++, a1 = a1->next, b1 = b1->next)
            match = a1->len == b1->len && !memcmp(a1->loc, b1->loc, a1->len);
    }
    (void)last;
    if (!match)
        return false;
    Token *opt = rhs;
    for (int i = 0; i < n - nx - 1; i++)
        opt = opt->next;
    int prec = update_op_prec(opt);
    if (prec < 0 || !binds_tighter(rhs, opt, prec, true))
        return false;
    a->reversed = true;
    a->op       = tok_text(vm, opt);
    a->e        = tokens_text(vm, rhs, opt);
    return true;
}

static const char *atomic_container(VirtualMachine *vm, Token *tok,
                                    const char *x) {
    Token *t = tokenize_string(vm, "<omp>", arena_format(vm, "%s", x));
    convert_pp_tokens(vm, t);
    Node *n = expr(vm, &t, t);
    add_type(vm, n);
    Type *ty = n->ty;
    if (!is_numeric(ty))
        error_tok(vm, tok, "'#pragma omp atomic' needs an arithmetic variable");
    switch (ty->size) {
        case 1:
            return "unsigned char";
        case 2:
            return "unsigned short";
        case 4:
            return "unsigned int";
        case 8:
            return "unsigned long";
    }
    error_tok(vm, tok, "'#pragma omp atomic' does not support this type");
    return NULL;
}

static bool atomic_is_integer(VirtualMachine *vm, const char *x) {
    Token *t = tokenize_string(vm, "<omp>", arena_format(vm, "%s", x));
    convert_pp_tokens(vm, t);
    Node *n = expr(vm, &t, t);
    add_type(vm, n);
    return is_integer(n->ty);
}

// The `;` ending the statement that starts at `s`, or NULL.
static Token *statement_end(Token *s) {
    int depth = 0;
    for (Token *t = s; t->kind != TK_EOF; t = t->next) {
        if (equal(t, "(") || equal(t, "[") || equal(t, "{"))
            depth++;
        else if (equal(t, ")") || equal(t, "]") || equal(t, "}")) {
            if (--depth < 0)
                return NULL;
        } else if (depth == 0 && equal(t, ";"))
            return t;
    }
    return NULL;
}

// The top-level `=` of `[s, end)` that is a plain assignment, or NULL.
static Token *plain_assign(Token *s, Token *end) {
    int depth = 0;
    for (Token *t = s; t != end; t = t->next) {
        if (equal(t, "(") || equal(t, "["))
            depth++;
        else if (equal(t, ")") || equal(t, "]"))
            depth--;
        else if (depth == 0 && equal(t, "="))
            return t;
    }
    return NULL;
}

// `v = r;` of a capture block: sets a->v and returns the text of r.
static char *capture_read(VirtualMachine *vm, Token *s, Token *end,
                          OmpAtomic *a) {
    Token *eq = plain_assign(s, end);
    if (!eq || eq == s || eq->next == end)
        return NULL;
    a->v = tokens_text(vm, s, eq);
    return tokens_text(vm, eq->next, end);
}

// The update statement `[s, end)` of a capture block, on the variable `r`.
static bool capture_update(VirtualMachine *vm, Token *s, Token *end,
                           const char *r, bool allow_swap, OmpAtomic *a) {
    OmpAtomic u = {};
    if (atomic_update_form(vm, s, end, &u) && !strcmp(u.x, r)) {
        a->x        = u.x;
        a->op       = u.op;
        a->e        = u.e;
        a->pre      = u.pre;
        a->post     = u.post;
        a->reversed = u.reversed;
        return true;
    }
    Token *eq = plain_assign(s, end);
    if (!allow_swap || !eq || eq == s || eq->next == end ||
        strcmp(tokens_text(vm, s, eq), r))
        return false;
    a->x    = tokens_text(vm, s, eq);
    a->e    = tokens_text(vm, eq->next, end);
    a->swap = true;
    return true;
}

// `{ v = x; x op= e; }`, `{ x op= e; v = x; }` or `{ v = x; x = e; }` with
// `tok` on the `{`; returns the token after the `}`.
static Token *atomic_capture_block(VirtualMachine *vm, Token *tok,
                                   OmpAtomic *a) {
    Token *s1 = tok->next, *e1 = statement_end(s1);
    Token *s2 = e1 ? e1->next : NULL, *e2 = s2 ? statement_end(s2) : NULL;
    if (!e2 || !equal(e2->next, "}"))
        error_tok(vm, tok,
                  "unsupported '#pragma omp atomic capture' block: expected "
                  "two statements");
    char *r = capture_read(vm, s1, e1, a);
    if (r && capture_update(vm, s2, e2, r, true, a)) {
        a->old = true;
        return e2->next->next;
    }
    *a = (OmpAtomic){};
    r  = capture_read(vm, s2, e2, a);
    if (r && capture_update(vm, s1, e1, r, false, a))
        return e2->next->next;
    error_tok(vm, tok,
              "unsupported '#pragma omp atomic capture' block: expected "
              "'{ v = x; x op= e; }', '{ x op= e; v = x; }' or '{ v = x; x = "
              "e; }'");
    return NULL;
}

// The closing token of the group opened at `open`, or NULL.
static Token *group_close(Token *open) {
    int depth = 0;
    for (Token *t = open; t->kind != TK_EOF; t = t->next) {
        if (equal(t, "(") || equal(t, "[") || equal(t, "{"))
            depth++;
        else if (equal(t, ")") || equal(t, "]") || equal(t, "}")) {
            if (--depth == 0)
                return t;
        }
    }
    return NULL;
}

// The token after the compare statement at `s`: an `if` with its braced
// bodies (and `else` body), otherwise the token after the `;`.
static Token *compare_stmt_end(Token *s) {
    if (!equal(s, "if")) {
        Token *semi = statement_end(s);
        return semi ? semi->next : NULL;
    }
    Token *t = equal(s->next, "(") ? group_close(s->next) : NULL;
    t        = t && equal(t->next, "{") ? group_close(t->next) : NULL;
    if (!t)
        return NULL;
    t = t->next;
    if (equal(t, "else")) {
        t = equal(t->next, "{") ? group_close(t->next) : NULL;
        return t ? t->next : NULL;
    }
    return t;
}

// `{ lhs = rhs; }` with `open` on the `{`.
static bool braced_assign(VirtualMachine *vm, Token *open, char **lhs,
                          char **rhs) {
    Token *s    = open->next;
    Token *semi = statement_end(s);
    if (!semi || group_close(open) != semi->next)
        return false;
    Token *eq = plain_assign(s, semi);
    if (!eq || eq == s || eq->next == semi)
        return false;
    *lhs = tokens_text(vm, s, eq);
    *rhs = tokens_text(vm, eq->next, semi);
    return true;
}

// The single top-level `==`, `<` or `>` of `[s, end)`.
static bool split_comparison(VirtualMachine *vm, Token *s, Token *end,
                             char **l, char **op, char **r) {
    int    depth = 0;
    Token *at    = NULL;
    for (Token *t = s; t != end; t = t->next) {
        if (equal(t, "(") || equal(t, "["))
            depth++;
        else if (equal(t, ")") || equal(t, "]"))
            depth--;
        else if (depth == 0 && (equal(t, "==") || equal(t, "<") || equal(t, ">"))) {
            if (at)
                return false;
            at = t;
        }
    }
    if (!at || at == s || at->next == end)
        return false;
    *l  = tokens_text(vm, s, at);
    *op = tok_text(vm, at);
    *r  = tokens_text(vm, at->next, end);
    return true;
}

// Sets a->cmp, a->e and a->x_left from `l op r` and the variable a->x.
static bool compare_operands(OmpAtomic *a, char *l, char *op, char *r) {
    if (!strcmp(l, a->x)) {
        a->x_left = true;
        a->e      = r;
    } else if (strcmp(op, "==") && !strcmp(r, a->x)) {
        a->e = l;
    } else {
        return false;
    }
    a->cmp = op;
    return true;
}

// The value stored when the comparison holds: d for `==`, e for `<` and `>`.
static bool compare_store(OmpAtomic *a, char *val) {
    if (!strcmp(a->cmp, "==")) {
        a->d = val;
        return true;
    }
    return !strcmp(val, a->e);
}

// `if (x == e) { x = d; } [else { v = x; }]`, `if (x < e) { x = e; }` or the
// `?:` equivalent in `[s, end)`.
static bool compare_update(VirtualMachine *vm, Token *s, Token *end,
                           OmpAtomic *a, bool allow_else) {
    char *l, *op, *r, *val;
    if (equal(s, "if")) {
        Token *rp = equal(s->next, "(") ? group_close(s->next) : NULL;
        Token *lb = rp ? rp->next : NULL;
        if (!lb || !equal(lb, "{") || !braced_assign(vm, lb, &a->x, &val) ||
            !split_comparison(vm, s->next->next, rp, &l, &op, &r) ||
            !compare_operands(a, l, op, r) || !compare_store(a, val))
            return false;
        Token *after = group_close(lb)->next;
        if (after == end)
            return true;
        if (!allow_else || strcmp(a->cmp, "==") || !equal(after, "else") ||
            !equal(after->next, "{") ||
            group_close(after->next)->next != end ||
            !braced_assign(vm, after->next, &a->v, &val) || strcmp(val, a->x))
            return false;
        a->v_on_fail = true;
        return true;
    }
    Token *semi = statement_end(s);
    Token *eq   = semi && semi->next == end ? plain_assign(s, semi) : NULL;
    if (!eq || eq == s || equal(s, "{"))
        return false;
    a->x = tokens_text(vm, s, eq);
    int    depth = 0;
    Token *q = NULL, *colon = NULL;
    for (Token *t = eq->next; t != semi; t = t->next) {
        if (equal(t, "(") || equal(t, "["))
            depth++;
        else if (equal(t, ")") || equal(t, "]"))
            depth--;
        else if (depth == 0 && equal(t, "?") && !q)
            q = t;
        else if (depth == 0 && equal(t, ":") && q)
            colon = t;
    }
    if (!q || !colon || q == eq->next || colon == q->next ||
        colon->next == semi || strcmp(tokens_text(vm, colon->next, semi), a->x) ||
        !split_comparison(vm, eq->next, q, &l, &op, &r) ||
        !compare_operands(a, l, op, r))
        return false;
    return compare_store(a, tokens_text(vm, q->next, colon));
}

static void compare_form_error(VirtualMachine *vm, Token *tok) {
    error_tok(vm, tok,
              "unsupported '#pragma omp atomic compare' statement: expected "
              "'if (x == e) { x = d; }', 'x = x == e ? d : x;', 'if (x < e) { "
              "x = e; }' or 'x = x > e ? e : x;' (in a capture, also "
              "'{ v = x; <update> }', '{ <update> v = x; }', 'if (x == e) { x = "
              "d; } else { v = x; }' or '{ r = x == e; if (r) { x = d; } "
              "[else { v = x; }] }')");
}

// `{ r = x == e; if (r) { x = d; } [else { v = x; }] }` of `[s, end)`, where
// `s1` is `r = x == e;` ending before `s2`.
static bool compare_flag_block(VirtualMachine *vm, Token *s1, Token *s2,
                               Token *end, OmpAtomic *a) {
    Token *semi = statement_end(s1);
    Token *eq   = semi ? plain_assign(s1, semi) : NULL;
    char  *l, *op, *r, *val;
    if (!eq || eq == s1 || !equal(s2, "if") || !equal(s2->next, "(") ||
        !split_comparison(vm, eq->next, semi, &l, &op, &r) || strcmp(op, "=="))
        return false;
    a->r = tokens_text(vm, s1, eq);
    a->x = l;
    if (!compare_operands(a, l, op, r))
        return false;
    Token *rp = group_close(s2->next);
    Token *lb = rp->next;
    if (strcmp(tokens_text(vm, s2->next->next, rp), a->r) || !equal(lb, "{") ||
        !braced_assign(vm, lb, &val, &a->d) || strcmp(val, a->x))
        return false;
    Token *after = group_close(lb)->next;
    if (after == end)
        return true;
    if (!equal(after, "else") || !equal(after->next, "{") ||
        group_close(after->next)->next != end ||
        !braced_assign(vm, after->next, &a->v, &val) || strcmp(val, a->x))
        return false;
    a->v_on_fail = true;
    return true;
}

// A capture block with `tok` on the `{`; returns the token after the `}`.
static Token *atomic_compare_block(VirtualMachine *vm, Token *tok,
                                   OmpAtomic *a) {
    Token *s1 = tok->next, *s2 = compare_stmt_end(s1);
    Token *e2 = s2 ? compare_stmt_end(s2) : NULL;
    Token *close = group_close(tok);
    if (!e2 || !close || e2 != close)
        compare_form_error(vm, tok);
    // { v = x; <update> }
    Token *semi1 = equal(s1, "if") ? NULL : statement_end(s1);
    char  *x     = semi1 ? capture_read(vm, s1, semi1, a) : NULL;
    if (x && compare_update(vm, s2, e2, a, false) && !strcmp(a->x, x)) {
        a->old = true;
        return close->next;
    }
    // { <update> v = x; }
    *a = (OmpAtomic){};
    Token *semi2 = equal(s2, "if") ? NULL : statement_end(s2);
    if (semi2 && compare_update(vm, s1, s2, a, false)) {
        char *x2 = capture_read(vm, s2, semi2, a);
        if (x2 && !strcmp(x2, a->x))
            return close->next;
    }
    // { r = x == e; if (r) { x = d; } [else { v = x; }] }
    *a = (OmpAtomic){};
    if (compare_flag_block(vm, s1, s2, e2, a))
        return close->next;
    compare_form_error(vm, tok);
    return NULL;
}

static Node *atomic_compare_stmt(VirtualMachine *vm, OmpDirective *d,
                                 Token **rest, Token *tok) {
    OmpAtomic a       = {};
    bool      capture = d->atomic_kind == 3;
    if (capture && equal(tok, "{")) {
        *rest = atomic_compare_block(vm, tok, &a);
    } else {
        Token *end = equal(tok, "{") ? NULL : compare_stmt_end(tok);
        if (!end || !compare_update(vm, tok, end, &a, capture) ||
            (capture && !a.v_on_fail))
            compare_form_error(vm, tok);
        *rest = end;
    }
    if (d->atomic_weak && strcmp(a.cmp, "=="))
        error_tok(vm, tok, "'weak' clause requires atomic equality comparison");

    const char *c    = atomic_container(vm, tok, a.x);
    const char *T    = arena_format(vm, "typeof_unqual(%s)", a.x);
    const char *load = arena_format(vm, "(%s *)&(%s)", c, a.x);
    const char *caps = "";
    if (!strcmp(a.cmp, "==")) {
        if (a.r)
            caps = arena_format(vm, "%s = __ok;", a.r);
        if (a.v && a.v_on_fail)
            caps = arena_format(vm, "%sif (!__ok) %s = __o;", caps, a.v);
        else if (a.v)
            caps = arena_format(vm, "%s = %s;", a.v,
                                a.old ? "__o" : "__ok ? __d : __o");
        return snippet(
            vm,
            "{ %s __e = (%s), __d = (%s); %s __eb, __db; %s __o; "
            "__builtin_memcpy(&__eb, &__e, sizeof(__e)); "
            "__builtin_memcpy(&__db, &__d, sizeof(__d)); "
            "_Bool __ok = __atomic_compare_exchange_n(%s, &__eb, __db, %d, %d, "
            "%d); __builtin_memcpy(&__o, &__eb, sizeof(__o)); %s }",
            T, a.e, a.d, c, T, load, d->atomic_weak ? 1 : 0, d->atomic_order,
            d->atomic_fail, caps);
    }
    if (a.v)
        caps = arena_format(vm, "%s = %s;", a.v,
                            a.old ? "__o" : "__st ? __e : __o");
    const char *cond = a.x_left ? arena_format(vm, "__o %s __e", a.cmp)
                                : arena_format(vm, "__e %s __o", a.cmp);
    return snippet(
        vm,
        "{ %s __e = (%s); %s __ob = __atomic_load_n(%s, %d), __eb; %s __o; "
        "_Bool __st = 0; __builtin_memcpy(&__eb, &__e, sizeof(__e)); "
        "for (;;) { __builtin_memcpy(&__o, &__ob, sizeof(__o)); if (!(%s)) "
        "break; if (__atomic_compare_exchange_n(%s, &__ob, __eb, 0, %d, %d)) "
        "{ __st = 1; break; } } %s }",
        T, a.e, c, load, d->atomic_fail, T, cond, load, d->atomic_order,
        d->atomic_fail, caps);
}

static Node *threaded_atomic(VirtualMachine *vm, OmpDirective *d, Token **rest,
                             Token *tok) {
    if (d->atomic_compare)
        return atomic_compare_stmt(vm, d, rest, tok);
    OmpAtomic a = {};
    char     *stmt_text;
    if (d->atomic_kind == 3 && equal(tok, "{")) {
        *rest = atomic_capture_block(vm, tok, &a);
    } else {
        Token *end = tok;
        while (!equal(end, ";")) {
            if (end->kind == TK_EOF || equal(end, "{"))
                error_tok(vm, tok,
                          "unsupported '#pragma omp atomic' statement");
            end = end->next;
        }
        *rest = end->next;

        if (d->atomic_kind == 1 || d->atomic_kind == 2 || d->atomic_kind == 3) {
            Token *eq = tok;
            while (eq != end && !equal(eq, "="))
                eq = eq->next;
            if (eq == end)
                error_tok(vm, tok,
                          "unsupported '#pragma omp atomic' statement");
            if (d->atomic_kind == 1) {
                a.v = tokens_text(vm, tok, eq);
                a.x = tokens_text(vm, eq->next, end);
            } else if (d->atomic_kind == 2) {
                a.x = tokens_text(vm, tok, eq);
                a.e = tokens_text(vm, eq->next, end);
            } else {
                a.v = tokens_text(vm, tok, eq);
                if (!atomic_update_form(vm, eq->next, end, &a))
                    error_tok(vm, tok,
                              "unsupported '#pragma omp atomic' statement");
                a.old = a.post;
            }
        } else if (!atomic_update_form(vm, tok, end, &a)) {
            error_tok(vm, tok, "unsupported '#pragma omp atomic' statement");
        }
    }

    const char *x = a.x;
    const char *c = atomic_container(vm, tok, x);
    const char *T = arena_format(vm, "typeof_unqual(%s)", x);
    const char *load =
        arena_format(vm, "(%s *)&(%s)", c, x); // container view of x
    const char *fetch = NULL;
    if (a.op && a.e && !a.reversed && atomic_is_integer(vm, x)) {
        if (!strcmp(a.op, "+"))
            fetch = "add";
        else if (!strcmp(a.op, "-"))
            fetch = "sub";
        else if (!strcmp(a.op, "&"))
            fetch = "and";
        else if (!strcmp(a.op, "|"))
            fetch = "or";
        else if (!strcmp(a.op, "^"))
            fetch = "xor";
    }
    if (a.op && !a.e && atomic_is_integer(vm, x))
        fetch = !strcmp(a.op, "+") ? "add" : "sub";
    const char *evalue = a.e ? a.e : "1";

    if (d->atomic_kind == 1) {
        stmt_text = arena_format(vm,
                                 "{ %s __b = __atomic_load_n(%s, %d); %s __t; "
                                 "__builtin_memcpy(&__t, "
                                 "&__b, sizeof(__t)); %s = __t; }",
                                 c, load, d->atomic_order, T, a.v);
    } else if (d->atomic_kind == 2) {
        stmt_text = arena_format(
            vm,
            "{ %s __t = (%s); %s __b; __builtin_memcpy(&__b, &__t, "
            "sizeof(__t)); __atomic_store_n(%s, __b, %d); }",
            T, a.e, c, load, d->atomic_order);
    } else if (a.swap) {
        stmt_text = arena_format(
            vm,
            "{ %s __t = (%s); %s __nb, __ob; __builtin_memcpy(&__nb, &__t, "
            "sizeof(__t)); __ob = __atomic_exchange_n(%s, __nb, %d); %s __o; "
            "__builtin_memcpy(&__o, &__ob, sizeof(__o)); %s = __o; }",
            T, a.e, c, load, d->atomic_order, T, a.v);
    } else if (fetch && d->atomic_kind != 3) {
        stmt_text = arena_format(vm, "__atomic_fetch_%s(&(%s), (%s), %d);",
                                 fetch, x, evalue, d->atomic_order);
    } else if (fetch && a.old) {
        stmt_text = arena_format(vm, "%s = __atomic_fetch_%s(&(%s), (%s), %d);",
                                 a.v, fetch, x, evalue, d->atomic_order);
    } else if (fetch) {
        stmt_text = arena_format(vm, "%s = __atomic_%s_fetch(&(%s), (%s), %d);",
                                 a.v, fetch, x, evalue, d->atomic_order);
    } else {
        const char *newv = a.reversed ? arena_format(vm, "__e %s __o", a.op)
                                      : arena_format(vm, "__o %s __e", a.op);
        stmt_text        = arena_format(
            vm,
            "{ typeof_unqual(%s) __e = (%s); %s __ob = __atomic_load_n(%s, "
            "%d), "
            "__nb; %s __o, __n; for (;;) { __builtin_memcpy(&__o, &__ob, "
            "sizeof(__o)); __n = %s; __builtin_memcpy(&__nb, &__n, "
            "sizeof(__n)); if (__atomic_compare_exchange_n(%s, &__ob, __nb, "
            "0, %d, %d)) break; } %s }",
            evalue, evalue, c, load, omp_load_order(d->atomic_order), T, newv,
            load, d->atomic_order, omp_load_order(d->atomic_order),
            d->atomic_kind == 3
                ? arena_format(vm, "%s = %s;", a.v, a.old ? "__o" : "__n")
                : "");
    }
    return snippet(vm, "%s", stmt_text);
}

// `threadprivate(list)` with `tok` on the word; returns the token after the
// directive. Marking the variable thread-local gives each thread its own copy.
static Token *threadprivate_list(VirtualMachine *vm, Token *tok) {
    tok = skip(vm, tok->next, "(");
    do {
        VarScope *sc = tok->kind == TK_IDENT ? find_var(vm, tok) : NULL;
        if (!sc || !sc->var || sc->var->is_function)
            error_tok(vm, tok, "'%.*s' in 'threadprivate' is not a variable",
                      tok->len, tok->loc);
        Obj *var = sc->var;
        if (var->is_local && !var->is_static)
            error_tok(vm, tok,
                      "'%.*s' in 'threadprivate' must be a global or static "
                      "variable",
                      tok->len, tok->loc);
        if (var->is_used)
            error_tok(vm, tok,
                      "'%.*s' declared 'threadprivate' after first use",
                      tok->len, tok->loc);
        var->is_tls = true;
        tok         = tok->next;
    } while (consume(vm, &tok, tok, ","));
    tok = skip(vm, tok, ")");
    return skip(vm, tok, ")");
}

// File-scope `#pragma omp`: only threadprivate has an effect there.
Token *omp_file_scope(VirtualMachine *vm, Token *tok) {
    Token *word = tok->next->next;
    if (!is_word(word, "threadprivate"))
        error_tok(vm, tok, "OpenMP directive at file scope is not supported");
    return threadprivate_list(vm, word);
}

static Node *find_node(Node *n, NodeKind kind) {
    while (n && n->kind != kind)
        n = n->kind == ND_BLOCK ? n->body : NULL;
    return n;
}

// Sections as a worksharing loop over the section index: each claim takes one
// section from the team's dynamic counter, and the thread that claims the last
// index copies lastprivate values back.
static void threaded_sections(VirtualMachine *vm, OmpDirective *d,
                              OmpBody *body, int id, Node **secs, int n,
                              Token *start, bool with_barrier) {
    body_push(vm, body,
              snippet(vm,
                      "long __omp%d_b, __omp%d_e, __omp%d_c = 0, __omp%d_k;",
                      id, id, id, id));
    body_push(vm, body, snippet(vm, "int __omp%d_last = 0;", id));
    Node *loop = snippet(vm,
                         "for (__omp%d_k = __omp%d_b; __omp%d_k < __omp%d_e; "
                         "__omp%d_k++) ;",
                         id, id, id, id, id);
    Node *for_node = find_node(loop, ND_FOR);
    if (!for_node)
        error_tok(vm, start, "internal error: omp sections shape");
    Node    head = {};
    OmpBody it   = {&head, &head};
    for (int j = 0; j < n; j++) {
        Node *guarded = snippet(vm, "if (__omp%d_k == %d) ;", id, j);
        Node *ifn     = find_node(guarded, ND_IF);
        if (!ifn)
            error_tok(vm, start, "internal error: omp sections shape");
        node_ctl_mut(vm, ifn)->then = secs[j];
        body_push(vm, &it, guarded);
    }
    Node *iter_block = new_node(vm, ND_BLOCK, start);
    iter_block->body = head.next;
    node_ctl_mut(vm, for_node)->then = iter_block;

    Node *claims = snippet(vm,
                           "while (__cccc_omp_loop_next(2, 1, %d, &__omp%d_c, "
                           "&__omp%d_b, &__omp%d_e)) ;",
                           n, id, id, id);
    Node *claims_node = find_node(claims, ND_FOR);
    if (!claims_node)
        error_tok(vm, start, "internal error: omp sections shape");
    Node    chead = {};
    OmpBody cb    = {&chead, &chead};
    body_push(vm, &cb, loop);
    body_push(vm, &cb,
              snippet(vm, "__omp%d_last |= __omp%d_e == %d;", id, id, n));
    Node *claim_block = new_node(vm, ND_BLOCK, start);
    claim_block->body = chead.next;
    node_ctl_mut(vm, claims_node)->then = claim_block;
    body_push(vm, body, claims);
    body_push(vm, body, snippet(vm, "__cccc_omp_loop_end(2, %d);", n));

    char *copy = arena_strdup(vm, "");
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->kind == OMP_CLAUSE_LASTPRIVATE)
            copy = arena_format(
                vm, "%s __builtin_memcpy(__omp_%d_%s, &%s, sizeof(%s));", copy,
                id, c->name, c->name, c->name);
    if (*copy)
        body_push(vm, body, snippet(vm, "if (__omp%d_last) {%s}", id, copy));
    if (with_barrier && !d->nowait)
        body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
}

// A task becomes a nested function, like a parallel region. Its copies of
// outer variables are read from a payload that the spawning code fills in and
// the runtime frees, so the task can run after the code that created it has
// moved on.
static void threaded_task(VirtualMachine *vm, OmpDirective *d, OmpBody *body,
                          Token **rest, Token *tok, Token *start, int id) {
    OutlineRegion r =
        outline_begin(vm, start, arena_format(vm, "__omp_task_%d", id));
    r.fn->is_omp_task = true;
    d->task_outlined  = true;

    Node     thead    = {};
    OmpBody  tbody    = {&thead, &thead};
    OmpFrame frame    = {};
    frame_push(vm, &frame, d, &tbody);
    for (OmpClause *c = d->clauses; c; c = c->next)
        if (c->kind == OMP_CLAUSE_PRIVATE || c->kind == OMP_CLAUSE_FIRSTPRIVATE)
            task_copy_decl(vm, d, &tbody, c->name, c->orig->ty,
                           c->kind == OMP_CLAUSE_FIRSTPRIVATE);
    body_push(vm, &tbody, stmt(vm, rest, tok));
    frame_pop(vm, &frame);
    Node *task_body = new_node(vm, ND_BLOCK, start);
    task_body->body = thead.next;
    outline_end(vm, &r, task_body);

    // In the spawning code, with the task's frame gone.
    char *flags = arena_strdup(vm, "0");
    if (d->if_text)
        flags = arena_format(vm, "%s | (!(%s) ? 1 : 0)", flags, d->if_text);
    if (d->final_text)
        flags = arena_format(vm, "%s | ((%s) ? 2 : 0)", flags, d->final_text);
    if (d->priority_text)
        flags = arena_format(vm, "%s | ((%s) > 0 ? 4 : 0)", flags,
                             d->priority_text);
    body_push(vm, body, snippet(vm, "int __omp%d_fl = %s;", id, flags));

    int ndeps = 0;
    for (OmpDepend *dep = d->depends; dep; dep = dep->next)
        ndeps++;
    if (ndeps) {
        char *init = arena_strdup(vm, "");
        for (OmpDepend *dep = d->depends; dep; dep = dep->next)
            init = arena_format(vm, "%s%s%s, (void *)%d", init,
                                *init ? ", " : "", dep->addr, dep->write);
        body_push(vm, body,
                  snippet(vm, "void *__omp%d_dep[] = {%s};", id, init));
    }

    if (d->task_size) {
        // Variable-length array data follows the fixed-size area, in order.
        char *total = arena_strdup(vm, "");
        for (OmpTaskCopy *c = d->copies; c; c = c->next)
            if (c->dims && c->copy)
                total = arena_format(vm, "%s + sizeof(%s)", total, c->name);
        body_push(vm, body,
                  snippet(vm, "char *__omp%d_d = __cccc_omp_task_alloc(%d%s);",
                          id, d->task_size, total));
        char *extra = arena_strdup(vm, "");
        for (OmpTaskCopy *c = d->copies; c; c = c->next) {
            if (!c->dims) {
                body_push(vm, body,
                          snippet(vm,
                                  "__builtin_memcpy(__omp%d_d + %d, &%s, "
                                  "sizeof(%s));",
                                  id, c->off, c->name, c->name));
                continue;
            }
            char *extents = arena_strdup(vm, "");
            for (int i = 0; i < c->dims; i++)
                extents = arena_format(
                    vm, "%ssizeof(%s%s) / sizeof(%s%s), ", extents, c->name,
                    zero_subscripts(vm, i), c->name, zero_subscripts(vm, i + 1));
            body_push(vm, body,
                      snippet(vm,
                              "unsigned long __omp%d_%s_vs[%d] = {%s%d%s};",
                              id, c->name, c->dims + 1, extents, d->task_size,
                              extra));
            body_push(vm, body,
                      snippet(vm,
                              "__builtin_memcpy(__omp%d_d + %d, "
                              "__omp%d_%s_vs, sizeof(__omp%d_%s_vs));",
                              id, c->off, id, c->name, id, c->name));
            if (!c->copy)
                continue;
            body_push(vm, body,
                      snippet(vm,
                              "__builtin_memcpy(__omp%d_d + __omp%d_%s_vs[%d], "
                              "%s, sizeof(%s));",
                              id, id, c->name, c->dims, c->name, c->name));
            extra = arena_format(vm, "%s + sizeof(%s)", extra, c->name);
        }
    }
    body_push(
        vm, body,
        snippet(vm, "__cccc_omp_task_stage(%s, __omp%d_fl, %s, %d);",
                d->task_size ? arena_format(vm, "__omp%d_d", id) : "(void *)0",
                id, ndeps ? arena_format(vm, "__omp%d_dep", id) : "(void **)0",
                ndeps));
    body_push(vm, body, snippet(vm, "%s();", r.fn->name));
}

static Node *threaded_directive(VirtualMachine *vm, OmpDirective *d,
                                Node *block, OmpBody *body, Token **rest,
                                Token *tok, Token *start) {
    int id    = vm->compiler.unique_name_counter++;
    d->omp_id = id;
    switch (d->kind) {
        case OMP_BARRIER:
            body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            *rest = tok;
            return block;

        case OMP_TASKWAIT:
            body_push(vm, body, snippet(vm, "__cccc_omp_taskwait();"));
            *rest = tok;
            return block;

        case OMP_TASKYIELD:
            *rest = tok;
            return block;

        case OMP_TASKGROUP:
            body_push(
                vm, body,
                snippet(vm, "void *__omp%d_tg = __cccc_omp_taskgroup_begin();",
                        id));
            body_push(vm, body, stmt(vm, rest, tok));
            body_push(vm, body,
                      snippet(vm, "__cccc_omp_taskgroup_end(__omp%d_tg);", id));
            return block;

        case OMP_TASK:
            threaded_task(vm, d, body, rest, tok, start, id);
            return block;

        case OMP_SECTIONS: {
            capture_pointers(vm, d, body, id);
            shadow_decls(vm, d, body, id);
            if (has_race_prone_clause(d))
                body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            int    n;
            Node **secs = sections_body(vm, rest, tok, &n);
            threaded_sections(vm, d, body, id, secs, n, start, true);
            merges(vm, d, body, id);
            if (!d->nowait)
                body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            return block;
        }

        case OMP_PARALLEL:
        case OMP_PARALLEL_SECTIONS:
        case OMP_PARALLEL_FOR: {
            body_push(vm, body,
                      outer_snippet(vm, d,
                                    arena_format(vm, "int __omp%d_nt = %s;", id,
                                                 d->num_threads_text
                                                     ? d->num_threads_text
                                                     : "0")));
            if (d->if_text)
                body_push(vm, body,
                          outer_snippet(vm, d,
                                        arena_format(vm,
                                                     "if (!(%s)) __omp%d_nt = "
                                                     "1;",
                                                     d->if_text, id)));
            if (d->kind == OMP_PARALLEL_FOR)
                chunk_decl(vm, d, body, id);
            Obj *nt =
                find_var(vm,
                         tokenize_string(vm, "<omp>",
                                         arena_format(vm, "__omp%d_nt", id)))
                    ->var;
            mark_snapshots(d);
            capture_pointers(vm, d, body, id);

            OutlineRegion r     = region_begin(vm, start, id, nt);
            Node      rhead = {};
            OmpBody   rbody = {&rhead, &rhead};
            shadow_decls(vm, d, &rbody, id);
            if (d->kind == OMP_PARALLEL_FOR) {
                tok = threaded_for(vm, d, &rbody, id, tok, false);
            } else if (d->kind == OMP_PARALLEL_SECTIONS) {
                int    n;
                Node **secs = sections_body(vm, &tok, tok, &n);
                threaded_sections(vm, d, &rbody, id, secs, n, start, false);
            } else {
                Node *inner = stmt(vm, &tok, tok);
                body_push(vm, &rbody, inner);
            }
            body_push(vm, &rbody, snippet(vm, "__cccc_omp_task_drain();"));
            merges(vm, d, &rbody, id);
            Node *region_body = new_node(vm, ND_BLOCK, start);
            region_body->body = rhead.next;
            outline_end(vm, &r, region_body);
            d->region_fn = r.fn;
            body_push(vm, body, snippet(vm, "%s();", r.fn->name));
            *rest = tok;
            return block;
        }

        case OMP_FOR: {
            chunk_decl(vm, d, body, id);
            capture_pointers(vm, d, body, id);
            shadow_decls(vm, d, body, id);
            if (has_race_prone_clause(d))
                body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            tok = threaded_for(vm, d, body, id, tok, true);
            merges(vm, d, body, id);
            if (!d->nowait)
                body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            *rest = tok;
            return block;
        }

        case OMP_SINGLE: {
            capture_pointers(vm, d, body, id);
            shadow_decls(vm, d, body, id);
            int ncp = 0;
            for (OmpClause *c = d->clauses; c; c = c->next)
                ncp += c->kind == OMP_CLAUSE_COPYPRIVATE;
            if (ncp)
                body_push(vm, body,
                          snippet(vm, "void *__omp%d_cp[%d];", id, ncp));
            Node *inner   = stmt(vm, rest, tok);
            Node *guarded = snippet(vm, "if (__cccc_omp_single_begin()) ;");
            Node *ifn     = guarded;
            while (ifn && ifn->kind != ND_IF)
                ifn = ifn->kind == ND_BLOCK ? ifn->body : NULL;
            if (!ifn)
                error_tok(vm, start, "internal error: omp single shape");
            Node    head = {};
            OmpBody ib   = {&head, &head};
            body_push(vm, &ib, inner);
            int k = 0;
            for (OmpClause *c = d->clauses; c; c = c->next)
                if (c->kind == OMP_CLAUSE_COPYPRIVATE)
                    body_push(
                        vm, &ib,
                        snippet(vm, "__omp%d_cp[%d] = &%s;", id, k++, c->name));
            if (ncp)
                body_push(
                    vm, &ib,
                    snippet(vm, "__cccc_omp_copyprivate_set(__omp%d_cp);", id));
            Node *then = new_node(vm, ND_BLOCK, start);
            then->body = head.next;
            node_ctl_mut(vm, ifn)->then = then;
            body_push(vm, body, guarded);
            if (!d->nowait)
                body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            if (ncp) {
                k = 0;
                for (OmpClause *c = d->clauses; c; c = c->next)
                    if (c->kind == OMP_CLAUSE_COPYPRIVATE)
                        body_push(vm, body,
                                  snippet(vm,
                                          "__builtin_memcpy(&%s, ((void **)"
                                          "__cccc_omp_copyprivate_get())[%d], "
                                          "sizeof(%s));",
                                          c->name, k++, c->name));
                body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            }
            merges(vm, d, body, id);
            return block;
        }

        case OMP_MASTER:
        case OMP_MASKED:
        case OMP_CRITICAL: {
            Node *inner = stmt(vm, rest, tok);
            if (d->kind == OMP_CRITICAL) {
                const char *name = d->critical_name ? d->critical_name : "";
                body_push(vm, body,
                          snippet(vm, "static void *__omp%d_cm;", id));
                body_push(vm, body,
                          snippet(vm,
                                  "void *__omp%d_m = "
                                  "__cccc_omp_critical_enter(&__omp%d_cm, "
                                  "\"%s\");",
                                  id, id, name));
                body_push(vm, body, inner);
                body_push(
                    vm, body,
                    snippet(vm, "__cccc_omp_critical_exit(__omp%d_m);", id));
            } else {
                Node *guarded =
                    snippet(vm, "if (__cccc_omp_thread_num() == 0) ;");
                Node *ifn = guarded;
                while (ifn && ifn->kind != ND_IF)
                    ifn = ifn->kind == ND_BLOCK ? ifn->body : NULL;
                if (!ifn)
                    error_tok(vm, start, "internal error: omp master shape");
                node_ctl_mut(vm, ifn)->then = inner;
                body_push(vm, body, guarded);
            }
            return block;
        }

        case OMP_ATOMIC:
            body_push(vm, body, threaded_atomic(vm, d, rest, tok));
            return block;

        case OMP_ORDERED:
            body_push(vm, body, snippet(vm, "__cccc_omp_ordered_begin();"));
            body_push(vm, body, stmt(vm, rest, tok));
            return block;

        case OMP_SIMD: {
            capture_pointers(vm, d, body, id);
            shadow_decls(vm, d, body, id);
            linear_prologue(vm, d, body, id);
            Node *inner = stmt(vm, rest, tok);
            if (has_clause(d, NULL, OMP_CLAUSE_LINEAR))
                linear_inject(vm, d, id, inner, start);
            body_push(vm, body, inner);
            merges(vm, d, body, id);
            return block;
        }

        default: {
            Node *inner = stmt(vm, rest, tok);
            body_push(vm, body, inner);
            return block;
        }
    }
}

Node *omp_directive(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    if (!(vm->flags & CCCC_OPENMP))
        error_tok(vm, tok,
                  "'__cccc_omp' is reserved for #pragma omp; enable it with "
                  "-fopenmp");
    OmpDirective *d = arena_alloc(&vm->compiler.parser_arena, sizeof(*d));
    memset(d, 0, sizeof(*d));

    Node *block = new_node(vm, ND_BLOCK, start);
    node_misc_mut(vm, block)->omp = d;

    enter_scope(vm);
    Node    head = {};
    OmpBody body = {&head, &head};

    tok          = skip(vm, tok->next, "(");
    if (is_word(tok, "threadprivate")) {
        *rest = threadprivate_list(vm, tok);
        leave_scope(vm);
        return block;
    }
    tok          = directive_name(vm, d, tok);
    tok          = clauses(vm, d, &body, tok);
    tok          = skip(vm, tok, ")");

    if (d->ordered_n && (d->kind == OMP_FOR || d->kind == OMP_PARALLEL_FOR))
        ordered_levels(vm, d, tok);

    OmpDirective *enclosing = vm->compiler.omp_enclosing;
    if (d->kind == OMP_ORDERED && (d->source || d->sinks)) {
        doacross_check(vm, d, enclosing, start);
        if (vm->compiler.omp_threaded)
            runtime_decls(vm);
        doacross_lower(vm, d, enclosing, &body);
        *rest = tok;
        leave_scope(vm);
        block->body = head.next;
        return block;
    }
    if (d->kind == OMP_ORDERED && enclosing && enclosing->ordered_n &&
        (enclosing->kind == OMP_FOR || enclosing->kind == OMP_PARALLEL_FOR))
        error_tok(vm, start,
                  "'ordered' region cannot be used in a loop with "
                  "'ordered(n)'; use 'sink' and 'source'");
    if (d->kind == OMP_ORDERED && enclosing &&
        !((enclosing->kind == OMP_FOR || enclosing->kind == OMP_PARALLEL_FOR) &&
          enclosing->ordered))
        error_tok(vm, start,
                  "'ordered' region must be closely nested inside a loop "
                  "region with an 'ordered' clause");
    vm->compiler.omp_enclosing = d;

    OmpFrame frame  = {};
    bool     framed = d->default_none || d->kind == OMP_TASK;
    // A threaded task pushes its own frame inside the function it outlines.
    if (d->kind == OMP_TASK && vm->compiler.omp_threaded)
        framed = false;
    if (framed)
        frame_push(vm, &frame, d, &body);
    Scope *saved_parallel = vm->compiler.omp_parallel;
    if (d->kind == OMP_PARALLEL || d->kind == OMP_PARALLEL_FOR ||
        d->kind == OMP_PARALLEL_SECTIONS)
        vm->compiler.omp_parallel = vm->compiler.scope;

    if (vm->compiler.omp_threaded) {
        runtime_decls(vm);
        threaded_directive(vm, d, block, &body, rest, tok, start);
        vm->compiler.omp_parallel = saved_parallel;
        vm->compiler.omp_enclosing = enclosing;
        if (framed)
            frame_pop(vm, &frame);
        leave_scope(vm);
        block->body = head.next;
        return block;
    }

    if (d->kind == OMP_BARRIER || d->kind == OMP_TASKWAIT ||
        d->kind == OMP_TASKYIELD) {
        vm->compiler.omp_enclosing = enclosing;
        leave_scope(vm);
        *rest = tok;
        return block;
    }

    int    id       = shadows(vm, d, &body);

    if (d->kind == OMP_SECTIONS || d->kind == OMP_PARALLEL_SECTIONS) {
        int    n;
        Node **secs = sections_body(vm, rest, tok, &n);
        for (int j = 0; j < n; j++)
            body_push(vm, &body, secs[j]);
        merges(vm, d, &body, id);
        vm->compiler.omp_parallel = saved_parallel;
        vm->compiler.omp_enclosing = enclosing;
        if (framed)
            frame_pop(vm, &frame);
        leave_scope(vm);
        block->body = head.next;
        return block;
    }

    Token *body_tok = tok;
    bool   is_loop  = d->kind == OMP_FOR || d->kind == OMP_PARALLEL_FOR;
    int    nloops   = d->ordered_n > d->collapse ? d->ordered_n : d->collapse;
    char  *saved_omp_brk         = vm->compiler.omp_loop_brk;
    vm->compiler.omp_assoc_loops = is_loop ? nloops : 0;
    Node *inner = d->kind == OMP_ATOMIC ? threaded_atomic(vm, d, rest, tok)
                                        : stmt(vm, rest, tok);
    vm->compiler.omp_assoc_loops = 0;
    vm->compiler.omp_loop_brk    = saved_omp_brk;
    if (d->kind == OMP_FOR || d->kind == OMP_PARALLEL_FOR) {
        if (!equal(body_tok, "for"))
            error_tok(vm, body_tok,
                      "the statement after '#pragma omp for' must be a for "
                      "loop");
        check_loop(vm, inner, nloops, body_tok);
        exempt_loop_vars(vm, inner, nloops);
        if (has_clause(d, NULL, OMP_CLAUSE_LINEAR))
            linear_inject(vm, d, id, inner, body_tok);
    } else if (d->kind == OMP_SIMD && has_clause(d, NULL, OMP_CLAUSE_LINEAR)) {
        check_loop(vm, inner, d->collapse, body_tok);
        linear_inject(vm, d, id, inner, body_tok);
    }
    body_push(vm, &body, inner);
    merges(vm, d, &body, id);

    vm->compiler.omp_parallel = saved_parallel;
    vm->compiler.omp_enclosing = enclosing;
    if (framed)
        frame_pop(vm, &frame);
    leave_scope(vm);
    block->body = head.next;
    return block;
}
