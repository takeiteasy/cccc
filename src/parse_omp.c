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
// parser lowers it to the one-thread form OpenMP allows: private, firstprivate
// and reduction variables become shadow locals declared by re-tokenised
// snippets, and the directive rides on the resulting ND_BLOCK as Node.omp.

#include "./parse_internal.h"
#include <stdarg.h>

// TODO: -c=native has no OpenMP lowering yet; outlining each region into a
// thread-pool function replaces this rejection.
void reject_omp_in_native(VirtualMachine *vm, Token *tok) {
    if (vm->compiler.native_mode)
        error_tok(vm, tok, "OpenMP is not supported with -c=native");
}

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
    char buf[512];
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
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
} ClauseSet;

static int clauses_allowed(OmpKind kind) {
    switch (kind) {
        case OMP_PARALLEL:
            return CL_DATA | CL_PARALLEL;
        case OMP_FOR:
            return CL_DATA | CL_LOOP | CL_NOWAIT;
        case OMP_PARALLEL_FOR:
            return CL_DATA | CL_PARALLEL | CL_LOOP;
        case OMP_SINGLE:
            return CL_FIRSTPRIVATE_ONLY | CL_NOWAIT;
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
               is_word(tok, "seq_cst"))
            tok = tok->next;
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
        d->kind = OMP_ORDERED;
        tok     = tok->next;
    } else {
        error_tok(vm, name, "unsupported OpenMP directive '%.*s'", name->len,
                  name->loc);
    }
    return tok;
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
            tok     = skip(vm, tok, "(");
            Node *e = expr(vm, &tok, tok);
            tok     = skip(vm, tok, ")");
            body_push(vm, body, new_unary(vm, ND_EXPR_STMT, e, name));
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
            if (equal(tok, "("))
                tok = skip_balanced_parens(vm, tok);
        } else if (is_word(name, "nowait") && (allowed & CL_NOWAIT)) {
            d->nowait = true;
        } else if ((is_word(name, "simdlen") || is_word(name, "safelen")) &&
                   (allowed & CL_SIMD)) {
            tok = skip_balanced_parens(vm, tok);
        } else if (is_word(name, "lastprivate") || is_word(name, "copyin") ||
                   is_word(name, "copyprivate") || is_word(name, "linear")) {
            error_tok(vm, name, "unsupported OpenMP clause '%.*s'", name->len,
                      name->loc);
        } else {
            error_tok(vm, name, "'%.*s' clause is not allowed here", name->len,
                      name->loc);
        }
        consume(vm, &tok, tok, ",");
    }
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
static int shadows(VirtualMachine *vm, OmpDirective *d, OmpBody *body) {
    int id = vm->compiler.unique_name_counter++;
    for (OmpClause *c = d->clauses; c; c = c->next) {
        if (c->kind == OMP_CLAUSE_SHARED || c->kind == OMP_CLAUSE_PRIVATE)
            continue;
        body_push(vm, body,
                  snippet(vm, "typeof_unqual(%s) *__omp_%d_%s = &%s;", c->name,
                          id, c->name, c->name));
    }
    for (OmpClause *c = d->clauses; c; c = c->next) {
        switch (c->kind) {
            case OMP_CLAUSE_SHARED:
                break;
            case OMP_CLAUSE_PRIVATE:
                body_push(
                    vm, body,
                    snippet(vm, "typeof_unqual(%s) %s;", c->name, c->name));
                break;
            case OMP_CLAUSE_FIRSTPRIVATE:
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
                body_push(vm, body,
                          snippet(vm, "typeof_unqual(%s) %s = %s;", c->name,
                                  c->name, init));
                break;
            }
        }
    }
    return id;
}

static void merges(VirtualMachine *vm, OmpDirective *d, OmpBody *body, int id) {
    for (OmpClause *c = d->clauses; c; c = c->next) {
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

Node *omp_directive(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    if (!(vm->flags & CCCC_OPENMP))
        error_tok(vm, tok,
                  "'__cccc_omp' is reserved for #pragma omp; enable it with "
                  "-fopenmp");
    reject_omp_in_native(vm, tok);

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
