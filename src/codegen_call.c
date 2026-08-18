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

#include "./codegen_internal.h"

// ========== Function Call Detection ==========
// Check if expression tree contains a function call (recursively)
// Used to determine if we need to save LHS before evaluating RHS

// Wide _BitInt(N>64) arithmetic/casts/assignment lower to a hidden CALLF (or
// a raw MCPY) that clobbers REG_A0-A7, just like a real function call — so
// callers that decide whether to save argument registers around a
// subexpression (contains_funcall) must treat these the same way.
// _Decimal32/64/128 (#402) arithmetic/comparisons/casts clobber REG_A0-A3
// via the same opaque-opcode convention (DADD/DCMP/DFROMI/...), so they
// need identical treatment here.
static bool is_wide_bitint_helper_op(Node *node) {
    if (!node)
        return false;
    switch (node->kind) {
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
    case ND_BITAND: case ND_BITOR: case ND_BITXOR: case ND_SHL: case ND_SHR:
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE: case ND_CAST:
    case ND_NEG: case ND_BITNOT:
        return node->lhs && (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->ty) ||
                              is_decimal(node->lhs->ty) || is_decimal(node->ty));
    case ND_ASSIGN:
        return is_wide_bitint(node->ty) || is_decimal(node->ty);
    case ND_DECIMAL_TO_CHARS:
        return true;
    default:
        return false;
    }
}

bool contains_funcall(Node *node) {
    if (!node)
        return false;

    if (node->kind == ND_FUNCALL || node->kind == ND_BLOCK_CALL)
        return true;
    if (is_wide_bitint_helper_op(node))
        return true;
    // ND_MEMZERO lowers to the MSET opcode, which clobbers REG_A0/REG_A2 (the
    // ABI argument registers) just like a function call.  When a partial
    // aggregate initialiser (`T x[N] = {0}`, struct `{...}`) appears as a call
    // argument, already-staged argument registers must be saved around it.
    if (node->kind == ND_MEMZERO)
        return true;

    // Check children
    if (contains_funcall(node->lhs))
        return true;
    if (contains_funcall(node->rhs))
        return true;
    if (contains_funcall(node->cond))
        return true;
    if (contains_funcall(node->then))
        return true;
    if (contains_funcall(node->els))
        return true;

    // Check arguments for nested calls
    for (Node *arg = node->args; arg; arg = arg->next) {
        if (contains_funcall(arg))
            return true;
    }

    return false;
}

bool contains_self_call(Node *node, Obj *fn) {
    if (!node)
        return false;
    if (node->kind == ND_FUNCALL && node->lhs->kind == ND_VAR &&
        node->lhs->var == fn)
        return true;
    if (contains_self_call(node->lhs, fn))
        return true;
    if (contains_self_call(node->rhs, fn))
        return true;
    if (contains_self_call(node->cond, fn))
        return true;
    if (contains_self_call(node->then, fn))
        return true;
    if (contains_self_call(node->els, fn))
        return true;
    if (contains_self_call(node->init, fn))
        return true;
    if (contains_self_call(node->inc, fn))
        return true;
    if (contains_self_call(node->body, fn))
        return true;
    for (Node *arg = node->args; arg; arg = arg->next) {
        if (contains_self_call(arg, fn))
            return true;
    }
    // Walk the statement sibling chain (for ND_BLOCK and similar)
    if (node->kind == ND_BLOCK) {
        for (Node *s = node->body; s; s = s->next)
            if (contains_self_call(s, fn))
                return true;
    }
    return false;
}

// #716: does the &-chain / aggregate-decay / pointer-arithmetic expression
// rooted at `n` bottom out at one of `fn`'s own locals or parameters?
// Mirrors mark_escaping_root's walk in parse.c, but returns a verdict instead
// of marking, and additionally follows ND_ADD/ND_SUB so pointer arithmetic
// off a frame-local base (e.g. `buf + i`) is recognized -- a route
// mark_addr_escapes deliberately leaves unmarked (safe there, since that
// pass only needs to *record more*; unsafe here, since we need to *reject
// more* — see the two-part guard in can_emit_tail_call below).
static bool addr_roots_at_frame_local(Obj *fn, Node *n) {
    while (n) {
        switch (n->kind) {
        case ND_VAR:
            if (!n->var || !n->var->is_local)
                return false;
            for (Obj *v = fn->locals; v; v = v->next)
                if (v == n->var)
                    return true;
            return false;
        case ND_MEMBER:
        case ND_CAST:
        case ND_DEREF:
            n = n->lhs;
            continue;
        case ND_ADD:
        case ND_SUB:
            if (n->lhs && n->lhs->ty &&
                (n->lhs->ty->kind == TY_PTR || n->lhs->ty->kind == TY_ARRAY)) {
                n = n->lhs;
            } else if (n->rhs && n->rhs->ty &&
                       (n->rhs->ty->kind == TY_PTR || n->rhs->ty->kind == TY_ARRAY)) {
                n = n->rhs;
            } else {
                return false;
            }
            continue;
        default:
            return false;
        }
    }
    return false;
}

// #716: does evaluating tail-call argument `n` yield a pointer into `fn`'s
// own frame? A sound over-approximation (errs toward "yes, disable TCO") —
// the inverse of find_and_mark_escaping_addr's under-approximation, which is
// safe for its own #676 purpose but not for this one.
static bool tail_arg_carries_frame_addr(Obj *fn, Node *n) {
    while (n) {
        switch (n->kind) {
        case ND_CAST:
            n = n->lhs;
            continue;
        case ND_COMMA:
        case ND_ASSIGN:
            n = n->rhs;
            continue;
        case ND_COND:
            return tail_arg_carries_frame_addr(fn, n->then) ||
                   tail_arg_carries_frame_addr(fn, n->els);
        case ND_ADDR:
            return addr_roots_at_frame_local(fn, n->lhs);
        case ND_ADD:
        case ND_SUB:
            // Pointer arithmetic off a frame-local base, e.g. `buf + i`.
            if (n->ty && n->ty->kind == TY_PTR)
                return addr_roots_at_frame_local(fn, n);
            return false;
        default:
            // Arrays/structs/unions decay to their own base address with no
            // explicit `&` in the source at all.
            if (n->ty && (n->ty->kind == TY_ARRAY || n->ty->kind == TY_STRUCT ||
                          n->ty->kind == TY_UNION))
                return addr_roots_at_frame_local(fn, n);
            return false;
        }
    }
    return false;
}

// #762: normalisation key mirroring gen_expr's ND_CAST case (~line 4990).
// Two types with the same non-negative key leave an already-normalised value
// in an identical register representation, so a cast between them is a
// runtime no-op -- safe for the ND_RETURN tail-call path to strip, since a
// funcall's result is already normalised to the callee's own return type by
// the callee's own ND_RETURN cast. A negative key never matches anything
// (including itself), so such a cast is never stripped.
static long long return_repr_key(Type *ty) {
    if (!ty)
        return -1;
    if (is_complex(ty) || is_vector(ty) || is_wide_bitint(ty) || is_decimal(ty))
        return -1;
    switch (ty->kind) {
    case TY_STRUCT: case TY_UNION: case TY_ARRAY: case TY_VLA:
    case TY_FUNC: case TY_ERROR: case TY_AUTO: case TY_BLOCK:
        return -1;
    case TY_FLOAT:
        return 1; // f32 -- emit_fround_f32 is idempotent, but f32->f64 is not
    case TY_DOUBLE: case TY_LDOUBLE:
        return 2; // f64/f80 share a representation; f32<->f64 is not a no-op
    case TY_BOOL:
        return 3; // SNE3 is idempotent on an already-0/1 value
    case TY_CHAR:
        return 10 + (ty->is_unsigned ? 1 : 0);
    case TY_SHORT:
        return 20 + (ty->is_unsigned ? 1 : 0);
    case TY_INT:
        return 30 + (ty->is_unsigned ? 1 : 0);
    case TY_BITINT:
        // Narrow (<=64-bit) _BitInt only -- is_wide_bitint already handled
        // above. Key on width/signedness: emit_bitint_trunc's mask depends
        // on both.
        return 1000 + ty->bit_width * 2 + (ty->is_unsigned ? 1 : 0);
    default:
        // TY_LONG, TY_PTR, TY_ENUM, TY_VOID, TY_NULLPTR_T: gen_expr's
        // ND_CAST case emits nothing for these -- one shared 64-bit,
        // no-conversion key.
        return 0;
    }
}

// True when casting `from` to `to` is a representation no-op on a value
// already normalised to `from` -- see return_repr_key.
bool cast_is_repr_noop(Type *to, Type *from) {
    long long k1 = return_repr_key(to);
    long long k2 = return_repr_key(from);
    return k1 >= 0 && k1 == k2;
}

// Return true when `expr` is a tail-call candidate: a direct, in-VM,
// non-variadic, non-nested, non-noreturn, non-struct-returning call with ≤8 args.
// The caller is responsible for the opt_level >= 1 and inline_exit_name guards.
bool can_emit_tail_call(VirtualMachine *vm, Node *expr) {
    if (!expr || expr->kind != ND_FUNCALL)
        return false;
    Node *lhs = expr->lhs;
    if (!lhs || lhs->kind != ND_VAR || !lhs->var->is_function)
        return false;
    Obj *callee = lhs->var;
    if (ffi_index_for_callee(vm, callee) >= 0)
        return false; // FFI — goes through CALLF, not CALL
    if (callee->is_nested)
        return false; // needs static link in REG_A0
    if (expr->func_ty && expr->func_ty->is_variadic)
        return false; // va_area is part of the frame
    if (callee->is_noreturn)
        return false; // BTRAP emitted after CALL; can't compose with CALLT
    if (expr->ty && (expr->ty->kind == TY_STRUCT || expr->ty->kind == TY_UNION ||
                     expr->ty->kind == TY_VECTOR))
        return false; // RETBUF machinery — incompatible with frame reuse
    // #763: a wide _BitInt(N>64) return is materialised as an address into a
    // frame-local scratch buffer (alloc_wide_bitint_temp, gen_expr's ND_CAST
    // wide-BitInt handling), the same frame-reuse hazard shape as the
    // struct/union/vector RETBUF case just above. In practice the ND_RETURN
    // strip loop already never reaches here for a wide-_BitInt-returning
    // call -- return_repr_key() gives is_wide_bitint() types a negative key,
    // so the mandatory return-site ND_CAST is never stripped and tco_expr
    // stops being ND_FUNCALL -- but that's an accidental consequence of
    // #762's fix, not a guarantee this function should rely on. Reject
    // explicitly so the invariant holds even if the strip logic changes.
    if (expr->ty && is_wide_bitint(expr->ty))
        return false;
    // _Decimal32/64/128 (#402): same frame-reuse hazard and same
    // return_repr_key() negative-key argument as wide _BitInt above -- a
    // decimal return also materialises into a frame-local scratch slot
    // (alloc_decimal_temp), so CALLT is excluded explicitly here too.
    if (expr->ty && is_decimal(expr->ty))
        return false;
    int nargs = 0;
    for (Node *a = expr->args; a; a = a->next) {
        nargs++;
        // #714: a vector arg's address is a compiler-synthesized scratch
        // slot in *this* frame (gen_vector_arg_ptr), invisible to
        // tail_arg_carries_frame_addr/addr_escapes below since it never
        // appears as such in the AST. CALLT reuses the caller's frame, so
        // that address would dangle the instant the callee's prologue
        // overwrites the slot -- same hazard class as #716/#718. Reject
        // outright rather than trying to teach the escape scan about it.
        if (is_vector(a->ty))
            return false;
        // #402: a decimal arg's address is likewise a compiler-synthesized
        // scratch slot in *this* frame (gen_decimal_arg_ptr) -- same #714
        // hazard, same rejection.
        if (is_decimal(a->ty))
            return false;
    }
    if (nargs > 8)
        return false; // stack-spill args would be below the unwound frame
    // #716: CALLT reuses the caller's frame (vm->sp = vm->bp in op_CALLT_fn),
    // so any pointer into that frame handed to the callee dangles the moment
    // the callee's own prologue/body overwrites the slot. Reject:
    //  (a) arguments that syntactically carry a frame-local address (sound
    //      over-approximation — see tail_arg_carries_frame_addr), and
    //  (b) any function whose address-escape scan (mark_addr_escapes,
    //      parse.c) already proved a local's address reaches somewhere
    //      outside this frame (pointer variable holding &x, &x stored to a
    //      global the callee reads, etc.) — a cheap net for routes that
    //      aren't syntactically in the argument list.
    // Residual gap, tracked in #718: a frame address laundered through a
    // wrapper mark_addr_escapes doesn't recognize (e.g. `int *q = buf + i;`
    // then passing `q`) is still missed.
    Obj *fn = vm->compiler.current_fn;
    if (fn) {
        for (Node *a = expr->args; a; a = a->next)
            if (tail_arg_carries_frame_addr(fn, a))
                return false;
        for (Obj *v = fn->locals; v; v = v->next)
            if (v->addr_escapes)
                return false;
    }
    return true;
}

// Count total AST nodes (statements + expressions) in a subtree.
// Used by the inliner to enforce the node-count threshold.
int count_ast_nodes(Node *node) {
    if (!node)
        return 0;
    int count = 1; // this node
    count += count_ast_nodes(node->next);
    count += count_ast_nodes(node->lhs);
    count += count_ast_nodes(node->rhs);
    count += count_ast_nodes(node->cond);
    count += count_ast_nodes(node->then);
    count += count_ast_nodes(node->els);
    count += count_ast_nodes(node->init);
    count += count_ast_nodes(node->inc);
    count += count_ast_nodes(node->body);
    count += count_ast_nodes(node->args);
    count += count_ast_nodes(node->cas_addr);
    count += count_ast_nodes(node->cas_old);
    count += count_ast_nodes(node->cas_new);
    count += count_ast_nodes(node->atomic_expr);
    // Don't follow goto_next/case_next/default_case/init_tail — they are
    // chain pointers within switch/label structures, not tree children.
    return count;
}

// Check if a subtree contains switch, goto, or label nodes — these have
// chain pointers (goto_next, case_next, default_case, init_tail) that
// clone_subst zeroes, so inlining them would produce broken code.
bool contains_unsupported_control_flow(Node *node) {
    if (!node)
        return false;
    if (node->kind == ND_SWITCH || node->kind == ND_CASE ||
        node->kind == ND_GOTO || node->kind == ND_LABEL ||
        node->kind == ND_GOTO_EXPR)
        return true;
    if (contains_unsupported_control_flow(node->lhs))
        return true;
    if (contains_unsupported_control_flow(node->rhs))
        return true;
    if (contains_unsupported_control_flow(node->cond))
        return true;
    if (contains_unsupported_control_flow(node->then))
        return true;
    if (contains_unsupported_control_flow(node->els))
        return true;
    if (contains_unsupported_control_flow(node->init))
        return true;
    if (contains_unsupported_control_flow(node->inc))
        return true;
    if (contains_unsupported_control_flow(node->body))
        return true;
    if (contains_unsupported_control_flow(node->args))
        return true;
    return false;
}

// Walk a cloned AST and replace ND_VAR pointers from the original local
// array with corresponding entries in the remapped array.
void replace_locals_in_ast(Node *node, Obj **orig, Obj **map, int count) {
    if (!node)
        return;
    if (node->kind == ND_VAR && node->var) {
        for (int i = 0; i < count; i++) {
            if (node->var == orig[i]) {
                node->var = map[i];
                break;
            }
        }
    }
    replace_locals_in_ast(node->next, orig, map, count);
    replace_locals_in_ast(node->lhs, orig, map, count);
    replace_locals_in_ast(node->rhs, orig, map, count);
    replace_locals_in_ast(node->cond, orig, map, count);
    replace_locals_in_ast(node->then, orig, map, count);
    replace_locals_in_ast(node->els, orig, map, count);
    replace_locals_in_ast(node->init, orig, map, count);
    replace_locals_in_ast(node->inc, orig, map, count);
    replace_locals_in_ast(node->body, orig, map, count);
    replace_locals_in_ast(node->args, orig, map, count);
    replace_locals_in_ast(node->cas_addr, orig, map, count);
    replace_locals_in_ast(node->cas_old, orig, map, count);
    replace_locals_in_ast(node->cas_new, orig, map, count);
    replace_locals_in_ast(node->atomic_expr, orig, map, count);
}

// Count how many stack slots a local variable needs.
int var_stack_slots(Obj *var) {
    if (var->ty->kind == TY_ARRAY)
        return (var->ty->size + 7) / 8;
    if (var->ty->kind == TY_STRUCT || var->ty->kind == TY_UNION ||
        var->ty->kind == TY_COMPLEX || var->ty->kind == TY_VECTOR)
        return (var->ty->size + 7) / 8;
    if (is_decimal(var->ty) && var->ty->size > 8) // #402: _Decimal128 is 2 words
        return (var->ty->size + 7) / 8;
    return 1;
}

static Node *clone_expr(VirtualMachine *vm, Node *src) {
    if (!src)
        return NULL;
    Node *n = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *n = *src;
    n->next = clone_expr(vm, src->next);
    n->lhs = clone_expr(vm, src->lhs);
    n->rhs = clone_expr(vm, src->rhs);
    n->cond = clone_expr(vm, src->cond);
    n->then = clone_expr(vm, src->then);
    n->els = clone_expr(vm, src->els);
    n->init = clone_expr(vm, src->init);
    n->inc = clone_expr(vm, src->inc);
    n->body = clone_expr(vm, src->body);
    n->args = clone_expr(vm, src->args);
    n->cas_addr = clone_expr(vm, src->cas_addr);
    n->cas_old = clone_expr(vm, src->cas_old);
    n->cas_new = clone_expr(vm, src->cas_new);
    n->atomic_expr = clone_expr(vm, src->atomic_expr);
    // #1018: va_ap/va_last/va_src are pure serializer annotation (see
    // Node.va_form's comment, src/cccc.h) but are themselves Node*, so a
    // scalar `*n = *src` alone would leave them pointing at the
    // *original* tree's subexpressions -- fine for VM codegen (which never
    // reads these fields), but wrong once the clone's own va_ap/va_last/
    // va_src get independently mutated or freed relative to the original.
    // Clone them in step with everything else so a comptime/macro clone
    // never silently degrades an annotated node back to unannotated
    // (today's broken) native output.
    n->va_ap = clone_expr(vm, src->va_ap);
    n->va_last = clone_expr(vm, src->va_last);
    n->va_src = clone_expr(vm, src->va_src);
    n->goto_next = NULL;
    n->case_next = NULL;
    n->default_case = NULL;
    n->init_tail = NULL;
    return n;
}

Node *clone_subst(VirtualMachine *vm, Node *src, Obj *params, Node *args) {
    if (!src)
        return NULL;
    if (src->kind == ND_VAR && src->var) {
        Obj *p = params;
        Node *a = args;
        while (p && a) {
            if (src->var == p)
                return clone_expr(vm, a);
            p = p->next;
            a = a->next;
        }
    }
    Node *n = arena_alloc(&vm->compiler.parser_arena, sizeof(Node));
    *n = *src;
    n->next = clone_subst(vm, src->next, params, args);
    n->lhs = clone_subst(vm, src->lhs, params, args);
    n->rhs = clone_subst(vm, src->rhs, params, args);
    n->cond = clone_subst(vm, src->cond, params, args);
    n->then = clone_subst(vm, src->then, params, args);
    n->els = clone_subst(vm, src->els, params, args);
    n->init = clone_subst(vm, src->init, params, args);
    n->inc = clone_subst(vm, src->inc, params, args);
    n->body = clone_subst(vm, src->body, params, args);
    n->args = clone_subst(vm, src->args, params, args);
    n->cas_addr = clone_subst(vm, src->cas_addr, params, args);
    n->cas_old = clone_subst(vm, src->cas_old, params, args);
    n->cas_new = clone_subst(vm, src->cas_new, params, args);
    n->atomic_expr = clone_subst(vm, src->atomic_expr, params, args);
    // #1018: see clone_expr's own comment above -- va_ap/va_last/va_src
    // need the same param-substitution pass as any other child field.
    n->va_ap = clone_subst(vm, src->va_ap, params, args);
    n->va_last = clone_subst(vm, src->va_last, params, args);
    n->va_src = clone_subst(vm, src->va_src, params, args);
    n->goto_next = NULL;
    n->case_next = NULL;
    n->default_case = NULL;
    n->init_tail = NULL;
    return n;
}

#define MAX_LABELS 256
#define MAX_LABEL_PATCHES 1024

typedef struct {
    char *name;
    Pc offset;
} LabelDef;

typedef struct {
    char *name;
    Pc patch_location;
    bool text_relative;
} LabelPatch;

static LabelDef label_defs[MAX_LABELS];
static int num_label_defs = 0;

static LabelPatch label_patches[MAX_LABEL_PATCHES];
static int num_label_patches = 0;

void reset_labels(void) {
    num_label_defs = 0;
    num_label_patches = 0;
}

// Define a label at the current position
void define_label(VirtualMachine *vm, char *name) {
    if (!name)
        return;
    if (num_label_defs >= MAX_LABELS) {
        error("codegen: too many labels");
    }
    Pc label_pc = vm->text_ptr + 1;
    label_defs[num_label_defs].name = name;
    label_defs[num_label_defs].offset = label_pc;
    num_label_defs++;

    // Also record in the persistent global map so apply_global_relocations() can
    // resolve &&label references stored in static/global initialisers (#573).
    if (num_global_labels >= global_labels_cap) {
        int new_cap = global_labels_cap ? global_labels_cap * 2 : 64;
        GlobalLabelEntry *buf = realloc(global_label_map, (size_t)new_cap * sizeof(GlobalLabelEntry));
        if (!buf)
            error("codegen: out of memory for global label map");
        global_label_map = buf;
        global_labels_cap = new_cap;
    }
    global_label_map[num_global_labels].name   = name;
    global_label_map[num_global_labels].offset = label_pc;
    num_global_labels++;

    // A label is a control-flow join point; the restrict cache is no longer valid.
    restrict_cache_invalidate_all(vm);
}

// Record a jump that needs to be patched later
void add_label_patch(char *name, Pc patch_location,
                            bool text_relative) {
    if (!name)
        return;
    if (num_label_patches >= MAX_LABEL_PATCHES) {
        error("codegen: too many label patches");
    }
    label_patches[num_label_patches].name = name;
    label_patches[num_label_patches].patch_location = patch_location;
    label_patches[num_label_patches].text_relative = text_relative;
    num_label_patches++;
}

// Patch all forward references to labels
void patch_labels(VirtualMachine *vm) {
    HashMap label_map = {};
    for (int i = 0; i < num_label_defs; i++)
        hashmap_put(&label_map, label_defs[i].name,
                    (void *)(uintptr_t)label_defs[i].offset);

    for (int i = 0; i < num_label_patches; i++) {
        char *name = label_patches[i].name;
        Pc patch = label_patches[i].patch_location;
        Pc offset = (Pc)(uintptr_t)hashmap_get(&label_map, name);
        if (!offset)
            continue;

        if (label_patches[i].text_relative) {
            cc_write_i64_at(vm, patch, cc_pc_to_byte_offset(offset));
        } else {
            vm->text_seg[patch] = offset;
        }
    }

    hashmap_deinit(&label_map);
}

