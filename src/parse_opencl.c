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

// OpenCL C vector syntax: swizzles (`v.xyzw`, `v.s01`, `v.lo`) and vector
// literals (`(float4)(a, b, c, d)`). Both lower to per-lane reads and writes
// through vector_lane_ref(), the way __builtin_shuffle does, so no opcode
// depends on the vector's width.

#include "./parse_internal.h"

enum { MAX_LANES = 16 };

static int hex_digit(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

static bool selector_is(Token *sel, const char *name) {
    return sel->len == (int)strlen(name) && !memcmp(sel->loc, name, sel->len);
}

// Lanes a selector names, written to `lanes`. Returns 0 if `sel` is not a
// selector spelling at all.
static int parse_selector(Token *sel, int total, int *lanes) {
    int n = 0;
    if (selector_is(sel, "lo") || selector_is(sel, "hi")) {
        int half = total / 2;
        int base = selector_is(sel, "hi") ? half : 0;
        for (; n < half; n++)
            lanes[n] = base + n;
        return n;
    }
    if (selector_is(sel, "even") || selector_is(sel, "odd")) {
        for (int i = selector_is(sel, "odd") ? 1 : 0; i < total; i += 2)
            lanes[n++] = i;
        return n;
    }
    if ((sel->loc[0] == 's' || sel->loc[0] == 'S') && sel->len > 1) {
        for (int i = 1; i < sel->len && n < MAX_LANES; i++) {
            int d = hex_digit(sel->loc[i]);
            if (d < 0)
                return 0;
            lanes[n++] = d;
        }
        return sel->len - 1 == n ? n : 0;
    }
    if (sel->len > MAX_LANES)
        return 0;
    for (int i = 0; i < sel->len; i++) {
        const char *pos = strchr("xyzw", sel->loc[i]);
        if (!pos || !*pos)
            return 0;
        lanes[n++] = (int)(pos - "xyzw");
    }
    return n;
}

// The type of a private temporary holding a copy of a `ty` value
static Type *value_type(VirtualMachine *vm, Type *ty) {
    ty              = copy_type(vm, ty);
    ty->is_const    = false;
    ty->is_volatile = false;
    ty->addr_space  = AS_NONE;
    return ty;
}

static bool is_lane_lvalue(Node *node) {
    return node->kind == ND_VAR || node->kind == ND_DEREF ||
           node->kind == ND_MEMBER;
}

static Node *chain_after(VirtualMachine *vm, Node *chain, Node *next,
                         Token *tok) {
    return chain ? new_binary(vm, ND_COMMA, chain, next, tok) : next;
}

// The vector type of `n` lanes of `elem`.
// TODO: vectors under 16 bytes do not exist, so a 2-lane result of 4-byte
// elements is rejected; #1407 A 3-lane vector is stored as four.
static Type *swizzle_result_type(VirtualMachine *vm, Type *elem, int n,
                                 Token *sel) {
    int storage = n == 3 ? 4 : n;
    int bytes   = storage * elem->size;
    if ((n != 2 && n != 3 && n != 4 && n != 8 && n != 16) ||
        (bytes != 16 && bytes != 32 && bytes != 64))
        error_tok(vm, sel,
                  "a swizzle of %d lanes would be a %d-byte vector; vectors "
                  "must be 16, 32 or 64 bytes",
                  n, bytes);
    Type *ty = vector_of(vm, elem, bytes);
    if (n == 3)
        ty->vec_visible = 3;
    return ty;
}

Node *opencl_swizzle(VirtualMachine *vm, Node *vec, Token *sel) {
    Type *vty = vec->ty;
    int   lanes[MAX_LANES];
    int   n = parse_selector(sel, vty->vec_len, lanes);
    if (!n)
        error_tok(vm, sel, "'%.*s' is not a vector component", sel->len,
                  sel->loc);
    int visible = vector_lanes(vty);
    for (int i = 0; i < n; i++)
        if (lanes[i] >= vty->vec_len ||
            (lanes[i] >= visible && !selector_is(sel, "hi")))
            error_tok(vm, sel, "'%.*s' names a lane the vector does not have",
                      sel->len, sel->loc);

    Type *elem = vty->base;
    Obj  *tmp  = NULL;
    Node *init = NULL;
    if (n > 1 || !is_lane_lvalue(vec)) {
        tmp  = new_lvar(vm, "", 0, value_type(vm, vty));
        init = new_binary(vm, ND_ASSIGN, new_var_node(vm, tmp, sel), vec, sel);
    }
    Node *src = tmp ? new_var_node(vm, tmp, sel) : vec;

    if (n == 1) {
        Node *lane =
            vector_lane_ref(vm, src, elem, new_num(vm, lanes[0], sel), sel);
        return init ? new_binary(vm, ND_COMMA, init, lane, sel) : lane;
    }

    Type *rty    = swizzle_result_type(vm, elem, n, sel);
    Obj  *result = new_lvar(vm, "", 0, rty);
    Node *chain  = init;
    for (int i = 0; i < rty->vec_len; i++) {
        Node *to   = vector_lane_ref(vm, new_var_node(vm, result, sel), elem,
                                     new_num(vm, i, sel), sel);
        Node *from = i < n
                         ? vector_lane_ref(vm, new_var_node(vm, tmp, sel), elem,
                                           new_num(vm, lanes[i], sel), sel)
                         : new_num(vm, 0, sel);
        chain = chain_after(vm, chain, new_binary(vm, ND_ASSIGN, to, from, sel),
                            sel);
    }
    Node *swizzle =
        new_binary(vm, ND_COMMA, chain, new_var_node(vm, result, sel), sel);

    if (is_lane_lvalue(vec)) {
        SwizzleStore *store =
            arena_alloc(&vm->compiler.parser_arena, sizeof(SwizzleStore));
        store->read_only = vty->is_const || vty->addr_space == AS_CONSTANT;
        for (int i = 0; i < n && !store->read_only; i++) {
            for (int j = 0; j < i; j++)
                store->repeats |= lanes[i] == lanes[j];
            Node *to =
                vector_lane_ref(vm, vec, elem, new_num(vm, lanes[i], sel), sel);
            Node *from = vector_lane_ref(vm, new_var_node(vm, result, sel),
                                         elem, new_num(vm, i, sel), sel);
            store->stores =
                chain_after(vm, store->stores,
                            new_binary(vm, ND_ASSIGN, to, from, sel), sel);
        }
        swizzle->swizzle_store = store;
    }
    return swizzle;
}

// `v.lo = rhs`: the swizzle's temporary takes the value, then each named lane
// of the vector is stored from it.
Node *opencl_swizzle_assign(VirtualMachine *vm, Node *lhs, Node *rhs,
                            Token *tok) {
    SwizzleStore *store = lhs->swizzle_store;
    if (store->read_only)
        error_tok(vm, tok, "cannot assign to a swizzle of a const vector");
    if (store->repeats)
        error_tok(vm, tok,
                  "a swizzle that names a lane twice cannot be assigned to");
    Obj  *value = lhs->rhs->var;
    Node *set =
        new_binary(vm, ND_ASSIGN, new_var_node(vm, value, tok), rhs, tok);
    Node *done = new_binary(vm, ND_COMMA, store->stores,
                            new_var_node(vm, value, tok), tok);
    return new_binary(vm, ND_COMMA, set, done, tok);
}

// `(T)(a, b, ...)`: `tok` is the '(' after the cast. One scalar fills every
// lane; otherwise the components, scalars and whole vectors, fill the lanes in
// order and must number exactly the lanes the type exposes.
Node *opencl_vector_literal(VirtualMachine *vm, Token **rest, Token *tok,
                            Type *vty, Token *start) {
    Type *elem    = vty->base;
    int   visible = vector_lanes(vty);
    Obj  *result  = new_lvar(vm, "", 0, vty);
    Node *chain   = NULL;
    int   lane    = 0;

    tok           = skip(vm, tok, "(");
    Node *first   = NULL;
    for (int count = 0;; count++) {
        Node *comp = assign(vm, &tok, tok);
        add_type(vm, comp);
        if (!first)
            first = comp;

        if (is_vector(comp->ty)) {
            Obj *tmp = new_lvar(vm, "", 0, value_type(vm, comp->ty));
            chain = chain_after(vm, chain,
                                new_binary(vm, ND_ASSIGN,
                                           new_var_node(vm, tmp, start), comp,
                                           start),
                                start);
            for (int i = 0; i < vector_lanes(comp->ty); i++, lane++) {
                if (lane >= visible)
                    error_tok(vm, start,
                              "too many components for a %d-lane vector",
                              visible);
                Node *to =
                    vector_lane_ref(vm, new_var_node(vm, result, start), elem,
                                    new_num(vm, lane, start), start);
                Node *from = vector_lane_ref(vm, new_var_node(vm, tmp, start),
                                             comp->ty->base,
                                             new_num(vm, i, start), start);
                chain = chain_after(vm, chain,
                                    new_binary(vm, ND_ASSIGN, to, from, start),
                                    start);
            }
        } else {
            if (lane >= visible)
                error_tok(vm, start, "too many components for a %d-lane vector",
                          visible);
            Node *to = vector_lane_ref(vm, new_var_node(vm, result, start),
                                       elem, new_num(vm, lane++, start), start);
            chain    = chain_after(
                vm, chain, new_binary(vm, ND_ASSIGN, to, comp, start), start);
        }
        if (!equal(tok, ","))
            break;
        tok = tok->next;
    }
    *rest      = skip(vm, tok, ")");

    bool splat = lane == 1 && first && !is_vector(first->ty);
    if (!splat && lane != visible)
        error_tok(vm, start,
                  "a %d-lane vector literal needs %d components, "
                  "not %d",
                  visible, visible, lane);
    // chain's first assignment is lane 0; a splat copies it to the others
    int filled = splat ? visible : lane;
    for (int i = splat ? 1 : filled; i < vty->vec_len; i++) {
        Node *to   = vector_lane_ref(vm, new_var_node(vm, result, start), elem,
                                     new_num(vm, i, start), start);
        Node *from = i < filled
                         ? vector_lane_ref(vm, new_var_node(vm, result, start),
                                           elem, new_num(vm, 0, start), start)
                         : new_num(vm, 0, start);
        chain = chain_after(vm, chain,
                            new_binary(vm, ND_ASSIGN, to, from, start), start);
    }
    return new_binary(vm, ND_COMMA, chain, new_var_node(vm, result, start),
                      start);
}
