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

// [[cccc::kernel]]: rejects code a Metal or CUDA kernel cannot run. The
// check covers the marked function and everything it calls, and runs once the
// whole translation unit is parsed so callees may be defined later.

#include "./parse_internal.h"

typedef struct KernelFrame KernelFrame;
struct KernelFrame {
    Obj         *fn;
    KernelFrame *up;
};

typedef struct {
    VirtualMachine *vm;
    HashMap         defs;         // function name -> definition
    HashMap         visited;      // function -> checked already
    HashMap         on_path;      // function -> currently being checked
    HashMap         seen;         // nodes already walked
    HashMap         param_space;  // unmarked pointer param -> inferred space
    HashMap         param_origin; // unmarked pointer param -> first caller
    HashMap         arg_checked;  // call-argument casts checked in check_call
    KernelFrame    *top;
    bool            fn_reported_float;
    bool            stop;
} KernelCtx;

static void append_path(char *buf, size_t cap, KernelFrame *frame) {
    if (frame->up) {
        append_path(buf, cap, frame->up);
        strncat(buf, " -> ", cap - strlen(buf) - 1);
    }
    strncat(buf, frame->fn->name, cap - strlen(buf) - 1);
}

static void kernel_error(KernelCtx *ctx, Token *tok, const char *what) {
    if (ctx->stop)
        return;
    char path[512] = "";
    append_path(path, sizeof(path), ctx->top);
    if (!error_tok_recover(ctx->vm, tok,
                           "%s is not allowed in kernel code (%s)", what, path))
        ctx->stop = true;
}

static bool is_float_kind(Type *ty) {
    return ty && (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE);
}

static bool is_const_object(Type *ty) {
    while (ty && ty->kind == TY_ARRAY)
        ty = ty->base;
    return ty && (ty->is_const || ty->addr_space == AS_CONSTANT);
}

static const char *space_name(AddrSpace space) {
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

static AddrSpace effective_space(AddrSpace space) {
    return space == AS_NONE ? AS_PRIVATE : space;
}

static Type *strip_arrays(Type *ty) {
    while (ty && ty->kind == TY_ARRAY)
        ty = ty->base;
    return ty;
}

static bool is_pointer_like(Type *ty) {
    return ty && (ty->kind == TY_PTR || ty->kind == TY_ARRAY);
}

static AddrSpace pointee_space(Type *ty) {
    Type *base = is_pointer_like(ty) ? strip_arrays(ty->base) : NULL;
    return base ? base->addr_space : AS_NONE;
}

static Obj *find_param(Obj *fn, Type *param) {
    if (!param->name)
        return NULL;
    for (Obj *local = fn->locals; local; local = local->next)
        if (local->is_param && (int)strlen(local->name) == param->name->len &&
            !memcmp(local->name, param->name->loc, param->name->len))
            return local;
    return NULL;
}

static AddrSpace inferred_param_space(KernelCtx *ctx, Obj *param) {
    return (AddrSpace)(intptr_t)hashmap_get_int(&ctx->param_space,
                                                (long long)(intptr_t)param);
}

static AddrSpace lvalue_space(KernelCtx *ctx, Node *node);

static Node *strip_implicit_ptr_cast(Node *node) {
    if (node->kind == ND_CAST && !node->is_explicit_cast &&
        is_pointer_like(node->lhs->ty))
        return node->lhs;
    return node;
}

// Space of the memory a pointer-valued expression refers to.
static AddrSpace ptr_space(KernelCtx *ctx, Node *node) {
    if (!node)
        return AS_PRIVATE;
    if (node->ty && node->ty->kind == TY_ARRAY)
        return effective_space(lvalue_space(ctx, node));
    switch (node->kind) {
        case ND_VAR:
            if (pointee_space(node->var->ty) == AS_NONE) {
                AddrSpace inferred = inferred_param_space(ctx, node->var);
                if (inferred != AS_NONE)
                    return inferred;
            }
            break;
        case ND_ADD:
        case ND_SUB:
            return ptr_space(ctx, is_pointer_like(node->lhs->ty) ? node->lhs
                                                                 : node->rhs);
        case ND_COMMA:
            return ptr_space(ctx, node->rhs);
        case ND_COND:
            return ptr_space(ctx, node->then);
        case ND_ADDR:
            return effective_space(lvalue_space(ctx, node->lhs));
        case ND_CAST:
            if (!is_pointer_like(node->lhs->ty))
                break;
            if (pointee_space(node->ty) != AS_NONE)
                return pointee_space(node->ty);
            return ptr_space(ctx, node->lhs);
        default:
            break;
    }
    return effective_space(pointee_space(node->ty));
}

// Space of the memory an lvalue lives in.
static AddrSpace lvalue_space(KernelCtx *ctx, Node *node) {
    switch (node->kind) {
        case ND_DEREF:
            return ptr_space(ctx, node->lhs);
        case ND_MEMBER:
            return lvalue_space(ctx, node->lhs);
        case ND_VAR: {
            AddrSpace declared = strip_arrays(node->var->ty)->addr_space;
            if (declared != AS_NONE)
                return declared;
            if (!node->var->is_local && is_const_object(node->var->ty))
                return AS_CONSTANT;
            return AS_PRIVATE;
        }
        default:
            return effective_space(strip_arrays(node->ty)->addr_space);
    }
}

static bool space_conversion_ok(AddrSpace from, AddrSpace to,
                                bool is_explicit) {
    if (from == to)
        return true;
    if (to == AS_GENERIC)
        return from != AS_CONSTANT;
    return from == AS_GENERIC && is_explicit;
}

static void check_space_conversion(KernelCtx *ctx, Token *tok, AddrSpace from,
                                   AddrSpace to, bool is_explicit) {
    if (space_conversion_ok(from, to, is_explicit))
        return;
    char what[128];
    snprintf(what, sizeof(what),
             "converting a pointer from the %s to the %s address space",
             space_name(from), space_name(to));
    kernel_error(ctx, tok, what);
}

static void check_fn(KernelCtx *ctx, Obj *fn);
static void walk(KernelCtx *ctx, Node *node);

// Work-item builtins declared by <cccc/kernel.h>; the only body-less
// functions kernel code may call.
bool is_kernel_builtin(const char *name) {
    static const char *const names[] = {
        "cccc_global_id",   "cccc_local_id",   "cccc_group_id",
        "cccc_global_size", "cccc_local_size", "cccc_num_groups",
        "cccc_work_dim",    "cccc_barrier",    "__cccc_local_base"};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++)
        if (!strcmp(name, names[i]))
            return true;
    return false;
}

// TODO: -c=native has no kernel runtime yet; a host loop (and a pthread
// barrier for kernels that use one) would replace the VM launcher.
void reject_kernel_runtime_in_native(VirtualMachine *vm, Token *tok) {
    if (vm->compiler.native_mode)
        error_tok(vm, tok,
                  "the kernel launch and work-item builtins are not "
                  "supported with -c=native");
}

static void mark_path_uses_barrier(KernelCtx *ctx) {
    for (KernelFrame *frame = ctx->top; frame; frame = frame->up)
        frame->fn->kernel_uses_barrier = true;
}

// An unmarked pointer parameter takes the space its callers pass; callers
// that disagree need [[cccc::generic]] or separate helpers.
static void bind_param_space(KernelCtx *ctx, Obj *def, Type *param, Token *tok,
                             AddrSpace from) {
    Obj *local = find_param(def, param);
    if (!local)
        return;
    long long key  = (long long)(intptr_t)local;
    AddrSpace prev = inferred_param_space(ctx, local);
    if (prev == AS_NONE) {
        hashmap_put_int(&ctx->param_space, key, (void *)(intptr_t)from);
        hashmap_put_int(&ctx->param_origin, key, ctx->top->fn->name);
        return;
    }
    if (prev == from)
        return;
    char what[384];
    snprintf(what, sizeof(what),
             "a call passing %s memory to parameter '%s' of '%s', which also "
             "receives %s memory from '%s' (mark the parameter "
             "[[cccc::generic]] or split the helper),",
             space_name(from), local->name, def->name, space_name(prev),
             (char *)hashmap_get_int(&ctx->param_origin, key));
    kernel_error(ctx, tok, what);
}

static void check_call_args(KernelCtx *ctx, Node *call, Obj *def) {
    Type *param = def->ty ? def->ty->params : NULL;
    for (Node *arg = call->args; arg && param; arg = arg->next) {
        Node *src = strip_implicit_ptr_cast(arg);
        if (param->kind == TY_PTR && is_pointer_like(src->ty)) {
            AddrSpace from = ptr_space(ctx, src);
            AddrSpace to   = pointee_space(param);
            if (src != arg)
                hashmap_put_int(&ctx->arg_checked, (long long)(intptr_t)arg,
                                arg);
            if (to == AS_NONE)
                bind_param_space(ctx, def, param, arg->tok, from);
            else
                check_space_conversion(ctx, arg->tok, from, to, false);
        }
        param = param->next;
    }
}

static void check_call(KernelCtx *ctx, Node *call) {
    Node *callee = call->lhs;
    if (!callee || callee->kind != ND_VAR || !callee->var->is_function) {
        kernel_error(ctx, call->tok, "a call through a function pointer");
        walk(ctx, callee);
        return;
    }

    Obj *var = callee->var;
    if (call->is_vla_alloca_call || var->is_builtin_alloca) {
        kernel_error(ctx, call->tok, "alloca or a variable-length array");
        return;
    }

    if (is_kernel_builtin(var->name)) {
        if (!strcmp(var->name, "cccc_barrier"))
            mark_path_uses_barrier(ctx);
        return;
    }
    if (!strcmp(var->name, "__cccc_launch")) {
        kernel_error(ctx, call->tok, "cccc_launch");
        return;
    }

    Obj *def = hashmap_get(&ctx->defs, var->name);
    if (!def) {
        char what[256];
        snprintf(what, sizeof(what),
                 "calling '%s', which has no definition in this file,",
                 var->name);
        kernel_error(ctx, call->tok, what);
        return;
    }
    if (def->is_nested) {
        kernel_error(ctx, call->tok, "a nested function");
        return;
    }
    if (hashmap_get(&ctx->on_path, def->name)) {
        char what[256];
        snprintf(what, sizeof(what), "recursion through '%s'", def->name);
        kernel_error(ctx, call->tok, what);
        return;
    }
    if (def->is_kernel && def->kernel_local_bytes > 0) {
        char what[256];
        snprintf(what, sizeof(what),
                 "calling kernel entry '%s', which declares local-memory "
                 "objects,",
                 def->name);
        kernel_error(ctx, call->tok, what);
        return;
    }
    check_call_args(ctx, call, def);
    check_fn(ctx, def);
    if (def->kernel_uses_barrier)
        mark_path_uses_barrier(ctx);
}

static void walk_node(KernelCtx *ctx, Node *node) {
    if (hashmap_get_int(&ctx->seen, (long long)(intptr_t)node))
        return;
    hashmap_put_int(&ctx->seen, (long long)(intptr_t)node, node);

    if (!ctx->fn_reported_float && is_float_kind(node->ty)) {
        ctx->fn_reported_float = true;
        kernel_error(ctx, node->tok, "double or long double");
    }
    if (node->va_form != VA_NONE)
        kernel_error(ctx, node->tok, "va_start, va_arg or va_copy");

    switch (node->kind) {
        case ND_FUNCALL:
            check_call(ctx, node);
            for (Node *arg = node->args; arg; arg = arg->next)
                walk(ctx, arg);
            return;
        case ND_VAR: {
            Obj *var = node->var;
            if (var->is_function)
                kernel_error(ctx, node->tok, "a function pointer");
            else if (var->is_string_literal)
                kernel_error(ctx, node->tok, "a string literal");
            else if (!var->is_local && !is_const_object(var->ty)) {
                char what[256];
                if (var->name[0] == '.')
                    snprintf(what, sizeof(what), "a static local variable");
                else
                    snprintf(what, sizeof(what), "the global variable '%s'",
                             var->name);
                kernel_error(ctx, node->tok, what);
            }
            if (var->is_tls)
                kernel_error(ctx, node->tok, "thread-local storage");
            return;
        }
        case ND_CAST:
            if (node->ty && node->ty->kind == TY_PTR &&
                is_pointer_like(node->lhs->ty) &&
                (node->is_explicit_cast ||
                 (node->lhs->ty->kind != TY_ARRAY &&
                  pointee_space(node->ty) != AS_NONE &&
                  !hashmap_get_int(&ctx->arg_checked,
                                   (long long)(intptr_t)node))))
                check_space_conversion(ctx, node->tok,
                                       ptr_space(ctx, node->lhs),
                                       effective_space(pointee_space(node->ty)),
                                       node->is_explicit_cast);
            break;
        case ND_ASSIGN: {
            if (lvalue_space(ctx, node->lhs) == AS_CONSTANT)
                kernel_error(ctx, node->tok, "writing to constant memory");
            Node *src = strip_implicit_ptr_cast(node->rhs);
            if (node->lhs->ty && node->lhs->ty->kind == TY_PTR &&
                is_pointer_like(src->ty)) {
                AddrSpace from = ptr_space(ctx, src);
                if (node->lhs->kind == ND_VAR && !node->lhs->var->name[0] &&
                    pointee_space(node->lhs->ty) == AS_NONE)
                    hashmap_put_int(&ctx->param_space,
                                    (long long)(intptr_t)node->lhs->var,
                                    (void *)(intptr_t)from);
                else if (src != node->rhs &&
                         pointee_space(node->rhs->ty) == AS_NONE)
                    check_space_conversion(ctx, node->tok, from,
                                           ptr_space(ctx, node->lhs), false);
            }
            break;
        }
        case ND_RETURN: {
            Type *ret = ctx->top->fn->ty ? ctx->top->fn->ty->return_ty : NULL;
            if (node->lhs && ret && ret->kind == TY_PTR &&
                pointee_space(ret) != AS_NONE) {
                Node *src = strip_implicit_ptr_cast(node->lhs);
                if (src != node->lhs)
                    hashmap_put_int(&ctx->arg_checked,
                                    (long long)(intptr_t)node->lhs, node->lhs);
                if (is_pointer_like(src->ty))
                    check_space_conversion(ctx, node->tok, ptr_space(ctx, src),
                                           pointee_space(ret), false);
            }
            break;
        }
        case ND_ASM:
            kernel_error(ctx, node->tok, "inline asm");
            return;
        case ND_GOTO:
            if (node->label)
                kernel_error(ctx, node->tok, "goto");
            return;
        case ND_GOTO_EXPR:
        case ND_LABEL_VAL:
            kernel_error(ctx, node->tok, "a computed goto");
            return;
        case ND_BLOCK_LITERAL:
        case ND_BLOCK_CALL:
            kernel_error(ctx, node->tok, "a block");
            return;
        case ND_VLA_PTR:
            kernel_error(ctx, node->tok, "a variable-length array");
            return;
        default:
            break;
    }

    walk(ctx, node->lhs);
    walk(ctx, node->rhs);
    walk(ctx, node->cond);
    walk(ctx, node->then);
    walk(ctx, node->els);
    walk(ctx, node->init);
    walk(ctx, node->inc);
    walk(ctx, node->body);
    walk(ctx, node->va_ap);
    walk(ctx, node->va_last);
    walk(ctx, node->va_src);
    walk(ctx, node->cas_addr);
    walk(ctx, node->cas_old);
    walk(ctx, node->cas_new);
    walk(ctx, node->atomic_expr);
    walk(ctx, node->init_tail);
}

static void walk(KernelCtx *ctx, Node *node) {
    for (; node && !ctx->stop; node = node->next)
        walk_node(ctx, node);
}

// A marked non-pointer object lives in that space, which only workgroup
// memory in a kernel entry's body can do.
static void check_object_space(KernelCtx *ctx, Obj *fn, Obj *local,
                               Token *fn_tok) {
    Type *obj = strip_arrays(local->ty);
    if (!obj || obj->kind == TY_PTR || obj->addr_space == AS_NONE ||
        obj->addr_space == AS_PRIVATE)
        return;
    Token *tok = local->tok ? local->tok : fn_tok;
    if (obj->addr_space != AS_LOCAL) {
        char what[128];
        snprintf(what, sizeof(what), "a %s-memory object on the stack",
                 space_name(obj->addr_space));
        kernel_error(ctx, tok, what);
    } else if (!fn->is_kernel || local->is_param) {
        kernel_error(ctx, tok,
                     "a local-memory object outside a kernel entry's body");
    }
}

// An unmarked pointer parameter of a kernel entry is global memory.
static void seed_entry_params(KernelCtx *ctx, Obj *fn) {
    if (!fn->ty)
        return;
    for (Type *param = fn->ty->params; param; param = param->next) {
        if (param->kind != TY_PTR)
            continue;
        AddrSpace space = pointee_space(param);
        if (space == AS_PRIVATE || space == AS_GENERIC) {
            char what[128];
            snprintf(what, sizeof(what),
                     "a %s pointer parameter on a kernel entry",
                     space_name(space));
            kernel_error(ctx, param->name ? param->name : fn->tok, what);
        } else if (space == AS_NONE) {
            Obj *local = find_param(fn, param);
            if (local)
                hashmap_put_int(&ctx->param_space, (long long)(intptr_t)local,
                                (void *)(intptr_t)AS_GLOBAL);
        }
    }
}

static void check_fn(KernelCtx *ctx, Obj *fn) {
    if (hashmap_get(&ctx->visited, fn->name))
        return;
    hashmap_put(&ctx->visited, fn->name, fn);
    hashmap_put(&ctx->on_path, fn->name, fn);

    KernelFrame frame      = {fn, ctx->top};
    ctx->top               = &frame;

    bool saved_float       = ctx->fn_reported_float;
    ctx->fn_reported_float = false;
    Token *tok             = fn->ty && fn->ty->name ? fn->ty->name : fn->tok;

    if (fn->ty && fn->ty->is_variadic)
        kernel_error(ctx, tok, "a variadic function");
    if (fn->is_kernel && !frame.up)
        seed_entry_params(ctx, fn);
    for (Obj *local = fn->locals; local; local = local->next) {
        check_object_space(ctx, fn, local, tok);
        if (local->ty && local->ty->kind == TY_VLA)
            kernel_error(ctx, local->tok ? local->tok : tok,
                         "a variable-length array");
        if (!ctx->fn_reported_float && is_float_kind(local->ty)) {
            ctx->fn_reported_float = true;
            kernel_error(ctx, local->tok ? local->tok : tok,
                         "double or long double");
        }
    }
    if (fn->ty && is_float_kind(fn->ty->return_ty) && !ctx->fn_reported_float) {
        ctx->fn_reported_float = true;
        kernel_error(ctx, tok, "double or long double");
    }
    walk(ctx, fn->body);

    ctx->fn_reported_float = saved_float;
    ctx->top               = frame.up;
    hashmap_delete(&ctx->on_path, fn->name);
}

// A [[cccc::local]] object in a kernel entry's body lives in work-group
// memory, so each use is rewritten to `*(T (*)[N])__cccc_local_base(...)`.
// TODO: every access is a host call; a dedicated opcode would make local
// memory as cheap as a stack slot.
void claim_kernel_local(VirtualMachine *vm, Obj *var, Token *tok) {
    Obj  *fn  = vm->compiler.current_fn;
    Type *obj = strip_arrays(var->ty);
    if (vm->compiler.native_mode || !fn || !fn->is_kernel || !obj ||
        obj->kind == TY_PTR || obj->addr_space != AS_LOCAL)
        return;
    if (var->ty->kind == TY_VLA)
        return;
    int align = var->align > 0 ? var->align : 16;
    int off   = (fn->kernel_local_bytes + align - 1) / align * align;
    var->kernel_local_off    = off;
    fn->kernel_local_bytes   = off + (int)var->ty->size;
    var->is_kernel_local_obj = true;
    (void)tok;
}

Node *kernel_local_ref(VirtualMachine *vm, Obj *var, Token *tok) {
    Type *ptr_ty              = pointer_to(vm, var->ty);
    Type *fn_ty               = func_type(vm, ptr_ty);
    fn_ty->params             = copy_type(vm, ty_long);
    fn_ty->params->next       = copy_type(vm, ty_long);
    fn_ty->params->next->next = copy_type(vm, ty_long);
    Obj  *fn      = new_private_func_obj(vm, "__cccc_local_base", fn_ty);
    Node *call    = new_unary(vm, ND_FUNCALL, new_var_node(vm, fn, tok), tok);
    call->func_ty = fn_ty;
    call->ty      = ptr_ty;
    call->args    = new_num(vm, var->kernel_local_off, tok);
    call->args->next       = new_num(vm, var->ty->size, tok);
    call->args->next->next = new_long(vm, (int64_t)(intptr_t)var, tok);
    for (Node *arg = call->args; arg; arg = arg->next)
        add_type(vm, arg);
    Node *ref = new_unary(vm, ND_DEREF, call, tok);
    ref->ty   = var->ty;
    return ref;
}

void check_kernel_subset(VirtualMachine *vm) {
    bool any = false;
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next)
        if (fn->is_function && fn->is_kernel && fn->body)
            any = true;
    if (!any)
        return;

    KernelCtx ctx = {.vm = vm};
    for (Obj *fn = vm->compiler.globals; fn; fn = fn->next)
        if (fn->is_function && fn->body)
            hashmap_put(&ctx.defs, fn->name, fn);
    for (Obj *fn = vm->compiler.globals; fn && !ctx.stop; fn = fn->next)
        if (fn->is_function && fn->is_kernel && fn->body)
            check_fn(&ctx, fn);

    hashmap_deinit(&ctx.defs);
    hashmap_deinit(&ctx.visited);
    hashmap_deinit(&ctx.on_path);
    hashmap_deinit(&ctx.seen);
    hashmap_deinit(&ctx.param_space);
    hashmap_deinit(&ctx.param_origin);
    hashmap_deinit(&ctx.arg_checked);
}
