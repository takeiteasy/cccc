/*
 JCC: JIT C Compiler

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

 This file was based on c4 by Robert Swierczek (rswier/c4) and the following
 write-a-C-interpreter tutorial by Jinzhou Zhang (lotabout/write-a-C-interpreter)
*/

#include "jcc.h"
#include "./internal.h"

#define X(NAME) extern int op_##NAME##_fn(JCC *vm);
OPS_X
#undef X

int vm_eval(JCC *vm) {
    static void *op_table[] = {
#define X(NAME) [NAME] = &&op_##NAME,
        OPS_X
#undef X
    };
    static const char *op_names[] = {
#define X(NAME) #NAME,
        OPS_X
#undef X
    };

    vm->cycle = 0;

dispatch:
    vm->cycle++;

    if (vm->flags & JCC_ENABLE_DEBUGGER) {
        if (debugger_check_breakpoint(vm)) {
            printf("\nBreakpoint hit at PC %u\n", vm->pc);
            cc_debug_repl(vm);
        }
        if (vm->dbg.single_step)
            cc_debug_repl(vm);
        if (vm->dbg.step_over && vm->pc == vm->dbg.step_over_return_addr) {
            vm->dbg.step_over = 0;
            cc_debug_repl(vm);
        }
        if (vm->dbg.step_out && vm->bp != vm->dbg.step_out_bp) {
            vm->dbg.step_out = 0;
            cc_debug_repl(vm);
        }
    }

    {
        if (__builtin_expect(vm->pc == JCC_INVALID_PC, 0))
            return (int)vm->regs[REG_A0];
        int op = (int)cc_read_word(vm);
        if (__builtin_expect(op < 0 || op >= (int)(sizeof(op_table) / sizeof(op_table[0])) || !op_table[op], 0)) {
            printf("unknown instruction:%d\n", op);
            return -1;
        }
        if (vm->debug_vm) {
            if (op >= 0 && op < (int)(sizeof(op_names) / sizeof(op_names[0])))
                printf("%lld> %s\n", vm->cycle, op_names[op]);
            else
                printf("%lld> OP_%d\n", vm->cycle, op);
        }
        goto *op_table[op];
    }

#define X(NAME)                                                  \
    op_##NAME: {                                                 \
        int _r = op_##NAME##_fn(vm);                             \
        if (__builtin_expect(_r != 0, 0)) return _r;             \
        if (__builtin_expect(vm->pc == JCC_INVALID_PC, 0))       \
            return (int)vm->regs[REG_A0];                        \
        goto dispatch;                                           \
    }
    OPS_X
#undef X

    return -1;
}

// ========== Segment reserve-and-commit helpers ==========

void vm_alloc_segments(JCC *vm) {
    size_t initial_text   = (size_t)vm->poolsize * sizeof(JCCInstrWord);
    size_t initial_data   = (size_t)vm->poolsize;
    size_t initial_stack  = (size_t)vm->poolsize * sizeof(long long);
    size_t initial_heap   = (size_t)vm->poolsize;

    size_t reserved_text  = (size_t)vm->poolsize_max * sizeof(JCCInstrWord);
    size_t reserved_data  = (size_t)vm->poolsize_max;
    size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
    size_t reserved_heap  = (size_t)vm->poolsize_max;

    // Reserve virtual ranges (base pointers will never move)
    vm->text_seg  = (JCCInstrWord *)jcc_vm_reserve(reserved_text);
    vm->data_seg  = (char *)jcc_vm_reserve(reserved_data);
    vm->stack_seg = (long long *)jcc_vm_reserve(reserved_stack);
    vm->heap_seg  = (char *)jcc_vm_reserve(reserved_heap);
    if (!vm->text_seg || !vm->data_seg || !vm->stack_seg || !vm->heap_seg)
        error("could not reserve VM memory segments");

    // Stack grows downward: commit the TOP initial_stack bytes so that
    // sp/bp start at the top of the reservation and stack_base sits at
    // the bottom of the initially-committed slice.
    size_t stack_top_off = reserved_stack - initial_stack;

    if (jcc_vm_commit(vm->text_seg,  0,             initial_text)  != 0 ||
        jcc_vm_commit(vm->data_seg,  0,             initial_data)  != 0 ||
        jcc_vm_commit(vm->stack_seg, stack_top_off, initial_stack) != 0 ||
        jcc_vm_commit(vm->heap_seg,  0,             initial_heap)  != 0)
        error("could not commit VM memory segments");

    vm->text_committed  = initial_text;
    vm->data_committed  = initial_data;
    vm->stack_committed = initial_stack;
    vm->heap_committed  = initial_heap;

    // Derived pointers
    vm->old_text_seg = vm->text_seg;
    vm->text_ptr     = 0;
    vm->data_ptr     = vm->data_seg;
    vm->heap_ptr     = vm->heap_seg;
    vm->heap_end     = vm->heap_seg + vm->heap_committed;
    vm->free_list    = NULL;

    // Stack: sp/bp at top of reservation; stack_base = bottom of committed region
    vm->sp = (long long *)((char *)vm->stack_seg + reserved_stack);
    vm->bp = vm->sp;
    vm->stack_base = (long long *)((char *)vm->stack_seg + stack_top_off);
    vm->initial_sp = vm->sp;
    vm->initial_bp = vm->bp;

    // CFI shadow stack mirrors the main stack layout
    if (vm->flags & JCC_CFI) {
        vm->shadow_stack = (long long *)jcc_vm_reserve(reserved_stack);
        if (!vm->shadow_stack)
            error("could not reserve shadow stack");
        if (jcc_vm_commit(vm->shadow_stack, stack_top_off, initial_stack) != 0)
            error("could not commit shadow stack");
        vm->shadow_sp = (long long *)((char *)vm->shadow_stack + reserved_stack);
    }
}

int vm_text_ensure_count(JCC *vm, JCCPc num_words) {
    size_t needed_bytes = (size_t)num_words * sizeof(JCCInstrWord);
    if (needed_bytes <= vm->text_committed)
        return 0;
    size_t reserved = (size_t)vm->poolsize_max * sizeof(JCCInstrWord);
    if (needed_bytes > reserved)
        return -1;
    // Round up to next poolsize-element chunk
    size_t chunk = (size_t)vm->poolsize * sizeof(JCCInstrWord);
    size_t new_committed = ((needed_bytes + chunk - 1) / chunk) * chunk;
    if (new_committed > reserved)
        new_committed = reserved;
    if (jcc_vm_commit(vm->text_seg, vm->text_committed,
                      new_committed - vm->text_committed) != 0)
        return -1;
    vm->text_committed = new_committed;
    return 0;
}

int vm_data_ensure(JCC *vm, long long needed) {
    long long current_used = vm->data_ptr - vm->data_seg;
    long long committed    = (long long)vm->data_committed;
    if (current_used + needed <= committed)
        return 0;
    long long want     = current_used + needed;
    long long reserved = (long long)vm->poolsize_max;
    if (want > reserved)
        return -1;
    long long chunk = (long long)vm->poolsize;
    long long new_committed = ((want + chunk - 1) / chunk) * chunk;
    if (new_committed > reserved)
        new_committed = reserved;
    if (jcc_vm_commit(vm->data_seg, (size_t)committed,
                      (size_t)(new_committed - committed)) != 0)
        return -1;
    vm->data_committed = (size_t)new_committed;
    return 0;
}

int vm_heap_grow(JCC *vm, size_t need) {
    size_t current  = vm->heap_committed;
    size_t reserved = (size_t)vm->poolsize_max;
    if (current >= reserved)
        return -1;
    size_t chunk = (size_t)vm->poolsize;
    // Commit at least chunk, or enough to satisfy 'need'
    if (chunk < need)
        chunk = ((need + (size_t)vm->poolsize - 1) /
                 (size_t)vm->poolsize) * (size_t)vm->poolsize;
    if (current + chunk > reserved)
        chunk = reserved - current;
    if (chunk < need)
        return -1; // Can't satisfy even after using all remaining space
    if (jcc_vm_commit(vm->heap_seg, current, chunk) != 0)
        return -1;
    vm->heap_committed += chunk;
    vm->heap_end = vm->heap_seg + vm->heap_committed;
    return 0;
}

int vm_stack_grow(JCC *vm, int slots_needed) {
    char *seg_start      = (char *)vm->stack_seg;
    char *current_base   = (char *)vm->stack_base;
    size_t need_bytes    = (size_t)slots_needed * sizeof(long long);
    size_t chunk         = (size_t)vm->poolsize * sizeof(long long);

    // Ensure chunk is large enough for the immediate need
    if (chunk < need_bytes)
        chunk = ((need_bytes + (size_t)vm->poolsize * sizeof(long long) - 1) /
                 ((size_t)vm->poolsize * sizeof(long long))) *
                ((size_t)vm->poolsize * sizeof(long long));

    // Clamp to reservation floor
    if (current_base - chunk < seg_start) {
        chunk = (size_t)(current_base - seg_start);
        if (chunk < need_bytes)
            return -1; // True overflow — reservation exhausted
    }

    char *new_base = current_base - chunk;
    size_t off = (size_t)(new_base - seg_start);

    if (jcc_vm_commit(seg_start, off, chunk) != 0)
        return -1;

    vm->stack_base = (long long *)new_base;
    vm->stack_committed += chunk;
    return 0;
}

// ========== End segment helpers ==========

void cc_init(JCC *vm, uint32_t flags) {
    // Zero-initialize the VM struct
    memset(vm, 0, sizeof(JCC));

    // Set runtime flags
    vm->flags = flags;

    // Set defaults
    vm->poolsize = 256 * 1024;              // 256K elements initial commit
    vm->poolsize_max = 256 * 256 * 1024;   // 64M elements reserved (address space only)
    vm->debug_vm = 0;

    // Set #embed directive defaults
    vm->compiler.embed_limit = 10 * 1024 * 1024;       // 10MB soft warning limit
    vm->compiler.embed_hard_limit = 50 * 1024 * 1024;  // 50MB secondary warning
    vm->compiler.embed_hard_error = false;              // Default to warnings, not errors

    // Return buffer pool will be allocated in data segment during codegen
    vm->compiler.return_buffer_size = 1024;
    vm->compiler.return_buffer_index = 0;  // Compile-time index (unused with RETBUF)
    vm->runtime_return_buffer_index = 0;   // Runtime rotation index for RETBUF opcode
    for (int i = 0; i < RETURN_BUFFER_POOL_SIZE; i++) {
        vm->compiler.return_buffer_pool[i] = NULL;  // Will be set to data segment locations
    }

    // Initialize parser arena BEFORE init_macros to avoid orphaning blocks
    // (init_macros allocates from the arena, so arena must be initialized first)
    arena_init(&vm->compiler.parser_arena, 0);  // 0 = use default (1MB)

    // Initialize init_state HashMap for uninitialized variable detection
    vm->init_state.capacity = 0;  // Will be allocated on first use by hashmap_put
    vm->init_state.buckets = NULL;
    vm->init_state.used = 0;

    // Initialize stack_ptrs HashMap for dangling pointer detection
    vm->stack_ptrs.capacity = 0;
    vm->stack_ptrs.buckets = NULL;
    vm->stack_ptrs.used = 0;

    // Initialize provenance HashMap for provenance tracking
    vm->provenance.capacity = 0;
    vm->provenance.buckets = NULL;
    vm->provenance.used = 0;

    // Initialize stack_var_meta HashMap for stack instrumentation
    vm->stack_var_meta.capacity = 0;
    vm->stack_var_meta.buckets = NULL;
    vm->stack_var_meta.used = 0;

    // Note: alloc_map and ptr_tags removed - now using sorted_allocs for heap tracking

    // Initialize included_headers HashMap for header-based stdlib loading
    vm->compiler.included_headers.capacity = 0;
    vm->compiler.included_headers.buckets = NULL;
    vm->compiler.included_headers.used = 0;

    // Initialize include_guards HashMap for header guard tracking
    vm->compiler.include_guards.capacity = 0;
    vm->compiler.include_guards.buckets = NULL;
    vm->compiler.include_guards.used = 0;

    // Initialize include_cache HashMap
    vm->compiler.include_cache.capacity = 0;
    vm->compiler.include_cache.buckets = NULL;
    vm->compiler.include_cache.used = 0;

    // Initialize file_buffers StringArray
    vm->compiler.file_buffers.data = NULL;
    vm->compiler.file_buffers.len = 0;
    vm->compiler.file_buffers.capacity = 0;

    init_macros(vm);
    cc_init_parser(vm);

    // Initialize sorted allocation array for O(log n) pointer validation
    vm->sorted_allocs.addresses = NULL;
    vm->sorted_allocs.headers = NULL;
    vm->sorted_allocs.count = 0;
    vm->sorted_allocs.capacity = 0;

    // Initialize CFI shadow stack (will be allocated if enable_cfi is set)
    vm->shadow_stack = NULL;
    vm->shadow_sp = NULL;

    // Initialize segregated free lists
    for (int i = 0; i < 12; i++) {  // NUM_SIZE_CLASSES
        vm->size_class_lists[i] = NULL;
    }
    vm->large_list = NULL;

    // Initialize stack instrumentation state
    vm->current_scope_id = 0;
    vm->current_function_scope_id = 0;
    vm->stack_high_water = 0;
    vm->scope_vars = NULL;
    vm->scope_vars_capacity = 0;

    // Initialize stack canary (will be set to random or fixed value based on flag)
    // The flag JCC_RANDOM_CANARIES will trigger regeneration in main.c
    // For now, initialize to fixed value; it will be regenerated if random canaries enabled
    vm->stack_canary = STACK_CANARY;

    // Add default system include path for <...> includes
    cc_system_include(vm, "./include");

    // Initialize error collection fields
    vm->errors = NULL;
    vm->errors_tail = NULL;
    vm->error_count = 0;
    vm->warning_count = 0;
    vm->max_errors = 20;  // Default max errors before stopping
    vm->collect_errors = false;  // Disabled by default (opt-in)
    vm->warnings_as_errors = false;  // Disabled by default

    if (vm->flags & JCC_ENABLE_DEBUGGER) {
        debugger_init(vm);
    }
}

void cc_destroy(JCC *vm) {
    if (!vm)
        return;

    // Release reserved virtual ranges (base pointers never moved)
    if (vm->text_seg)
        jcc_vm_release(vm->text_seg,
                       (size_t)vm->poolsize_max * sizeof(JCCInstrWord));
    if (vm->data_seg)
        jcc_vm_release(vm->data_seg,  (size_t)vm->poolsize_max);
    if (vm->stack_seg)
        jcc_vm_release(vm->stack_seg,
                       (size_t)vm->poolsize_max * sizeof(long long));
    if (vm->heap_seg)
        jcc_vm_release(vm->heap_seg,  (size_t)vm->poolsize_max);
    if (vm->shadow_stack)
        jcc_vm_release(vm->shadow_stack,
                       (size_t)vm->poolsize_max * sizeof(long long));
    // return_buffer is part of data_seg, no need to free separately

    // Free VM runtime HashMaps. Integer keys (keylen == -1) are skipped by
    // hashmap_deinit. Heap-allocated values must be freed first.
    // Free init_state HashMap (integer keys, no values to free)
    hashmap_deinit(&vm->init_state);

    // Free stack_ptrs HashMap (integer keys + StackPtrInfo values)
    if (vm->stack_ptrs.buckets) {
        for (int i = 0; i < vm->stack_ptrs.capacity; i++) {
            HashEntry *entry = &vm->stack_ptrs.buckets[i];
            if (entry->key && entry->key != (void *)-1 && entry->val)
                free(entry->val);
        }
        hashmap_deinit(&vm->stack_ptrs);
    }

    // Free provenance HashMap (integer keys + ProvenanceInfo values)
    if (vm->provenance.buckets) {
        for (int i = 0; i < vm->provenance.capacity; i++) {
            HashEntry *entry = &vm->provenance.buckets[i];
            if (entry->key && entry->key != (void *)-1 && entry->val)
                free(entry->val);
        }
        hashmap_deinit(&vm->provenance);
    }

    // Free stack_var_meta HashMap (integer keys + StackVarMeta values)
    if (vm->stack_var_meta.buckets) {
        for (int i = 0; i < vm->stack_var_meta.capacity; i++) {
            HashEntry *entry = &vm->stack_var_meta.buckets[i];
            if (entry->key && entry->key != (void *)-1 && entry->val)
                free(entry->val);
        }
        hashmap_deinit(&vm->stack_var_meta);
    }

    // Free scope variable lists
    if (vm->scope_vars) {
        for (int i = 0; i < vm->scope_vars_capacity; i++) {
            ScopeVarNode *node = vm->scope_vars[i].head;
            while (node) {
                ScopeVarNode *next = node->next;
                free(node);
                node = next;
            }
        }
        free(vm->scope_vars);
    }


    // Note: alloc_map and ptr_tags removed - now using sorted_allocs for heap tracking

    // Free compiler HashMaps. Keys are owned by the HashMap (copied on
    // insert); values are integers, arena-allocated, or freed separately.
    hashmap_deinit(&vm->compiler.included_headers);

    // Free sorted allocation arrays
    if (vm->sorted_allocs.addresses)
        free(vm->sorted_allocs.addresses);
    if (vm->sorted_allocs.headers)
        free(vm->sorted_allocs.headers);

    // Free macros HashMap (Macro values are arena-allocated; do not free them)
    hashmap_deinit(&vm->compiler.macros);

    // Free pragma_once HashMap
    hashmap_deinit(&vm->compiler.pragma_once);

    // Free include_guards HashMap
    hashmap_deinit(&vm->compiler.include_guards);

    // Free FFI table
    if (vm->compiler.ffi_table) {
        for (int i = 0; i < vm->compiler.ffi_count; i++)
            free(vm->compiler.ffi_table[i].name);
        free(vm->compiler.ffi_table);
    }

    // Free error message buffer if set
    if (vm->error_message) {
        free(vm->error_message);
        vm->error_message = NULL;
    }

    // Free include paths
    if (vm->compiler.include_paths.data) {
        for (int i = 0; i < vm->compiler.include_paths.len; i++)
            free(vm->compiler.include_paths.data[i]);
        free(vm->compiler.include_paths.data);
    }

    // Free system include paths
    if (vm->compiler.system_include_paths.data) {
        for (int i = 0; i < vm->compiler.system_include_paths.len; i++)
            free(vm->compiler.system_include_paths.data[i]);
        free(vm->compiler.system_include_paths.data);
    }

    // Free input files array
    if (vm->compiler.input_files)
        free(vm->compiler.input_files);

    // Free include_cache HashMap (values are malloc'd path strings)
    if (vm->compiler.include_cache.buckets) {
        for (int i = 0; i < vm->compiler.include_cache.capacity; i++) {
            HashEntry *entry = &vm->compiler.include_cache.buckets[i];
            if (entry->key && entry->key != (void *)-1 && entry->val)
                free(entry->val);
        }
        hashmap_deinit(&vm->compiler.include_cache);
    }

    // Free file buffers
    if (vm->compiler.file_buffers.data) {
        for (int i = 0; i < vm->compiler.file_buffers.len; i++)
            free(vm->compiler.file_buffers.data[i]);
        free(vm->compiler.file_buffers.data);
    }

    // Free watchpoint expressions
    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (vm->dbg.watchpoints[i].expr) {
            free(vm->dbg.watchpoints[i].expr);
            vm->dbg.watchpoints[i].expr = NULL;
        }
    }

    // Destroy parser arena (frees all tokens, AST nodes, preprocessor state)
    arena_destroy(&vm->compiler.parser_arena);
}

void cc_print_stack_report(JCC *vm) {
    if (!vm || !(vm->flags & JCC_STACK_INSTR)) {
        printf("Stack instrumentation not enabled.\n");
        return;
    }

    printf("\n========== STACK INSTRUMENTATION REPORT ==========\n");
    printf("Stack high water mark: %lld bytes\n", vm->stack_high_water);
    printf("Total scopes created: %d\n", vm->current_scope_id);
    printf("\n");

    // Collect and display variable statistics
    printf("Variable Access Statistics:\n");
    printf("%-20s %10s %10s %10s %10s\n", "Variable", "Scope", "Reads", "Writes", "Status");
    printf("%-20s %10s %10s %10s %10s\n", "--------", "-----", "-----", "------", "------");

    for (int i = 0; i < vm->stack_var_meta.capacity; i++) {
        if (vm->stack_var_meta.buckets[i].key != NULL) {
            StackVarMeta *meta = (StackVarMeta *)vm->stack_var_meta.buckets[i].val;
            if (meta) {
                const char *status = meta->is_alive ? "alive" : "dead";
                printf("%-20s %10d %10lld %10lld %10s\n",
                       meta->name ? meta->name : "<unknown>",
                       meta->scope_id,
                       meta->read_count,
                       meta->write_count,
                       status);
            }
        }
    }

    printf("=================================================\n\n");
}

void cc_include(JCC *vm, const char *path) {
    strarray_push(&vm->compiler.include_paths, strdup(path));
}

void cc_system_include(JCC *vm, const char *path) {
    strarray_push(&vm->compiler.system_include_paths, strdup(path));
}

void cc_define(JCC *vm, char *name, char *buf) {
    define_macro(vm, name, buf);
}

void cc_undef(JCC *vm, char *name) {
    undef_macro(vm, name);
}

void cc_set_asm_callback(JCC *vm, JCCAsmCallback callback, void *user_data) {
    vm->compiler.asm_callback = callback;
    vm->compiler.asm_user_data = user_data;
}

void cc_register_cfunc(JCC *vm, const char *name, void *func_ptr, int num_args, int returns_double) {
    cc_register_cfunc_ex(vm, name, func_ptr, num_args, returns_double, 0);
}

void cc_register_cfunc_ex(JCC *vm, const char *name, void *func_ptr, int num_args, int returns_double, uint64_t double_arg_mask) {
    if (!vm)
        error("cc_register_cfunc_ex: vm is NULL");
    if (!name || !func_ptr)
        error("cc_register_cfunc_ex: name or func_ptr is NULL");

    // Expand capacity if needed
    if (vm->compiler.ffi_count >= vm->compiler.ffi_capacity) {
        vm->compiler.ffi_capacity = vm->compiler.ffi_capacity ? vm->compiler.ffi_capacity * 2 : 32;
        vm->compiler.ffi_table = realloc(vm->compiler.ffi_table, vm->compiler.ffi_capacity * sizeof(ForeignFunc));
        if (!vm->compiler.ffi_table)
            error("cc_register_cfunc_ex: realloc failed");
    }

    // Add function to registry (non-variadic)
    vm->compiler.ffi_table[vm->compiler.ffi_count++] = (ForeignFunc){
        .name = strdup(name),
        .func_ptr = func_ptr,
        .num_args = num_args,
        .returns_double = returns_double,
        .is_variadic = 0,
        .num_fixed_args = num_args,
        .double_arg_mask = double_arg_mask
    };
}

void cc_register_variadic_cfunc(JCC *vm, const char *name, void *func_ptr, int num_fixed_args, int returns_double) {
    if (!vm)
        error("cc_register_variadic_cfunc: vm is NULL");
    if (!name || !func_ptr)
        error("cc_register_variadic_cfunc: name or func_ptr is NULL");

    // Expand capacity if needed
    if (vm->compiler.ffi_count >= vm->compiler.ffi_capacity) {
        vm->compiler.ffi_capacity = vm->compiler.ffi_capacity ? vm->compiler.ffi_capacity * 2 : 32;
        vm->compiler.ffi_table = realloc(vm->compiler.ffi_table, vm->compiler.ffi_capacity * sizeof(ForeignFunc));
        if (!vm->compiler.ffi_table)
            error("cc_register_variadic_cfunc: realloc failed");
    }

    // Add variadic function to registry
    // Note: num_args will be updated dynamically during CALLF based on actual call
    // For now, we set it to num_fixed_args as a placeholder
    vm->compiler.ffi_table[vm->compiler.ffi_count++] = (ForeignFunc){
        .name = strdup(name),
        .func_ptr = func_ptr,
        .num_args = num_fixed_args,  // Will be updated during CALLF
        .returns_double = returns_double,
        .is_variadic = 1,
        .num_fixed_args = num_fixed_args,
        .double_arg_mask = 0  // Variadic functions don't use mask - doubles passed as bits
    };
}

int cc_dlsym(JCC *vm, const char *name, void *func_ptr, int num_args, int returns_double) {
    if (!vm || !name || !func_ptr)
        return -1;

    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (strlen(vm->compiler.ffi_table[i].name) == strlen(name) &&
            strncmp(vm->compiler.ffi_table[i].name, name, strlen(name)) == 0) {
            if (vm->compiler.ffi_table[i].num_args != num_args || vm->compiler.ffi_table[i].returns_double != returns_double) {
                fprintf(stderr, "error: FFI function '%s' signature mismatch\n", name);
                return -1;
            }
            vm->compiler.ffi_table[i].func_ptr = func_ptr;
            return 0;
        }
    }

    fprintf(stderr, "error: FFI function '%s' not found in bytecode\n", name);
    return -1;
}

int cc_dlopen(JCC *vm, const char *lib_path) {
    if (!vm)
        return -1;

#ifdef _WIN32
    HMODULE handle;
    if (lib_path) {
        // Open the specified dynamic library
        handle = LoadLibraryA(lib_path);
        if (!handle) {
            DWORD err = GetLastError();
            fprintf(stderr, "error: failed to load library %s: error code %lu\n", lib_path, err);
            return -1;
        }
    } else {
        // For searching all loaded libraries, use NULL handle in GetProcAddress
        handle = NULL;
    }
#else
    void *handle = dlopen(lib_path, RTLD_LAZY);
    if (!handle && lib_path) {
        fprintf(stderr, "error: failed to load library %s: %s\n", lib_path, dlerror());
        return -1;
    }
#endif

    int success_count = 0;
    int total_count = vm->compiler.ffi_count;

    // Try to resolve each FFI function
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];

#ifdef _WIN32
        // Look up the symbol
        void *func_ptr = GetProcAddress(handle, ff->name);
        if (!func_ptr) {
            DWORD err = GetLastError();
            fprintf(stderr, "warning: failed to resolve symbol '%s': error code %lu\n", ff->name, err);
        } else {
            ff->func_ptr = func_ptr;
            success_count++;
            if (vm->debug_vm)
                printf("Resolved FFI function '%s' at %p\n", ff->name, func_ptr);
        }
#else
        // Clear any previous error
        dlerror();

        // Look up the symbol
        void *func_ptr = dlsym(handle, ff->name);
        const char *error = dlerror();

        if (error)
            fprintf(stderr, "warning: failed to resolve symbol '%s': %s\n", ff->name, error);
        else {
            ff->func_ptr = func_ptr;
            success_count++;
            if (vm->debug_vm)
                printf("Resolved FFI function '%s' at %p\n", ff->name, func_ptr);
        }
#endif
    }

    if (success_count == 0 && total_count > 0) {
        fprintf(stderr, "error: no FFI functions could be resolved\n");
#ifdef _WIN32
        if (lib_path)
            FreeLibrary(handle);
#else
        if (lib_path)
            dlclose(handle);
#endif
        return -1;
    }

    if (vm->debug_vm)
        printf("Loaded %d/%d FFI functions from %s\n", success_count, total_count, lib_path ? lib_path : "default libraries");

    // Don't close! Function pointers are still in use!
    // dlclose(handle); <- NO! BAD!
    return 0;
}

#ifndef PATH_MAX
#define PATH_MAX 256
#endif

// Get the platform-specific path of the standard C library
static const char* find_libc() {
#ifdef _WIN32
    // On Windows, LoadLibrary searches system paths, so return just the name
    return "msvcrt.dll";
#else
    static char path[PATH_MAX];
    const char *libname;
    const char **search_paths;

#ifdef __APPLE__
    libname = "libSystem.dylib";
    const char *apple_paths[] = {"/usr/lib/", NULL};
    search_paths = apple_paths;
#elif defined(__linux__)
    libname = "libc.so.6";
    const char *linux_paths[] = {"/lib64/", "/lib/x86_64-linux-gnu/", "/lib/", "/usr/lib64/", "/usr/lib/", NULL};
    search_paths = linux_paths;
#elif defined(__FreeBSD__)
    libname = "libc.so.7";
    const char *freebsd_paths[] = {"/lib/", "/usr/lib/", NULL};
    search_paths = freebsd_paths;
#else
    libname = "libc.so";
    const char *default_paths[] = {"/lib/", "/usr/lib/", NULL};
    search_paths = default_paths;
#endif

    // Try to find the library in standard locations
    for (const char **p = search_paths; *p; p++) {
        if (snprintf(path, sizeof(path), "%s%s", *p, libname) < sizeof(path)) {
            if (access(path, F_OK) == 0)
                return path;
        }
    }
    // Fallback to just the library name if full path not found
    return libname;
#endif
}

int cc_load_libc(JCC *vm) {
    const char *libc_path = find_libc();
    if (vm->debug_vm)
        printf("Loading standard C library: %s\n", libc_path);
    return cc_dlopen(vm, libc_path);
}

// Load standard library (for backward compatibility)
// This function is kept for programs that don't use #include,
// or want all stdlib functions available regardless of includes
void cc_load_stdlib(JCC *vm) {
    // Register all standard library functions regardless of includes
    register_ctype_functions(vm);
    register_math_functions(vm);
    register_stdio_functions(vm);
    register_stdlib_functions(vm);
    register_string_functions(vm);
    register_time_functions(vm);

    // Mark all headers as included
    hashmap_put(&vm->compiler.included_headers, "ctype.h", (void*)1);
    hashmap_put(&vm->compiler.included_headers, "math.h", (void*)1);
    hashmap_put(&vm->compiler.included_headers, "stdio.h", (void*)1);
    hashmap_put(&vm->compiler.included_headers, "stdlib.h", (void*)1);
    hashmap_put(&vm->compiler.included_headers, "string.h", (void*)1);
    hashmap_put(&vm->compiler.included_headers, "time.h", (void*)1);
}

int cc_run(JCC *vm, int argc, char **argv) {
    if (!vm || !vm->text_seg) {
        error("VM not initialized - call cc_compile first");
    }

    // Get entry point (main function) from text_seg[0]
    JCCPc main_addr = vm->text_seg[0];
    vm->pc = main_addr;

    // Setup stack — use poolsize_max so sp/bp sit at top of full reservation
    {
        size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
        size_t initial_stack  = (size_t)vm->poolsize * sizeof(long long);
        vm->sp = (long long *)((char *)vm->stack_seg + reserved_stack);
        vm->bp = vm->sp;  // Initialize base pointer to top of stack
        vm->stack_base = (long long *)((char *)vm->stack_seg +
                                       reserved_stack - initial_stack);

        // Shadow stack for CFI
        if (vm->flags & JCC_CFI) {
            vm->shadow_sp = (long long *)((char *)vm->shadow_stack + reserved_stack);
        }
    }

    // Save initial stack/base pointers for exit detection in vm_eval
    vm->initial_sp = vm->sp;
    vm->initial_bp = vm->bp;

    // Push a sentinel return address (0) so LEV can detect when main returns
    // Stack layout before main's ENT:
    // [argv] [argc] [ret=0] ← sp
    // ENT will push old_bp and set bp=sp
    *--vm->sp = (long long)argv;  // argv parameter (will be at bp+3 after ENT)
    *--vm->sp = argc;             // argc parameter (will be at bp+2 after ENT)
    *--vm->sp = 0;                // Return address = NULL (signals exit, will be at bp+1 after ENT)

    return (vm->flags & JCC_ENABLE_DEBUGGER) ? debugger_run(vm, argc, argv) : vm_eval(vm);
}

#if defined(_WIN32) || defined(_WIN64)
static size_t jcc_vm_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

void *jcc_vm_reserve(size_t bytes) {
    void *p = VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_NOACCESS);
    return p; // NULL on failure
}

int jcc_vm_commit(void *base, size_t off, size_t len) {
    size_t pgsz = jcc_vm_page_size();
    // Align offset down, extend length to cover full pages
    size_t aligned_off = off & ~(pgsz - 1);
    size_t aligned_end = (off + len + pgsz - 1) & ~(pgsz - 1);
    size_t aligned_len = aligned_end - aligned_off;
    void *p = VirtualAlloc((char *)base + aligned_off, aligned_len,
                           MEM_COMMIT, PAGE_READWRITE);
    return (p == NULL) ? -1 : 0;
}

void jcc_vm_release(void *base, size_t bytes) {
    (void)bytes;
    VirtualFree(base, 0, MEM_RELEASE);
}
#else // POSIX
static size_t jcc_vm_page_size(void) {
    return (size_t)sysconf(_SC_PAGESIZE);
}

void *jcc_vm_reserve(size_t bytes) {
    void *p = mmap(NULL, bytes, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

int jcc_vm_commit(void *base, size_t off, size_t len) {
    size_t pgsz = jcc_vm_page_size();
    // Align offset down, extend length to cover full pages
    size_t aligned_off = off & ~(pgsz - 1);
    size_t aligned_end = (off + len + pgsz - 1) & ~(pgsz - 1);
    size_t aligned_len = aligned_end - aligned_off;
    return mprotect((char *)base + aligned_off, aligned_len,
                    PROT_READ | PROT_WRITE);
}

void jcc_vm_release(void *base, size_t bytes) {
    munmap(base, bytes);
}
#endif
