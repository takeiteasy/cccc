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

// ========== Statement Generation ==========

void emit_source_location(VirtualMachine *vm, Node *node) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !node || !node->tok)
        return;
    if (node->tok->file == vm->dbg.last_debug_file &&
        node->tok->line_no == vm->dbg.last_debug_line &&
        node->tok->col_no == vm->dbg.last_debug_col)
        return;

    if (vm->dbg.source_map_count >= vm->dbg.source_map_capacity) {
        vm->dbg.source_map_capacity = vm->dbg.source_map_capacity
                                          ? vm->dbg.source_map_capacity * 2
                                          : 1024;
        vm->dbg.source_map =
            realloc(vm->dbg.source_map,
                    vm->dbg.source_map_capacity * sizeof(SourceMap));
    }

    SourceMap *entry        = &vm->dbg.source_map[vm->dbg.source_map_count++];
    entry->pc_offset        = vm->text_ptr + 1;
    entry->file             = node->tok->file;
    entry->line_no          = node->tok->line_no;
    entry->col_no           = node->tok->col_no;
    entry->end_col_no       = node->tok->col_no + node->tok->len;

    vm->dbg.last_debug_file = node->tok->file;
    vm->dbg.last_debug_line = node->tok->line_no;
    vm->dbg.last_debug_col  = node->tok->col_no;
}

// #981: true if `blk` (a real, non-is_decl_group C block scope) declares a
// VLA anywhere among its *immediate* statements, looking exactly one level
// through a declaration()-built synthetic wrapper (Node.is_decl_group,
// cccc.h) -- declaration() never nests one such wrapper inside another, so
// one level is exhaustive. Deliberately distinct from block_defines_vla
// (internal.h), which serialize_stmt.c applies directly to a CANDIDATE wrapper
// itself (deciding whether that specific node needs unbracing); this one
// is applied to the ENCLOSING real scope, which is where a VLA's storage
// must actually stay reachable until, and is the only correct place for
// this design's HMRK/HREL pair to live -- see gen_stmt's ND_BLOCK case
// below and is_decl_group's own comment (cccc.h) for the miscompile this
// avoids (reclaiming a VLA's storage the instant it's declared, before any
// later statement in its real scope that actually uses it has run).
static bool block_needs_heap_mark(Node *blk) {
    if (!blk || blk->kind != ND_BLOCK)
        return false;
    for (Node *s = blk->body; s; s = s->next) {
        if (s->kind == ND_EXPR_STMT && (node_is_vla_ptr_assign(s->lhs) ||
                                        node_is_deferred_vla_ptr_init(s->lhs)))
            return true;
        if (s->kind == ND_BLOCK && s->is_decl_group && block_defines_vla(s))
            return true;
    }
    return false;
}

void gen_stmt(VirtualMachine *vm, Node *node) {
    if (!node)
        return;

    emit_source_location(vm, node);

    switch (node->kind) {
        case ND_BLOCK: {
            int block_scope_id = -1;
            if (vm->flags & CCCC_STACK_INSTR) {
                block_scope_id = vm->current_scope_id++;
                emit_scopein(vm, block_scope_id);
            }

            // #981: heap-reclamation watermark for a VLA-declaring block.
            // cc_heap_reclaim_flags_ok is the flags-only half of the full
            // vm->heap_reclaim_enabled gate -- the other half
            // (!dynobjsz_present) isn't resolvable until after a full text-
            // segment scan that can't run until codegen finishes (see its own
            // comment, cccc.h), so this only prunes the common case (any of
            // -1/-2/-3's flags already forbidding reclamation) rather than
            // being fully authoritative; op_HMRK_fn/op_HREL_fn (ops.c) re-
            // check the complete runtime gate and no-op if it's false.
            //
            // !node->is_decl_group is load-bearing, not defensive: node itself
            // must be a real C block scope, never a declaration()-built
            // synthetic wrapper (see is_decl_group's own comment, cccc.h) --
            // instrumenting a wrapper directly would reclaim its VLA's storage
            // the instant it's declared, before any later statement in the
            // *enclosing* real scope (where block_needs_heap_mark below finds
            // it instead, looking exactly one level through such a wrapper)
            // ever runs. A block that doesn't declare a VLA never gets this
            // pair at all: there's nothing here for a bare alloca in the same
            // block to be endangered by (see ALCA/ALCV's own comments, cccc.h,
            // for why the two need different opcodes and different sweep
            // timing).
            int  heap_mark_depth = -1;
            bool emit_heap_marks = !node->is_decl_group &&
                                   cc_heap_reclaim_flags_ok(vm->flags) &&
                                   block_needs_heap_mark(node);
            if (emit_heap_marks) {
                heap_mark_depth = vm->heap_mark_nest_depth++;
                emit_hmrk(vm, heap_mark_depth);
            }

            // Push a cleanup scope entry if this block declared cleanup vars.
            CleanupScopeEntry  cleanup_entry = {};
            CleanupScopeEntry *saved_cleanup = g_cleanup_scope;
            if (node->cleanup_vars) {
                cleanup_entry.vars  = node->cleanup_vars;
                cleanup_entry.depth = node->cleanup_scope_depth;
                cleanup_entry.outer = g_cleanup_scope;
                g_cleanup_scope     = &cleanup_entry;
            }

            for (Node *n = node->body; n; n = n->next) {
                gen_stmt(vm, n);
            }

            // Emit cleanups at natural block exit (LIFO order).
            if (node->cleanup_vars)
                emit_scope_cleanups(vm, g_cleanup_scope);

            g_cleanup_scope = saved_cleanup;

            // #981: release this block's heap-reclamation watermark on the
            // natural (fall-through) exit path only -- same "LIFO order,
            // natural exit only" scope as the cleanup emission just above. A
            // break/continue/goto/return out of this block skips this HREL;
            // that's by design, see emit_hrel's own callers' comments
            // (HeapMarks, cccc.h) for why a skipped release only forfeits
            // reclamation rather than causing incorrect behavior.
            if (emit_heap_marks) {
                emit_hrel(vm, heap_mark_depth);
                vm->heap_mark_nest_depth--;
            }

            if (block_scope_id >= 0)
                emit_scopeout(vm, block_scope_id);
            return;
        }

        case ND_EXPR_STMT:
            reset_temp_regs();
            gen_expr(vm, node->lhs, REG_ZERO);
            return;

        case ND_RETURN:
            reset_temp_regs();
            if (vm->compiler.inline_exit_name) {
                // Inlining mode: store result to the inline result register,
                // then jump to the shared exit label. Skip LEV3.
                if (node->lhs) {
                    if (is_flonum(node->lhs->ty)) {
                        gen_expr(vm, node->lhs, FREG_A0);
                        emit_fmov3(vm, vm->compiler.inline_result_reg, FREG_A0);
                    } else {
                        gen_expr(vm, node->lhs, vm->compiler.inline_result_reg);
                    }
                }
                emit(vm, JMP);
                add_label_patch(vm->compiler.inline_exit_name,
                                emit_word_ptr(vm), false);
                return;
            }

            // Tail-call optimisation: return f(args) → CALLT instead of
            // CALL+LEV3. Guards: opt >= 1, not inlining, predicate checks
            // FFI/variadic/nested/etc. After gen_expr, pending_tail_callee is
            // set only if CALL was reached; inlining/builtins leave it NULL and
            // we fall through to the LEV3 path. expr_already_eval prevents
            // re-evaluating node->lhs in the LEV3 path below.
            //
            // The parser always wraps the return expression in ND_CAST, even
            // for identity conversions (e.g. int→int).  Strip through those
            // cast wrappers to expose the underlying ND_FUNCALL for
            // can_emit_tail_call, but only while each cast is a representation
            // no-op (return_repr_key/ cast_is_repr_noop, ~line 1350): the
            // callee's own ND_RETURN cast has already normalised its result to
            // the callee's return type, so a no-op cast on top of that is
            // redundant and safe to skip. A cast that genuinely changes
            // representation (#762, e.g. `return (unsigned char) g(...);`
            // truncating a wider result) must NOT be stripped -- CALLT hands
            // the callee's raw value straight to the *original* caller with no
            // opportunity to apply it, so stopping the strip there leaves
            // tco_expr as an ND_CAST, which can_emit_tail_call rejects outright
            // (only ND_FUNCALL is eligible), correctly falling back to the
            // ordinary CALL+LEV3 path below.
            bool  expr_already_eval = false;
            Node *tco_expr          = node->lhs;
            while (tco_expr && tco_expr->kind == ND_CAST && tco_expr->lhs &&
                   cast_is_repr_noop(tco_expr->ty, tco_expr->lhs->ty))
                tco_expr = tco_expr->lhs;
            if (tco_expr && vm->compiler.tail_calls &&
                can_emit_tail_call(vm, tco_expr)) {
                int tco_dest = is_flonum(tco_expr->ty) ? FREG_A0 : REG_A0;
                vm->compiler.emitting_tail_call  = true;
                vm->compiler.pending_tail_callee = NULL;
                gen_expr(vm, tco_expr, tco_dest);
                vm->compiler.emitting_tail_call =
                    false; // belt-and-suspenders; cleared in ND_FUNCALL
                if (vm->compiler.pending_tail_callee) {
                    Obj *tco_fn = vm->compiler.pending_tail_callee;
                    vm->compiler.pending_tail_callee = NULL;
                    if (vm->flags & CCCC_STACK_INSTR)
                        emit_scopeout(vm, vm->current_function_scope_id);
                    emit(vm, CALLT);
                    Pc tco_patch            = emit_word_ptr(vm);
                    vm->text_seg[tco_patch] = 0;
                    PATCH_GROW(vm, call_patches, num_call_patches,
                               call_patches_cap);
                    vm->compiler.call_patches[vm->compiler.num_call_patches]
                        .location = tco_patch;
                    vm->compiler.call_patches[vm->compiler.num_call_patches]
                        .function = tco_fn;
                    vm->compiler.num_call_patches++;
                    return;
                }
                // Inlining/builtin handled the call; result already in
                // tco_dest. Fall through to flush/restore/LEV3, but skip
                // re-evaluating node->lhs.
                expr_already_eval = true;
            }

            if (node->lhs && !expr_already_eval) {
                // If returning struct/union/wide-_BitInt, copy to return buffer
                // at runtime. Wide _BitInt values are address-based (like
                // structs), so returning the raw address would leave a dangling
                // pointer into the callee's torn-down frame once LEV3 runs.
                if (node->lhs->ty && (node->lhs->ty->kind == TY_STRUCT ||
                                      node->lhs->ty->kind == TY_UNION ||
                                      is_wide_bitint(node->lhs->ty) ||
                                      is_decimal(node->lhs->ty))) {
                    // #402: _Decimal32/64/128 is address-based, same dangling-
                    // frame hazard as struct/union/wide-_BitInt above -- the
                    // value's alloc_decimal_temp scratch slot lives in THIS
                    // (callee) frame, so it must be copied into the RETBUF
                    // pool before LEV3 tears the frame down.
                    // Evaluate source (struct address) into a temp register
                    // first
                    int r_src = alloc_temp_reg();
                    gen_expr(vm, node->lhs, r_src);
                    // node->lhs may be a wide-_BitInt expression whose codegen
                    // emits a helper CALLF, which resets the temp allocator's
                    // free list. Re-mark r_src as in-use so the next
                    // alloc_temp_reg() below can't hand out the same register.
                    mark_temp_reg_used(r_src);

                    // Get next buffer from rotating pool at runtime
                    // RETBUF puts the buffer address in REG_A0
                    emit(vm, RETBUF);
                    int r_dest = alloc_temp_reg();
                    emit_mov3(vm, r_dest, REG_A0); // Save buffer address

                    // MCPY uses registers: dest in REG_A0, src in REG_A1, count
                    // in REG_A2 REG_A0 already has dest from RETBUF, but we
                    // saved it to r_dest
                    emit_mov3(vm, REG_A1, r_src); // src = struct address
                    emit_li3(vm, REG_A2, node->lhs->ty->size); // count
                    emit_mov3(vm, REG_A0, r_dest); // dest = buffer address
                    emit(vm, MCPY);

                    // Return buffer address in REG_A0 (already there from
                    // r_dest)
                    emit_mov3(vm, REG_A0, r_dest);

                    free_temp_reg(r_src);
                    free_temp_reg(r_dest);
                } else if (is_vector(node->lhs->ty)) {
                    // Vector return (#714): unlike struct/union, the value
                    // lives in a vregs[] register (gen_vector_expr), not memory
                    // -- there is no source address to MCPY from. Materialize
                    // the value into a vreg, then VSTR it into a fresh RETBUF
                    // buffer and return that buffer's address, mirroring the
                    // struct path.
                    int v_src = alloc_temp_reg();
                    gen_expr(vm, node->lhs,
                             v_src); // vector value -> vregs[v_src]
                    // Belt-and-suspenders as with the struct/wide-_BitInt path
                    // above: node->lhs may contain a nested call that resets
                    // the temp allocator's free list.
                    mark_temp_reg_used(v_src);

                    emit(vm, RETBUF);              // buffer address -> REG_A0
                    int r_dest = alloc_temp_reg();
                    emit_mov3(vm, r_dest, REG_A0); // Save buffer address
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, r_dest, 0);
                    emit_rrs(vm, VSTR, v_src, r_dest, node->lhs->ty->size);
                    emit_mov3(vm, REG_A0, r_dest); // Return buffer address

                    free_temp_reg(v_src);
                    free_temp_reg(r_dest);
                } else if (is_flonum(node->lhs->ty)) {
                    gen_expr(vm, node->lhs, FREG_A0);
                } else {
                    gen_expr(vm, node->lhs, REG_A0);
                }
            }
            // Emit cleanup calls for all active scopes before returning (LIFO,
            // innermost first). If non-void, preserve the return value across
            // cleanup calls (they clobber REG_A0).
            if (g_cleanup_scope) {
                bool is_void_ret =
                    !node->lhs ||
                    (node->lhs->ty && node->lhs->ty->kind == TY_VOID);
                bool is_float_ret =
                    !is_void_ret && node->lhs && is_flonum(node->lhs->ty);
                Obj *cur_fn = vm->compiler.current_fn;
                if (!is_void_ret) {
                    if (!is_float_ret) {
                        // Integer/pointer/struct-addr return: save via stack
                        // push
                        emit_psh3(vm, REG_A0);
                    } else {
                        // Float/double return: save to synthetic stack slot
                        emit_ri(vm, FSTR_LOCAL, FREG_A0,
                                cur_fn->cleanup_fp_retval_offset);
                    }
                }
                emit_cleanups_to_depth(vm, 0);
                if (!is_void_ret) {
                    if (!is_float_ret) {
                        emit_pop3(vm, REG_A0);
                    } else {
                        emit_ri(vm, FLDR_LOCAL, FREG_A0,
                                cur_fn->cleanup_fp_retval_offset);
                    }
                }
            }

            // Deactivate function-level scope before returning.
            if (vm->flags & CCCC_STACK_INSTR)
                emit_scopeout(vm, vm->current_function_scope_id);
            emit(vm, LEV3);
            return;

        case ND_IF: {
            reset_temp_regs();
            int r_cond = alloc_temp_reg();
            gen_cond_expr(vm, node->cond, r_cond);
            Pc jz_else = emit_jz3(vm, r_cond);
            free_temp_reg(r_cond);

            gen_stmt(vm, node->then);

            if (node->els) {
                emit(vm, JMP);
                Pc jmp_end            = emit_word_ptr(vm);
                vm->text_seg[jz_else] = vm->text_ptr + 1;
                gen_stmt(vm, node->els);
                vm->text_seg[jmp_end] = vm->text_ptr + 1;
            } else {
                vm->text_seg[jz_else] = vm->text_ptr + 1;
            }
            return;
        }

        case ND_FOR: {
            // Init
            if (node->init) {
                gen_stmt(vm, node->init);
            }

            Pc loop_start = vm->text_ptr + 1;

            // Condition
            Pc jz_end = CCCC_INVALID_PC;
            if (node->cond) {
                reset_temp_regs();
                int r_cond = alloc_temp_reg();
                gen_cond_expr(vm, node->cond, r_cond);
                jz_end = emit_jz3(vm, r_cond);
                free_temp_reg(r_cond);
            }

            // Body
            gen_stmt(vm, node->then);

            // Define continue label (jumps to increment)
            if (node->cont_label) {
                define_label(vm, node->cont_label);
            }

            // Increment
            if (node->inc) {
                reset_temp_regs();
                gen_expr(vm, node->inc, REG_ZERO);
            }

            // Jump back to start
            emit(vm, JMP);
            emit_word(vm, loop_start);

            // Define break label (jumps past loop)
            if (node->brk_label) {
                define_label(vm, node->brk_label);
            }

            // Patch exit
            if (jz_end != CCCC_INVALID_PC) {
                vm->text_seg[jz_end] = vm->text_ptr + 1;
            }
            return;
        }

        case ND_DO: {
            Pc loop_start = vm->text_ptr + 1;

            gen_stmt(vm, node->then);

            // Define continue label (jumps to condition)
            if (node->cont_label) {
                define_label(vm, node->cont_label);
            }

            reset_temp_regs();
            int r_cond = alloc_temp_reg();
            gen_cond_expr(vm, node->cond, r_cond);
            Pc jnz_start            = emit_jnz3(vm, r_cond);
            vm->text_seg[jnz_start] = loop_start;
            free_temp_reg(r_cond);

            // Define break label (jumps past loop)
            if (node->brk_label) {
                define_label(vm, node->brk_label);
            }
            return;
        }

        case ND_SWITCH: {
            reset_temp_regs();
            int r_val = alloc_temp_reg();
            gen_expr(vm, node->cond, r_val);

            int              num_cases      = 0;
            long             min_case       = 0;
            long             max_case       = 0;
            long             covered_values = 0;
            SwitchCasePatch *cases          = collect_switch_cases(
                node, &num_cases, &min_case, &max_case, &covered_values);
            long      span           = num_cases ? max_case - min_case + 1 : 0;
            bool      use_jump_table = num_cases > 0 && covered_values >= 4 &&
                                       span <= covered_values * 2;

            Pc        default_patch  = CCCC_INVALID_PC;
            Pc        end_patch      = CCCC_INVALID_PC;
            Pc        table_start    = CCCC_INVALID_PC;
            PatchList fail_patches   = {};

            if (num_cases == 0) {
                emit(vm, JMP);
                if (node->default_case)
                    default_patch = emit_word_ptr(vm);
                else
                    end_patch = emit_word_ptr(vm);
            } else if (use_jump_table) {
                emit_addi3(vm, REG_A0, r_val, -min_case);
                emit(vm, JMPT);
                Pc table_operand = emit_word_ptr(vm);
                emit_word(vm, (InstrWord)span);
                default_patch               = emit_word_ptr(vm);
                table_start                 = vm->text_ptr + 1;
                vm->text_seg[table_operand] = table_start;
                for (long i = 0; i < span; i++)
                    emit_word(vm, 0);

                for (int i = 0; i < num_cases; i++) {
                    for (long value = cases[i].begin; value <= cases[i].end;
                         value++) {
                        Pc entry = table_start + (Pc)(value - min_case);
                        vm->text_seg[entry]  = CCCC_INVALID_PC;
                        cases[i].table_entry = entry;
                    }
                }
            } else {
                int r_cmp = alloc_temp_reg();
                emit_sparse_switch_tree(vm, cases, 0, num_cases - 1, r_val,
                                        r_cmp, &fail_patches);
                free_temp_reg(r_cmp);
                if (node->default_case) {
                    emit(vm, JMP);
                    default_patch = emit_word_ptr(vm);
                }
            }

            void *saved_cases         = vm->compiler.current_switch_cases;
            int   saved_num_cases     = vm->compiler.current_switch_num;
            Pc    saved_table_start   = vm->compiler.current_switch_table_start;
            long  saved_switch_min    = vm->compiler.current_switch_min;
            long  saved_switch_size   = vm->compiler.current_switch_size;
            Node *saved_default       = vm->compiler.current_switch_default;
            Pc    saved_default_patch = vm->compiler.current_default_patch;

            vm->compiler.current_switch_cases       = cases;
            vm->compiler.current_switch_num         = num_cases;
            vm->compiler.current_switch_table_start = table_start;
            vm->compiler.current_switch_min         = min_case;
            vm->compiler.current_switch_size        = span;
            vm->compiler.current_switch_default     = node->default_case;
            vm->compiler.current_default_patch      = default_patch;

            gen_stmt(vm, node->then);

            vm->compiler.current_switch_cases       = saved_cases;
            vm->compiler.current_switch_num         = saved_num_cases;
            vm->compiler.current_switch_table_start = saved_table_start;
            vm->compiler.current_switch_min         = saved_switch_min;
            vm->compiler.current_switch_size        = saved_switch_size;
            vm->compiler.current_switch_default     = saved_default;
            vm->compiler.current_default_patch      = saved_default_patch;

            if (node->brk_label) {
                define_label(vm, node->brk_label);
            }
            Pc end_target = vm->text_ptr + 1;
            if (end_patch != CCCC_INVALID_PC) {
                vm->text_seg[end_patch] = end_target;
            }
            if (!node->default_case && default_patch != CCCC_INVALID_PC) {
                vm->text_seg[default_patch] = end_target;
            }
            for (int i = 0; i < fail_patches.len; i++) {
                vm->text_seg[fail_patches.items[i]] =
                    node->default_case ? vm->text_seg[default_patch]
                                       : end_target;
            }
            if (use_jump_table) {
                Pc default_target = node->default_case
                                        ? vm->text_seg[default_patch]
                                        : end_target;
                for (long i = 0; i < span; i++) {
                    Pc entry = table_start + (Pc)i;
                    if (vm->text_seg[entry] == 0)
                        vm->text_seg[entry] = default_target;
                }
            }

            free(fail_patches.items);
            free_switch_cases(cases, num_cases);
            free_temp_reg(r_val);
            return;
        }

        case ND_CASE: {
            // Case within switch - patch jump address and generate body
            Pc target = vm->text_ptr + 1;

            // Check if this is the default case
            if (node == vm->compiler.current_switch_default) {
                if (vm->compiler.current_default_patch != CCCC_INVALID_PC) {
                    vm->text_seg[vm->compiler.current_default_patch] = target;
                }
            } else {
                SwitchCasePatch *cases =
                    (SwitchCasePatch *)vm->compiler.current_switch_cases;
                SwitchCasePatch *entry = find_switch_case(
                    cases, vm->compiler.current_switch_num, node);
                if (entry) {
                    if (vm->compiler.current_switch_table_start !=
                        CCCC_INVALID_PC) {
                        for (long value = entry->begin; value <= entry->end;
                             value++) {
                            Pc table_entry =
                                vm->compiler.current_switch_table_start +
                                (Pc)(value - vm->compiler.current_switch_min);
                            vm->text_seg[table_entry] = target;
                        }
                    }
                    for (int i = 0; i < entry->num_patches; i++) {
                        vm->text_seg[entry->patches[i]] = target;
                    }
                }
            }

            // Generate the body of this case
            gen_stmt(vm, node->lhs);
            return;
        }

        case ND_GOTO:
            // Emit cleanup calls for any scopes being exited, then jump.
            // By the time codegen runs, resolve_goto_labels has already set
            // unique_label and cleanup_target_depth on all gotos (named, break,
            // continue), so there is only one path here.
            if (node->unique_label) {
                if (g_cleanup_scope)
                    emit_cleanups_to_depth(vm, node->cleanup_target_depth);
                emit(vm, JMP);
                Pc patch            = emit_word_ptr(vm);
                vm->text_seg[patch] = 0;
                add_label_patch(node->unique_label, patch, false);
            }
            return;

        case ND_LABEL:
            // Named label statement - define the label and generate the body
            if (node->unique_label) {
                define_label(vm, node->unique_label);
            } else if (node->label) {
                define_label(vm, node->label);
            }
            gen_stmt(vm, node->lhs);
            return;

        case ND_ASM:
            if (vm->compiler.asm_callback)
                vm->compiler.asm_callback(vm, node->asm_str,
                                          vm->compiler.asm_user_data);
            else if (vm->compiler.asm_passthru)
                cccc_default_asm_passthru(vm, node->asm_str);
            // else: no-op (default behavior)
            return;

        case ND_GOTO_EXPR: {
            // Computed goto: goto *expr
            // Evaluate expression to get target address into a register
            reset_temp_regs();
            int r_target = alloc_temp_reg();
            gen_expr(vm, node->lhs, r_target);
            // Emit JMPI - jump indirect to address in register
            emit(vm, JMPI);
            emit_word(vm, ENCODE_R(r_target));
            free_temp_reg(r_target);
            return;
        }

        default:
            error_tok(vm, node->tok,
                      "codegen: unsupported statement node kind %d",
                      node->kind);
    }
}

static bool fn_has_any_cleanup_vars(Obj *fn) {
    for (Obj *v = fn->locals; v; v = v->next)
        if (v->cleanup_fn)
            return true;
    return false;
}

// Assign stack offsets for parameters and locals
// Returns the total stack size (aligned to 16 bytes)
int assign_stack_offsets(VirtualMachine *vm, Obj *fn) {
    if (!fn->is_function)
        return 0;

    int param_count = 0;
    for (Obj *param = fn->params; param; param = param->next) {
        param_count++;
    }

    // For variadic functions, copy all 8 potential arg registers so va_arg can
    // consume any register-passed variadic tail. Fixed params beyond 8 need
    // their own local slots too.
    bool is_variadic       = fn->ty && fn->ty->is_variadic;
    int  spill_param_count = is_variadic && param_count < 8 ? 8 : param_count;

    // When stack canaries are enabled, ENT3 reserves bp[-1] for the canary and
    // spills params/locals one slot lower (bp[-2] downward). Bake that one-slot
    // shift into the assigned offsets so codegen reads where ENT3 wrote (#445).
    int canary_bias = (vm->flags & CCCC_STACK_CANARIES) ? 1 : 0;

    // Stack size starts with space for parameters (at negative offsets), plus
    // the reserved canary slot when enabled.
    int stack_size = spill_param_count + canary_bias;

    // Assign parameter offsets (negative). Without canaries: bp[-1], bp[-2],
    // ... With canaries: bp[-2], bp[-3], ... (bp[-1] is the canary).
    int param_offset = -1 - canary_bias;
    for (Obj *param = fn->params; param; param = param->next) {
        param->offset   = param_offset;
        param->is_local = true;
        param->is_param = true;
        param_offset--;
    }

    // Assign local variable offsets (negative, after params)
    for (Obj *var = fn->locals; var; var = var->next) {
        // Check if this is a parameter (params are also in locals list)
        bool is_param = false;
        for (Obj *p = fn->params; p; p = p->next) {
            if (p == var) {
                is_param = true;
                break;
            }
        }

        // Skip builtin variables (va_area and alloca_bottom) and params.
        bool is_builtin = (var == fn->va_area) || (var == fn->alloca_bottom);

        if (!is_param && !is_builtin) {
            // Calculate how many slots this variable needs
            int var_size = 1;
            if (var->ty->kind == TY_ARRAY) {
                var_size = (var->ty->size + 7) / 8;
            } else if (var->ty->kind == TY_VLA) {
                var_size = 1;
            } else if (var->ty->kind == TY_STRUCT ||
                       var->ty->kind == TY_UNION ||
                       var->ty->kind == TY_COMPLEX ||
                       var->ty->kind == TY_VECTOR ||
                       (var->ty->kind == TY_BITINT && var->ty->size > 8) ||
                       (is_decimal(var->ty) && var->ty->size > 8)) {
                // #402: _Decimal128 is 16 bytes (2 words) -- _Decimal32/64
                // fit the default 1-word slot, same as float/double do.
                var_size = (var->ty->size + 7) / 8;
            }
            stack_size  += var_size;
            var->offset  = -stack_size;
        }
    }

    // Allocate a float retval save slot if this function has cleanup vars and
    // a floating-point return type. Used to preserve FREG_A0 across cleanup
    // calls.
    if (fn_has_any_cleanup_vars(fn) && fn->ty && fn->ty->return_ty &&
        is_flonum(fn->ty->return_ty)) {
        stack_size++;
        fn->cleanup_fp_retval_offset = -stack_size;
    }

    // Ensure 16-byte stack alignment
    if (stack_size % 2 != 0) {
        stack_size++;
    }
    return stack_size;
}
