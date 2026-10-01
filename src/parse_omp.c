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
// the runtime in src/shims/omp.c instead ("Threaded lowering" below).

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

static void body_push(VirtualMachine *vm, OmpBody *body, Node *node) {
    add_type(vm, node);
    body->cur = body->cur->next = node;
}

static Obj *clause_var(VirtualMachine *vm, Token *tok, const char *clause) {
    VarScope *sc = tok->kind == TK_IDENT ? find_var(vm, tok) : NULL;
    if (!sc || !sc->var)
        error_tok(vm, tok, "'%.*s' in '%s' clause is not a variable", tok->len,
                  tok->loc, clause);
    return sc->var;
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
        add_clause(vm, d, kind, var, tok, op);
        tok = tok->next;
    } while (consume(vm, &tok, tok, ","));
    return skip(vm, tok, ")");
}

typedef enum {
    CL_DATA     = 1 << 0, // private/firstprivate/shared/reduction/default
    CL_PARALLEL = 1 << 1, // num_threads/if/proc_bind
    CL_LOOP     = 1 << 2, // schedule/collapse/ordered
    CL_NOWAIT   = 1 << 3,
    CL_SIMD     = 1 << 4, // simdlen/safelen
    CL_FIRSTPRIVATE_ONLY = 1 << 5, // single: private/firstprivate only
    CL_LASTPRIVATE       = 1 << 6,
    CL_COPYPRIVATE       = 1 << 7,
} ClauseSet;

static int clauses_allowed(OmpKind kind) {
    switch (kind) {
        case OMP_PARALLEL:
            return CL_DATA | CL_PARALLEL;
        case OMP_FOR:
            return CL_DATA | CL_LOOP | CL_NOWAIT | CL_LASTPRIVATE;
        case OMP_PARALLEL_FOR:
            return CL_DATA | CL_PARALLEL | CL_LOOP | CL_LASTPRIVATE;
        case OMP_SINGLE:
            return CL_FIRSTPRIVATE_ONLY | CL_NOWAIT | CL_COPYPRIVATE;
        case OMP_SIMD:
            return CL_SIMD | CL_LOOP;
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
        while (is_word(tok, "read") || is_word(tok, "write") ||
               is_word(tok, "update") || is_word(tok, "capture") ||
               is_word(tok, "seq_cst")) {
            if (is_word(tok, "read"))
                d->atomic_kind = 1;
            else if (is_word(tok, "write"))
                d->atomic_kind = 2;
            else if (is_word(tok, "capture"))
                d->atomic_kind = 3;
            tok = tok->next;
        }
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
    } else if (is_word(tok, "ordered")) {
        if (vm->compiler.omp_threaded)
            error_tok(vm, tok, "'ordered' is not supported with -c=native");
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
            bool ok =
                (allowed & CL_DATA) ||
                ((allowed & CL_FIRSTPRIVATE_ONLY) &&
                 (is_word(name, "private") || is_word(name, "firstprivate")));
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
        } else if (is_word(name, "default") && (allowed & CL_DATA)) {
            tok = skip(vm, tok, "(");
            // TODO: default(none) is recorded but not enforced.
            if (is_word(tok, "none"))
                d->default_none = true;
            else if (!is_word(tok, "shared"))
                error_tok(vm, tok, "expected 'shared' or 'none'");
            tok = skip(vm, tok->next, ")");
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
            tok = skip_balanced_parens(vm, tok);
        } else if (is_word(name, "collapse") && (allowed & CL_LOOP)) {
            tok         = skip(vm, tok, "(");
            Node *n     = conditional(vm, &tok, tok);
            tok         = skip(vm, tok, ")");
            d->collapse = (int)eval(vm, n);
            if (d->collapse < 1)
                error_tok(vm, name, "collapse argument must be positive");
        } else if (is_word(name, "ordered") && (allowed & CL_LOOP)) {
            // TODO: native ordered needs an iteration ticket lock (#1401).
            if (vm->compiler.omp_threaded)
                error_tok(vm, name,
                          "'ordered' is not supported with -c=native");
            if (equal(tok, "("))
                tok = skip_balanced_parens(vm, tok);
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
        } else if (is_word(name, "copyin") || is_word(name, "linear")) {
            error_tok(vm, name, "unsupported OpenMP clause '%.*s'", name->len,
                      name->loc);
        } else {
            error_tok(vm, name, "'%.*s' clause is not allowed here", name->len,
                      name->loc);
        }
        consume(vm, &tok, tok, ",");
    }
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
    }
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
                    body_push(vm, body,
                              snippet(vm,
                                      "__builtin_memcpy(&%s, __omp_%d_%s, "
                                      "sizeof(%s));",
                                      c->name, id, c->name, c->name));
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
                          snippet(vm,
                                  "__builtin_memcpy(&%s, __omp_%d_%s, "
                                  "sizeof(%s));",
                                  c->name, id, c->name, c->name));
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

static int shadows(VirtualMachine *vm, OmpDirective *d, OmpBody *body) {
    int id = vm->compiler.unique_name_counter++;
    capture_pointers(vm, d, body, id);
    shadow_decls(vm, d, body, id);
    return id;
}

static void merges(VirtualMachine *vm, OmpDirective *d, OmpBody *body, int id) {
    bool locked = false;
    for (OmpClause *c = d->clauses; c; c = c->next) {
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

static Node *single_stmt(Node *block) {
    if (block->kind == ND_BLOCK && block->body && !block->body->next)
        return block->body;
    return block;
}

static void check_loop(VirtualMachine *vm, Node *loop, int depth, Token *tok) {
    for (int level = 0; level < depth; level++) {
        if (!loop || loop->kind != ND_FOR || !loop->init || !loop->cond ||
            !loop->inc)
            error_tok(vm, tok,
                      "the statement after '#pragma omp for' must be a for "
                      "loop in canonical form");
        if (level + 1 < depth)
            loop = single_stmt(loop->then);
    }
}

static void check_atomic(VirtualMachine *vm, Node *stmt, Token *tok) {
    if (stmt->kind != ND_EXPR_STMT || !node_has_side_effects(stmt->lhs))
        error_tok(vm, tok,
                  "the statement after '#pragma omp atomic' must update a "
                  "variable");
}

// ---- Threaded lowering (-c=native, -m, -c=generated) ----
//
// A parallel region becomes a nested function (an Obj with is_omp_region); the
// serializer turns the call to it into a __cccc_pool_run() fork and captures
// the enclosing locals by reference through the nested-function environment,
// which is OpenMP's `shared`. Worksharing, single, critical and atomic are
// lowered in place onto the runtime in src/shims/omp.c.

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
    Token  *tok  = tokenize_string(vm, "<runtime>", (char *)proto);
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
         "void __cccc_omp_critical_enter(const char *);"},
        {"__cccc_omp_critical_exit",
         "void __cccc_omp_critical_exit(const char *);"},
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
    r.saved_locals  = vm->compiler.locals;
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
    fn->tok                        = tok;
    fn->parent_fn                  = r.parent;
    fn->nesting_depth              = r.saved_depth + 1;
    fn->block_outer_locals         = r.saved_locals;
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
    mark_addr_escapes(fn->body);
    propagate_checked_bounds(vm, fn);
    verify_checked_assign_bounds(vm, fn);
    mark_nested_captures(fn, fn->body);

    vm->compiler.current_fn         = r->parent;
    vm->compiler.locals             = r->saved_locals;
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

typedef struct {
    char *name;
    Type *ty;
    bool  declared; // `for (int i = ...)` rather than an existing variable
    bool  up;
    char *lb, *ub, *step, *rel;
} OmpLoopLevel;

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
    if (!is_integer(lv->ty))
        error_tok(vm, name,
                  "the loop variable of '#pragma omp for' must be an integer "
                  "with -c=native");
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

    for (int j = 0; j < m; j++) {
        body_push(vm, body,
                  snippet(vm,
                          "long __omp%d_lb%d = (long)(%s), __omp%d_ub%d = "
                          "(long)(%s), __omp%d_st%d = (long)(%s);",
                          id, j, lv[j].lb, id, j, lv[j].ub, id, j, lv[j].step));
        body_push(vm, body,
                  snippet(vm, "long __omp%d_n%d = %s;", id, j,
                          iter_count(vm, id, j, &lv[j])));
    }
    char *total = arena_format(vm, "__omp%d_n0", id);
    for (int j = 1; j < m; j++)
        total = arena_format(vm, "%s * __omp%d_n%d", total, id, j);
    body_push(vm, body, snippet(vm, "long __omp%d_n = %s;", id, total));
    body_push(vm, body, snippet(vm, "long __omp%d_b, __omp%d_e;", id, id));
    body_push(vm, body,
              snippet(vm,
                      "__cccc_omp_static_range(__omp%d_n, &__omp%d_b, "
                      "&__omp%d_e);",
                      id, id, id));

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

    Node *inner = stmt(vm, &tok, tok);
    (void)for_tok;

    body_push(vm, body, snippet(vm, "long __omp%d_k;", id));
    Node *loop = snippet(vm,
                         "for (__omp%d_k = __omp%d_b; __omp%d_k < __omp%d_e; "
                         "__omp%d_k++) ;",
                         id, id, id, id, id);
    Node *for_node = loop;
    while (for_node && for_node->kind != ND_FOR)
        for_node = for_node->kind == ND_BLOCK ? for_node->body : NULL;
    if (!for_node)
        error_tok(vm, for_tok, "internal error: omp loop shape");

    Node    head = {};
    OmpBody it   = {&head, &head};
    body_push(vm, &it, snippet(vm, "long __omp%d_i = __omp%d_k;", id, id));
    for (int j = m - 1; j >= 0; j--) {
        body_push(vm, &it,
                  snippet(vm,
                          "%s = (typeof_unqual(%s))(__omp%d_lb%d %c "
                          "(__omp%d_i %% __omp%d_n%d) * __omp%d_st%d);",
                          lv[j].name, lv[j].name, id, j, lv[j].up ? '+' : '-',
                          id, id, j, id, j));
        if (j)
            body_push(vm, &it,
                      snippet(vm, "__omp%d_i /= __omp%d_n%d;", id, id, j));
    }
    body_push(vm, &it, inner);
    Node *iter_block = new_node(vm, ND_BLOCK, for_tok);
    iter_block->body = head.next;
    for_node->then   = iter_block;
    body_push(vm, body, loop);

    // lastprivate: the thread that ran the sequentially last iteration copies
    // its values back; a loop variable ends one step past the last iteration.
    bool any_last = false;
    for (OmpClause *c = d->clauses; c; c = c->next)
        any_last |= c->kind == OMP_CLAUSE_LASTPRIVATE;
    if (any_last) {
        char *copy = arena_strdup(vm, "");
        for (OmpClause *c = d->clauses; c; c = c->next) {
            if (c->kind != OMP_CLAUSE_LASTPRIVATE)
                continue;
            for (int j = 0; j < m; j++)
                if (!strcmp(lv[j].name, c->name))
                    copy = arena_format(
                        vm,
                        "%s %s = (typeof_unqual(%s))(__omp%d_lb%d %c "
                        "__omp%d_n%d * __omp%d_st%d);",
                        copy, c->name, c->name, id, j, lv[j].up ? '+' : '-', id,
                        j, id, j);
            copy = arena_format(
                vm, "%s __builtin_memcpy(__omp_%d_%s, &%s, sizeof(%s));", copy,
                id, c->name, c->name, c->name);
        }
        body_push(vm, body,
                  snippet(vm,
                          "if (__omp%d_b < __omp%d_e && __omp%d_e == "
                          "__omp%d_n) { %s }",
                          id, id, id, id, copy));
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
} OmpAtomic;

static bool is_update_op(Token *t) {
    static const char *const ops[] = {
        "+=", "-=", "*=", "/=", "&=", "|=", "^=", "<<=", ">>="};
    for (size_t i = 0; i < sizeof(ops) / sizeof(*ops); i++)
        if (equal(t, (char *)ops[i]))
            return true;
    return false;
}

static void atomic_update_form(VirtualMachine *vm, Token *s, Token *end,
                               OmpAtomic *a) {
    if (equal(s, "++") || equal(s, "--")) {
        a->pre = true;
        a->op  = equal(s, "++") ? "+" : "-";
        a->x   = tokens_text(vm, s->next, end);
        return;
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
        error_tok(vm, s, "unsupported '#pragma omp atomic' statement");
    a->x = tokens_text(vm, s, p);
    if (equal(p, "++") || equal(p, "--")) {
        a->post = true;
        a->op   = equal(p, "++") ? "+" : "-";
        return;
    }
    if (is_update_op(p)) {
        a->op                    = tok_text(vm, p);
        a->op[strlen(a->op) - 1] = '\0';
        a->e                     = tokens_text(vm, p->next, end);
        return;
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
        a->op = tok_text(vm, t2);
        a->e  = tokens_text(vm, t2->next, end);
        return;
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
        error_tok(vm, s, "unsupported '#pragma omp atomic' statement");
    Token *opt = rhs;
    for (int i = 0; i < n - nx - 1; i++)
        opt = opt->next;
    a->reversed = true;
    a->op       = tok_text(vm, opt);
    a->e        = tokens_text(vm, rhs, opt);
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

static Node *threaded_atomic(VirtualMachine *vm, OmpDirective *d, Token **rest,
                             Token *tok) {
    Token *end = tok;
    while (!equal(end, ";")) {
        if (end->kind == TK_EOF || equal(end, "{"))
            error_tok(vm, tok,
                      "unsupported '#pragma omp atomic' statement (capture "
                      "blocks are not supported with -c=native)");
        end = end->next;
    }
    *rest       = end->next;

    OmpAtomic a = {};
    char     *stmt_text;
    if (d->atomic_kind == 1 || d->atomic_kind == 2 || d->atomic_kind == 3) {
        Token *eq = tok;
        while (eq != end && !equal(eq, "="))
            eq = eq->next;
        if (eq == end)
            error_tok(vm, tok, "unsupported '#pragma omp atomic' statement");
        if (d->atomic_kind == 1) {
            a.v = tokens_text(vm, tok, eq);
            a.x = tokens_text(vm, eq->next, end);
        } else if (d->atomic_kind == 2) {
            a.x = tokens_text(vm, tok, eq);
            a.e = tokens_text(vm, eq->next, end);
        } else {
            a.v = tokens_text(vm, tok, eq);
            atomic_update_form(vm, eq->next, end, &a);
        }
    } else {
        atomic_update_form(vm, tok, end, &a);
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
        stmt_text = arena_format(
            vm,
            "{ %s __b = __atomic_load_n(%s, 5); %s __t; __builtin_memcpy(&__t, "
            "&__b, sizeof(__t)); %s = __t; }",
            c, load, T, a.v);
    } else if (d->atomic_kind == 2) {
        stmt_text = arena_format(
            vm,
            "{ %s __t = (%s); %s __b; __builtin_memcpy(&__b, &__t, "
            "sizeof(__t)); __atomic_store_n(%s, __b, 5); }",
            T, a.e, c, load);
    } else if (fetch && d->atomic_kind != 3) {
        stmt_text = arena_format(vm, "__atomic_fetch_%s(&(%s), (%s), 5);",
                                 fetch, x, evalue);
    } else if (fetch && a.post) {
        stmt_text = arena_format(vm, "%s = __atomic_fetch_%s(&(%s), (%s), 5);",
                                 a.v, fetch, x, evalue);
    } else if (fetch) {
        stmt_text = arena_format(vm, "%s = __atomic_%s_fetch(&(%s), (%s), 5);",
                                 a.v, fetch, x, evalue);
    } else {
        const char *newv = a.reversed ? arena_format(vm, "__e %s __o", a.op)
                                      : arena_format(vm, "__o %s __e", a.op);
        stmt_text        = arena_format(
            vm,
            "{ typeof_unqual(%s) __e = (%s); %s __ob = __atomic_load_n(%s, 5), "
            "__nb; %s __o, __n; for (;;) { __builtin_memcpy(&__o, &__ob, "
            "sizeof(__o)); __n = %s; __builtin_memcpy(&__nb, &__n, "
            "sizeof(__n)); if (__atomic_compare_exchange_n(%s, &__ob, __nb, "
            "0, 5, 5)) break; } %s }",
            evalue, evalue, c, load, T, newv, load,
            d->atomic_kind == 3
                ? arena_format(vm, "%s = %s;", a.v, a.post ? "__o" : "__n")
                : "");
    }
    return snippet(vm, "%s", stmt_text);
}

static Node *threaded_directive(VirtualMachine *vm, OmpDirective *d,
                                Node *block, OmpBody *body, Token **rest,
                                Token *tok, Token *start) {
    int id = vm->compiler.unique_name_counter++;
    switch (d->kind) {
        case OMP_BARRIER:
            body_push(vm, body, snippet(vm, "__cccc_omp_barrier();"));
            *rest = tok;
            return block;

        case OMP_PARALLEL:
        case OMP_PARALLEL_FOR: {
            body_push(vm, body,
                      snippet(vm, "int __omp%d_nt = %s;", id,
                              d->num_threads_text ? d->num_threads_text : "0"));
            if (d->if_text)
                body_push(
                    vm, body,
                    snippet(vm, "if (!(%s)) __omp%d_nt = 1;", d->if_text, id));
            Obj *nt =
                find_var(vm,
                         tokenize_string(vm, "<omp>",
                                         arena_format(vm, "__omp%d_nt", id)))
                    ->var;
            capture_pointers(vm, d, body, id);

            OutlineRegion r     = region_begin(vm, start, id, nt);
            Node      rhead = {};
            OmpBody   rbody = {&rhead, &rhead};
            shadow_decls(vm, d, &rbody, id);
            if (d->kind == OMP_PARALLEL_FOR) {
                tok = threaded_for(vm, d, &rbody, id, tok, false);
            } else {
                Node *inner = stmt(vm, &tok, tok);
                body_push(vm, &rbody, inner);
            }
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
            capture_pointers(vm, d, body, id);
            shadow_decls(vm, d, body, id);
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
            ifn->then  = then;
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
                body_push(
                    vm, body,
                    snippet(vm, "__cccc_omp_critical_enter(\"%s\");", name));
                body_push(vm, body, inner);
                body_push(
                    vm, body,
                    snippet(vm, "__cccc_omp_critical_exit(\"%s\");", name));
            } else {
                Node *guarded =
                    snippet(vm, "if (__cccc_omp_thread_num() == 0) ;");
                Node *ifn = guarded;
                while (ifn && ifn->kind != ND_IF)
                    ifn = ifn->kind == ND_BLOCK ? ifn->body : NULL;
                if (!ifn)
                    error_tok(vm, start, "internal error: omp master shape");
                ifn->then = inner;
                body_push(vm, body, guarded);
            }
            return block;
        }

        case OMP_ATOMIC:
            body_push(vm, body, threaded_atomic(vm, d, rest, tok));
            return block;

        default: { // simd: a hint
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
    block->omp  = d;

    enter_scope(vm);
    Node    head = {};
    OmpBody body = {&head, &head};

    tok          = skip(vm, tok->next, "(");
    tok          = directive_name(vm, d, tok);
    tok          = clauses(vm, d, &body, tok);
    tok          = skip(vm, tok, ")");

    if (vm->compiler.omp_threaded) {
        runtime_decls(vm);
        threaded_directive(vm, d, block, &body, rest, tok, start);
        leave_scope(vm);
        block->body = head.next;
        return block;
    }

    if (d->kind == OMP_BARRIER) {
        leave_scope(vm);
        *rest = tok;
        return block;
    }

    int    id       = shadows(vm, d, &body);

    Token *body_tok = tok;
    Node  *inner    = stmt(vm, rest, tok);
    if (d->kind == OMP_FOR || d->kind == OMP_PARALLEL_FOR) {
        if (!equal(body_tok, "for"))
            error_tok(vm, body_tok,
                      "the statement after '#pragma omp for' must be a for "
                      "loop");
        check_loop(vm, inner, d->collapse, body_tok);
    } else if (d->kind == OMP_ATOMIC) {
        check_atomic(vm, inner, body_tok);
    }
    body_push(vm, &body, inner);
    merges(vm, d, &body, id);

    leave_scope(vm);
    block->body = head.next;
    return block;
}
