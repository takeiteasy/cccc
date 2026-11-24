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

// Clang's call-site attributes: enable_if and diagnose_if conditions over a
// function's parameters, evaluated with a call's arguments, and
// pass_object_size, whose hidden argument carries each annotated pointer
// argument's object size.

#include "./parse_internal.h"

FnCond *cc_fn_conds_concat(VirtualMachine *vm, FnCond *a, FnCond *b) {
    FnCond  head = {0};
    FnCond *cur  = &head;
    for (FnCond *lists[2] = {a, b}, **l = lists; l < lists + 2; l++)
        for (FnCond *c = *l; c; c = c->next) {
            cur = cur->next =
                arena_alloc(&vm->compiler.parser_arena, sizeof *c);
            *cur      = *c;
            cur->next = NULL;
        }
    return head.next;
}

int cc_fn_hidden_params(Type *fn) {
    int n = 0;
    for (Type *p = fn->params; p; p = p->next)
        n += type_attrs(p)->objsize_kind != OBJSIZE_NONE;
    return n;
}

int cc_fn_enable_if_count(Type *fn) {
    int n = 0;
    for (FnCond *c = type_attrs(fn)->fn_conds; c; c = c->next)
        n += c->kind == FN_COND_ENABLE_IF;
    return n;
}

static bool is_placeholder(Node *n) {
    return n->kind == ND_VAR && n->var &&
           obj_chk(n->var)->checked_self_param_idx;
}

bool cc_has_param_placeholder(Node *n) {
    if (!n)
        return false;
    return is_placeholder(n) || cc_has_param_placeholder(n->lhs) ||
           cc_has_param_placeholder(n->rhs) ||
           cc_has_param_placeholder(node_ctl(n)->cond) ||
           cc_has_param_placeholder(node_ctl(n)->then) ||
           cc_has_param_placeholder(node_ctl(n)->els);
}

// Parses each condition against placeholders for the parameters, at file
// scope, the way resolve_param_checked_bounds() does for bounds.
void cc_fn_conds_resolve(VirtualMachine *vm, Type *fn) {
    FnCond *conds = type_attrs(fn)->fn_conds;
    if (!conds || conds->tmpl || vm->compiler.in_type_lookahead)
        return;
    Scope *saved_scope = vm->compiler.scope;
    walk_to_file_scope(vm);
    enter_scope(vm);
    int idx = 0;
    for (Type *p = fn->params; p; p = p->next) {
        idx++;
        if (!p->param_name)
            continue;
        Obj *ph = arena_alloc(&vm->compiler.parser_arena, sizeof(Obj));
        memset(ph, 0, sizeof(Obj));
        ph->name = arena_strndup(vm, p->param_name->loc, p->param_name->len);
        ph->display_name                                            = ph->name;
        ph->ty                                                      = p;
        obj_chk_mut(vm, ph)->checked_self_param_idx                 = idx;
        push_scope(vm, p->param_name->loc, p->param_name->len)->var = ph;
    }
    bool saved_in_cond      = vm->compiler.in_fn_cond;
    vm->compiler.in_fn_cond = true;
    for (FnCond *c = conds; c; c = c->next) {
        Token *tok = c->expr;
        Node  *n   = assign(vm, &tok, tok);
        add_type(vm, n);
        if (tok != c->end)
            error_tok(vm, tok, "expected ',' after the condition");
        if (!is_error_type(n->ty) && !is_numeric(n->ty) &&
            n->ty->kind != TY_PTR)
            error_tok(vm, c->expr, "the condition must have scalar type");
        c->tmpl = n;
    }
    vm->compiler.in_fn_cond = saved_in_cond;
    leave_scope(vm);
    vm->compiler.scope = saved_scope;
}

static const char *objsize_attr_name(Type *p) {
    return type_attrs(p)->objsize_kind == OBJSIZE_DYNAMIC
               ? "pass_dynamic_object_size"
               : "pass_object_size";
}

// A declaration of function type `fn` named `name`: resolves its conditions
// and checks its pass_object_size parameters.
void cc_fn_declared(VirtualMachine *vm, Type *fn, Token *name) {
    if (vm->compiler.in_type_lookahead)
        return;
    cc_fn_conds_resolve(vm, fn);
    for (Type *p = fn->params; p; p = p->next) {
        if (!type_attrs(p)->objsize_kind)
            continue;
        Token *at = p->param_name ? p->param_name : name;
        if (p->kind != TY_PTR || !p->is_const)
            error_tok(vm, at,
                      "'%s' attribute only applies to constant pointer "
                      "parameters",
                      objsize_attr_name(p));
    }
}

// Whether two declarations agree on their pass_object_size parameters.
bool cc_fn_objsize_agree(Type *a, Type *b) {
    Type *p = a->params, *q = b->params;
    for (; p && q; p = p->next, q = q->next)
        if (type_attrs(p)->objsize_kind != type_attrs(q)->objsize_kind ||
            type_attrs(p)->objsize_type != type_attrs(q)->objsize_type)
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Evaluation
// ---------------------------------------------------------------------------

typedef struct {
    VirtualMachine *vm;
    Node          **args;
    int             nargs;
    bool            unknown; // an argument the condition reads is missing
} Subst;

// `n` with each parameter placeholder replaced by its argument, converted to
// the parameter's type, and each ND_PARAM_QUERY answered for the arguments.
static Node *subst(Subst *s, Node *n) {
    if (!n || s->unknown)
        return NULL;
    VirtualMachine *vm = s->vm;
    if (is_placeholder(n)) {
        int k = obj_chk(n->var)->checked_self_param_idx;
        if (k > s->nargs || !s->args[k - 1]) {
            s->unknown = true;
            return NULL;
        }
        Node *a = s->args[k - 1];
        add_type(vm, a);
        if (is_numeric(n->ty) || n->ty->kind == TY_PTR)
            a = new_cast(vm, a, n->ty);
        return a;
    }
    if (n->kind == ND_PARAM_QUERY) {
        Node *arg = subst(s, n->lhs);
        if (!arg)
            return NULL;
        if (n->val < 0) {
            Node *lit = arg;
            while (lit->kind == ND_CAST)
                lit = lit->lhs;
            bool c =
                is_const_expr(vm, arg) || (lit->kind == ND_VAR && lit->var &&
                                           lit->var->is_string_literal);
            return new_num(vm, c, n->tok);
        }
        return cc_objsize_query(vm, arg, n->val & 3, n->val & 4, n->tok);
    }
    Node *c = node_clone(vm, n);
    c->next = NULL;
    c->lhs  = subst(s, n->lhs);
    c->rhs  = subst(s, n->rhs);
    if (n->ctl) {
        node_ctl_mut(vm, c)->cond = subst(s, node_ctl(n)->cond);
        node_ctl_mut(vm, c)->then = subst(s, node_ctl(n)->then);
        node_ctl_mut(vm, c)->els  = subst(s, node_ctl(n)->els);
    }
    return s->unknown ? NULL : c;
}

// 1 when `c` holds for the call's arguments, 0 when it does not, -1 when it
// is not a constant for them. `args` holds the call's positional arguments,
// NULL where one is not known.
static int cond_eval(VirtualMachine *vm, FnCond *c, Node **args, int nargs) {
    Subst s = {vm, args, nargs, false};
    Node *n = subst(&s, c->tmpl);
    if (!n || !is_const_expr(vm, n))
        return -1;
    return eval(vm, n) != 0;
}

// The first enable_if of `fn` the arguments fail, or NULL when they pass all.
FnCond *cc_fn_failed_enable_if(VirtualMachine *vm, Type *fn, Node **args,
                               int nargs) {
    cc_fn_conds_resolve(vm, fn);
    for (FnCond *c = type_attrs(fn)->fn_conds; c; c = c->next)
        if (c->kind == FN_COND_ENABLE_IF &&
            cond_eval(vm, c, args, nargs) != 1)
            return c;
    return NULL;
}

static bool same_tokens(FnCond *a, FnCond *b) {
    Token *x = a->expr, *y = b->expr;
    for (; x != a->end && y != b->end; x = x->next, y = y->next)
        if (x->len != y->len || strncmp(x->loc, y->loc, (size_t)x->len))
            return false;
    return x == a->end && y == b->end;
}

// Whether `a` has more enable_ifs than `b` and starts with the ones `b` has,
// which makes `a` the better of two otherwise equal candidates.
bool cc_fn_enable_ifs_extend(Type *a, Type *b) {
    FnCond *x = type_attrs(a)->fn_conds, *y = type_attrs(b)->fn_conds;
    for (;; x = x->next, y = y->next) {
        while (x && x->kind != FN_COND_ENABLE_IF)
            x = x->next;
        while (y && y->kind != FN_COND_ENABLE_IF)
            y = y->next;
        if (!y)
            return x != NULL;
        if (!x || !same_tokens(x, y))
            return false;
    }
}

static Node **arg_array(Node *args, int *n) {
    *n = 0;
    for (Node *a = args; a; a = a->next)
        (*n)++;
    Node **array = calloc((size_t)*n + 1, sizeof *array);
    int    i     = 0;
    for (Node *a = args; a; a = a->next)
        array[i++] = a;
    return array;
}

static void diagnose(VirtualMachine *vm, Type *fn, Node **args, int nargs,
                     Token *site) {
    for (FnCond *c = type_attrs(fn)->fn_conds; c; c = c->next) {
        if (c->kind == FN_COND_ENABLE_IF ||
            cond_eval(vm, c, args, nargs) != 1)
            continue;
        if (c->kind == FN_COND_DIAG_ERROR)
            error_tok(vm, site, "%s", c->msg);
        warn_tok(vm, site, CCCC_WARN_ATTRIBUTES, "%s", c->msg);
    }
}

// A live call of `fn` with its converted arguments: a plain function's failed
// enable_if is an error, and each diagnose_if that holds is reported.
void cc_fn_check_call(VirtualMachine *vm, Type *fn, Node *args, Token *site,
                      const char *name) {
    if (!type_attrs(fn)->fn_conds || vm->compiler.dead_code_depth)
        return;
    int    nargs;
    Node **array = arg_array(args, &nargs);
    cc_fn_conds_resolve(vm, fn);
    FnCond *failed = fn->is_overloadable
                         ? NULL
                         : cc_fn_failed_enable_if(vm, fn, array, nargs);
    if (failed) {
        free(array);
        error_tok(vm, site,
                  "no matching function for call to '%s'\n    candidate "
                  "disabled: %s",
                  name, failed->msg);
    }
    diagnose(vm, fn, array, nargs, site);
    free(array);
}

// `fn` named other than as the callee of a call: taking the address of a
// function with a pass_object_size parameter, or with an enable_if that is
// not always true, is an error, and a diagnose_if that holds regardless of
// the arguments is reported.
void cc_fn_check_reference(VirtualMachine *vm, Obj *fn, Token *tok) {
    Type *ty = fn->ty;
    if (!ty || ty->kind != TY_FUNC || vm->compiler.in_type_lookahead)
        return;
    if (cc_fn_hidden_params(ty))
        error_tok(vm, tok,
                  "cannot take the address of '%s', which has a "
                  "pass_object_size parameter",
                  obj_display_name(fn));
    if (!type_attrs(ty)->fn_conds || vm->compiler.dead_code_depth)
        return;
    if (cc_fn_failed_enable_if(vm, ty, NULL, 0))
        error_tok(vm, tok,
                  "cannot take the address of '%s', which has an enable_if "
                  "that is not always true",
                  obj_display_name(fn));
    diagnose(vm, ty, NULL, 0, tok);
}

// ---------------------------------------------------------------------------
// pass_object_size
// ---------------------------------------------------------------------------

// Inserts the hidden size argument after each pass_object_size argument of a
// call of `fn`.
void cc_fn_objsize_args(VirtualMachine *vm, Type *fn, Node *args, Token *tok) {
    Node *a = args;
    for (Type *p = fn->params; p && a; p = p->next, a = a->next) {
        const TypeAttrs *pa = type_attrs(p);
        if (!pa->objsize_kind)
            continue;
        // A dynamic size reads the argument again, so it must be pure.
        bool dynamic =
            pa->objsize_kind == OBJSIZE_DYNAMIC && !node_has_side_effects(a);
        Node *size = cc_objsize_query(vm, clone_bounds_node(vm, a),
                                      pa->objsize_type, dynamic, tok);
        size       = new_cast(vm, size, ty_ulong);
        size->next = a->next;
        a->next    = size;
        a          = size;
    }
}

// The hidden size parameter of pass_object_size parameter `param` of the
// function being defined, or NULL. Locals are prepended, so it is made before
// the parameter's own local to follow it.
Obj *cc_fn_objsize_param(VirtualMachine *vm, Type *param) {
    if (!type_attrs(param)->objsize_kind)
        return NULL;
    char *name = arena_format(vm, "__cccc_objsize_%.*s", param->param_name->len,
                              param->param_name->loc);
    Obj  *size = new_lvar(vm, name, (int)strlen(name), ty_ulong);
    size->is_param = true;
    return size;
}

// The parameter whose hidden size answers __builtin_object_size(`ptr`, type)
// in the function being defined, or NULL.
Obj *cc_fn_objsize_hidden(Node *ptr, int64_t type) {
    while (ptr->kind == ND_CAST)
        ptr = ptr->lhs;
    if (ptr->kind != ND_VAR || !ptr->var || !ptr->var->is_param)
        return NULL;
    Obj *size = obj_size(ptr->var)->objsize_hidden;
    return size && type_attrs(ptr->var->ty)->objsize_type == type ? size : NULL;
}
