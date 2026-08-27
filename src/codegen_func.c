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

// ========== Function Generation ==========

void gen_function(VirtualMachine *vm, Obj *fn) {
    if (!fn->is_function || !fn->body)
        return;

    // Set current function context for nested function checks (e.g. in
    // gen_addr)
    vm->compiler.current_fn = fn;

    // Reset cleanup scope stack for this function
    g_cleanup_scope = NULL;

    // Reset inlining context for this function
    vm->compiler.inline_exit_name = NULL;
    vm->compiler.ent3_extra_stack = 0;

    // Reset lazy frame-epoch tracking for this function (#703).
    vm->compiler.frame_has_esc_agg    = false;
    vm->compiler.frame_has_esc_scalar = false;

    // Reset label tracking for this function
    reset_labels();

    // Count parameters first
    // Assign stack offsets early
    int stack_size      = assign_stack_offsets(vm, fn);
    int base_stack_size = stack_size;
    prepare_local_promotion(vm, fn, base_stack_size);
    prepare_fp_local_promotion(
        vm, fn, base_stack_size); // must follow prepare_local_promotion
    prepare_restrict_cache(vm, fn, base_stack_size);
    stack_size += vm->compiler.promoted_count + vm->compiler.fp_promoted_count +
                  vm->compiler.restrict_cache_capacity;
    if (stack_size % 2 != 0)
        stack_size++;

    // Helper vars needed for ENT3 emission
    int param_count = 0;
    for (Obj *param = fn->params; param; param = param->next)
        param_count++;
    bool is_variadic       = fn->ty && fn->ty->is_variadic;
    int  spill_param_count = is_variadic && param_count < 8 ? 8 : param_count;

    // Record source location for function entry
    emit_source_location(vm, fn->body);

    // Record function address (offset from text_seg start)
    fn->code_addr = vm->text_ptr + 1;

    // Compute float parameter masks for ENT3
    unsigned int float_param_mask = 0;
    unsigned int f32_param_mask   = 0;
    int          pindex           = 0;
    for (Obj *param = fn->params; param && pindex < 8;
         param      = param->next, pindex++) {
        if (is_flonum(param->ty)) {
            float_param_mask |= (1u << pindex);
            if (param->ty->kind == TY_FLOAT)
                f32_param_mask |= (1u << pindex);
        }
    }

    // Assign a function-level scope ID for stack instrumentation.
    int fn_scope_id               = vm->current_scope_id++;
    vm->current_function_scope_id = fn_scope_id;

    // Register all params and locals in the variable metadata map.
    for (Obj *param = fn->params; param; param = param->next) {
        add_stack_var_meta(vm, param->name, param->offset, param->ty,
                           fn_scope_id);
        add_debug_symbol(vm, param->name, param->offset, param->ty, 1, fn);
    }
    for (Obj *var = fn->locals; var; var = var->next) {
        bool is_param = false;
        for (Obj *p = fn->params; p; p = p->next)
            if (p == var) {
                is_param = true;
                break;
            }
        bool is_builtin = (var == fn->va_area) || (var == fn->alloca_bottom);
        if (!is_param && !is_builtin) {
            add_stack_var_meta(vm, var->name, var->offset, var->ty,
                               fn_scope_id);
            add_debug_symbol(vm, var->name, var->offset, var->ty, 1, fn);
        }
    }

    // Emit ENT3: [stack_size:32|param_count:32] [f32_mask:32|float_mask:32]
    long long ent3_operand =
        ((long long)stack_size) | (((long long)spill_param_count) << 32);
    long long ent3_masks =
        (long long)float_param_mask | ((long long)f32_param_mask << 32);
    emit(vm, ENT3);
    vm->compiler.ent3_stack_loc   = emit_i64(vm, ent3_operand);
    vm->compiler.ent3_masks_loc   = emit_i64(vm, ent3_masks);
    vm->compiler.ent3_base_stack  = stack_size;
    vm->compiler.ent3_extra_stack = 0;

    // Activate function-level scope (marks params/locals as alive).
    if (vm->flags & CCCC_STACK_INSTR)
        emit_scopein(vm, fn_scope_id);

    emit_save_promoted_registers(vm);
    emit_save_fp_promoted_registers(vm);
    emit_save_restrict_cache_regs(vm);
    emit_init_promoted_params(vm);
    emit_init_fp_promoted_params(vm);

    // Mark parameters initialized (they arrive via registers).
    if (vm->flags & CCCC_UNINIT_DETECTION) {
        for (Obj *param = fn->params; param; param = param->next)
            emit_marki(vm, param->offset);
    }

    // Allocate heap storage for __block variables
    // Each __block variable gets heap allocation of its type's size
    // The heap pointer is stored in the variable's stack slot
    for (Obj *var = fn->locals; var; var = var->next) {
        if (var->is_block_var) {
            // Allocate heap memory for this __block variable via ALCB, not
            // MALC (#979) -- the box is compiler-internal storage the guest
            // never frees directly, so it must not appear in a leak report.
            // ALCB (not ALCA -- #981's prerequisite) has MALC's exact
            // register shape (size=A0, result=A0), but tags the AllocHeader
            // as ALLOC_KIND_BLOCK_BOX rather than ALLOC_KIND_FRAME, since a
            // __block box may legitimately outlive this frame (Block_copy)
            // while alloca/VLA storage never does.
            emit_li3(vm, REG_A0, var->ty->size);
            emit(vm, ALCB);
            // Store the heap pointer in the variable's stack slot
            int r_addr = alloc_temp_reg();
            // Slot address only feeds the immediate store below (#676).
            emit_lea3_internal(vm, r_addr,
                               var->offset);    // Address of stack slot
            emit_rr(vm, STR_D, REG_A0, r_addr); // Store heap pointer in slot
            free_temp_reg(r_addr);
        }
    }

    // #1078: a struct/union-by-value parameter's slot holds a pointer to
    // the CALLER's own addressable storage (gen_addr's by-pointer aggregate
    // param branch, codegen_addr.c) -- nothing ever copied it, so a write
    // through the parameter inside this function silently aliased the
    // caller's argument for this function's whole lifetime. Every real
    // host C compiler, and -c=native, copies the argument; only the VM's
    // own calling convention didn't. Fixed here, once, in the callee's own
    // prologue (rather than at each caller-side arg-passing site, #714's
    // vector/decimal shape): copy each struct/union param into a fresh
    // frame-local scratch slot and rebind the param's own slot to point at
    // the copy instead of the caller's object. Every downstream reader
    // (gen_addr's by-pointer param branch, addr_is_local_frame, the
    // serializer, debug metadata) already treats the slot as "a pointer to
    // the value" and is unaffected -- only the pointee moves into this
    // frame. Safe to clobber A0-A2 for the MCPY here: ENT3 has already
    // spilled every argument register into its own slot by this point.
    for (Obj *param = fn->params; param; param = param->next) {
        if ((param->ty->kind != TY_STRUCT && param->ty->kind != TY_UNION) ||
            param->ty->size <= 0)
            continue;
        long long copy_off =
            alloc_wide_bitint_temp(vm, (param->ty->size + 7) / 8);
        int r_src = alloc_temp_reg();
        // Slot address only feeds the immediate pointer load below (#676).
        emit_lea3_internal(vm, r_src, param->offset);
        emit_rr(vm, LDR_D, r_src, r_src);         // caller's object address
        emit_lea3_internal(vm, REG_A0, copy_off); // copy dest
        emit_mov3(vm, REG_A1, r_src);             // copy src
        emit_li3(vm, REG_A2, param->ty->size);    // copy count
        emit(vm, MCPY);                           // clobbers A0-A2
        free_temp_reg(r_src);
        // The copy is a genuine new object living at [copy_off,
        // copy_off+size) in this frame -- if the original param's address
        // was ever observed to escape (Obj->addr_escapes, set by
        // mark_addr_escapes() over the whole function body before codegen
        // runs, params included), mirror emit_lea3_var's own STKTAG
        // treatment of an escaping aggregate local so an interior pointer
        // derived from &param (e.g. __builtin_dynamic_object_size, #648) can
        // still resolve against this frame's epoch. Without this, the copy
        // is invisible to stack_interval_stab even though &param now points
        // squarely inside it.
        if (param->addr_escapes) {
            emit_stktag(vm, copy_off, param->ty->size);
            vm->compiler.frame_has_esc_agg = true;
        }
        // Rebind the param's slot to point at the copy, not the caller's
        // object. emit_lea3_internal(copy_off) is recomputed since MCPY
        // clobbered A0.
        int r_dst = alloc_temp_reg();
        emit_lea3_internal(vm, r_dst, copy_off);
        int r_slot = alloc_temp_reg();
        emit_lea3_internal(vm, r_slot, param->offset);
        emit_rr(vm, STR_D, r_dst, r_slot);
        free_temp_reg(r_slot);
        free_temp_reg(r_dst);
    }

    // Generate function body
    gen_stmt(vm, fn->body);

    // Patch ENT3 stack size if inlining added local variables
    if (vm->compiler.ent3_extra_stack > 0) {
        int new_stack =
            vm->compiler.ent3_base_stack + vm->compiler.ent3_extra_stack;
        if (new_stack % 2 != 0)
            new_stack++;
        long long new_operand =
            ((long long)new_stack) | (((long long)spill_param_count) << 32);
        vm->text_seg[vm->compiler.ent3_stack_loc]     = cc_i64_lo(new_operand);
        vm->text_seg[vm->compiler.ent3_stack_loc + 1] = cc_i64_hi(new_operand);
    }

    // Patch ENT3 masks with lazy frame-epoch push bits (#703), now that the
    // whole body has been generated and emit_lea3_var has recorded whether
    // this function owns an escaping local/param.
    if (vm->compiler.frame_has_esc_agg || vm->compiler.frame_has_esc_scalar) {
        // Pack into an unsigned long long: ENT3_PUSH_EPOCH_AGG/SCALAR both
        // carry bit 31 set (0x80000000), and shifting that into a *signed*
        // long long's top half is UB (signed-left-shift-into-sign-bit).
        // Compute unsigned, cast to long long only at the end -- same bit
        // pattern, no UB (#739).
        unsigned long long new_masks =
            (unsigned long long)float_param_mask |
            ((unsigned long long)f32_param_mask << 32);
        if (vm->compiler.frame_has_esc_agg)
            new_masks |= (unsigned long long)ENT3_PUSH_EPOCH_AGG;
        if (vm->compiler.frame_has_esc_scalar)
            new_masks |= ((unsigned long long)ENT3_PUSH_EPOCH_SCALAR << 32);
        vm->text_seg[vm->compiler.ent3_masks_loc] =
            cc_i64_lo((long long)new_masks);
        vm->text_seg[vm->compiler.ent3_masks_loc + 1] =
            cc_i64_hi((long long)new_masks);
    }

    // Patch all forward jumps (break/continue/goto)
    patch_labels(vm);

    // Implicit return 0 from entry function
    const char *entry_fn =
        vm->compiler.entry_name ? vm->compiler.entry_name : "main";
    if (strncmp(fn->name, entry_fn, strlen(entry_fn) + 1) == 0) {
        emit_li3(vm, REG_A0, 0);
    }
    emit_flush_promoted_locals(vm);
    emit_flush_fp_promoted_locals(vm);
    // Deactivate function scope (for fall-through returns).
    if (vm->flags & CCCC_STACK_INSTR)
        emit_scopeout(vm, fn_scope_id);
    emit_restore_restrict_cache_regs(vm);
    emit_restore_fp_promoted_registers(vm);
    emit_restore_promoted_registers(vm);
    emit(vm, LEV3);
    fn->code_end_addr = vm->text_ptr + 1;
}

// ========== Top-Level Code Generation ==========

// Sort __attribute__((constructor))/((destructor)) entries in place.
// Functions with no explicit priority (CCCC_NO_INIT_PRIORITY) form the
// default-priority group, which GCC runs last among constructors and first
// among destructors relative to explicitly prioritised ones — modelled here
// by substituting INT_MAX for the missing priority before sorting.
// ascending=true sorts lowest-priority-first (constructors); ascending=false
// sorts highest-priority-first (destructors — the reverse order), with the
// seq tie-break reversed too so same-priority destructors unwind in reverse
// declaration order.
static int init_entry_effective_priority(const CCCCInitEntry *e) {
    return e->priority == CCCC_NO_INIT_PRIORITY ? INT_MAX : e->priority;
}

static void sort_init_entries(CCCCInitEntry *list, int count, bool ascending) {
    // Simple insertion sort: these lists are tiny (a handful of functions),
    // and seq guarantees a strict order so no comparator ties are possible.
    for (int i = 1; i < count; i++) {
        CCCCInitEntry key     = list[i];
        int           key_pri = init_entry_effective_priority(&key);
        int           j       = i - 1;
        while (j >= 0) {
            int  cur_pri = init_entry_effective_priority(&list[j]);
            bool key_before_cur =
                ascending ? (key_pri < cur_pri ||
                             (key_pri == cur_pri && key.seq < list[j].seq))
                          : (key_pri > cur_pri ||
                             (key_pri == cur_pri && key.seq > list[j].seq));
            if (!key_before_cur)
                break;
            list[j + 1] = list[j];
            j--;
        }
        list[j + 1] = key;
    }
}

// Allocate the RETBUF rotating pool (struct/union/vector/wide-_BitInt
// returns) at the current end of the data segment. Idempotent -- guarded on
// slot 0 being unset, the same check src/bytecode.c's incremental-cache
// adoption path (~line 1354) already uses for this resource -- so it is
// safe to call from both gen()'s whole-program pass and
// cc_repl_compile_new()'s incremental pass without double-allocating.
// cc_repl_compile_new() must call this too: before #666 it never did, so
// any REPL expression (or debugger conditional-breakpoint expression, which
// shares cc_repl_compile_new) returning a struct/union/vector hit "return
// buffer pool was not rehydrated" at RETBUF-execution time.
static void alloc_return_buffer_pool(VirtualMachine *vm) {
    if (vm->compiler.return_buffer_pool[0] != NULL)
        return;
    for (int i = 0; i < RETURN_BUFFER_POOL_SIZE; i++) {
        // #1136: align to CCCC_MAX_DATA_ALIGN, not a hardcoded 8 -- this
        // pool backs by-value struct/union/vector/wide-_BitInt returns, and
        // a vector return (#722, up to 64-byte alignment) needs the wider
        // boundary just as much as a global of the same type would.
        long long offset = vm->data_ptr - vm->data_seg;
        offset           = (offset + (CCCC_MAX_DATA_ALIGN - 1)) &
                           ~(long long)(CCCC_MAX_DATA_ALIGN - 1);
        check_data_capacity(vm, offset + vm->compiler.return_buffer_size);
        vm->data_ptr                          = vm->data_seg + offset;
        vm->compiler.return_buffer_pool[i]    = vm->data_ptr;
        vm->compiler.return_buffer_offsets[i] = offset;
        memset(vm->compiler.return_buffer_pool[i], 0,
               vm->compiler.return_buffer_size);
        vm->data_ptr += vm->compiler.return_buffer_size;
    }
}

void gen(VirtualMachine *vm, Obj *prog) {
    // Reset patch counters
    vm->compiler.num_call_patches      = 0;
    vm->compiler.num_func_addr_patches = 0;
    vm->compiler.num_data_relocs       = 0;

    // Reset the persistent global label map (populated by define_label during
    // function codegen; consumed by apply_global_relocations for &&label
    // static initialisers).  We reuse any previously allocated buffer.
    num_global_labels = 0;

    // Initialize text pointer - text_seg[0] is reserved for main entry point
    vm->text_ptr = 0;

    // Initialize global variables in data segment (TLS vars go into
    // tls_template) Zero the template so uninitialised TLS vars start at 0
    // across recompiles.
    if (vm->tls_template_cap > 0)
        memset(vm->tls_template, 0, vm->tls_template_cap);
    vm->tls_template_size = 0;
    for (Obj *var = prog; var; var = var->next) {
        if (!var->is_function) {
            if (var->is_tls) {
                // Thread-local variable: allocate in tls_template. #1136:
                // rounded to the variable's own effective alignment (an
                // explicit _Alignas overriding its type's), not a hardcoded
                // 8 -- see cc_effective_align()'s own comment. The
                // per-thread base this offset is later added to
                // (vm->current_tls_seg / pthread.c's rec->tls_seg) is
                // aligned to match, see vm.c/pthread.c.
                int    align = cc_effective_align(var->align, var->ty->align);
                size_t tls_offset =
                    (vm->tls_template_size + (size_t)(align - 1)) &
                    ~(size_t)(align - 1);
                check_tls_capacity(vm, tls_offset + (size_t)var->ty->size);
                var->offset = (long long)tls_offset;
                if (var->init_data)
                    memcpy(vm->tls_template + tls_offset, var->init_data,
                           (size_t)var->ty->size);
                vm->tls_template_size = tls_offset + (size_t)var->ty->size;
            } else {
                // #1136: align data pointer to the variable's own effective
                // alignment, not a hardcoded 8 -- see cc_effective_align().
                int align = cc_effective_align(var->align, var->ty->align);
                long long offset = vm->data_ptr - vm->data_seg;
                offset = (offset + (align - 1)) & ~(long long)(align - 1);
                check_data_capacity(vm, offset + var->ty->size);
                vm->data_ptr = vm->data_seg + offset;

                // Store the offset in the variable
                var->offset = vm->data_ptr - vm->data_seg;
                add_debug_symbol(vm, var->name, var->offset, var->ty, 0, NULL);

                // Copy init_data if present
                if (var->init_data) {
                    memcpy(vm->data_ptr, var->init_data, var->ty->size);
                }

                vm->data_ptr += var->ty->size;
            }
        }
    }

    // #957: propagate the canonical global's data-segment offset (and
    // is_tls, which gen_addr also branches on for global loads/stores) onto
    // every non-canonical alias Obj that cc_link_progs left behind in a
    // non-canonical translation unit's own AST. Must run here: after the
    // allocation loop above (which is what assigns var->offset in the first
    // place) and before function codegen begins (gen_addr bakes offsets in
    // as immediates, so any alias reference compiled before this point
    // would still get the wrong/zero offset).
    for (int i = 0; i < vm->compiler.global_aliases_count; i++) {
        Obj *alias     = vm->compiler.global_aliases[i].alias;
        Obj *canonical = vm->compiler.global_aliases[i].canonical;
        alias->offset  = canonical->offset;
        alias->is_tls  = canonical->is_tls;
    }

    // Allocate return buffer pool for struct/union returns at end of data
    // segment
    alloc_return_buffer_pool(vm);

    // Pre-pass: Assign stack offsets for all functions
    // This is critical for nested functions, which are compiled before their
    // parents but need to access parent's variables (which need assigned
    // offsets)
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->is_function && fn->is_definition) {
            assign_stack_offsets(vm, fn);
        }
    }

    // First pass: Generate code for all live functions
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body) {
            if (fn->is_inline && fn->is_static && !fn->is_live)
                continue;
            gen_function(vm, fn);
        }
    }

    // Free per-function local_set hash tables built lazily by
    // belongs_to_outer_function during the pass above. (#165)
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->local_set_built) {
            hashmap_deinit_borrowed(&fn->local_set);
            fn->local_set_built = false;
        }
    }

    HashMap fn_defs = {};
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body && !fn->is_static)
            hashmap_put(&fn_defs, fn->name, fn);
    }

    // Second pass: Patch function call addresses
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj        *target  = vm->compiler.call_patches[i].function;
        const char *fn_name = obj_external_name(target);
        Pc          loc     = vm->compiler.call_patches[i].location;

        Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);

        if (!fn_def) {
            // Check for FFI function
            int ffi_idx = find_ffi_function(vm, fn_name);
            if (ffi_idx >= 0) {
                // FFI - not handled via CALL, skip
                continue;
            }
            // Under -c/--compile (any format), defer the undefined-function
            // error: the native backend hands the call off to the host C
            // compiler/linker, which is the one that actually knows whether
            // the symbol exists.
            if (vm->compiler.compile_only && fn_name)
                continue;
            error("undefined function: %s", fn_name);
        }

        vm->text_seg[loc] = (Pc)fn_def->code_addr;
    }

    // Third pass: Patch function address references (for function pointers)
    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *target = vm->compiler.func_addr_patches[i].function;
        Pc   loc    = vm->compiler.func_addr_patches[i].location;

        Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);

        if (fn_def) {
            cc_write_i64_at(vm, loc,
                            cc_pc_to_byte_offset((Pc)fn_def->code_addr));
        } else {
            const char *fn_name = obj_external_name(target);
            int         ffi_idx = find_ffi_function(vm, fn_name);
            if (ffi_idx >= 0) {
                // FFI function used as a value: store token so CALLN can
                // call it. (JMPI, the other indirect-control-flow opcode,
                // carries no callsite metadata and is only ever emitted for
                // computed goto -- landing on this token there is a runtime
                // error, not a call.)
                cc_write_i64_at(vm, loc, CCCC_FFI_TOKEN_BASE - ffi_idx);
            }
            // else: under -c/--compile, deferred to the host C
            // compiler/linker, same as the call-patch pass above; otherwise
            // the parser already rejects address-of-undeclared.
        }
    }

    hashmap_deinit(&fn_defs);

    // #957: cross-TU reference roll-up. A reference compiled in a
    // *non-defining* translation unit marks the alias Obj (gen_addr sets
    // is_referenced on whichever Obj the AST node points at), but aliases
    // are dropped from `prog` by cc_link_progs and so are invisible to the
    // fourth pass below. Fold each alias's mark onto its canonical Obj
    // before checking. Must run after function codegen (gen_addr has by now
    // run for every reference) and before the check.
    for (int i = 0; i < vm->compiler.global_aliases_count; i++) {
        Obj *alias                = vm->compiler.global_aliases[i].alias;
        Obj *canonical            = vm->compiler.global_aliases[i].canonical;
        canonical->is_referenced |= alias->is_referenced;
    }

    // Fourth pass: a global variable that is referenced but never defined
    // (mirrors the "undefined function" check above -- see #957). Suppressed
    // under -c/--compile for the same reason as the function case: the host
    // C compiler/linker is the one that actually knows whether the symbol
    // exists; a real miss surfaces as a link-time/runtime failure instead.
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function || !var->is_referenced)
            continue;
        if (var->is_definition || var->is_tentative || var->init_data)
            continue;
        if (vm->compiler.compile_only)
            continue;
        error("undefined global: %s", obj_external_name(var));
    }

    apply_global_relocations(vm, prog);

    // Release the persistent label map; it is no longer needed after relocs are
    // applied.  Keeping this explicit rather than leaking it into the process
    // lifetime, as the repo is leak-paranoid.
    free(global_label_map);
    global_label_map  = NULL;
    global_labels_cap = 0;
    num_global_labels = 0;

    // Collect __attribute__((constructor)) / ((destructor)) functions.
    // gen() is a whole-program pass (re-run for REPL/incremental use), so
    // reset the lists each time rather than appending across runs.
    vm->compiler.ctor_count = 0;
    vm->compiler.dtor_count = 0;
    {
        int seq = 0;
        for (Obj *fn = prog; fn; fn = fn->next) {
            if (!fn->is_function)
                continue;
            if (fn->is_constructor) {
                PATCH_GROW(vm, ctor_list, ctor_count, ctor_capacity);
                CCCCInitEntry *e =
                    &vm->compiler.ctor_list[vm->compiler.ctor_count++];
                e->code_addr = fn->code_addr;
                e->priority  = fn->init_priority;
                e->seq       = seq;
            }
            if (fn->is_destructor) {
                PATCH_GROW(vm, dtor_list, dtor_count, dtor_capacity);
                CCCCInitEntry *e =
                    &vm->compiler.dtor_list[vm->compiler.dtor_count++];
                e->code_addr = fn->code_addr;
                e->priority  = fn->init_priority;
                e->seq       = seq;
            }
            seq++;
        }
    }
    sort_init_entries(vm->compiler.ctor_list, vm->compiler.ctor_count, true);
    sort_init_entries(vm->compiler.dtor_list, vm->compiler.dtor_count, false);

    // Find entry function and store its address in text_seg[0]
    const char *entry =
        vm->compiler.entry_name ? vm->compiler.entry_name : "main";
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->is_function &&
            strncmp(fn->name, entry, strlen(entry) + 1) == 0) {
            vm->text_seg[0] = fn->code_addr;
            return;
        }
    }

    if (!vm->compiler.compile_only && !vm->compiler.testing_mode &&
        !vm->compiler.build_mode)
        error("%s() function not found", entry);
}

// ========== Incremental Code Generation (REPL support, ticket #661) ==========
//
// gen() above is a whole-program pass: it resets text_ptr/data_ptr to zero and
// regenerates every function and global from scratch every time it runs. That
// is wrong for an interactive REPL, where each evaluated line must build on a
// VM state that is still live -- global variables may have been mutated at
// runtime by a previous line, and any pointers a previous line computed into
// the data or text segment must stay valid. A full rebuild would silently
// reset every global back to its initializer and invalidate those addresses.
//
// cc_repl_compile_new() instead compiles only the globals that were prepended
// to vm->compiler.globals since `old_head` (the list head captured by the
// caller before parsing/synthesizing the new unit): it assigns data-segment
// storage for new non-function globals, generates code for new function
// definitions via gen_function(), and patches only the call/function-address
// relocations recorded during *this* call (a new function may still call any
// previously-compiled function, so patch resolution consults the full globals
// list). Already-compiled globals and functions are never revisited, so their
// code_addr, data offsets, and current runtime contents are untouched. This
// mirrors the sequence compile_macro_program() uses to compile the separate
// comptime program (macros.c), retargeted at the main globals list.
void cc_repl_compile_new(VirtualMachine *vm, Obj *old_head) {
    if (!vm->text_seg) {
        vm_alloc_segments(vm);
        vm->compiler.current_codegen_fn = NULL;
        vm->text_ptr = 0; // reserve text_seg[0], as gen() does
        if (vm->flags & CCCC_ENABLE_DEBUGGER) {
            vm->dbg.source_map_capacity = 1024;
            vm->dbg.source_map =
                malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
            if (!vm->dbg.source_map)
                error("could not malloc for source map");
            vm->dbg.source_map_count   = 0;
            vm->dbg.last_debug_file    = NULL;
            vm->dbg.last_debug_line    = -1;
            vm->dbg.source_index       = NULL;
            vm->dbg.source_index_count = 0;
            vm->dbg.num_debug_symbols  = 0;
            vm->dbg.num_watchpoints    = 0;
        }
    }

    // #666: the RETBUF pool must exist before any incrementally-compiled
    // expression executes a struct/union/vector-returning call or return
    // statement -- allocated here (not inside the !vm->text_seg block
    // above) so it is correct regardless of whether this call or an earlier
    // whole-program gen() allocated the segments first. Idempotent.
    alloc_return_buffer_pool(vm);

    // Count and collect the new non-function globals, oldest-first (the list
    // is built by prepending, so walking old_head..globals gives newest-first).
    int num_new_vars = 0;
    for (Obj *v = vm->compiler.globals; v != old_head; v = v->next)
        if (!v->is_function)
            num_new_vars++;
    if (num_new_vars > 0) {
        Obj **arr = alloca((size_t)num_new_vars * sizeof(Obj *));
        int   idx = num_new_vars - 1;
        for (Obj *v = vm->compiler.globals; v != old_head; v = v->next)
            if (!v->is_function)
                arr[idx--] = v;
        for (int i = 0; i < num_new_vars; i++) {
            Obj *var = arr[i];
            if (var->is_tls) {
                // #1136: see gen()'s own comment on this same allocation
                // shape above.
                int    align = cc_effective_align(var->align, var->ty->align);
                size_t tls_offset =
                    (vm->tls_template_size + (size_t)(align - 1)) &
                    ~(size_t)(align - 1);
                check_tls_capacity(vm, tls_offset + (size_t)var->ty->size);
                var->offset = (long long)tls_offset;
                if (var->init_data)
                    memcpy(vm->tls_template + tls_offset, var->init_data,
                           (size_t)var->ty->size);
                vm->tls_template_size = tls_offset + (size_t)var->ty->size;
            } else {
                // #1136: see gen()'s own comment on this same allocation
                // shape above.
                int align = cc_effective_align(var->align, var->ty->align);
                long long offset = vm->data_ptr - vm->data_seg;
                offset = (offset + (align - 1)) & ~(long long)(align - 1);
                check_data_capacity(vm, offset + var->ty->size);
                vm->data_ptr = vm->data_seg + offset;
                var->offset  = vm->data_ptr - vm->data_seg;
                add_debug_symbol(vm, var->name, var->offset, var->ty, 0, NULL);
                if (var->init_data)
                    memcpy(vm->data_ptr, var->init_data, var->ty->size);
                vm->data_ptr += var->ty->size;
            }
        }
    }

    // Count and collect the new function definitions, oldest-first.
    int num_new_fns = 0;
    for (Obj *v = vm->compiler.globals; v != old_head; v = v->next)
        if (v->is_function && v->body)
            num_new_fns++;
    if (num_new_fns > 0) {
        Obj **farr = alloca((size_t)num_new_fns * sizeof(Obj *));
        int   idx  = num_new_fns - 1;
        for (Obj *v = vm->compiler.globals; v != old_head; v = v->next)
            if (v->is_function && v->body)
                farr[idx--] = v;

        int call_patch_start = vm->compiler.num_call_patches;
        int addr_patch_start = vm->compiler.num_func_addr_patches;

        for (int i = 0; i < num_new_fns; i++) {
            Obj *fn = farr[i];
            if (fn->is_inline && fn->is_static && !fn->is_live)
                continue;
            gen_function(vm, fn);
        }

        // A newly-compiled function may call any previously-compiled function,
        // so build the patch-resolution map over the full globals list.
        HashMap fn_defs = {};
        for (Obj *fn = vm->compiler.globals; fn; fn = fn->next)
            if (fn->is_function && fn->body && !fn->is_static)
                hashmap_put(&fn_defs, fn->name, fn);

        for (int i = call_patch_start; i < vm->compiler.num_call_patches; i++) {
            Obj        *target  = vm->compiler.call_patches[i].function;
            const char *fn_name = obj_external_name(target);
            Pc          loc     = vm->compiler.call_patches[i].location;

            Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);
            if (!fn_def) {
                int ffi_idx = find_ffi_function(vm, fn_name);
                if (ffi_idx >= 0)
                    continue; // FFI - not handled via CALL
                error("undefined function: %s", fn_name);
            }
            vm->text_seg[loc] = (Pc)fn_def->code_addr;
        }

        for (int i = addr_patch_start; i < vm->compiler.num_func_addr_patches;
             i++) {
            Obj *target = vm->compiler.func_addr_patches[i].function;
            Pc   loc    = vm->compiler.func_addr_patches[i].location;

            Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);
            if (fn_def) {
                cc_write_i64_at(vm, loc,
                                cc_pc_to_byte_offset((Pc)fn_def->code_addr));
            } else {
                const char *fn_name = obj_external_name(target);
                int         ffi_idx = find_ffi_function(vm, fn_name);
                if (ffi_idx >= 0)
                    cc_write_i64_at(vm, loc, CCCC_FFI_TOKEN_BASE - ffi_idx);
                // else: REPL disallows -c/deferred-link, so there is no
                // cross-module reloc path to fall back to here.
            }
        }

        hashmap_deinit(&fn_defs);
    }

    // Idempotent per-variable: only touches globals that carry a ->rel list,
    // safe to run over the whole (small, REPL-sized) globals list each time.
    apply_global_relocations(vm, vm->compiler.globals);
}
