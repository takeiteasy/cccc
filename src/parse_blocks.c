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

// Apple Blocks (^{ ... }) literal support: capture collection and the
// block-literal parser/lowering entry point.

#include "./parse_internal.h"

// ========== Block Literal Support (Apple Blocks Extension) ==========

// Recursively collect variables from outer scopes that are referenced in an
// expression
static void collect_captures_in_node(VirtualMachine *vm, Node *node, Obj *outer_locals,
                                     Obj ***captures, int *num_captures,
                                     int *cap_capacity) {
    if (!node)
        return;

    if (node->kind == ND_VAR && node->var && node->var->is_local) {
        // Check if this variable belongs to an outer function (in outer_locals
        // list)
        bool is_outer = false;
        for (Obj *local = outer_locals; local; local = local->next) {
            if (local == node->var) {
                is_outer = true;
                break;
            }
        }

        if (is_outer) {
            // Check if already captured
            for (int i = 0; i < *num_captures; i++) {
                if ((*captures)[i] == node->var)
                    return; // Already captured
            }

            // Add to captures list
            if (*num_captures >= *cap_capacity) {
                *cap_capacity = (*cap_capacity == 0) ? 8 : *cap_capacity * 2;
                Obj **new_caps = arena_alloc(&vm->compiler.parser_arena,
                                             sizeof(Obj *) * (*cap_capacity));
                for (int i = 0; i < *num_captures; i++)
                    new_caps[i] = (*captures)[i];
                *captures = new_caps;
            }
            (*captures)[(*num_captures)++] = node->var;
            node->var->is_captured = true;
        }
    }

    // Recursively check all children
    collect_captures_in_node(vm, node->lhs, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->rhs, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->cond, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->then, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->els, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->init, outer_locals, captures,
                             num_captures, cap_capacity);
    collect_captures_in_node(vm, node->inc, outer_locals, captures,
                             num_captures, cap_capacity);

    for (Node *n = node->body; n; n = n->next)
        collect_captures_in_node(vm, n, outer_locals, captures, num_captures,
                                 cap_capacity);
    for (Node *n = node->args; n; n = n->next)
        collect_captures_in_node(vm, n, outer_locals, captures, num_captures,
                                 cap_capacity);

    // Recurse into nested block literals so intermediate blocks pick up
    // transitive captures (e.g. outer block sees x used inside inner block).
    if (node->kind == ND_BLOCK_LITERAL && node->block_fn)
        collect_captures_in_node(vm, node->block_fn->body, outer_locals, captures,
                                 num_captures, cap_capacity);
}

// #965: collect every `return` statement in a block literal's own body, used
// to infer its return type when none was written explicitly (see
// Obj.block_return_ty_pending). Deliberately does NOT descend into a nested
// block literal's body -- that lives on a separate Obj (node->block_fn->body),
// never reached through any of the generic child fields walked here, so a
// `return` inside a nested block is naturally excluded without special-casing
// ND_BLOCK_LITERAL. Mirrors collect_captures_in_node's traversal shape.
static void collect_block_returns(VirtualMachine *vm, Node *node, Node ***out,
                                  int *count, int *cap) {
    if (!node)
        return;

    if (node->kind == ND_RETURN) {
        if (*count >= *cap) {
            *cap = (*cap == 0) ? 8 : *cap * 2;
            Node **new_arr = arena_alloc(&vm->compiler.parser_arena,
                                         sizeof(Node *) * (*cap));
            for (int i = 0; i < *count; i++)
                new_arr[i] = (*out)[i];
            *out = new_arr;
        }
        (*out)[(*count)++] = node;
    }

    collect_block_returns(vm, node->lhs, out, count, cap);
    collect_block_returns(vm, node->rhs, out, count, cap);
    collect_block_returns(vm, node->cond, out, count, cap);
    collect_block_returns(vm, node->then, out, count, cap);
    collect_block_returns(vm, node->els, out, count, cap);
    collect_block_returns(vm, node->init, out, count, cap);
    collect_block_returns(vm, node->inc, out, count, cap);

    for (Node *n = node->body; n; n = n->next)
        collect_block_returns(vm, n, out, count, cap);
    for (Node *n = node->args; n; n = n->next)
        collect_block_returns(vm, n, out, count, cap);
}

// #994: byte offset of block_fn->captures[idx]'s descriptor slot, measured
// from the descriptor's own base (0 = invoke ptr, 8 = byte-size field, so
// the first capture slot never starts before 16). A __block local's slot
// holds its heap-box pointer (8 bytes, matching codegen's is_block_var
// copy) and a TY_VLA capture keeps its placeholder-sized 8-byte pointer
// slot (its ->size is a compile-time constant, not the real row-major
// byte count -- see the TY_VLA comment elsewhere in this file); every
// other by-value capture gets align_to(cap->ty->size, 8) bytes so a
// struct/union/array/wide-_BitInt/_Decimal larger than one word is no
// longer truncated. Each slot starts aligned to MAX(8, cap->ty->align).
// codegen.c calls this instead of re-deriving the layout so the two
// files can't drift apart; the descriptor local's own size is always
// cc_block_desc_size(block_fn), read back by codegen for Block_copy's
// byte count.
long cc_block_capture_offset(Obj *block_fn, int idx) {
    long off = 16;
    for (int i = 0; i < idx; i++) {
        Obj *cap = block_fn->captures[i];
        int slot_size = (cap->is_block_var || cap->ty->kind == TY_VLA)
                             ? 8 : align_to((int)cap->ty->size, 8);
        int slot_align = (cap->is_block_var || cap->ty->kind == TY_VLA)
                              ? 8 : MAX(8, cap->ty->align);
        off = align_to((int)off, slot_align);
        off += slot_size;
    }
    return off;
}

// #994: total descriptor byte size (invoke + size fields + every capture
// slot per cc_block_capture_offset's layout, including trailing padding
// so the last capture's slot ends on an 8-byte boundary).
long cc_block_desc_size(Obj *block_fn) {
    long off = cc_block_capture_offset(block_fn, block_fn->num_captures);
    return align_to((int)off, 8);
}

// Parse a block literal: ^{ ... } or ^(params){ ... } or ^returntype(params){
// ... }
Node *block_literal(VirtualMachine *vm, Token **rest, Token *tok) {
    Token *start = tok;
    tok = tok->next; // Skip ^

    // Determine return type and parameters
    Type *return_ty = ty_void;
    Type *params = NULL;
    bool return_ty_explicit = false;
    // bool has_params = false;

    // Check for explicit return type (anything before '(' that's a type)
    if (!equal(tok, "{") && !equal(tok, "(") && is_typename(vm, tok)) {
        return_ty = typename(vm, &tok, tok);
        return_ty_explicit = true;
    }

    // Check for parameter list
    if (equal(tok, "(")) {
        tok = tok->next;
        // has_params = true;

        if (!equal(tok, ")")) {
            // Parse parameters using declspec + declarator (like func_params)
            Type head = {};
            Type *cur = &head;
            while (!equal(tok, ")")) {
                if (cur != &head)
                    tok = skip(vm, tok, ",");

                VarAttr attr = {};
                Type *param_ty = declspec(vm, &tok, tok, &attr);
                param_ty = declarator(vm, &tok, tok, param_ty);
                param_ty = apply_var_attrs_to_type(vm, param_ty, &attr);
                if (has_custom_attrs(param_ty, &attr))
                    error_tok(vm, param_ty->name ? param_ty->name : tok,
                              "custom attributes are only supported on file-scope declarations");

                // Convert array and function parameters to pointers
                if (param_ty->kind == TY_ARRAY) {
                    Token *name = param_ty->name;
                    param_ty = pointer_to(vm, param_ty->base);
                    param_ty->name = name;
                } else if (param_ty->kind == TY_FUNC) {
                    Token *name = param_ty->name;
                    param_ty = pointer_to(vm, param_ty);
                    param_ty->name = name;
                }

                cur = cur->next = copy_type(vm, param_ty);
            }
            params = head.next;
        }
        tok = skip(vm, tok, ")");
    }

    // Now we must have a compound statement
    if (!equal(tok, "{"))
        error_tok(vm, tok, "expected '{' in block literal");

    // Save current function context
    Obj *outer_fn = vm->compiler.current_fn;
    Obj *saved_locals = vm->compiler.locals;

    // Create a synthetic function for this block
    char *block_name = new_unique_name(vm);
    Type *block_func_ty = func_type(vm, return_ty);
    block_func_ty->params = params;

    Obj *block_fn = new_gvar(vm, block_name, strlen(block_name), block_func_ty);
    block_fn->is_function = true;
    block_fn->is_definition = true;
    block_fn->is_static = true;
    block_fn->is_block = true;
    block_fn->parent_fn = outer_fn;
    block_fn->is_nested =
        true; // Treat blocks like nested functions for codegen
    block_fn->nesting_depth = outer_fn ? outer_fn->nesting_depth + 1 : 1;
    // Store parent's locals snapshot before entering our own scope so nested
    // blocks can walk the ancestry chain during transitive capture collection.
    block_fn->block_outer_locals = saved_locals;
    // #965: return type inferred from the body below when not written
    // explicitly, matching clang's block-literal inference.
    block_fn->block_return_ty_pending = !return_ty_explicit;

    // Set up block function context
    vm->compiler.current_fn = block_fn;
    vm->compiler.locals = NULL;
    // #642: blocks get their own pending __builtin_object_size query list,
    // resolved against block_fn->body below — otherwise a query on a
    // block-local malloc-tracked pointer would be poison-scanned against the
    // *enclosing* function's body, which never mentions the block-local var,
    // and could wrongly resolve to the allocation size.
    struct ObjSizeQuery *saved_objsize_queries = vm->compiler.objsize_queries;
    vm->compiler.objsize_queries = NULL;

    enter_scope(vm);

    // Create params in correct order for calling convention:
    // A0 = __static_link (descriptor), A1 = first user param, A2 = second, etc.
    // Since new_lvar prepends, we need to add in REVERSE order:
    // add last user param, then prev, ..., then first user param, then
    // __static_link

    // Count user params and store in array for reverse iteration
    int param_count = 0;
    for (Type *p = params; p; p = p->next)
        param_count++;

    Type **param_array = NULL;
    if (param_count > 0) {
        param_array = arena_alloc(&vm->compiler.parser_arena,
                                  sizeof(Type *) * param_count);
        int idx = 0;
        for (Type *p = params; p; p = p->next) {
            param_array[idx++] = p;
        }
    }

    // Add user params in reverse order (last first)
    for (int i = param_count - 1; i >= 0; i--) {
        Type *p = param_array[i];
        if (p->name) {
            Obj *param =
                new_lvar(vm, get_ident(vm, p->name), p->name->len, p);
            param->is_param = true;
        }
    }

    // Add __static_link LAST so it ends up FIRST in the list (receives A0)
    new_lvar(vm, "__static_link", 13, pointer_to(vm, ty_void));

    block_fn->params = vm->compiler.locals;
    block_fn->alloca_bottom =
        new_lvar(vm, "__alloca_size__", 15, pointer_to(vm, ty_char));

    // Now parse the block body - params are visible in scope
    tok = skip(vm, tok, "{");
    block_fn->body = compound_stmt(vm, &tok, tok, NULL);
    block_fn->locals = vm->compiler.locals;

    leave_scope(vm);
    resolve_objsize_queries(vm, block_fn->body);
    mark_addr_escapes(block_fn->body);
    propagate_checked_bounds(vm, block_fn);
    verify_checked_assign_bounds(vm, block_fn);

    // #965: infer the block's return type from its own `return` statements
    // when it wasn't written explicitly. The VM never needed this -- a
    // block's result always comes back through REG_A0/FREG_A0 regardless of
    // the declared type -- but the native serializer needs a real,
    // non-placeholder return type to spell out the lifted function's
    // signature. stmt()'s "return" handling (see block_infer_pending there)
    // deliberately skipped the mismatch warning and the implicit-cast
    // insertion while this was still pending; both are done here instead,
    // now that the real type is known.
    if (block_fn->block_return_ty_pending) {
        Node **rets = NULL;
        int num_rets = 0, rets_cap = 0;
        collect_block_returns(vm, block_fn->body, &rets, &num_rets, &rets_cap);

        Type *inferred = ty_void;
        for (int i = 0; i < num_rets; i++) {
            if (rets[i]->lhs) {
                inferred = rets[i]->lhs->ty;
                break;
            }
        }

        return_ty = inferred;
        block_func_ty->return_ty = inferred;

        if (inferred->kind != TY_STRUCT && inferred->kind != TY_UNION) {
            for (int i = 0; i < num_rets; i++)
                if (rets[i]->lhs)
                    rets[i]->lhs = new_cast(vm, rets[i]->lhs, inferred);
        }
        block_fn->block_return_ty_pending = false;
    }

    // Collect captured variables from the parsed body.
    // Walk all ancestor scopes so that variables from grandparent+ scopes are
    // captured transitively (inner block gets x from outer block's descriptor).
    Obj **captures = NULL;
    int num_captures = 0, cap_capacity = 0;

    // Level 0: immediate parent's locals
    if (saved_locals)
        collect_captures_in_node(vm, block_fn->body, saved_locals, &captures,
                                 &num_captures, &cap_capacity);
    // Levels 1+: walk block ancestor chain via block_outer_locals snapshots
    for (Obj *anc = outer_fn; anc && anc->is_block && anc->block_outer_locals;
         anc = anc->parent_fn)
        collect_captures_in_node(vm, block_fn->body, anc->block_outer_locals,
                                 &captures, &num_captures, &cap_capacity);

    block_fn->captures = captures;
    block_fn->num_captures = num_captures;
    // Descriptor offsets are computed per-block at codegen time via
    // find_capture_index so no per-Obj offset field is needed.

    // Restore outer function context
    vm->compiler.current_fn = outer_fn;
    vm->compiler.locals = saved_locals;
    vm->compiler.objsize_queries = saved_objsize_queries;

    // Allocate descriptor storage on the enclosing function's stack frame.
    // Layout: [invoke_ptr(0) | desc_size(8) | cap0(16) | cap1(cc_block_capture_offset(block_fn,1)) | ...]
    // -- capture slots are no longer a flat one-word-each array; a
    // by-value aggregate capture wider than 8 bytes gets a wider slot
    // (#994, see cc_block_capture_offset above). Per-frame stack
    // allocation ensures each invocation gets its own descriptor, so
    // multiple calls to a function returning the same block literal are
    // independent.
    long desc_bytes = cc_block_desc_size(block_fn);
    Type *desc_arr_ty = array_of(vm, ty_long, (int)(desc_bytes / 8));
    Obj *desc_var = new_lvar(vm, "", 0, desc_arr_ty);
    // #965: pure serializer bookkeeping -- see Obj.block_desc_of.
    desc_var->block_desc_of = block_fn;

    // Create the block literal node
    Node *node = new_node(vm, ND_BLOCK_LITERAL, start);
    node->block_fn = block_fn;
    node->block_captures = captures;
    node->num_block_captures = num_captures;
    node->block_desc_var = desc_var;

    // Block type: pointer to function type (blocks are first-class callable
    // values)
    node->ty = block_type(vm, return_ty, params);

    *rest = tok;
    return node;
}
