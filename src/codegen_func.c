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

// ========== Intrinsic address stubs ==========

// Functions the VM lowers to an opcode at the call site have no body and no
// FFI entry. Taking one's address points at a stub: a frame whose body is the
// opcode, with the arguments still in REG_A0/REG_A1.
typedef struct {
    const char *name;
    int         op;
    bool        returns_zero;
    Pc          addr;
} IntrinsicStub;

static IntrinsicStub intrinsic_stubs[] = {
    {"signal", VSIGNAL, false, 0},     {"raise", VRAISE, true, 0},
    {"longjmp", LONGJMP, false, 0},    {"_longjmp", LONGJMP, false, 0},
    {"siglongjmp", LONGJMP, false, 0}, {"dlopen", DLOPEN, false, 0},
    {"dlsym", DLSYM, false, 0},        {"dlclose", DLCLOSE, false, 0},
    {"dlerror", DLERROR, false, 0},
};

void cc_reset_intrinsic_stubs(void) {
    for (size_t i = 0; i < sizeof intrinsic_stubs / sizeof *intrinsic_stubs;
         i++)
        intrinsic_stubs[i].addr = 0;
}

// Code address of the stub for a body-less intrinsic, emitting it at the top
// of the text on first use; 0 when `target` is not an intrinsic.
Pc cc_intrinsic_stub_addr(VirtualMachine *vm, Obj *target) {
    if (!target || !target->is_function || target->body || !target->name)
        return 0;
    for (size_t i = 0; i < sizeof intrinsic_stubs / sizeof *intrinsic_stubs;
         i++) {
        IntrinsicStub *st = &intrinsic_stubs[i];
        if (strcmp(st->name, target->name))
            continue;
        if (!st->addr) {
            st->addr = vm->text_ptr + 1;
            emit(vm, ENT3);
            emit_i64(vm, 2); // empty frame; keeps the canary slot
            emit_i64(vm, 0);
            emit(vm, st->op);
            if (st->returns_zero)
                emit_li3(vm, REG_A0, 0);
            emit(vm, LEV3);
        }
        return st->addr;
    }
    return 0;
}

void cc_reject_setjmp_address(VirtualMachine *vm, Node *node) {
    Obj *fn = node->var;
    if (obj_is_reserved_builtin(fn, "setjmp") ||
        obj_is_reserved_builtin(fn, "_setjmp") ||
        obj_is_reserved_builtin(fn, "sigsetjmp"))
        error_tok(vm, node->tok, "cannot take the address of '%s'", fn->name);
}

// ========== Function Generation ==========

// Frees the per-function local_set tables belongs_to_outer_function builds
// lazily while code is generated. A table lives on the enclosing function,
// which a block adopted by a generated function may not share a list with.
void cc_free_local_sets(Obj *prog) {
    for (Obj *fn = prog; fn; fn = fn->next)
        for (Obj *o = fn; o; o = o->parent_fn)
            if (o->local_set_built) {
                hashmap_deinit_borrowed(&o->local_set);
                o->local_set_built = false;
            }
}

void gen_function(VirtualMachine *vm, Obj *fn) {
    if (!fn->is_function || !fn->body)
        return;

    // Set current function context for nested function checks (e.g. in
    // gen_addr)
    vm->compiler.current_fn = fn;

    // Reset cleanup scope stack for this function
    g_cleanup_scope = NULL;
    fn->cleanup_ranges = NULL;
    for (Obj *v = fn->locals; v; v = v->next)
        v->cleanup_cur = NULL;

    vm->compiler.ent3_extra_stack = 0;

    // Reset lazy frame-epoch tracking for this function (#703).
    vm->compiler.frame_has_esc_agg    = false;
    vm->compiler.frame_has_esc_scalar = false;

    // Reset label tracking for this function
    reset_labels();

    // Count parameters first
    // Assign stack offsets early
    int stack_size = assign_stack_offsets(vm, fn);
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
        add_debug_symbol(vm, param->name, param->offset, param->ty, 1, fn,
                         local_align_log(param));
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
            add_debug_symbol(vm, var->name, var->offset, var->ty, 1, fn,
                             local_align_log(var));
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

    // #1137: a slot-resident over-aligned scalar param (e.g. a 16-aligned
    // long double) lives at its positional ENT3 spill slot, which is only
    // 8-aligned. assign_stack_offsets gave it a padded slot (param->offset);
    // copy the spilled value there so &param is correctly aligned.
    {
        int spill_off = -1 - ((vm->flags & CCCC_STACK_CANARIES) ? 1 : 0);
        for (Obj *param = fn->params; param; param = param->next, spill_off--) {
            if (!local_align_log(param))
                continue;
            int r_val = alloc_temp_reg();
            int r_dst = alloc_temp_reg();
            emit_lea3_internal(vm, r_val, spill_off);
            emit_rr(vm, LDR_D, r_val, r_val);
            emit_lea3_var_internal(vm, r_dst, param);
            emit_rr(vm, STR_D, r_val, r_dst);
            free_temp_reg(r_dst);
            free_temp_reg(r_val);
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
        // #1137: a wide _BitInt/decimal param is by-pointer too, but into the
        // caller's (unaligned) scratch temp; only worth the copy when the
        // param's address is observable and its type wants > 8 alignment.
        bool wants_aligned_copy =
            (is_wide_bitint(param->ty) || is_decimal(param->ty)) &&
            (param->addr_taken || param->addr_escapes) &&
            cc_effective_align(param->align, param->ty->align) > 8;
        if ((param->ty->kind != TY_STRUCT && param->ty->kind != TY_UNION &&
             !wants_aligned_copy) ||
            param->ty->size <= 0)
            continue;
        int copy_align_log =
            align_log_of(cc_effective_align(param->align, param->ty->align));
        long long copy_off =
            alloc_aligned_temp(vm, param->ty->size, copy_align_log);
        int r_src = alloc_temp_reg();
        // Slot address only feeds the immediate pointer load below (#676).
        emit_lea3_internal(vm, r_src, param->offset);
        emit_rr(vm, LDR_D, r_src, r_src);         // caller's object address
        emit_lea3_aligned(vm, REG_A0, copy_off, copy_align_log,
                          false);                 // copy dest
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
            emit_stktag(vm, copy_off, param->ty->size, copy_align_log);
            vm->compiler.frame_has_esc_agg = true;
        }
        // Rebind the param's slot to point at the copy, not the caller's
        // object. emit_lea3_internal(copy_off) is recomputed since MCPY
        // clobbered A0.
        int r_dst = alloc_temp_reg();
        emit_lea3_aligned(vm, r_dst, copy_off, copy_align_log, false);
        int r_slot = alloc_temp_reg();
        emit_lea3_internal(vm, r_slot, param->offset);
        emit_rr(vm, STR_D, r_dst, r_slot);
        free_temp_reg(r_slot);
        free_temp_reg(r_dst);
    }

    // Generate function body
    gen_stmt(vm, fn->body);

    // Patch ENT3 stack size if scratch temporaries were allocated
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
    // Deactivate function scope (for fall-through returns).
    if (vm->flags & CCCC_STACK_INSTR)
        emit_scopeout(vm, fn_scope_id);
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
// slot 0 being unset -- so it is safe to call from gen()'s whole-program
// pass, cc_repl_compile_new()'s incremental pass, and
// compile_macro_program()'s comptime pass without double-allocating.
// cc_repl_compile_new() must call this too: before #666 it never did, so
// any REPL expression (or debugger conditional-breakpoint expression, which
// shares cc_repl_compile_new) returning a struct/union/vector hit "return
// buffer pool was never allocated" at RETBUF-execution time.
// compile_macro_program() (src/macros.c) must call it for the same reason: a
// [[cccc::comptime]] function body returning a struct/union/vector by value hit
// the identical fault before this call site existed.
void cc_alloc_return_buffer_pool(VirtualMachine *vm) {
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
    cc_reset_intrinsic_stubs();

    // Reset the persistent global label map (populated by define_label during
    // function codegen; consumed by apply_global_relocations for &&label
    // static initialisers).  We reuse any previously allocated buffer.
    num_global_labels = 0;

    // Initialize text pointer - text_seg[0] is reserved for main entry point
    vm->text_ptr = 0;
    // The comptime code compiled before this point is about to be overwritten.
    vm->compiler.macro_code_stale = true;

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
                add_debug_symbol(vm, var->name, var->offset, var->ty, 0, NULL,
                                 0);

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
    cc_alloc_return_buffer_pool(vm);

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
            if (obj_is_inline_only(fn) && !fn->is_live)
                continue;
            gen_function(vm, fn);
        }
    }
    cc_asm_passthru_flush(vm);

    cc_free_local_sets(prog);

    HashMap fn_defs     = {};
    HashMap static_defs = {};
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->is_function && fn->body && fn->is_static)
            add_static_function_def(&static_defs, fn);
        else if (fn->is_function && fn->body &&
                 !(fn->is_gnu_inline && !fn->is_live))
            hashmap_put(&fn_defs, fn->name, fn);
    }

    // Second pass: Patch function call addresses
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj        *target  = vm->compiler.call_patches[i].function;
        const char *fn_name = obj_external_name(target);
        Pc          loc     = vm->compiler.call_patches[i].location;

        Obj        *fn_def =
            find_function_definition_for_patch(&fn_defs, &static_defs, target);

        if (!fn_def) {
            // Check for FFI function
            int ffi_idx = ffi_index_for_obj(vm, target);
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

        Obj *fn_def =
            find_function_definition_for_patch(&fn_defs, &static_defs, target);

        if (fn_def) {
            cc_write_i64_at(vm, loc,
                            cc_pc_to_byte_offset((Pc)fn_def->code_addr));
        } else {
            int ffi_idx = ffi_index_for_obj(vm, target);
            Pc  stub    = ffi_idx < 0 ? cc_intrinsic_stub_addr(vm, target) : 0;
            if (ffi_idx >= 0) {
                // FFI function used as a value: store token so CALLN can
                // call it. (JMPI, the other indirect-control-flow opcode,
                // carries no callsite metadata and is only ever emitted for
                // computed goto -- landing on this token there is a runtime
                // error, not a call.)
                cc_write_i64_at(vm, loc, CCCC_FFI_TOKEN_BASE - ffi_idx);
            } else if (stub) {
                cc_write_i64_at(vm, loc, cc_pc_to_byte_offset(stub));
            }
            // else: under -c/--compile, deferred to the host C
            // compiler/linker, same as the call-patch pass above; otherwise
            // the parser already rejects address-of-undeclared.
        }
    }

    hashmap_deinit(&fn_defs);

    hashmap_deinit(&static_defs);

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
        if (vm->compiler.compile_only || cc_cdata_index_for_obj(vm, var) >= 0)
            continue;
        error("undefined global: %s", obj_external_name(var));
    }

    apply_global_relocations(vm, prog, NULL);

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
            if (fn->is_kernel && fn->body)
                cccc_kernel_register_meta(
                    vm, cc_pc_to_byte_offset((Pc)fn->code_addr),
                    fn->kernel_uses_barrier, fn->kernel_local_bytes);
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

// A function whose body this unit supplies. Besides the Objs prepended since
// `old_head`, that includes an earlier prototype's Obj, which a later
// definition reuses in place.
static bool repl_fn_needs_compile(Obj *v, bool is_new) {
    if (!v->is_function || !v->body || v->reuse_of)
        return false;
    return is_new || !v->code_addr;
}

// A block-scope static of a reloaded function body keeps the storage of the
// same-named, same-typed static in the body it replaces. Earlier statics of the
// new body (arr[0..i)) have already claimed theirs, so duplicate names pair up
// in declaration order.
static Obj *find_reloaded_static(VirtualMachine *vm, Obj *var, Obj **arr,
                                 int i) {
    Obj *owner = var->static_owner;
    if (!owner || !(owner->reload_of || owner->reuse_of))
        return NULL;
    Obj *prev  = owner->reload_of ? owner->reload_of : owner->reuse_of;
    Obj *found = NULL;
    for (Obj *c = vm->compiler.globals; c; c = c->next) {
        if (c->is_function || c->static_owner != prev || c == var ||
            !c->display_name || !var->display_name ||
            strcmp(c->display_name, var->display_name) ||
            c->ty->size != var->ty->size || !is_compatible(c->ty, var->ty))
            continue;
        bool claimed = false;
        for (int j = 0; j < i && !claimed; j++)
            claimed = arr[j]->offset == c->offset && !arr[j]->is_tls &&
                      arr[j]->static_owner == var->static_owner;
        if (!claimed)
            found = c; // keep walking: the list is newest-first
    }
    return found;
}

// Reload: overwrite the replaced function's entry with a jump to the new body,
// so every route in (direct CALL, function pointers, FFI callbacks) lands
// there. Repatching the recorded direct CALL sites would skip the jump if it
// ever shows up in profiles.
static void redirect_function_entry(VirtualMachine *vm, Obj *fn) {
    Obj *root = fn_canonical(fn);
    if (!fn->code_addr || !root->code_addr)
        error("cannot reload %s: no compiled body to redirect", fn->name);
    vm->text_seg[root->code_addr + 1] = (Pc)fn->code_addr; // target before op
    vm->text_seg[root->code_addr]     = JMP;
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
// ========== Reclaimed text (holes) ==========

// Index of the first source-map entry at or after `pc`; the map is sorted.
static int source_map_lower_bound(VirtualMachine *vm, Pc pc) {
    int lo = 0, hi = vm->dbg.source_map_count;
    while (lo < hi) {
        int mid = lo + (hi - lo) / 2;
        if (vm->dbg.source_map[mid].pc_offset < pc)
            lo = mid + 1;
        else
            hi = mid;
    }
    return lo;
}

// Entries appended since `first` came from code below the top: put them where
// their pcs belong.
static void source_map_merge_tail(VirtualMachine *vm, int first) {
    int total = vm->dbg.source_map_count;
    if (!vm->dbg.source_map || first >= total)
        return;
    SourceMap *map  = vm->dbg.source_map;
    int        pos  = source_map_lower_bound(vm, map[first].pc_offset);
    int        tail = total - first;
    if (pos >= first)
        return;
    SourceMap *copy = malloc((size_t)tail * sizeof(SourceMap));
    if (!copy)
        error("out of memory merging the source map");
    memcpy(copy, &map[first], (size_t)tail * sizeof(SourceMap));
    memmove(&map[pos + tail], &map[pos],
            (size_t)(first - pos) * sizeof(SourceMap));
    memcpy(&map[pos], copy, (size_t)tail * sizeof(SourceMap));
    free(copy);
}

// Drop what refers to the text words [start, end).
static void forget_text_range(VirtualMachine *vm, Pc start, Pc end) {
    if (vm->dbg.source_map) {
        int lo = source_map_lower_bound(vm, start);
        int hi = source_map_lower_bound(vm, end);
        memmove(&vm->dbg.source_map[lo], &vm->dbg.source_map[hi],
                (size_t)(vm->dbg.source_map_count - hi) * sizeof(SourceMap));
        vm->dbg.source_map_count -= hi - lo;
    }

    int kept = 0;
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Pc loc = vm->compiler.call_patches[i].location;
        if (loc < start || loc >= end)
            vm->compiler.call_patches[kept++] = vm->compiler.call_patches[i];
    }
    vm->compiler.num_call_patches = kept;

    kept                          = 0;
    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Pc loc = vm->compiler.func_addr_patches[i].location;
        if (loc < start || loc >= end)
            vm->compiler.func_addr_patches[kept++] =
                vm->compiler.func_addr_patches[i];
    }
    vm->compiler.num_func_addr_patches = kept;

    kept                               = 0;
    for (int i = 0; i < num_global_labels; i++) {
        Pc off = global_label_map[i].offset;
        if (off < start || off >= end)
            global_label_map[kept++] = global_label_map[i];
    }
    num_global_labels                  = kept;

    kept                               = 0;
    for (int i = 0; i < vm->dbg.num_debug_symbols; i++) {
        DebugSymbol *sym = &vm->dbg.debug_symbols[i];
        bool         own = sym->is_local && sym->owner_fn &&
                           sym->owner_fn->code_addr >= start &&
                           sym->owner_fn->code_addr < end;
        if (!own)
            vm->dbg.debug_symbols[kept++] = *sym;
    }
    vm->dbg.num_debug_symbols = kept;

    if (vm->dynobjsz_scan_pc > (long long)start)
        vm->dynobjsz_scan_pc = (long long)start;

    vm->dbg.source_index_built_for = -1;
    if (vm->dbg.source_map_count)
        cc_build_source_index(vm);
    else
        vm->dbg.source_index_count = 0;
}

static void fill_reclaimed_text(VirtualMachine *vm, Pc start, Pc end) {
    for (Pc pc = start; pc < end; pc++)
        vm->text_seg[pc] = HOLE;
}

static void add_text_hole(VirtualMachine *vm, Pc start, Pc end) {
    TextHole *h = vm->compiler.text_holes;
    int       n = vm->compiler.num_text_holes;
    int       i = 0;
    while (i < n && h[i].start < start)
        i++;
    if (i > 0 && h[i - 1].end == start) {
        h[i - 1].end = end;
        i--;
    } else {
        if (n == vm->compiler.text_holes_cap) {
            vm->compiler.text_holes_cap = vm->compiler.text_holes_cap
                                              ? vm->compiler.text_holes_cap * 2
                                              : 8;
            h = realloc(vm->compiler.text_holes,
                        (size_t)vm->compiler.text_holes_cap * sizeof(TextHole));
            if (!h)
                error("out of memory for reclaimed text");
            vm->compiler.text_holes = h;
        }
        memmove(&h[i + 1], &h[i], (size_t)(n - i) * sizeof(TextHole));
        h[i].start = start;
        h[i].end   = end;
        vm->compiler.num_text_holes++;
    }
    if (i + 1 < vm->compiler.num_text_holes && h[i].end == h[i + 1].start) {
        h[i].end = h[i + 1].end;
        memmove(&h[i + 1], &h[i + 2],
                (size_t)(vm->compiler.num_text_holes - i - 2) *
                    sizeof(TextHole));
        vm->compiler.num_text_holes--;
    }
}

// The top has moved down onto a hole: take the hole back into the top.
static void absorb_text_holes(VirtualMachine *vm) {
    while (vm->compiler.num_text_holes > 0) {
        TextHole *h = &vm->compiler.text_holes[vm->compiler.num_text_holes - 1];
        if (h->end != vm->text_ptr + 1)
            break;
        memset(&vm->text_seg[h->start], 0,
               (size_t)(h->end - h->start) * sizeof(vm->text_seg[0]));
        vm->text_ptr = h->start - 1;
        vm->compiler.num_text_holes--;
    }
}

bool cc_text_hole_find(VirtualMachine *vm, Pc words, Pc *start, Pc *end) {
    bool found = false;
    for (int i = 0; i < vm->compiler.num_text_holes; i++) {
        Pc hs = vm->compiler.text_holes[i].start;
        Pc he = vm->compiler.text_holes[i].end;
        if (he - hs < words || (found && he - hs >= *end - *start))
            continue;
        *start = hs;
        *end   = he;
        found  = true;
    }
    return found;
}

// A compile into a hole is done (or threw): the top, the limit and the dedup
// state go back to what they were.
static void restore_top_state(VirtualMachine *vm, CcCodeMark *m) {
    m->text_end             = vm->text_ptr;
    vm->text_ptr            = m->saved_top;
    vm->compiler.text_limit = 0;
    vm->dbg.last_debug_file = m->last_file;
    vm->dbg.last_debug_line = m->last_line;
    vm->dbg.last_debug_col  = m->last_col;
    source_map_merge_tail(vm, m->saved_source_map);
}

// The compile took [hole_start, text_end]: what is left of the hole stays one.
static void leave_text_hole(VirtualMachine *vm, CcCodeMark *m) {
    restore_top_state(vm, m);
    if (vm->dynobjsz_scan_pc > (long long)m->hole_start)
        vm->dynobjsz_scan_pc = (long long)m->hole_start;
    for (int i = 0; i < vm->compiler.num_text_holes; i++) {
        TextHole *h = &vm->compiler.text_holes[i];
        if (h->start != m->hole_start)
            continue;
        h->start = m->text_end + 1;
        if (h->start >= h->end) {
            memmove(h, h + 1,
                    (size_t)(vm->compiler.num_text_holes - i - 1) *
                        sizeof(TextHole));
            vm->compiler.num_text_holes--;
        }
        return;
    }
}

// ========== Reclaimed data (holes) ==========

// Drop what refers to the data bytes [start, end) and zero them.
static void forget_data_range(VirtualMachine *vm, char *start, char *end) {
    long long lo = start - vm->data_seg, hi = end - vm->data_seg;
    int       kept = 0;
    for (int i = 0; i < vm->dbg.num_debug_symbols; i++) {
        DebugSymbol *sym = &vm->dbg.debug_symbols[i];
        if (sym->is_local || sym->offset < lo || sym->offset >= hi)
            vm->dbg.debug_symbols[kept++] = *sym;
    }
    vm->dbg.num_debug_symbols = kept;

    size_t len                = (size_t)(end - start);
    memset(start, 0, len);
    cc_type_shadow_clear_range(vm, start, len);
}

static void add_data_hole(VirtualMachine *vm, char *start, char *end) {
    DataHole *h = vm->compiler.data_holes;
    int       n = vm->compiler.num_data_holes;
    int       i = 0;
    while (i < n && h[i].start < start)
        i++;
    if (i > 0 && h[i - 1].end == start) {
        h[i - 1].end = end;
        i--;
    } else {
        if (n == vm->compiler.data_holes_cap) {
            vm->compiler.data_holes_cap = vm->compiler.data_holes_cap
                                              ? vm->compiler.data_holes_cap * 2
                                              : 8;
            h = realloc(vm->compiler.data_holes,
                        (size_t)vm->compiler.data_holes_cap * sizeof(DataHole));
            if (!h)
                error("out of memory for reclaimed data");
            vm->compiler.data_holes = h;
        }
        memmove(&h[i + 1], &h[i], (size_t)(n - i) * sizeof(DataHole));
        h[i].start = start;
        h[i].end   = end;
        vm->compiler.num_data_holes++;
    }
    if (i + 1 < vm->compiler.num_data_holes && h[i].end == h[i + 1].start) {
        h[i].end = h[i + 1].end;
        memmove(&h[i + 1], &h[i + 2],
                (size_t)(vm->compiler.num_data_holes - i - 2) *
                    sizeof(DataHole));
        vm->compiler.num_data_holes--;
    }
}

// The top has moved down onto a hole: take the hole back into the top.
static void absorb_data_holes(VirtualMachine *vm) {
    while (vm->compiler.num_data_holes > 0) {
        DataHole *h = &vm->compiler.data_holes[vm->compiler.num_data_holes - 1];
        if (h->end != vm->data_ptr)
            break;
        vm->data_ptr = h->start;
        vm->compiler.num_data_holes--;
    }
}

// TODO: linear scan; fine for the few holes a debugging session leaves
bool cc_data_hole_find(VirtualMachine *vm, size_t bytes, char **start,
                       char **end) {
    bool found = false;
    for (int i = 0; i < vm->compiler.num_data_holes; i++) {
        char *hs = vm->compiler.data_holes[i].start;
        char *he = vm->compiler.data_holes[i].end;
        if ((size_t)(he - hs) < bytes || (found && he - hs >= *end - *start))
            continue;
        *start = hs;
        *end   = he;
        found  = true;
    }
    return found;
}

// The new globals took [vm->data_seg + hole_start, cursor) of a hole: what is
// left of it stays one.
static void shrink_data_hole(VirtualMachine *vm, char *hole_start,
                             char *cursor) {
    for (int i = 0; i < vm->compiler.num_data_holes; i++) {
        DataHole *h = &vm->compiler.data_holes[i];
        if (h->start != hole_start)
            continue;
        h->start = cursor;
        if (h->start >= h->end) {
            memmove(h, h + 1,
                    (size_t)(vm->compiler.num_data_holes - i - 1) *
                        sizeof(DataHole));
            vm->compiler.num_data_holes--;
        }
        return;
    }
}

// Where `n` globals would end if placed from data offset `off`: the same
// alignment and skips as the allocation loop in cc_repl_compile_new_marked.
static long long new_vars_data_end(VirtualMachine *vm, Obj **arr, int n,
                                   long long off) {
    for (int i = 0; i < n; i++) {
        Obj *var = arr[i];
        if (var->reuse_of || var->is_tls ||
            find_reloaded_static(vm, var, arr, i))
            continue;
        int align  = cc_effective_align(var->align, var->ty->align);
        off        = (off + (align - 1)) & ~(long long)(align - 1);
        off       += var->ty->size;
    }
    return off;
}

void cc_repl_compile_new(VirtualMachine *vm, Obj *old_head) {
    cc_repl_compile_new_marked(vm, old_head, NULL);
}

void cc_code_mark_begin(VirtualMachine *vm, CcCodeMark *mark) {
    Pc    hole_start = mark->hole_start, hole_end = mark->hole_end;
    char *data_hole_start = mark->data_hole_start,
         *data_hole_end   = mark->data_hole_end;
    *mark = (CcCodeMark){.started          = true,
                         .text_start       = vm->text_ptr,
                         .data_start       = vm->data_ptr,
                         .global_labels    = num_global_labels,
                         .last_file        = vm->dbg.last_debug_file,
                         .last_line        = vm->dbg.last_debug_line,
                         .last_col         = vm->dbg.last_debug_col,
                         .hole_start       = hole_start,
                         .hole_end         = hole_end,
                         .data_hole_start  = data_hole_start,
                         .data_hole_end    = data_hole_end,
                         .saved_top        = vm->text_ptr,
                         .saved_source_map = vm->dbg.source_map_count};
}

void cc_code_mark_end(VirtualMachine *vm, CcCodeMark *mark, int new_fns,
                      int new_vars) {
    mark->ended = true;
    if (!mark->in_hole)
        mark->text_end = vm->text_ptr;
    if (!mark->data_in_hole)
        mark->data_end = vm->data_ptr;
    mark->global_labels_end = num_global_labels;
    mark->new_fns           = new_fns;
    mark->new_vars          = new_vars;
}

void cc_repl_compile_new_marked(VirtualMachine *vm, Obj *old_head,
                                CcCodeMark *mark) {
    cc_reset_intrinsic_stubs();
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
    cc_alloc_return_buffer_pool(vm);

    // After the RETBUF pool, which a later release must not take back.
    if (mark)
        cc_code_mark_begin(vm, mark);

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
        // The new globals go in the requested data hole if they all fit.
        char *hole_cursor = NULL;
        if (mark && mark->data_hole_start &&
            new_vars_data_end(vm, arr, num_new_vars,
                              mark->data_hole_start - vm->data_seg) <=
                mark->data_hole_end - vm->data_seg)
            hole_cursor = mark->data_hole_start;
        char **cursor = hole_cursor ? &hole_cursor : &vm->data_ptr;

        for (int i = 0; i < num_new_vars; i++) {
            Obj *var = arr[i];
            if (var->reuse_of) {
                var->offset = reuse_root(var)->offset;
                continue;
            }
            Obj *prev =
                var->is_tls ? NULL : find_reloaded_static(vm, var, arr, i);
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
            } else if (prev) {
                var->offset = prev->offset;
                add_debug_symbol(vm, var->name, var->offset, var->ty, 0, NULL,
                                 0);
            } else {
                // #1136: see gen()'s own comment on this same allocation
                // shape above.
                int align = cc_effective_align(var->align, var->ty->align);
                long long offset = *cursor - vm->data_seg;
                offset = (offset + (align - 1)) & ~(long long)(align - 1);
                if (!hole_cursor)
                    check_data_capacity(vm, offset + var->ty->size);
                *cursor     = vm->data_seg + offset;
                var->offset = *cursor - vm->data_seg;
                if (mark && !mark->data_first)
                    mark->data_first = *cursor;
                add_debug_symbol(vm, var->name, var->offset, var->ty, 0, NULL,
                                 0);
                if (var->init_data)
                    memcpy(*cursor, var->init_data, var->ty->size);
                *cursor += var->ty->size;
            }
        }
        if (hole_cursor) {
            mark->data_in_hole = true;
            mark->data_start   = mark->data_hole_start;
            mark->data_end     = hole_cursor;
            shrink_data_hole(vm, mark->data_hole_start, hole_cursor);
        }
    }

    // Count and collect the new function definitions, oldest-first.
    int  num_new_fns = 0;
    bool is_new      = true;
    for (Obj *v = vm->compiler.globals; v; v = v->next) {
        is_new = is_new && v != old_head;
        if (repl_fn_needs_compile(v, is_new))
            num_new_fns++;
    }
    if (num_new_fns > 0) {
        Obj **farr = alloca((size_t)num_new_fns * sizeof(Obj *));
        int   idx  = num_new_fns - 1;
        is_new     = true;
        for (Obj *v = vm->compiler.globals; v; v = v->next) {
            is_new = is_new && v != old_head;
            if (repl_fn_needs_compile(v, is_new))
                farr[idx--] = v;
        }

        int call_patch_start = vm->compiler.num_call_patches;
        int addr_patch_start = vm->compiler.num_func_addr_patches;

        if (mark && mark->hole_start) {
            mark->in_hole           = true;
            mark->text_start        = mark->hole_start - 1;
            vm->text_ptr            = mark->text_start;
            vm->compiler.text_limit = mark->hole_end;
            vm->dbg.last_debug_file = NULL;
            vm->dbg.last_debug_line = -1;
            vm->dbg.last_debug_col  = -1;
        }

        for (int i = 0; i < num_new_fns; i++) {
            // No liveness pass runs in a session, so a static inline body is
            // compiled even though nothing has marked it live.
            gen_function(vm, farr[i]);
        }
        cc_asm_passthru_flush(vm);

        // A newly-compiled function may call any previously-compiled function,
        // so build the patch-resolution map over the full globals list.
        HashMap fn_defs     = {};
        HashMap static_defs = {};
        for (Obj *fn = vm->compiler.globals; fn; fn = fn->next) {
            if (fn->is_function && fn->body && fn->is_static)
                add_static_function_def(&static_defs, fn);
            else if (fn->is_function && fn->body)
                hashmap_put(&fn_defs, fn->name, fn);
        }

        for (int i = call_patch_start; i < vm->compiler.num_call_patches; i++) {
            Obj        *target  = vm->compiler.call_patches[i].function;
            const char *fn_name = obj_external_name(target);
            Pc          loc     = vm->compiler.call_patches[i].location;

            Obj        *fn_def  = find_function_definition_for_patch(
                &fn_defs, &static_defs, target);
            if (!fn_def) {
                int ffi_idx = ffi_index_for_obj(vm, target);
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

            Obj *fn_def = find_function_definition_for_patch(
                &fn_defs, &static_defs, target);
            if (fn_def) {
                cc_write_i64_at(vm, loc,
                                cc_pc_to_byte_offset((Pc)fn_def->code_addr));
            } else {
                int ffi_idx = ffi_index_for_obj(vm, target);
                Pc  stub = ffi_idx < 0 ? cc_intrinsic_stub_addr(vm, target) : 0;
                if (ffi_idx >= 0)
                    cc_write_i64_at(vm, loc, CCCC_FFI_TOKEN_BASE - ffi_idx);
                else if (stub)
                    cc_write_i64_at(vm, loc, cc_pc_to_byte_offset(stub));
                // else: REPL disallows -c/deferred-link, so there is no
                // cross-module reloc path to fall back to here.
            }
        }

        hashmap_deinit(&fn_defs);

        hashmap_deinit(&static_defs);

        // Only now is each new body fully patched and safe to jump into.
        for (int i = 0; i < num_new_fns; i++)
            if (farr[i]->reload_of)
                redirect_function_entry(vm, farr[i]);
    }

    if (mark && mark->in_hole)
        leave_text_hole(vm, mark);

    // Only this unit's globals: re-applying an older initializer would
    // overwrite whatever the program has stored there since.
    apply_global_relocations(vm, vm->compiler.globals, old_head);

    cc_build_source_index(vm);

    if (mark)
        cc_code_mark_end(vm, mark, num_new_fns, num_new_vars);
}

int cc_code_release(VirtualMachine *vm, CcCodeMark *m) {
    if (!m->started)
        return 0;
    // A compile into a hole that threw left the top and the limit moved.
    if (m->in_hole && !m->ended && vm->compiler.text_limit)
        restore_top_state(vm, m);

    // A running wrapper's return addresses must not point into reused code.
    if (vm->dbg.cond_depth > 0)
        return 0;

    // Data first: it stays reachable through anything the text left behind.
    if (!m->data_done) {
        char *data_end =
            m->ended || m->data_in_hole ? m->data_end : vm->data_ptr;
        if (m->ended && m->new_vars > 0 && !m->confined) {
            m->data_done = true; // a string literal or static lives here
        } else if (data_end == m->data_start) {
            m->data_done = true;
        } else if (vm->data_ptr == data_end) {
            forget_data_range(vm, m->data_start, data_end);
            vm->data_ptr = m->data_start;
            absorb_data_holes(vm);
            m->data_done = true;
        } else if (m->confined) {
            forget_data_range(vm, m->data_start, data_end);
            add_data_hole(vm, m->data_start, data_end);
            m->data_done = true;
        }
    }

    if (m->text_done)
        return 1;
    if (m->ended && !m->confined &&
        (m->new_fns > 1 || m->global_labels_end != m->global_labels))
        return -1;

    Pc text_end  = m->ended || m->in_hole ? m->text_end : vm->text_ptr;
    m->text_done = true;
    if (!m->ended && num_global_labels > m->global_labels)
        num_global_labels = m->global_labels;
    if (text_end == m->text_start)
        return 1;

    Pc start = m->text_start + 1, end = text_end + 1;
    forget_text_range(vm, start, end);
    if (m->in_hole || vm->text_ptr != text_end) {
        // Below live code: the words stay, as a hole.
        fill_reclaimed_text(vm, start, end);
        if (m->ended)
            add_text_hole(vm, start, end);
        return 1;
    }

    memset(&vm->text_seg[start], 0,
           (size_t)(end - start) * sizeof(vm->text_seg[0]));
    vm->text_ptr = m->text_start;
    if (m->ended && num_global_labels == m->global_labels_end)
        num_global_labels = m->global_labels;
    // Nothing is left to dedup against; the next entry is always written.
    vm->dbg.last_debug_file = NULL;
    vm->dbg.last_debug_line = -1;
    vm->dbg.last_debug_col  = -1;
    absorb_text_holes(vm);
    return 1;
}

void cc_code_discard(VirtualMachine *vm, CcCodeMark *m) {
    if (!m->started)
        return;
    if (cc_code_release(vm, m) != 0)
        return;
    if (vm->compiler.num_dead_marks == vm->compiler.dead_marks_cap) {
        vm->compiler.dead_marks_cap =
            vm->compiler.dead_marks_cap ? vm->compiler.dead_marks_cap * 2 : 8;
        vm->compiler.dead_marks =
            realloc(vm->compiler.dead_marks,
                    (size_t)vm->compiler.dead_marks_cap * sizeof(CcCodeMark));
        if (!vm->compiler.dead_marks)
            error("out of memory for discarded wrappers");
    }
    vm->compiler.dead_marks[vm->compiler.num_dead_marks++] = *m;
}

void cc_code_retry_dead(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.num_dead_marks;) {
        if (cc_code_release(vm, &vm->compiler.dead_marks[i]) == 0) {
            i++;
            continue;
        }
        memmove(&vm->compiler.dead_marks[i], &vm->compiler.dead_marks[i + 1],
                (size_t)(vm->compiler.num_dead_marks - i - 1) *
                    sizeof(CcCodeMark));
        vm->compiler.num_dead_marks--;
    }
}
