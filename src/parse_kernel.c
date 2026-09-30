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
    HashMap         defs;    // function name -> definition
    HashMap         visited; // function -> checked already
    HashMap         on_path; // function -> currently being checked
    HashMap         seen;    // nodes already walked
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
    return ty && ty->is_const;
}

static void check_fn(KernelCtx *ctx, Obj *fn);
static void walk(KernelCtx *ctx, Node *node);

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
    check_fn(ctx, def);
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
    for (Obj *local = fn->locals; local; local = local->next) {
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
}
