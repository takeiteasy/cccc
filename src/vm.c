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

 This file was based on c4 by Robert Swierczek (rswier/c4) and the following
 write-a-C-interpreter tutorial by Jinzhou Zhang (lotabout/write-a-C-interpreter)
*/

#include "./internal.h"
#include <fenv.h> // host fenv.h -- #832, cc_run()'s pre-execution reset
#if !defined(_WIN32) && !defined(_WIN64)
#include <pthread.h>
#endif

// Global pointer to the currently executing VM instance.
// Set at the start of cc_run_at and cleared on return.
// Used by FFI shims (e.g. __cccc_pc_to_name) that need to reach the live VM
// without an explicit vm parameter, mirroring the __builtin_current_vm pattern.
// Protected by the recursive GIL (cccc_gil_acquire/release), so this is safe
// even when multiple VMs run sequentially in a process.
VirtualMachine *cc_running_vm = NULL;

#define CCCC_DYN_TOKEN_BASE (-0x4a434300LL)

static void cccc_set_dyn_error(VirtualMachine *vm, const char *fmt, ...) {
    if (!vm)
        return;
    free(vm->dyn_error);
    vm->dyn_error = NULL;

    va_list ap;
    va_start(ap, fmt);
    va_list ap2;
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n >= 0) {
        vm->dyn_error = malloc((size_t)n + 1);
        if (vm->dyn_error)
            vsnprintf(vm->dyn_error, (size_t)n + 1, fmt, ap2);
    }
    va_end(ap2);
}

static void cccc_clear_dyn_error(VirtualMachine *vm) {
    if (!vm)
        return;
    free(vm->dyn_error);
    vm->dyn_error = NULL;
}

static DynamicLibrary *cccc_find_dynamic_library(VirtualMachine *vm, long long token,
                                                int *idx_out) {
    if (!vm)
        return NULL;
    for (int i = 0; i < vm->dynlib_count; i++) {
        if (vm->dynlibs[i].token == (int)token && !vm->dynlibs[i].is_closed) {
            if (idx_out)
                *idx_out = i;
            return &vm->dynlibs[i];
        }
    }
    return NULL;
}

DynamicSymbol *cccc_find_dynamic_symbol(VirtualMachine *vm, long long token) {
    if (!vm)
        return NULL;
    for (int i = 0; i < vm->dynsym_count; i++) {
        DynamicSymbol *sym = &vm->dynsyms[i];
        if (sym->token == (int)token && sym->is_live)
            return sym;
    }
    return NULL;
}

static int cccc_add_dynamic_library(VirtualMachine *vm, void *handle, const char *path) {
    if (vm->dynlib_count >= vm->dynlib_capacity) {
        int new_cap = vm->dynlib_capacity ? vm->dynlib_capacity * 2 : 16;
        DynamicLibrary *new_libs =
            realloc(vm->dynlibs, (size_t)new_cap * sizeof(DynamicLibrary));
        if (!new_libs) {
            cccc_set_dyn_error(vm, "dynamic library registry allocation failed");
            return 0;
        }
        vm->dynlibs = new_libs;
        vm->dynlib_capacity = new_cap;
    }

    int token = CCCC_DYN_TOKEN_BASE - vm->dyn_next_token++;
    vm->dynlibs[vm->dynlib_count++] = (DynamicLibrary){
        .handle = handle,
        .path = path ? strdup(path) : NULL,
        .token = token,
        .live_symbol_count = 0,
        .is_closed = 0,
    };
    return token;
}

static int cccc_add_dynamic_symbol(VirtualMachine *vm, int lib_idx, void *func_ptr,
                                  const char *name) {
    if (vm->dynsym_count >= vm->dynsym_capacity) {
        int new_cap = vm->dynsym_capacity ? vm->dynsym_capacity * 2 : 32;
        DynamicSymbol *new_syms =
            realloc(vm->dynsyms, (size_t)new_cap * sizeof(DynamicSymbol));
        if (!new_syms) {
            cccc_set_dyn_error(vm, "dynamic symbol registry allocation failed");
            return 0;
        }
        vm->dynsyms = new_syms;
        vm->dynsym_capacity = new_cap;
    }

    int token = CCCC_DYN_TOKEN_BASE - vm->dyn_next_token++;
    vm->dynsyms[vm->dynsym_count++] = (DynamicSymbol){
        .func_ptr = func_ptr,
        .name = name ? strdup(name) : NULL,
        .token = token,
        .library_index = lib_idx,
        .is_live = 1,
    };
    vm->dynlibs[lib_idx].live_symbol_count++;
    return token;
}

long long cccc_rt_dlopen(VirtualMachine *vm, const char *path, int mode) {
    if (!vm)
        return 0;
    cccc_clear_dyn_error(vm);

#if defined(_WIN32) || defined(_WIN64)
    (void)mode;
    HMODULE handle = path ? LoadLibraryA(path) : GetModuleHandleA(NULL);
    if (!handle) {
        cccc_set_dyn_error(vm, "dlopen failed: error code %lu",
                          (unsigned long)GetLastError());
        return 0;
    }
    return cccc_add_dynamic_library(vm, (void *)handle, path);
#else
    void *handle = dlopen(path, mode ? mode : RTLD_LAZY);
    if (!handle) {
        const char *err = dlerror();
        cccc_set_dyn_error(vm, "%s", err ? err : "dlopen failed");
        return 0;
    }
    return cccc_add_dynamic_library(vm, handle, path);
#endif
}

long long cccc_rt_dlsym(VirtualMachine *vm, long long handle_token, const char *symbol) {
    if (!vm || !symbol) {
        cccc_set_dyn_error(vm, "dlsym requires a symbol name");
        return 0;
    }
    cccc_clear_dyn_error(vm);

    int lib_idx = -1;
    DynamicLibrary *lib = cccc_find_dynamic_library(vm, handle_token, &lib_idx);
    if (!lib) {
        cccc_set_dyn_error(vm, "invalid dynamic library handle");
        return 0;
    }

#if defined(_WIN32) || defined(_WIN64)
    void *ptr = (void *)GetProcAddress((HMODULE)lib->handle, symbol);
    if (!ptr) {
        cccc_set_dyn_error(vm, "dlsym failed for '%s': error code %lu", symbol,
                          (unsigned long)GetLastError());
        return 0;
    }
#else
    dlerror();
    void *ptr = dlsym(lib->handle, symbol);
    const char *err = dlerror();
    if (err) {
        cccc_set_dyn_error(vm, "%s", err);
        return 0;
    }
#endif

    // Apply FFI policy checks at lookup time so denied symbols cannot be
    // obtained as callable function pointers.
    if (vm->disable_all_ffi) {
        cccc_set_dyn_error(vm, "dlsym blocked: all FFI calls are disabled");
        return 0;
    }
    if (vm->ffi_allow_count > 0 &&
        !cccc_ffi_name_in_list(vm->ffi_allow_list, vm->ffi_allow_count, symbol)) {
        cccc_set_dyn_error(vm, "dlsym blocked: '%s' not in --ffi-allow list",
                          symbol);
        return 0;
    }
    if (vm->ffi_allow_count == 0 &&
        cccc_ffi_name_in_list(vm->ffi_deny_list, vm->ffi_deny_count, symbol)) {
        cccc_set_dyn_error(vm, "dlsym blocked: '%s' is in --ffi-deny list",
                          symbol);
        return 0;
    }

    return cccc_add_dynamic_symbol(vm, lib_idx, ptr, symbol);
}

long long cccc_rt_dlclose(VirtualMachine *vm, long long handle_token) {
    if (!vm)
        return -1;
    cccc_clear_dyn_error(vm);

    int lib_idx = -1;
    DynamicLibrary *lib = cccc_find_dynamic_library(vm, handle_token, &lib_idx);
    (void)lib_idx;
    if (!lib) {
        cccc_set_dyn_error(vm, "invalid dynamic library handle");
        return -1;
    }
    if (lib->live_symbol_count > 0) {
        cccc_set_dyn_error(vm, "cannot dlclose handle with live callable symbols");
        return -1;
    }

#if defined(_WIN32) || defined(_WIN64)
    if (lib->path && !FreeLibrary((HMODULE)lib->handle)) {
        cccc_set_dyn_error(vm, "dlclose failed: error code %lu",
                          (unsigned long)GetLastError());
        return -1;
    }
#else
    if (dlclose(lib->handle) != 0) {
        const char *err = dlerror();
        cccc_set_dyn_error(vm, "%s", err ? err : "dlclose failed");
        return -1;
    }
#endif
    lib->is_closed = 1;
    return 0;
}

long long cccc_rt_dlerror(VirtualMachine *vm) {
    if (!vm || !vm->dyn_error)
        return 0;
    return (long long)vm->dyn_error;
}

#include "ops.c"

void cc_vm_profile_reset(VirtualMachine *vm) {
    if (!vm)
        return;
    memset(vm->vm_profile_counts, 0, sizeof(vm->vm_profile_counts));
    vm->vm_profile_total = 0;
    memset(vm->vm_profile_bigram_counts, 0,
           sizeof(vm->vm_profile_bigram_counts));
    vm->vm_profile_bigram_total = 0;
    vm->vm_profile_prev_op = -1;
    vm->vm_profile_bigram_started = false;
    if (vm->vm_profile_trigram_counts)
        memset(vm->vm_profile_trigram_counts, 0,
               (size_t)OP_COUNT * OP_COUNT * OP_COUNT * sizeof(uint64_t));
    vm->vm_profile_trigram_total = 0;
    vm->vm_profile_prev2_op = -1;
    vm->vm_profile_trigram_started = false;
}

// #767: number of currently-allocated (non-NULL) CHKT3 shadow pages across
// both segments -- derived on demand rather than tracked incrementally, so
// it costs nothing on the hot alloc/stamp/clear paths. Counts pages
// type_shadow_sweep hasn't (yet, or ever needed to) reclaim; a healthy
// --type-checks run's live count tracks stamped footprint, not segment
// reservation.
static size_t type_shadow_pages_live(VirtualMachine *vm) {
    size_t live = 0;
    for (size_t i = 0; i < vm->heap_shadow.page_count; i++)
        if (vm->heap_shadow.pages[i])
            live++;
    for (size_t i = 0; i < vm->data_shadow.page_count; i++)
        if (vm->data_shadow.pages[i])
            live++;
    return live;
}

void cc_vm_profile_print(VirtualMachine *vm, FILE *f) {
    if (!vm || !f || !vm->vm_profile_enabled)
        return;

    fprintf(f, "\nVM opcode profile\n");
    fprintf(f, "total_opcodes: %llu\n",
            (unsigned long long)vm->vm_profile_total);
    fprintf(f, "cycles:        %lld\n", vm->cycle);
    fprintf(f, "shadow_sweeps: %llu\n",
            (unsigned long long)vm->type_shadow_sweeps);
    fprintf(f, "shadow_pages_swept: %llu\n",
            (unsigned long long)vm->type_shadow_pages_swept);
    fprintf(f, "shadow_pages_live: %llu\n",
            (unsigned long long)type_shadow_pages_live(vm));
    if (vm->vm_profile_total == 0)
        return;

    bool printed[OP_COUNT] = {0};
    for (;;) {
        int best = -1;
        for (int op = 0; op < OP_COUNT; op++) {
            if (printed[op] || vm->vm_profile_counts[op] == 0)
                continue;
            if (best < 0 ||
                vm->vm_profile_counts[op] > vm->vm_profile_counts[best])
                best = op;
        }
        if (best < 0 || vm->vm_profile_counts[best] == 0)
            break;
        printed[best] = true;

        const char *name = cc_opcode_name(best);
        double pct = (double)vm->vm_profile_counts[best] * 100.0 /
                     (double)vm->vm_profile_total;
        fprintf(f, "%-12s %12llu %6.2f%%\n", name ? name : "UNKNOWN",
                (unsigned long long)vm->vm_profile_counts[best], pct);
    }

    fprintf(f, "\nVM opcode bigram profile (top 25)\n");
    fprintf(f, "total_transitions: %llu\n",
            (unsigned long long)vm->vm_profile_bigram_total);
    if (vm->vm_profile_bigram_total == 0)
        return;

    bool bg_printed[OP_COUNT * OP_COUNT] = {0};
    for (int shown = 0; shown < 25; shown++) {
        int best = -1;
        for (int i = 0; i < OP_COUNT * OP_COUNT; i++) {
            if (bg_printed[i] || vm->vm_profile_bigram_counts[i] == 0)
                continue;
            if (best < 0 ||
                vm->vm_profile_bigram_counts[i] >
                    vm->vm_profile_bigram_counts[best])
                best = i;
        }
        if (best < 0 || vm->vm_profile_bigram_counts[best] == 0)
            break;
        bg_printed[best] = true;

        int prev = best / OP_COUNT;
        int cur = best % OP_COUNT;
        const char *pn = cc_opcode_name(prev);
        const char *cn = cc_opcode_name(cur);
        double bpct = (double)vm->vm_profile_bigram_counts[best] * 100.0 /
                      (double)vm->vm_profile_bigram_total;
        fprintf(f, "%-12s %-12s %12llu %6.2f%%\n", pn ? pn : "UNKNOWN",
                cn ? cn : "UNKNOWN",
                (unsigned long long)vm->vm_profile_bigram_counts[best], bpct);
    }
}

static void json_escape(FILE *f, const char *s) {
    for (; s && *s; s++) {
        unsigned char c = (unsigned char)*s;
        switch (c) {
        case '\\':
            fputs("\\\\", f);
            break;
        case '"':
            fputs("\\\"", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if (c < 0x20)
                fprintf(f, "\\u%04x", c);
            else
                fputc(c, f);
            break;
        }
    }
}

int cc_vm_profile_write_json(VirtualMachine *vm, FILE *f, const char *mode,
                             const char *input_name) {
    if (!vm || !f)
        return -1;

    fprintf(f, "{\n");
    fprintf(f, "  \"tool\": \"cccc-vm-profile\",\n");
    fprintf(f, "  \"version\": \"1\",\n");
    fprintf(f, "  \"mode\": \"");
    json_escape(f, mode ? mode : "unknown");
    fprintf(f, "\",\n");
    fprintf(f, "  \"input\": \"");
    json_escape(f, input_name ? input_name : "");
    fprintf(f, "\",\n");
    fprintf(f, "  \"optimize_level\": %d,\n", vm->compiler.opt_level);
    fprintf(f, "  \"cycles\": %lld,\n", vm->cycle);
    fprintf(f, "  \"shadow_sweeps\": %llu,\n",
            (unsigned long long)vm->type_shadow_sweeps);
    fprintf(f, "  \"shadow_pages_swept\": %llu,\n",
            (unsigned long long)vm->type_shadow_pages_swept);
    fprintf(f, "  \"shadow_pages_live\": %llu,\n",
            (unsigned long long)type_shadow_pages_live(vm));
    fprintf(f, "  \"total_opcodes\": %llu,\n",
            (unsigned long long)vm->vm_profile_total);
    fprintf(f, "  \"opcodes\": [\n");

    bool first = true;
    for (int op = 0; op < OP_COUNT; op++) {
        uint64_t count = vm->vm_profile_counts[op];
        if (count == 0)
            continue;
        const char *name = cc_opcode_name(op);
        double pct = vm->vm_profile_total
                         ? (double)count * 100.0 /
                               (double)vm->vm_profile_total
                         : 0.0;
        if (!first)
            fprintf(f, ",\n");
        first = false;
        fprintf(f, "    {\"opcode\": \"");
        json_escape(f, name ? name : "UNKNOWN");
        fprintf(f, "\", \"count\": %llu, \"percent\": %.6f}",
                (unsigned long long)count, pct);
    }

    fprintf(f, "\n  ],\n");
    fprintf(f, "  \"total_bigrams\": %llu,\n",
            (unsigned long long)vm->vm_profile_bigram_total);
    fprintf(f, "  \"bigrams\": [");
    bool first_bg = true;
    for (int prev = 0; prev < OP_COUNT; prev++) {
        for (int cur = 0; cur < OP_COUNT; cur++) {
            uint64_t count =
                vm->vm_profile_bigram_counts[prev * OP_COUNT + cur];
            if (count == 0)
                continue;
            const char *pn = cc_opcode_name(prev);
            const char *cn = cc_opcode_name(cur);
            double bpct = vm->vm_profile_bigram_total
                              ? (double)count * 100.0 /
                                    (double)vm->vm_profile_bigram_total
                              : 0.0;
            fprintf(f, "%s\n    {\"from\": \"", first_bg ? "" : ",");
            json_escape(f, pn ? pn : "UNKNOWN");
            fprintf(f, "\", \"to\": \"");
            json_escape(f, cn ? cn : "UNKNOWN");
            fprintf(f, "\", \"count\": %llu, \"percent\": %.6f}",
                    (unsigned long long)count, bpct);
            first_bg = false;
        }
    }
    fprintf(f, "%s\n  ]", first_bg ? "" : "\n  ");

    // Trigram section (only when tracking was enabled and data exists)
    if (vm->vm_profile_trigram_counts && vm->vm_profile_trigram_total > 0) {
        // Collect top-25 trigrams by count
        typedef struct { int a, b, c; uint64_t count; } TG;
        int cap = 25;
        TG *top = calloc(cap, sizeof(TG));
        if (top) {
            for (int a = 0; a < OP_COUNT; a++) {
                for (int b = 0; b < OP_COUNT; b++) {
                    for (int c = 0; c < OP_COUNT; c++) {
                        uint64_t cnt = vm->vm_profile_trigram_counts[
                            ((size_t)a * OP_COUNT + b) * OP_COUNT + c];
                        if (cnt == 0) continue;
                        // Insert into top[] if larger than minimum
                        int min_idx = 0;
                        for (int k = 1; k < cap; k++)
                            if (top[k].count < top[min_idx].count)
                                min_idx = k;
                        if (cnt > top[min_idx].count)
                            top[min_idx] = (TG){a, b, c, cnt};
                    }
                }
            }
            // Sort descending by count (simple selection sort for 25 elements)
            for (int i = 0; i < cap - 1; i++)
                for (int j = i + 1; j < cap; j++)
                    if (top[j].count > top[i].count) { TG tmp = top[i]; top[i] = top[j]; top[j] = tmp; }

            fprintf(f, ",\n  \"total_trigrams\": %llu,\n",
                    (unsigned long long)vm->vm_profile_trigram_total);
            fprintf(f, "  \"trigrams\": [");
            bool first_tg = true;
            for (int i = 0; i < cap; i++) {
                if (top[i].count == 0) break;
                const char *an = cc_opcode_name(top[i].a);
                const char *bn = cc_opcode_name(top[i].b);
                const char *cn = cc_opcode_name(top[i].c);
                double tpct = (double)top[i].count * 100.0 /
                              (double)vm->vm_profile_trigram_total;
                fprintf(f, "%s\n    {\"a\": \"", first_tg ? "" : ",");
                json_escape(f, an ? an : "UNKNOWN");
                fprintf(f, "\", \"b\": \"");
                json_escape(f, bn ? bn : "UNKNOWN");
                fprintf(f, "\", \"c\": \"");
                json_escape(f, cn ? cn : "UNKNOWN");
                fprintf(f, "\", \"count\": %llu, \"percent\": %.6f}",
                        (unsigned long long)top[i].count, tpct);
                first_tg = false;
            }
            fprintf(f, "\n  ]");
            free(top);
        }
    }

    fprintf(f, "\n}\n");
    return ferror(f) ? -1 : 0;
}

/* Whether a fatal VM error should drop into the interactive debugger instead
 * of returning to the caller (ticket #405). Active whenever the debugger is
 * enabled (explicit -g, or auto-enabled for an interactive TTY session) and
 * the user hasn't opted out via --no-debug-on-crash. */
static inline bool vm_crash_trap_active(VirtualMachine *vm) {
    return (vm->flags & CCCC_ENABLE_DEBUGGER) &&
           !(vm->flags & CCCC_NO_DEBUG_ON_CRASH);
}

// #1013: record that an opcode handler aborted execution with a fatal
// safety violation, so a caller like --testing (src/testing.c) can tell a
// genuine trap apart from a guest function that legitimately returns -1
// through REG_A0 -- vm_eval's own return value can't distinguish the two.
static inline void cccc_vm_note_fault(VirtualMachine *vm, const char *op_name) {
    vm->runtime_fault    = true;
    vm->runtime_fault_op = op_name;
    vm->runtime_fault_pc = vm->pc;
}

int cccc_vm_eval_dispatch(VirtualMachine *vm, volatile Pc *current_pc) {
    static void *op_table[] = {
#define X(NAME, OPERANDS) [NAME] = &&op_##NAME,
        OPS_X
#undef X
    };

dispatch:
    if (__builtin_expect(vm->dbg.host_fault_signal != 0, 0))
        return CCCC_HOST_SIGNAL_RC;
    vm->cycle++;
    if (current_pc)
        *current_pc = vm->pc;

/* Trap into the debugger REPL on a fatal error and retry (the user may
 * inspect/fix state and 'continue'); otherwise propagate the error as
 * before. Defined inside vm_eval so it can `goto dispatch`. */
#define VM_TRAP_OR_RETURN(rc)                  \
    do {                                       \
        if (vm_crash_trap_active(vm)) {        \
            cc_debug_repl(vm);                 \
            vm->runtime_fault = false;         \
            goto dispatch;                     \
        }                                      \
        return (rc);                           \
    } while (0)

    /* #787: unwind SigFrames for VM signal handlers that have returned,
       restoring vm->sig_blocked so a deferred signal can be redelivered. */
    if (__builtin_expect(vm->sig_depth != 0, 0))
        cccc_signal_poll_handler_returns(vm);

    /* Poll pending signals (fast path: branch predicted not-taken) */
    if (__builtin_expect(_cccc_any_pending != 0, 0)) {
        _cccc_any_pending = 0;
        bool _still_pending = false;
        for (int _sig = 1; _sig < CCCC_NSIG; _sig++) {
            if (!_cccc_pending[_sig]) continue;
            /* #787: a blocked signal stays pending (delivered once
               unblocked) rather than being dropped. */
            if (vm->sig_blocked & (1u << (unsigned)(_sig - 1))) {
                _still_pending = true;
                continue;
            }
            SigSlot *_slot = &vm->vm_sigslots[_sig];
            /* #877: a VM handler needs a SigFrame slot to carry its
               register-file snapshot (async delivery can land mid-
               instruction, unlike a call boundary). If the frame stack is
               full, defer this delivery -- leave it pending -- rather than
               deliver with unsaved registers. */
            if (_slot->action == 2 && vm->sig_depth >= CCCC_SIG_FRAME_MAX) {
                _still_pending = true;
                continue;
            }
            _cccc_pending[_sig] = 0;
            if (_slot->action == 1) continue; /* IGN */
            if (_slot->action == 2) {
                /* VM handler: push return address and jump to handler */
                Pc _target = cc_byte_offset_to_pc(_slot->handler_fn);
                if (_target == CCCC_INVALID_PC || _target > vm->text_ptr) {
                    fprintf(stderr, "error: invalid signal handler for sig %d\n", _sig);
                    VM_TRAP_OR_RETURN(-1);
                }
                if (check_stack_overflow(vm, 1)) VM_TRAP_OR_RETURN(-1);
                *--vm->sp = (long long)vm->pc;
                if (vm->flags & CCCC_CFI) *--vm->shadow_sp = (long long)vm->pc;
                vm->regs[REG_A0] = (long long)_sig;
                int _flags = cccc_signal_prepare_delivery(vm, _sig, _slot, /*async=*/true);
                if (_flags & SA_SIGINFO) {
                    /* #745: real captured siginfo -- this path only runs
                       after an actual host-level signal delivery. */
                    vm->regs[REG_A1] = cccc_guest_siginfo_for(_sig, 0);
                    vm->regs[REG_A2] = 0; /* ucontext: not modelled */
                }
                vm->pc = _target;
                /* Other signals may still be pending behind this one --
                   don't let clearing _cccc_any_pending above cause them to
                   be missed until some unrelated event re-sets it. */
                for (int _k = 1; _k < CCCC_NSIG; _k++) {
                    if (_cccc_pending[_k]) { _cccc_any_pending = 1; break; }
                }
                goto dispatch; /* resume at handler; delivers one signal per re-entry */
            }
            /* DFL: delegate to host */
            raise(_sig);
        }
        if (_still_pending) _cccc_any_pending = 1;
    }

    /* Poll pending SIGEV_THREAD notifications (#870) -- same safe point and
       same shape as the pending-signal poll just above: a host notification
       thread (aio_*()/mq_notify(), src/stdlib/posix_aio.c/posix_mqueue.c) can only latch a
       cookie async-signal-safely, so the guest sigev_notify_function is
       actually invoked here, one notification per re-entry.
       Shares the pending-signal poll's async-delivery hazard -- this can
       land between ANY two bytecode instructions, not just a call
       boundary -- and shares its fix (#877): cccc_signal_push_async_frame
       snapshots the full register file into a SigFrame before REG_A0 is
       overwritten below, and cccc_signal_poll_handler_returns (called at
       the top of this same dispatch loop, gated on vm->sig_depth != 0)
       restores it once the guest sigev_notify_function genuinely returns.
       As with the pending-signal poll, a full frame stack defers this
       notification (leaves it pending) rather than delivering with
       unsaved registers. */
    if (__builtin_expect(_cccc_sigev_any_pending != 0, 0)) {
        _cccc_sigev_any_pending = 0;
        for (int _idx = 0; _idx < CCCC_SIGEV_MAX; _idx++) {
            if (!_cccc_sigev_pending[_idx]) continue;
            if (vm->sig_depth >= CCCC_SIG_FRAME_MAX) {
                _cccc_sigev_any_pending = 1; /* defer -- retry once a frame frees up */
                break;
            }
            _cccc_sigev_pending[_idx] = 0;
            long long _sigev_fn = 0, _sigev_sival_addr = 0;
            if (!cccc_sigev_cookie_guest_fn(_idx, &_sigev_fn, &_sigev_sival_addr))
                continue; /* cancelled/deregistered since the trampoline fired */
            Pc _sigev_target = cc_byte_offset_to_pc(_sigev_fn);
            if (_sigev_target == CCCC_INVALID_PC || _sigev_target > vm->text_ptr) {
                fprintf(stderr, "error: invalid sigev_notify_function\n");
                cccc_sigev_cookie_free(_idx);
                continue;
            }
            if (check_stack_overflow(vm, 1)) VM_TRAP_OR_RETURN(-1);
            *--vm->sp = (long long)vm->pc;
            if (vm->flags & CCCC_CFI) *--vm->shadow_sp = (long long)vm->pc;
            cccc_signal_push_async_frame(vm); /* snapshot regs before REG_A0 is clobbered below */
            /* union sigval is passed by reference (struct/union-by-value
               ABI, see cccc_sigev_cookie_guest_fn's comment) -- REG_A0 is
               the address of the cookie's persistent storage, not its
               8 raw bytes. */
            vm->regs[REG_A0] = _sigev_sival_addr;
            cccc_sigev_cookie_free(_idx); /* one-shot, matches POSIX semantics */
            vm->pc = _sigev_target;
            /* Other notifications may still be pending behind this one. */
            for (int _k = 0; _k < CCCC_SIGEV_MAX; _k++) {
                if (_cccc_sigev_pending[_k]) { _cccc_sigev_any_pending = 1; break; }
            }
            goto dispatch;
        }
    }

    if (vm->flags & CCCC_ENABLE_DEBUGGER) {
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
        if (__builtin_expect(vm->pc == CCCC_INVALID_PC, 0))
            return (int)vm->regs[REG_A0];
        int op = (int)cc_read_word(vm);
        if (__builtin_expect(op < 0 || op >= (int)(sizeof(op_table) / sizeof(op_table[0])) || !op_table[op], 0)) {
            printf("unknown instruction:%d\n", op);
            cccc_vm_note_fault(vm, "<unknown>");
            VM_TRAP_OR_RETURN(-1);
        }
        if (__builtin_expect(vm->vm_profile_enabled, 0)) {
            vm->vm_profile_counts[op]++;
            vm->vm_profile_total++;
            if (vm->vm_profile_bigram_started) {
                int prev = vm->vm_profile_prev_op;
                vm->vm_profile_bigram_counts[prev * OP_COUNT + op]++;
                vm->vm_profile_bigram_total++;
                if (vm->vm_profile_trigram_counts) {
                    if (vm->vm_profile_trigram_started) {
                        int prev2 = vm->vm_profile_prev2_op;
                        vm->vm_profile_trigram_counts[
                            ((size_t)prev2 * OP_COUNT + prev) * OP_COUNT + op]++;
                        vm->vm_profile_trigram_total++;
                    }
                    vm->vm_profile_prev2_op = prev;
                    vm->vm_profile_trigram_started = true;
                }
            } else {
                vm->vm_profile_bigram_started = true;
            }
            vm->vm_profile_prev_op = op;
        }
        if (vm->debug_vm) {
            const char *name = cc_opcode_name(op);
            if (name)
                printf("%lld> %s\n", vm->cycle, name);
            else
                printf("%lld> OP_%d\n", vm->cycle, op);
        }
        goto *op_table[op];
    }

#define X(NAME, OPERANDS)                                        \
    op_##NAME: {                                                 \
        int _r = op_##NAME##_fn(vm);                             \
        if (__builtin_expect(_r != 0, 0)) {                      \
            cccc_vm_note_fault(vm, #NAME);                       \
            VM_TRAP_OR_RETURN(_r);                                \
        }                                                        \
        if (__builtin_expect(vm->pc == CCCC_INVALID_PC, 0))       \
            return (int)vm->regs[REG_A0];                        \
        goto dispatch;                                           \
    }
    OPS_X
#undef X

#undef VM_TRAP_OR_RETURN
    return -1;
}

// ========== Segment reserve-and-commit helpers ==========

void vm_alloc_segments(VirtualMachine *vm) {
    size_t initial_text   = (size_t)vm->poolsize * sizeof(InstrWord);
    size_t initial_data   = (size_t)vm->poolsize;
    size_t initial_stack  = (size_t)vm->poolsize * sizeof(long long);
    size_t initial_heap   = (size_t)vm->poolsize;

    size_t reserved_text  = (size_t)vm->poolsize_max * sizeof(InstrWord);
    size_t reserved_data  = (size_t)vm->poolsize_max;
    size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
    size_t reserved_heap  = (size_t)vm->poolsize_max;

    // Reserve virtual ranges (base pointers will never move)
    vm->text_seg  = (InstrWord *)cccc_vm_reserve(reserved_text);
    vm->data_seg  = (char *)cccc_vm_reserve(reserved_data);
    vm->stack_seg = (long long *)cccc_vm_reserve(reserved_stack);
    vm->heap_seg  = (char *)cccc_vm_reserve(reserved_heap);
    if (!vm->text_seg || !vm->data_seg || !vm->stack_seg || !vm->heap_seg)
        error("could not reserve VM memory segments");

    // Stack grows downward: commit the TOP initial_stack bytes so that
    // sp/bp start at the top of the reservation and stack_base sits at
    // the bottom of the initially-committed slice.
    size_t stack_top_off = reserved_stack - initial_stack;

    if (cccc_vm_commit(vm->text_seg,  0,             initial_text)  != 0 ||
        cccc_vm_commit(vm->data_seg,  0,             initial_data)  != 0 ||
        cccc_vm_commit(vm->stack_seg, stack_top_off, initial_stack) != 0 ||
        cccc_vm_commit(vm->heap_seg,  0,             initial_heap)  != 0)
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
    if (vm->flags & CCCC_CFI) {
        vm->shadow_stack = (long long *)cccc_vm_reserve(reserved_stack);
        if (!vm->shadow_stack)
            error("could not reserve shadow stack");
        if (cccc_vm_commit(vm->shadow_stack, stack_top_off, initial_stack) != 0)
            error("could not commit shadow stack");
        vm->shadow_sp = (long long *)((char *)vm->shadow_stack + reserved_stack);
    }
}

void cccc_exec_state_save(VirtualMachine *vm, ExecState *state) {
    memcpy(state->regs, vm->regs, sizeof(state->regs));
    memcpy(state->fregs, vm->fregs, sizeof(state->fregs));
    memcpy(state->vregs, vm->vregs, sizeof(state->vregs));
    state->pc = vm->pc;
    state->bp = vm->bp;
    state->sp = vm->sp;
    state->cycle = vm->cycle;
    state->initial_sp = vm->initial_sp;
    state->initial_bp = vm->initial_bp;
    state->stack_seg = vm->stack_seg;
    state->stack_base = vm->stack_base;
    state->stack_committed = vm->stack_committed;
    state->shadow_stack = vm->shadow_stack;
    state->shadow_sp = vm->shadow_sp;
    state->init_state = vm->init_state;
    // #866: dangling-detector bookkeeping is per-thread, same as everything
    // else above -- must move with the rest of this thread's execution
    // state, not stay behind as VM-wide state another thread would then
    // desync against.
    state->frame_epoch_counter = vm->frame_epoch_counter;
    state->frame_epochs = vm->frame_epochs;
    state->live_epochs = vm->live_epochs;
    state->stack_ptr_epochs = vm->stack_ptr_epochs;
    state->stack_intervals = vm->stack_intervals;
}

void cccc_exec_state_restore(VirtualMachine *vm, const ExecState *state) {
    memcpy(vm->regs, state->regs, sizeof(state->regs));
    memcpy(vm->fregs, state->fregs, sizeof(state->fregs));
    memcpy(vm->vregs, state->vregs, sizeof(state->vregs));
    vm->pc = state->pc;
    vm->bp = state->bp;
    vm->sp = state->sp;
    vm->cycle = state->cycle;
    vm->initial_sp = state->initial_sp;
    vm->initial_bp = state->initial_bp;
    vm->stack_seg = state->stack_seg;
    vm->stack_base = state->stack_base;
    vm->stack_committed = state->stack_committed;
    vm->shadow_stack = state->shadow_stack;
    vm->shadow_sp = state->shadow_sp;
    vm->init_state = state->init_state;
    // #866: see the matching comment in cccc_exec_state_save above.
    vm->frame_epoch_counter = state->frame_epoch_counter;
    vm->frame_epochs = state->frame_epochs;
    vm->live_epochs = state->live_epochs;
    vm->stack_ptr_epochs = state->stack_ptr_epochs;
    vm->stack_intervals = state->stack_intervals;
}

int cccc_exec_state_alloc_stack(VirtualMachine *vm, ExecState *state) {
    memset(state, 0, sizeof(*state));

    size_t initial_stack = (size_t)vm->poolsize * sizeof(long long);
    size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
    size_t stack_top_off = reserved_stack - initial_stack;

    state->stack_seg = (long long *)cccc_vm_reserve(reserved_stack);
    if (!state->stack_seg)
        return -1;
    if (cccc_vm_commit(state->stack_seg, stack_top_off, initial_stack) != 0) {
        cccc_vm_release(state->stack_seg, reserved_stack);
        memset(state, 0, sizeof(*state));
        return -1;
    }

    state->stack_committed = initial_stack;
    state->sp = (long long *)((char *)state->stack_seg + reserved_stack);
    state->bp = state->sp;
    state->stack_base = (long long *)((char *)state->stack_seg + stack_top_off);
    state->initial_sp = state->sp;
    state->initial_bp = state->bp;

    if (vm->flags & CCCC_CFI) {
        state->shadow_stack = (long long *)cccc_vm_reserve(reserved_stack);
        if (!state->shadow_stack) {
            cccc_exec_state_release_stack(vm, state);
            return -1;
        }
        if (cccc_vm_commit(state->shadow_stack, stack_top_off, initial_stack) != 0) {
            cccc_exec_state_release_stack(vm, state);
            return -1;
        }
        state->shadow_sp = (long long *)((char *)state->shadow_stack + reserved_stack);
    }

    return 0;
}

void cccc_exec_state_release_stack(VirtualMachine *vm, ExecState *state) {
    if (!state)
        return;
    size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
    if (state->stack_seg)
        cccc_vm_release(state->stack_seg, reserved_stack);
    if (state->shadow_stack)
        cccc_vm_release(state->shadow_stack, reserved_stack);
    hashmap_deinit(&state->init_state);
    // #866: mirrors the vm-level dangling-detector cleanup in cc_destroy --
    // now needed here too since this state is a thread's own copy rather
    // than an alias of the VM-wide fields. By the time a thread's ExecState
    // reaches here (via free_thread_record, after cccc_exec_state_save has
    // captured its final state), frame_epochs/live_epochs/stack_ptr_epochs
    // should already be logically empty (the thread's own call chain fully
    // unwound), but stack_intervals retains dead-frame entries by design
    // (#675) and the hashmaps/arrays themselves still own allocated storage
    // either way.
    hashmap_deinit(&state->live_epochs);
    hashmap_deinit(&state->stack_ptr_epochs);
    free(state->frame_epochs.bps);
    free(state->frame_epochs.epochs);
    free(state->stack_intervals.iv);
    state->frame_epochs.bps = NULL;
    state->frame_epochs.epochs = NULL;
    state->frame_epochs.count = 0;
    state->frame_epochs.capacity = 0;
    state->stack_intervals.iv = NULL;
    state->stack_intervals.count = 0;
    state->stack_intervals.capacity = 0;
    state->stack_seg = NULL;
    state->shadow_stack = NULL;
}

void cccc_exec_state_prepare_call(VirtualMachine *vm, ExecState *state, Pc entry,
                                  long long arg) {
    size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
    size_t initial_stack = (size_t)vm->poolsize * sizeof(long long);

    memset(state->regs, 0, sizeof(state->regs));
    memset(state->fregs, 0, sizeof(state->fregs));
    memset(state->vregs, 0, sizeof(state->vregs));
    state->pc = entry;
    state->cycle = 0;
    state->sp = (long long *)((char *)state->stack_seg + reserved_stack);
    state->bp = state->sp;
    state->stack_base = (long long *)((char *)state->stack_seg +
                                      reserved_stack - initial_stack);
    state->initial_sp = state->sp;
    state->initial_bp = state->bp;
    if (vm->flags & CCCC_CFI)
        state->shadow_sp = (long long *)((char *)state->shadow_stack + reserved_stack);
    state->regs[REG_A0] = arg;
    *--state->sp = 0;
}

int vm_text_ensure_count(VirtualMachine *vm, Pc num_words) {
    size_t needed_bytes = (size_t)num_words * sizeof(InstrWord);
    if (needed_bytes <= vm->text_committed)
        return 0;
    size_t reserved = (size_t)vm->poolsize_max * sizeof(InstrWord);
    if (needed_bytes > reserved)
        return -1;
    // Round up to next poolsize-element chunk
    size_t chunk = (size_t)vm->poolsize * sizeof(InstrWord);
    size_t new_committed = ((needed_bytes + chunk - 1) / chunk) * chunk;
    if (new_committed > reserved)
        new_committed = reserved;
    if (cccc_vm_commit(vm->text_seg, vm->text_committed,
                      new_committed - vm->text_committed) != 0)
        return -1;
    vm->text_committed = new_committed;
    return 0;
}

int vm_data_ensure(VirtualMachine *vm, long long needed) {
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
    if (cccc_vm_commit(vm->data_seg, (size_t)committed,
                      (size_t)(new_committed - committed)) != 0)
        return -1;
    vm->data_committed = (size_t)new_committed;
    return 0;
}

int vm_heap_grow(VirtualMachine *vm, size_t need) {
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
    if (cccc_vm_commit(vm->heap_seg, current, chunk) != 0)
        return -1;
    vm->heap_committed += chunk;
    vm->heap_end = vm->heap_seg + vm->heap_committed;
    return 0;
}

int vm_stack_grow(VirtualMachine *vm, int slots_needed) {
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

    if (cccc_vm_commit(seg_start, off, chunk) != 0)
        return -1;

    vm->stack_base = (long long *)new_base;
    vm->stack_committed += chunk;
    return 0;
}

// ========== End segment helpers ==========

void cc_init(VirtualMachine *vm, uint32_t flags) {
    // Zero-initialize the VM struct
    memset(vm, 0, sizeof(VirtualMachine));

    // Seed the compiler-internal VM global that every comptime __builtin_*
    // (src/reflection.c) reads instead of taking a VirtualMachine* parameter.
    // Macro execution windows in src/macros.c save/restore this around
    // nested calls; cc_destroy clears it back to NULL so a later reader
    // never dereferences a VM that has gone out of scope (see src/fuzzing.c,
    // which constructs a fresh stack-local VirtualMachine per iteration).
    __builtin_current_vm = vm;

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
    vm->compiler.macro_recursion_limit = 256;
    vm->compiler.warnings = 0;
    vm->compiler.warning_errors = 0;
    vm->compiler.warning_no_errors = 0;

    // Return buffer pool will be allocated in data segment during codegen
    vm->compiler.return_buffer_size = 1024;
    vm->compiler.return_buffer_count = RETURN_BUFFER_POOL_SIZE;
    vm->compiler.return_buffer_index = 0;  // Compile-time index (unused with RETBUF)
    vm->runtime_return_buffer_index = 0;   // Runtime rotation index for RETBUF opcode
    for (int i = 0; i < RETURN_BUFFER_POOL_SIZE; i++) {
        vm->compiler.return_buffer_pool[i] = NULL;  // Will be set to data segment locations
        vm->compiler.return_buffer_offsets[i] = -1;
    }

    // Initialize parser arena BEFORE init_macros to avoid orphaning blocks
    // (init_macros allocates from the arena, so arena must be initialized first)
    arena_init(&vm->compiler.parser_arena, 0);  // 0 = use default (1MB)

    // Initialize init_state HashMap for uninitialized variable detection
    vm->init_state.capacity = 0;  // Will be allocated on first use by hashmap_put
    vm->init_state.buckets = NULL;
    vm->init_state.used = 0;

    // Initialize provenance HashMap for provenance tracking
    vm->provenance.capacity = 0;
    vm->provenance.buckets = NULL;
    vm->provenance.used = 0;

    // Initialize stack_var_meta HashMap for stack instrumentation
    vm->stack_var_meta.capacity = 0;
    vm->stack_var_meta.buckets = NULL;
    vm->stack_var_meta.used = 0;

    // Initialize stack_var_active HashMap (runtime liveness set, #671)
    vm->stack_var_active.capacity = 0;
    vm->stack_var_active.buckets = NULL;
    vm->stack_var_active.used = 0;

    // Note: alloc_map and ptr_tags removed - now using sorted_allocs for heap tracking

    // Initialize included_headers HashMap for header-based stdlib loading
    vm->compiler.included_headers.capacity = 0;
    vm->compiler.included_headers.buckets = NULL;
    vm->compiler.included_headers.used = 0;

    // Initialize include_guards HashMap for header guard tracking
    vm->compiler.include_guards.capacity = 0;
    vm->compiler.include_guards.buckets = NULL;
    vm->compiler.include_guards.used = 0;

    // Initialize url_to_path HashMap for URL include tracking
    vm->compiler.url_to_path.capacity = 0;
    vm->compiler.url_to_path.buckets = NULL;
    vm->compiler.url_to_path.used = 0;
    vm->compiler.url_cache_dir = NULL;  // Initialized on first URL include

    // Initialize include_cache HashMap
    vm->compiler.include_cache.capacity = 0;
    vm->compiler.include_cache.buckets = NULL;
    vm->compiler.include_cache.used = 0;

    // Initialize file_buffers StringArray
    vm->compiler.file_buffers.data = NULL;
    vm->compiler.file_buffers.len = 0;
    vm->compiler.file_buffers.capacity = 0;

    // Default to GNU C17 (matches modern gcc/clang defaults)
    vm->compiler.c_std = CCCC_STD_C23;
    vm->compiler.c_std_gnu = true;
    vm->compiler.attr_target = CCCC_ATTR_TARGET_AUTO;

    init_macros(vm);
    cc_init_parser(vm);

    // Initialize sorted allocation array for O(log n) pointer validation
    vm->sorted_allocs.addresses = NULL;
    vm->sorted_allocs.headers = NULL;
    vm->sorted_allocs.count = 0;
    vm->sorted_allocs.capacity = 0;

    // #981: heap-reclamation mark stack. heap_reclaim_enabled starts false
    // (conservative) and is properly computed per top-level call in
    // cc_run_at_regs, once dynobjsz_present is resolvable.
    vm->heap_marks.bps = NULL;
    vm->heap_marks.depths = NULL;
    vm->heap_marks.marks = NULL;
    vm->heap_marks.count = 0;
    vm->heap_marks.capacity = 0;
    vm->heap_reclaim_enabled = false;

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
    vm->heap_mark_nest_depth = 0; // #981 HMRK/HREL codegen-time nesting counter
    vm->scope_vars = NULL;
    vm->scope_vars_capacity = 0;

    // Initialize stack canary (will be set to random or fixed value based on flag)
    // The flag CCCC_RANDOM_CANARIES will trigger regeneration in main.c
    // For now, initialize to fixed value; it will be regenerated if random canaries enabled
    vm->stack_canary = STACK_CANARY;

    vm->dynlibs = NULL;
    vm->dynlib_count = 0;
    vm->dynlib_capacity = 0;
    vm->dynsyms = NULL;
    vm->dynsym_count = 0;
    vm->dynsym_capacity = 0;
    vm->dyn_next_token = 1;
    vm->dyn_error = NULL;

    // CCCC's own header directory, used only as an on-disk fallback for
    // standard headers (search_include_paths tries the embedded src/std.c
    // table first) and for the stage0 std_stub.c build, where the embedded
    // table is empty. Deliberately not pushed into include_paths or
    // system_include_paths: standard headers no longer need a search-path
    // entry to resolve (see man/HEADERS.md), and keeping this out of both
    // lists means a user -I always wins, and -c=native never forwards
    // CCCC's polyfill headers to the native compiler.
    vm->compiler.builtin_include_dir = "./include";

    cc_define(vm, "CCCC_HAS_FFI", "1");

    // Initialize error collection fields
    vm->errors = NULL;
    vm->errors_tail = NULL;
    vm->error_count = 0;
    vm->warning_count = 0;
    vm->max_errors = 20;  // Default max errors before stopping
    vm->collect_errors = false;  // Disabled by default (opt-in)
    vm->warnings_as_errors = false;  // Disabled by default

    if (vm->flags & CCCC_ENABLE_DEBUGGER) {
        debugger_init(vm);
    }

    cccc_gil_init(vm);
}

void cccc_gil_init(VirtualMachine *vm) {
    if (!vm || vm->gil_initialized)
        return;
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_t *mutex = malloc(sizeof(*mutex));
    if (!mutex)
        error("could not allocate VM GIL");
    pthread_mutexattr_t attr;
    if (pthread_mutexattr_init(&attr) != 0)
        error("could not initialize VM GIL attributes");
    if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE) != 0)
        error("could not make VM GIL recursive");
    if (pthread_mutex_init(mutex, &attr) != 0)
        error("could not initialize VM GIL");
    pthread_mutexattr_destroy(&attr);
    vm->gil_mutex = mutex;
#endif
    vm->gil_initialized = 1;
}

void cccc_gil_destroy(VirtualMachine *vm) {
    if (!vm || !vm->gil_initialized)
        return;
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_t *mutex = (pthread_mutex_t *)vm->gil_mutex;
    if (mutex) {
        pthread_mutex_destroy(mutex);
        free(mutex);
    }
#endif
    vm->gil_mutex = NULL;
    vm->gil_initialized = 0;
}

void cccc_gil_acquire(VirtualMachine *vm) {
    if (!vm)
        return;
    if (!vm->gil_initialized)
        cccc_gil_init(vm);
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_lock((pthread_mutex_t *)vm->gil_mutex);
#endif
}

void cccc_gil_release(VirtualMachine *vm) {
    if (!vm || !vm->gil_initialized)
        return;
#if !defined(_WIN32) && !defined(_WIN64)
    pthread_mutex_unlock((pthread_mutex_t *)vm->gil_mutex);
#endif
}

void cc_destroy(VirtualMachine *vm) {
    if (!vm)
        return;

    // Drop the compiler-internal VM global if it still points at this VM,
    // so a stack-local VirtualMachine going out of scope (e.g. src/fuzzing.c,
    // one fresh VM per iteration) never leaves a dangling read target.
    if (__builtin_current_vm == vm)
        __builtin_current_vm = NULL;

    // If compile_macro_program was interrupted by longjmp (error exit), its
    // hashmap_snapshot is still live in macro_snapshot_backup.  Free it here
    // so it doesn't leak.  All normal return paths clear has_macro_snapshot
    // before calling hashmap_restore, so this branch is only reached on the
    // longjmp error path.
    if (vm->compiler.has_macro_snapshot) {
        hashmap_deinit(&vm->compiler.macro_snapshot_backup);
        vm->compiler.has_macro_snapshot = false;
    }

    cccc_pthread_cleanup(vm);
    cccc_gil_destroy(vm);

    // Report memory leaks before releasing segments
    if ((vm->flags & CCCC_MEMORY_LEAK_DETECT) && vm->alloc_list) {
        int count = 0;
        for (AllocRecord *r = vm->alloc_list; r; r = r->next)
            count++;
        printf("\n========== MEMORY LEAK DETECTED ==========\n");
        printf("%d allocation(s) not freed:\n", count);
        for (AllocRecord *r = vm->alloc_list; r; r = r->next) {
            printf("  0x%llx  %zu bytes  (alloc PC: %lld)\n",
                   (long long)r->address, r->size, r->alloc_pc);
        }
        printf("==========================================\n");
        AllocRecord *r = vm->alloc_list;
        while (r) {
            AllocRecord *next = r->next;
            free(r);
            r = next;
        }
        vm->alloc_list = NULL;
    }

    // Release reserved virtual ranges (base pointers never moved)
    if (vm->text_seg)
        cccc_vm_release(vm->text_seg,
                       (size_t)vm->poolsize_max * sizeof(InstrWord));
    if (vm->data_seg)
        cccc_vm_release(vm->data_seg,  (size_t)vm->poolsize_max);
    if (vm->stack_seg)
        cccc_vm_release(vm->stack_seg,
                       (size_t)vm->poolsize_max * sizeof(long long));
    if (vm->heap_seg)
        cccc_vm_release(vm->heap_seg,  (size_t)vm->poolsize_max);
    if (vm->shadow_stack)
        cccc_vm_release(vm->shadow_stack,
                       (size_t)vm->poolsize_max * sizeof(long long));
    // TLS template and main-thread TLS copy (simple malloc, not mmap'd)
    free(vm->tls_template);
    vm->tls_template = NULL;
    vm->tls_template_size = 0;
    vm->tls_template_cap = 0;
    free(vm->current_tls_seg);
    vm->current_tls_seg = NULL;
    // return_buffer is part of data_seg, no need to free separately
    if (vm->vm_profile_trigram_counts) {
        free(vm->vm_profile_trigram_counts);
        vm->vm_profile_trigram_counts = NULL;
    }
    // #877: async-delivery register-snapshot save area, lazily malloc'd on
    // first use -- see AsyncRegSave in src/cccc.h.
    if (vm->async_reg_saves) {
        free(vm->async_reg_saves);
        vm->async_reg_saves = NULL;
    }

    // Free VM runtime HashMaps. Integer keys (keylen == -1) are skipped by
    // hashmap_deinit. Heap-allocated values must be freed first.
    // Free init_state HashMap (integer keys, no values to free)
    hashmap_deinit(&vm->init_state);
    // Free ptr_tags HashMap (integer keys, integer values cast to void*)
    hashmap_deinit(&vm->ptr_tags);
    // Free race_shadow HashMap (thread-safety write-tracking, integer keys)
    hashmap_deinit(&vm->race_shadow);
    // Free atomic_shadow HashMap (mixed atomic/non-atomic detection)
    hashmap_deinit(&vm->atomic_shadow);

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

    // Free stack_var_active HashMap. Values are StackVarMeta* borrowed from
    // stack_var_meta (freed just above) -- do not free them again here.
    if (vm->stack_var_active.buckets)
        hashmap_deinit(&vm->stack_var_active);

    // Free frame-epoch liveness state (#673: dangling deref through a
    // deeper call). live_epochs/stack_ptr_epochs are integer-keyed with no
    // heap-allocated values to free; frame_epochs is a plain parallel array.
    hashmap_deinit(&vm->live_epochs);
    hashmap_deinit(&vm->stack_ptr_epochs);
    free(vm->frame_epochs.bps);
    free(vm->frame_epochs.epochs);
    free(vm->stack_intervals.iv); // #675: retained interior-pointer intervals

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

    // Free link_syms HashMap (#882: --link symbol pre-scan; presence-only
    // set, values are not pointers to free)
    hashmap_deinit(&vm->compiler.link_syms);

    // Free sorted allocation arrays
    if (vm->sorted_allocs.addresses)
        free(vm->sorted_allocs.addresses);
    if (vm->sorted_allocs.headers)
        free(vm->sorted_allocs.headers);

    // Free the #981 heap-reclamation mark stack
    if (vm->heap_marks.bps)
        free(vm->heap_marks.bps);
    if (vm->heap_marks.depths)
        free(vm->heap_marks.depths);
    if (vm->heap_marks.marks)
        free(vm->heap_marks.marks);

    // Free the heap and data (globals, #752) type shadows (#653;
    // page-chunked #753 -- lazily allocated/grown by type_shadow_ensure in
    // ops.c)
    if (vm->heap_shadow.pages) {
        for (size_t i = 0; i < vm->heap_shadow.page_count; i++)
            free(vm->heap_shadow.pages[i]);
        free(vm->heap_shadow.pages);
    }
    vm->heap_shadow.pages = NULL;
    vm->heap_shadow.page_count = 0;
    if (vm->data_shadow.pages) {
        for (size_t i = 0; i < vm->data_shadow.page_count; i++)
            free(vm->data_shadow.pages[i]);
        free(vm->data_shadow.pages);
    }
    vm->data_shadow.pages = NULL;
    vm->data_shadow.page_count = 0;

    // Free macros HashMap (Macro values are arena-allocated; do not free them)
    hashmap_deinit(&vm->compiler.macros);

    // Free #pragma GCC diagnostic stack
    if (vm->compiler.diag_stack_warnings)
        free(vm->compiler.diag_stack_warnings);
    if (vm->compiler.diag_stack_werror)
        free(vm->compiler.diag_stack_werror);

    // Free comptime/emit context stack
    if (vm->compiler.ctx_stack)
        free(vm->compiler.ctx_stack);

    // Free suite stack and any unconsumed current_suite (e.g. after longjmp)
    if (vm->compiler.current_suite)
        free(vm->compiler.current_suite);
    if (vm->compiler.suite_len_stack)
        free(vm->compiler.suite_len_stack);

    // Free macro-scope snapshot stack (#283)
    if (vm->compiler.macro_scope_stack)
        free(vm->compiler.macro_scope_stack);

    // Free pragma_once HashMap
    hashmap_deinit(&vm->compiler.pragma_once);

    // Free include_guards HashMap
    hashmap_deinit(&vm->compiler.include_guards);

    // Free guard_macros HashMap (set of macro names used as include guards)
    hashmap_deinit(&vm->compiler.guard_macros);

    // Free command_line_inputs HashMap (#1002 investigation) -- borrowed
    // keys (main.c's input_files[] strings), so _borrowed is correct here.
    hashmap_deinit_borrowed(&vm->compiler.command_line_inputs);

    // Free dynamic patch tables (#555)
    if (vm->compiler.call_patches)
        free(vm->compiler.call_patches);
    if (vm->compiler.func_addr_patches)
        free(vm->compiler.func_addr_patches);
    if (vm->compiler.data_relocs)
        free(vm->compiler.data_relocs);
    if (vm->compiler.tls_relocs)
        free(vm->compiler.tls_relocs);
    if (vm->compiler.sym_table) {
        for (int i = 0; i < vm->compiler.num_sym_table; i++)
            free(vm->compiler.sym_table[i].name);
        free(vm->compiler.sym_table);
    }
    if (vm->compiler.text_relocs) {
        for (int i = 0; i < vm->compiler.num_text_relocs; i++)
            free(vm->compiler.text_relocs[i].name);
        free(vm->compiler.text_relocs);
    }
    if (vm->compiler.addr_relocs) {
        for (int i = 0; i < vm->compiler.num_addr_relocs; i++)
            free(vm->compiler.addr_relocs[i].name);
        free(vm->compiler.addr_relocs);
    }
    if (vm->compiler.ctor_list)
        free(vm->compiler.ctor_list);
    if (vm->compiler.dtor_list)
        free(vm->compiler.dtor_list);

    // Free FFI table
    if (vm->compiler.ffi_table) {
        for (int i = 0; i < vm->compiler.ffi_count; i++) {
            free(vm->compiler.ffi_table[i].name);
            free(vm->compiler.ffi_table[i].asm_src);
        }
        free(vm->compiler.ffi_table);
    }

    for (int i = 0; i < vm->dynlib_count; i++) {
        DynamicLibrary *lib = &vm->dynlibs[i];
        if (lib->handle && !lib->is_closed) {
#if defined(_WIN32) || defined(_WIN64)
            if (lib->path)
                FreeLibrary((HMODULE)lib->handle);
#else
            dlclose(lib->handle);
#endif
        }
        free(lib->path);
    }
    for (int i = 0; i < vm->dynsym_count; i++)
        free(vm->dynsyms[i].name);
    free(vm->dynlibs);
    free(vm->dynsyms);
    free(vm->dyn_error);
    vm->dyn_error = NULL;

    cc_ffi_clear_allow_list(vm);
    free(vm->ffi_allow_list);
    vm->ffi_allow_list = NULL;
    vm->ffi_allow_capacity = 0;
    cc_ffi_clear_deny_list(vm);
    free(vm->ffi_deny_list);
    vm->ffi_deny_list = NULL;
    vm->ffi_deny_capacity = 0;

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

    // Free URL cache directory
    if (vm->compiler.url_cache_dir)
        free(vm->compiler.url_cache_dir);

    // Free URL to path map
    hashmap_deinit(&vm->compiler.url_to_path);

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

    // Free emit_directives (strdup'd strings + data array)
    if (vm->compiler.emit_directives.data) {
        for (int i = 0; i < vm->compiler.emit_directives.len; i++)
            free(vm->compiler.emit_directives.data[i]);
        free(vm->compiler.emit_directives.data);
    }

    // Free comptime_pending_includes data array (strings are arena-allocated)
    free(vm->compiler.comptime_pending_includes.data);

    // Free pragma_link_libs (strdup'd strings + data array; #357)
    if (vm->compiler.pragma_link_libs.data) {
        for (int i = 0; i < vm->compiler.pragma_link_libs.len; i++)
            free(vm->compiler.pragma_link_libs.data[i]);
        free(vm->compiler.pragma_link_libs.data);
    }

    // Free linked programs array
    free(vm->compiler.link_progs);
    free(vm->compiler.global_aliases);
    hashmap_deinit(&vm->compiler.global_decl_map);

    // Free source map and index (debugger)
    free(vm->dbg.source_map);
    free(vm->dbg.source_index);

    // Free watchpoint expressions
    for (int i = 0; i < MAX_WATCHPOINTS; i++) {
        if (vm->dbg.watchpoints[i].expr) {
            free(vm->dbg.watchpoints[i].expr);
            vm->dbg.watchpoints[i].expr = NULL;
        }
    }

    // Free scope HashMap buckets (heap-allocated; not in the arena)
    for (Scope *sc = vm->compiler.scope; sc; sc = sc->next) {
        hashmap_deinit_borrowed(&sc->var_map);
        hashmap_deinit_borrowed(&sc->tag_map);
    }

    // Free macro_context_scope HashMap buckets — this scope was entered by
    // parse() and stashed by compile_macro_program, then temporarily spliced
    // into the scope chain during macro execution.  After cc_expand_macros
    // restores scope_before->next it is no longer reachable from the head of
    // the chain, so the main loop above misses it.
    if (vm->compiler.macro_context_scope) {
        hashmap_deinit_borrowed(&vm->compiler.macro_context_scope->var_map);
        hashmap_deinit_borrowed(&vm->compiler.macro_context_scope->tag_map);
    }

    // Free test function records (test_fns linked list)
    for (TestFnRecord *r = vm->compiler.test_fns, *nr; r; r = nr) {
        nr = r->next;
        free(r->name);
        free(r->display_name);
        free(r->suite);
        free(r->error_pat);
        free(r->test_flags);
        free(r->expect_stderr);
        free(r->reject_stderr);
        free(r->expect_stdout);
        free(r->reject_stdout);
        if (r->ret_kind == RET_STR)
            free(r->ret_expect.ret_str);
        else if (r->ret_kind == RET_STRUCT) {
            cc_free_ret_fields(r->ret_expect.ret_fields);
            free(r->ret_struct_text);
        }
        for (int i = 0; i < r->test_ffi_allow_count; i++)
            free(r->test_ffi_allow[i]);
        free(r->test_ffi_allow);
        free(r);
    }
    vm->compiler.test_fns = NULL;

    // Free test setup/teardown records (test_setups linked list)
    for (TestSetupRecord *s = vm->compiler.test_setups, *ns; s; s = ns) {
        ns = s->next;
        free(s->fn_name);
        free(s->name_pat);
        free(s->suite);
        free(s);
    }
    vm->compiler.test_setups = NULL;

    // Free build-entry records (build_fns linked list)
    for (BuildFnRecord *b = vm->compiler.build_fns, *nb; b; b = nb) {
        nb = b->next;
        free(b->name);
        free(b);
    }
    vm->compiler.build_fns = NULL;

    // Free build-target factory records (#540)
    for (BuildTargetFnRecord *b = vm->compiler.build_target_fns, *nb; b; b = nb) {
        nb = b->next;
        free(b->name);
        free(b->kind);
        free(b);
    }
    vm->compiler.build_target_fns = NULL;

    // Destroy parser arena (frees all tokens, AST nodes, preprocessor state)
    arena_destroy(&vm->compiler.parser_arena);
}

void cc_print_stack_report(VirtualMachine *vm) {
    if (!vm || !(vm->flags & CCCC_STACK_INSTR)) {
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

void cc_include(VirtualMachine *vm, const char *path) {
    strarray_push(&vm->compiler.include_paths, strdup(path));
}

void cc_system_include(VirtualMachine *vm, const char *path) {
    strarray_push(&vm->compiler.system_include_paths, strdup(path));
}

void cc_define(VirtualMachine *vm, char *name, char *buf) {
    define_macro(vm, name, buf);
}

void cc_undef(VirtualMachine *vm, char *name) {
    undef_macro(vm, name);
}

void cc_set_asm_callback(VirtualMachine *vm, AsmCallback callback, void *user_data) {
    vm->compiler.asm_callback = callback;
    vm->compiler.asm_user_data = user_data;
}

static void cc_ffi_list_add(char ***list, int *count, int *capacity,
                            const char *name) {
    if (!name || !*name)
        return;
    size_t len = strlen(name);
    for (int i = 0; i < *count; i++) {
        if (strlen((*list)[i]) == len && memcmp((*list)[i], name, len) == 0)
            return;
    }
    if (*count >= *capacity) {
        *capacity = *capacity ? *capacity * 2 : 8;
        *list = realloc(*list, (size_t)*capacity * sizeof(char *));
        if (!*list)
            error("cc_ffi_list_add: realloc failed");
    }
    (*list)[(*count)++] = strdup(name);
}

void cc_ffi_allow(VirtualMachine *vm, const char *name) {
    if (!vm)
        error("cc_ffi_allow: vm is NULL");
    cc_ffi_list_add(&vm->ffi_allow_list, &vm->ffi_allow_count,
                    &vm->ffi_allow_capacity, name);
}

void cc_ffi_deny(VirtualMachine *vm, const char *name) {
    if (!vm)
        error("cc_ffi_deny: vm is NULL");
    cc_ffi_list_add(&vm->ffi_deny_list, &vm->ffi_deny_count,
                    &vm->ffi_deny_capacity, name);
}

void cc_ffi_clear_allow_list(VirtualMachine *vm) {
    if (!vm)
        return;
    for (int i = 0; i < vm->ffi_allow_count; i++)
        free(vm->ffi_allow_list[i]);
    vm->ffi_allow_count = 0;
}

void cc_ffi_clear_deny_list(VirtualMachine *vm) {
    if (!vm)
        return;
    for (int i = 0; i < vm->ffi_deny_count; i++)
        free(vm->ffi_deny_list[i]);
    vm->ffi_deny_count = 0;
}

void cc_register_cfunc(VirtualMachine *vm, const char *name, void *func_ptr, int num_args, int returns_double) {
    cc_register_cfunc_ex(vm, name, func_ptr, num_args, returns_double, 0);
}

// Tri-state return-type encoding used by cc_register_cfunc[_ex|_variadic]
// and cc_dlsym: 0 = long long, 1 = double, 2 = float (#406).
static void set_return_kind(ForeignFunc *ff, int returns_double_param) {
    ff->returns_double = (returns_double_param == 1);
    ff->returns_float = (returns_double_param == 2);
}

static int get_return_kind(ForeignFunc *ff) {
    return ff->returns_float ? 2 : ff->returns_double ? 1 : 0;
}

static ForeignFunc *find_registered_ffi(VirtualMachine *vm, const char *name) {
    size_t name_len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        if (ff->name && ff->name_len == name_len &&
            memcmp(ff->name, name, name_len) == 0)
            return ff;
    }
    return NULL;
}

static void ensure_ffi_capacity(VirtualMachine *vm, const char *caller) {
    if (vm->compiler.ffi_count < vm->compiler.ffi_capacity)
        return;

    vm->compiler.ffi_capacity =
        vm->compiler.ffi_capacity ? vm->compiler.ffi_capacity * 2 : 32;
    vm->compiler.ffi_table =
        realloc(vm->compiler.ffi_table,
                vm->compiler.ffi_capacity * sizeof(ForeignFunc));
    if (!vm->compiler.ffi_table)
        error("%s: realloc failed", caller);
}

void cc_register_cfunc_ex(VirtualMachine *vm, const char *name, void *func_ptr, int num_args, int returns_double, uint64_t double_arg_mask) {
    if (!vm)
        error("cc_register_cfunc_ex: vm is NULL");
    if (!name || !func_ptr)
        error("cc_register_cfunc_ex: name or func_ptr is NULL");

    ForeignFunc *existing = find_registered_ffi(vm, name);
    if (existing) {
        existing->func_ptr = func_ptr;
        existing->num_args = num_args;
        set_return_kind(existing, returns_double);
        existing->is_variadic = 0;
        existing->num_fixed_args = num_args;
        existing->double_arg_mask = double_arg_mask;
        existing->is_dynamic_placeholder = 0;
        return;
    }

    ensure_ffi_capacity(vm, "cc_register_cfunc_ex");

    // Add function to registry (non-variadic)
    vm->compiler.ffi_table[vm->compiler.ffi_count++] = (ForeignFunc){
        .name = strdup(name),
        .name_len = strlen(name),
        .func_ptr = func_ptr,
        .num_args = num_args,
        .returns_double = (returns_double == 1),
        .returns_float = (returns_double == 2),
        .is_variadic = 0,
        .num_fixed_args = num_args,
        .double_arg_mask = double_arg_mask
    };
}

void cc_register_variadic_cfunc(VirtualMachine *vm, const char *name, void *func_ptr, int num_fixed_args, int returns_double) {
    if (!vm)
        error("cc_register_variadic_cfunc: vm is NULL");
    if (!name || !func_ptr)
        error("cc_register_variadic_cfunc: name or func_ptr is NULL");

    ForeignFunc *existing = find_registered_ffi(vm, name);
    if (existing) {
        existing->func_ptr = func_ptr;
        existing->num_args = num_fixed_args;
        set_return_kind(existing, returns_double);
        existing->is_variadic = 1;
        existing->num_fixed_args = num_fixed_args;
        existing->double_arg_mask = 0;
        existing->is_dynamic_placeholder = 0;
        return;
    }

    ensure_ffi_capacity(vm, "cc_register_variadic_cfunc");

    // Add variadic function to registry. num_args is set to num_fixed_args
    // and stays that way -- CALLF/CALLN never update it; both instead
    // carry the real per-callsite argument count as an instruction operand
    // and use that directly. num_args here is only a placeholder for
    // registration bookkeeping, not a value any dispatch path should read
    // for a variadic entry.
    vm->compiler.ffi_table[vm->compiler.ffi_count++] = (ForeignFunc){
        .name = strdup(name),
        .name_len = strlen(name),
        .func_ptr = func_ptr,
        .num_args = num_fixed_args,
        .returns_double = (returns_double == 1),
        .returns_float = (returns_double == 2),
        .is_variadic = 1,
        .num_fixed_args = num_fixed_args,
        .double_arg_mask = 0  // Variadic functions don't use mask - doubles passed as bits
    };
}

int cc_dlsym(VirtualMachine *vm, const char *name, void *func_ptr, int num_args, int returns_double) {
    if (!vm || !name || !func_ptr)
        return -1;

    size_t name_len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name_len == name_len &&
            memcmp(vm->compiler.ffi_table[i].name, name, name_len) == 0) {
            if (vm->compiler.ffi_table[i].num_args != num_args || get_return_kind(&vm->compiler.ffi_table[i]) != returns_double) {
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

int cc_dlopen(VirtualMachine *vm, const char *lib_path) {
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
            if (vm->debug_vm)
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

        if (error) {
            if (vm->debug_vm)
                fprintf(stderr, "warning: failed to resolve symbol '%s': %s\n", ff->name, error);
        } else {
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

int cc_load_libc(VirtualMachine *vm) {
    const char *libc_path = find_libc();
    if (vm->debug_vm)
        printf("Loading standard C library: %s\n", libc_path);
    return cc_dlopen(vm, libc_path);
}

// Load standard library (for backward compatibility)
// This function is kept for programs that don't use #include,
// or want all stdlib functions available regardless of includes
void cc_load_stdlib(VirtualMachine *vm) {
    // Register all standard library functions regardless of includes
    register_ctype_functions(vm);
    register_decimal_math_functions(vm);
    register_fenv_functions(vm);
    register_locale_functions(vm);
    register_math_functions(vm);
    register_posix_functions(vm);
    register_pthread_functions(vm);
    register_threads_functions(vm);
    register_signal_functions(vm);
    register_stdio_functions(vm);
    register_stdlib_functions(vm);
    register_string_functions(vm);
    register_time_functions(vm);
    register_wide_functions(vm);
    register_wide_bitint_functions(vm);

    // Mark all headers as included
    for (int i = 0; ; i++) {
        const char *h = get_std_header_name(i);
        if (!h) break;
        hashmap_put(&vm->compiler.included_headers, h, (void*)1);
    }
}

// Incremental text-segment scan (#648): set vm->dynobjsz_present the first
// time a DYNOBJSZ opcode is seen, so stack_extents_enabled() (ops.c) knows
// to populate frame_epochs/stack_intervals for programs that use
// __builtin_dynamic_object_size, even with dangling-pointer detection off.
// Scanning the serialized opcode stream (rather than latching a codegen-time
// flag) keeps this correct across a .c4 save/reload round-trip. Resumes from
// vm->dynobjsz_scan_pc rather than rescanning from the top, since text_seg
// can still grow between cc_run_at calls (comptime macros / --build factory
// functions run interleaved with codegen; testing.c also calls cc_run_at
// once per test function). Same walk shape as the ngram/fusion analyzers
// (analyze.c): step by cc_instr_words(), starting after the reserved
// text_seg[0] entry-point slot.
static void cc_scan_dynobjsz(VirtualMachine *vm) {
    if (vm->dynobjsz_present)
        return; // already found; nothing left to look for
    long long num_words = (long long)vm->text_ptr + 1;
    long long pc = vm->dynobjsz_scan_pc ? vm->dynobjsz_scan_pc : 1; // [0] = entry slot
    while (pc < num_words) {
        int op = (int)vm->text_seg[pc];
        int words = cc_instr_words(op);
        if (words <= 0)
            break;
        if (op == DYNOBJSZ) {
            vm->dynobjsz_present = true;
            return;
        }
        pc += words;
    }
    vm->dynobjsz_scan_pc = pc;
}

// Shared by cc_run_at (main-style argc/argv entry point) and cc_run_at1
// (single register-argument entry point, used to drain TSS/pthread-key
// destructors when pthread_exit() is called on the main thread -- #863).
// Everything from GIL acquire through vm_eval/teardown is common "run a
// complete top-level VM execution cycle" setup; only how the two argument
// registers are populated, and what debugger_run is told argc/argv were,
// differs between the two callers.
static int cc_run_at_regs(VirtualMachine *vm, Pc entry, long long a0, long long a1,
                           int debugger_argc, char **debugger_argv) {
    if (!vm || !vm->text_seg) {
        error("VM not initialized - call cc_compile first");
    }
    cc_scan_dynobjsz(vm);
    // #981: (re)compute the full heap-reclamation gate now that
    // dynobjsz_present is resolved for whatever bytecode exists so far --
    // cc_heap_reclaim_flags_ok (cccc.h) is the flags-only half codegen.c
    // used to decide whether HMRK/HREL were worth emitting at all; ANDing
    // in !dynobjsz_present here is the other half, only knowable after a
    // full text-segment scan (which can't happen until codegen has
    // finished for whatever's been compiled so far -- see cc_scan_dynobjsz
    // above). Recomputed on every top-level call, not just once, since
    // vm->flags can change between calls (a #pragma or per-test
    // CCCC_FLAGS override, testing.c's one-cc_run_at-per-test-function
    // model) and dynobjsz_present can flip from false to true if a later-
    // compiled function introduces a DYNOBJSZ opcode.
    vm->heap_reclaim_enabled =
        cc_heap_reclaim_flags_ok(vm->flags) && !vm->dynobjsz_present;
    cc_running_vm = vm;
    cccc_gil_acquire(vm);
    cc_vm_profile_reset(vm);
    vm->dbg.host_fault_signal = 0;
    // #1013: clear the previous run's fault marker so each cc_run_at cycle
    // starts clean -- otherwise a fault from an earlier call (e.g. a prior
    // test) could be misattributed to this one.
    vm->runtime_fault    = false;
    vm->runtime_fault_op = NULL;
    vm->runtime_fault_pc = 0;

    vm->pc = entry;

    // Setup stack — use poolsize_max so sp/bp sit at top of full reservation
    {
        size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
        size_t initial_stack  = (size_t)vm->poolsize * sizeof(long long);
        vm->sp = (long long *)((char *)vm->stack_seg + reserved_stack);
        vm->bp = vm->sp;  // Initialize base pointer to top of stack
        vm->stack_base = (long long *)((char *)vm->stack_seg +
                                       reserved_stack - initial_stack);

        // Shadow stack for CFI — lazily allocated here to handle per-test -C flag
        // changes where vm_alloc_segments ran before CCCC_CFI was set (#605).
        if (vm->flags & CCCC_CFI) {
            if (!vm->shadow_stack) {
                size_t stack_top_off = reserved_stack - initial_stack;
                vm->shadow_stack = (long long *)cccc_vm_reserve(reserved_stack);
                if (!vm->shadow_stack)
                    error("could not reserve shadow stack");
                if (cccc_vm_commit(vm->shadow_stack, stack_top_off, initial_stack) != 0)
                    error("could not commit shadow stack");
            }
            vm->shadow_sp = (long long *)((char *)vm->shadow_stack + reserved_stack);
        }
    }

    // Save initial stack/base pointers for exit detection in vm_eval
    vm->initial_sp = vm->sp;
    vm->initial_bp = vm->bp;

    // Reset per-call-stack liveness bookkeeping (#648). A fresh top-level
    // call means a fresh guest call stack -- but a *previous* call can leave
    // this state non-empty: the test harness's __builtin_assert failure path
    // (testing.c) does a host-level longjmp straight back to the harness's
    // setjmp, abandoning the guest call stack without ever running the
    // aborted frames' LEV3 (so their frame_epochs/stack_intervals/
    // stack_ptr_epochs entries are never retired). Previously this was inert
    // dead weight (CCCC_DANGLING_DETECT test files aren't expected to fail
    // assertions), but with DYNOBJSZ able to activate the same bookkeeping,
    // any assertion failure in a dynobjsz-using test file would leave stale
    // epochs that desync the NEXT call's LEV3 tripwire assert. Pop down to
    // empty (rather than a raw memset) so frame_epochs and live_epochs empty
    // together and frame_epoch_counter is deliberately left untouched --
    // resetting the counter while it's still monotonic across calls is what
    // keeps a new call's epoch N from ever colliding with a stale retained
    // interval also tagged N.
    while (vm->frame_epochs.count > 0)
        frame_epoch_pop(vm);
    hashmap_deinit(&vm->live_epochs);
    hashmap_deinit(&vm->stack_ptr_epochs);
    vm->stack_intervals.count = 0;

    // #981: same abandoned-call-stack problem as frame_epochs just above,
    // for the heap-reclamation mark stack -- a host-level longjmp out of a
    // failed __builtin_assert skips every abandoned frame's LEV3, so their
    // heap_marks entries (and the VLA/alloca storage they were tracking)
    // are never retired. vm->bp already sits at this fresh call's initial
    // (shallowest) value from the stack setup above, so every leftover
    // entry -- belonging exclusively to frames deeper than that -- gets
    // reclaimed by the same heap_marks_truncate_to LONGJMP already uses,
    // sweeping both VLA and bare-alloca storage since whole frames are
    // being discarded, not just one block.
    if (vm->heap_reclaim_enabled && !vm->thread_records)
        heap_marks_truncate_to(vm, vm->bp);

    // Initialise main thread's TLS copy from the template built by gen()
    if (vm->tls_template_size > 0) {
        free(vm->current_tls_seg);
        vm->current_tls_seg = malloc(vm->tls_template_size);
        if (!vm->current_tls_seg)
            error("cc_run_at: failed to allocate main-thread TLS segment");
        memcpy(vm->current_tls_seg, vm->tls_template, vm->tls_template_size);
    }

    // Pass the entry's argument(s) via integer argument registers (ENT3 spills
    // these to the stack frame at bp[-1]/bp[-2], matching the register-based
    // calling convention).
    vm->regs[REG_A0] = a0;
    vm->regs[REG_A1] = a1;

    // Push a sentinel return address (0) so LEV can detect when main returns.
    // Stack layout before main's ENT:  [ret=0] ← sp
    // ENT will push old_bp and set bp=sp; ret_addr sits at bp[+1] after ENT.
    *--vm->sp = 0;

    int rc = ((vm->flags & CCCC_ENABLE_DEBUGGER) && !vm->dbg.crash_debug_auto)
                 ? debugger_run(vm, debugger_argc, debugger_argv) : vm_eval(vm);
    if (rc == CCCC_HOST_SIGNAL_RC && vm->dbg.host_fault_signal > 0)
        rc = 128 + vm->dbg.host_fault_signal;
    cccc_gil_release(vm);
    cc_running_vm = NULL;
    return rc;
}

int cc_run_at(VirtualMachine *vm, Pc entry, int argc, char **argv) {
    return cc_run_at_regs(vm, entry, argc, (long long)argv, argc, argv);
}

// Runs `entry` with a single pointer argument in REG_A0 (matching a
// void (*)(void *) signature) -- used to invoke TSS/pthread-key destructors
// from cccc_pthread_run_main_tss_destructors (src/stdlib/pthread.c) when
// pthread_exit() is called ON THE MAIN THREAD. That path runs after
// cc_run_at(main) has already returned -- the GIL is released and there is
// no live vm_eval on the C stack, so cccc_call_guest_callback's nested-
// reentry mechanism does not apply (same reasoning as cc_run_atexit_entries
// just below). If the debugger is enabled, debugger_run is told argc=0/
// argv=NULL, same as every other non-main cc_run_at caller (ctors/dtors,
// test functions) -- debugger_run always re-locates "main" internally
// regardless of what entry point was actually requested, a pre-existing
// characteristic of every one of those call sites, not something this
// function changes.
int cc_run_at1(VirtualMachine *vm, Pc entry, void *arg) {
    return cc_run_at_regs(vm, entry, (long long)arg, 0, 0, NULL);
}

// Synchronously invoke a guest function-pointer value from host C code that
// is already mid-vm_eval on this thread with the GIL held (see the doc
// comment in internal.h). Two cases, matching how a bare function-name
// expression can evaluate (codegen.c's third patch pass):
//
//   - An FFI token (fn_value <= CCCC_FFI_TOKEN_BASE): the "callback" is
//     itself an already-registered host function taken as a value (e.g.
//     qsort(a, n, sz, alphasort)). No VM reentry needed -- call the real
//     host function pointer directly via the existing FFI call path.
//
//   - A byte offset (any other value): a real guest bytecode function.
//     Reentering vm_eval on the same C stack is safe because op_LEV3_fn
//     recognizes a `0` return-address sentinel as "stop here" (the same
//     mechanism debugger_exec_condition_wrapper in debugger.c already
//     relies on for zero-argument condition wrappers); this generalizes
//     that pattern to N integer arguments in REG_A0.. and restores every
//     piece of state a nested call could disturb.
int cccc_call_guest_callback(VirtualMachine *vm, long long fn_value,
                              const long long *args, int nargs,
                              long long *out_ival) {
    if (!vm || nargs < 0 || nargs > 8)
        return -1;

    if (fn_value <= CCCC_FFI_TOKEN_BASE) {
        int ffi_idx = (int)(CCCC_FFI_TOKEN_BASE - fn_value);
        if (ffi_idx < 0 || ffi_idx >= vm->compiler.ffi_count)
            return -1;
        ForeignFunc *ff = &vm->compiler.ffi_table[ffi_idx];
        long long argbuf[8];
        for (int i = 0; i < nargs; i++)
            argbuf[i] = args[i];
        if (cccc_call_native_function(vm, ff->func_ptr, ff->name, argbuf, nargs,
                                      0, 0, 0, 0, ff->is_variadic,
                                      ff->num_fixed_args) != 0)
            return -1;
        *out_ival = vm->regs[REG_A0];
        return 0;
    }

    Pc entry = cc_byte_offset_to_pc(fn_value);
    if (entry == CCCC_INVALID_PC || entry > vm->text_ptr)
        return -1;

    long long saved_regs[NUM_REGS];
    FReg saved_fregs[NUM_REGS];
    VReg saved_vregs[NUM_REGS];
    memcpy(saved_regs, vm->regs, sizeof(saved_regs));
    memcpy(saved_fregs, vm->fregs, sizeof(saved_fregs));
    // #877: same defect class as the async-delivery register-liveness fix
    // just above in this file -- vregs was missing here even though
    // cccc_exec_state_save (this file) saves it, and a vector local can
    // legitimately be live in vregs[] across the nested reentry.
    memcpy(saved_vregs, vm->vregs, sizeof(saved_vregs));
    Pc saved_pc = vm->pc;
    long long *saved_bp = vm->bp;
    long long *saved_sp = vm->sp;
    long long *saved_shadow_sp = vm->shadow_sp;
    uint32_t saved_flags = vm->flags;
    int saved_single_step = vm->dbg.single_step;
    int saved_step_over = vm->dbg.step_over;
    int saved_step_out = vm->dbg.step_out;
    Pc saved_step_over_return_addr = vm->dbg.step_over_return_addr;
    long long *saved_step_out_bp = vm->dbg.step_out_bp;
    int saved_debugger_attached = vm->dbg.debugger_attached;

    if (check_stack_overflow(vm, 1))
        return -1;

    vm->flags &= ~CCCC_ENABLE_DEBUGGER;
    vm->dbg.single_step = 0;
    vm->dbg.step_over = 0;
    vm->dbg.step_out = 0;
    vm->dbg.debugger_attached = 0;

    for (int i = 0; i < nargs; i++)
        vm->regs[REG_A0 + i] = args[i];

    if (vm->flags & CCCC_CFI)
        *--vm->shadow_sp = 0;
    *--vm->sp = 0; // sentinel return address, recognized by op_LEV3_fn
    vm->pc = entry;
    vm_eval(vm);
    // vm_eval()'s return value is not a status code on the path taken by a
    // normal return -- when the dispatch loop's LEV handler pops the
    // sentinel return address (0) pushed above, it sets vm->pc to
    // CCCC_INVALID_PC and vm_eval propagates (int)vm->regs[REG_A0]
    // straight through as its own return value (see the `return (int)
    // vm->regs[REG_A0]` completion paths in cccc_vm_eval_dispatch,
    // src/vm.c). That register holds the *callback's own* return value,
    // which is legitimately negative for the extremely common case of a
    // three-way comparator (tsearch/tfind/tdelete/lfind/lsearch/qsort/
    // bsearch), so treating a negative vm_eval() result as failure here
    // silently misreported every "a < b" comparison as a callback fault.
    // The actual, unambiguous signal for "did we return cleanly" is
    // whether vm->pc reached CCCC_INVALID_PC via that sentinel -- any
    // other value (VM_TRAP_OR_RETURN paths return without reaching the
    // sentinel) means a genuine trap.
    int completed_cleanly = (vm->pc == CCCC_INVALID_PC);
    long long ival = vm->regs[REG_A0];

    memcpy(vm->regs, saved_regs, sizeof(saved_regs));
    memcpy(vm->fregs, saved_fregs, sizeof(saved_fregs));
    memcpy(vm->vregs, saved_vregs, sizeof(saved_vregs));
    vm->pc = saved_pc;
    vm->bp = saved_bp;
    vm->sp = saved_sp;
    vm->shadow_sp = saved_shadow_sp;
    vm->flags = saved_flags;
    vm->dbg.single_step = saved_single_step;
    vm->dbg.step_over = saved_step_over;
    vm->dbg.step_out = saved_step_out;
    vm->dbg.step_over_return_addr = saved_step_over_return_addr;
    vm->dbg.step_out_bp = saved_step_out_bp;
    vm->dbg.debugger_attached = saved_debugger_attached;

    if (!completed_cleanly)
        return -1;
    *out_ival = ival;
    return 0;
}

// Run __attribute__((constructor))/((destructor)) functions collected by
// gen() (codegen.c), already sorted into execution order. Each is invoked as
// a complete, non-nested cc_run_at cycle — reusing the normal startup/
// teardown machinery — rather than being spliced into main's call graph.
// Non-TLS globals live in vm->data_seg, which persists across these calls,
// so a constructor's writes are visible to main() and to later destructors.
// Caveat: cc_run_at() re-copies vm->tls_template into a fresh TLS segment on
// every call, so writes to _Thread_local variables inside a constructor are
// NOT visible to main() or to destructors — main-thread TLS is only ever
// seeded from the compile-time template, never carried across these
// pre/post-main invocations.
static void cc_run_init_entries(VirtualMachine *vm, CCCCInitEntry *list, int count) {
    for (int i = 0; i < count; i++)
        cc_run_at(vm, list[i].code_addr, 0, NULL);
}

// Drain vm->atexit_handlers in LIFO order on normal return from main() (#738)
// -- ISO C requires atexit handlers to run here too, not only on an explicit
// exit() call. Unlike wrap_exit's drain (stdlib.c), the GIL has already been
// released by the time cc_run_at(main) returns and there is no live vm_eval
// on the C stack, so cccc_call_guest_callback's nested-reentry mechanism
// does not apply here. Each handler instead gets its own complete,
// top-level cc_run_at cycle -- exactly the same mechanism already used for
// constructors/destructors just above/below this call.
static void cc_run_atexit_entries(VirtualMachine *vm) {
    // A running handler may itself call atexit() to register another one
    // (ISO C requires the newly-registered handler to also run before
    // termination, verified against real glibc behavior). Re-checking
    // vm->atexit_count on every iteration -- rather than capturing it once
    // -- means a handler pushed during the loop becomes the new top of
    // stack and runs next, exactly matching glibc's observed order.
    while (vm->atexit_count > 0) {
        long long fn_value = vm->atexit_handlers[--vm->atexit_count];
        if (fn_value <= CCCC_FFI_TOKEN_BASE) {
            int ffi_idx = (int)(CCCC_FFI_TOKEN_BASE - fn_value);
            if (ffi_idx < 0 || ffi_idx >= vm->compiler.ffi_count)
                continue;
            ForeignFunc *ff = &vm->compiler.ffi_table[ffi_idx];
            cccc_call_native_function(vm, ff->func_ptr, ff->name, NULL, 0,
                                      0, 0, 0, 0, ff->is_variadic,
                                      ff->num_fixed_args);
            continue;
        }
        Pc entry = cc_byte_offset_to_pc(fn_value);
        if (entry == CCCC_INVALID_PC || entry > vm->text_ptr)
            continue;
        cc_run_at(vm, entry, 0, NULL);
    }
}

int cc_run(VirtualMachine *vm, int argc, char **argv) {
    // #832: guarantee the guest program observes ISO C's startup contract
    // (round-to-nearest, no pending FP exceptions) regardless of what the
    // *compile* phase left in the host FP environment. This is a real,
    // independently-reproducible pre-existing issue, not something #832
    // introduced: tokenize.c's convert_pp_number scans every floating (and
    // decimal) literal's extent via a host strtold() call whose *value* is
    // discarded, but whose side effect isn't -- on at least one verified
    // platform (macOS/arm64), strtold("1.1", NULL) alone leaves the host's
    // FE_UNDERFLOW flag set, and nothing previously cleared it before
    // running the compiled guest program in the same process. Reproduced
    // with a plain `double x = 1.1;` global and no decimal code at all.
    // Cleared exactly once here (not inside cc_run_at, which also runs
    // constructors/main/atexit as separate top-level cycles) so a
    // constructor's own fesetround()/feraiseexcept() calls still correctly
    // persist into main() and beyond, matching real linked-C behavior.
    fesetround(FE_TONEAREST);
    feclearexcept(FE_ALL_EXCEPT);

    // #680: gates wrap_exit's (stdlib.c) destructor drain -- see the comment
    // on run_started/dtors_drained in cccc.h.
    vm->run_started = true;

    cc_run_init_entries(vm, vm->compiler.ctor_list, vm->compiler.ctor_count);
    int rc = cc_run_at(vm, vm->text_seg[0], argc, argv);
    // pthread_exit() called on the main thread reaches here the same way a
    // plain `return` from main() does (wrap_pthread_exit just sets pc =
    // CCCC_INVALID_PC), but POSIX/glibc DO run TSS/pthread-key destructors
    // for the former and not the latter -- cccc_pthread_run_main_tss_
    // destructors (stdlib/pthread.c) tells the two apart via a flag set only
    // by an actual pthread_exit()/thrd_exit() call, and is a no-op otherwise
    // (#863). Must run before atexit handlers, matching a real joined
    // pthread's destructors running at thread-exit time, well before any
    // later process-level atexit.
    cccc_pthread_run_main_tss_destructors(vm);
    // atexit handlers and destructors run here on normal return from main().
    // An explicit guest exit() reaches this point differently -- it drains
    // both atexit_handlers and dtor_list itself (wrap_exit, stdlib.c, #680)
    // before ever returning to the host libc exit() call, so this function
    // is never reached again in that case. _Exit()/quick_exit()/abort() are
    // untouched: they run neither atexit handlers nor destructors, matching
    // GCC and ISO C (see man/COVERAGE.md).
    cc_run_atexit_entries(vm);
    // Mark drained here too, purely so the flag's meaning ("destructors have
    // run") stays accurate regardless of which of the two paths (normal
    // return vs. wrap_exit) got here first -- nothing reads it after this.
    vm->dtors_drained = true;
    cc_run_init_entries(vm, vm->compiler.dtor_list, vm->compiler.dtor_count);
    return rc;
}

#if defined(_WIN32) || defined(_WIN64)
static size_t cccc_vm_page_size(void) {
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (size_t)si.dwPageSize;
}

void *cccc_vm_reserve(size_t bytes) {
    void *p = VirtualAlloc(NULL, bytes, MEM_RESERVE, PAGE_NOACCESS);
    return p; // NULL on failure
}

int cccc_vm_commit(void *base, size_t off, size_t len) {
    size_t pgsz = cccc_vm_page_size();
    // Align offset down, extend length to cover full pages
    size_t aligned_off = off & ~(pgsz - 1);
    size_t aligned_end = (off + len + pgsz - 1) & ~(pgsz - 1);
    size_t aligned_len = aligned_end - aligned_off;
    void *p = VirtualAlloc((char *)base + aligned_off, aligned_len,
                           MEM_COMMIT, PAGE_READWRITE);
    return (p == NULL) ? -1 : 0;
}

void cccc_vm_release(void *base, size_t bytes) {
    (void)bytes;
    VirtualFree(base, 0, MEM_RELEASE);
}
#else // POSIX
static size_t cccc_vm_page_size(void) {
    return (size_t)sysconf(_SC_PAGESIZE);
}

void *cccc_vm_reserve(size_t bytes) {
    void *p = mmap(NULL, bytes, PROT_NONE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return (p == MAP_FAILED) ? NULL : p;
}

int cccc_vm_commit(void *base, size_t off, size_t len) {
    size_t pgsz = cccc_vm_page_size();
    // Align offset down, extend length to cover full pages
    size_t aligned_off = off & ~(pgsz - 1);
    size_t aligned_end = (off + len + pgsz - 1) & ~(pgsz - 1);
    size_t aligned_len = aligned_end - aligned_off;
    return mprotect((char *)base + aligned_off, aligned_len,
                    PROT_READ | PROT_WRITE);
}

void cccc_vm_release(void *base, size_t bytes) {
    munmap(base, bytes);
}
#endif

char *cccc_path_find_executable(const char *name) {
    if (!name || !*name)
        return NULL;
    if (strchr(name, '/'))
        return access(name, X_OK) == 0 ? strdup(name) : NULL;

    const char *path_env = getenv("PATH");
    if (!path_env)
        return NULL;
    char *paths = strdup(path_env);
    if (!paths)
        return NULL;

    char *saveptr = NULL;
    for (char *dir = strtok_r(paths, ":", &saveptr); dir;
         dir = strtok_r(NULL, ":", &saveptr)) {
        size_t len = strlen(dir) + 1 + strlen(name) + 1;
        char *candidate = malloc(len);
        if (!candidate)
            continue;
        snprintf(candidate, len, "%s/%s", dir, name);
        if (access(candidate, X_OK) == 0) {
            free(paths);
            return candidate;
        }
        free(candidate);
    }
    free(paths);
    return NULL;
}

char *cccc_find_native_cc(void) {
    const char *env_cc = getenv("CCCC_NATIVE_CC");
    if (env_cc && *env_cc) {
        char *found = cccc_path_find_executable(env_cc);
        if (found)
            return found;
        fprintf(stderr, "error: CCCC_NATIVE_CC compiler '%s' not found\n",
                env_cc);
        return NULL;
    }

    const char *candidates[] = {"cc", "clang", "gcc"};
    for (size_t i = 0; i < sizeof(candidates) / sizeof(candidates[0]); i++) {
        char *found = cccc_path_find_executable(candidates[i]);
        if (found)
            return found;
    }

    fprintf(stderr,
            "error: no native C compiler found (tried cc, clang, gcc)\n");
    return NULL;
}

// Locate a toolchain tool (e.g. "ar", "ld") for --build mode.  An uppercased
// CCCC_NATIVE_<TOOL> env var overrides the PATH lookup.
char *cccc_find_native_tool(const char *tool) {
    char envname[64];
    snprintf(envname, sizeof(envname), "CCCC_NATIVE_%s", tool);
    for (char *p = envname + strlen("CCCC_NATIVE_"); *p; p++)
        if (*p >= 'a' && *p <= 'z')
            *p = (char)(*p - 'a' + 'A');
    const char *override = getenv(envname);
    if (override && *override) {
        char *found = cccc_path_find_executable(override);
        if (found)
            return found;
        fprintf(stderr, "error: %s tool '%s' not found\n", envname, override);
        return NULL;
    }
    char *found = cccc_path_find_executable(tool);
    if (!found)
        fprintf(stderr, "error: native tool '%s' not found in PATH\n", tool);
    return found;
}
