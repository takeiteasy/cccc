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
#include "./host_ffi_family.h"

// ========== FFI Helper ==========

int find_ffi_function(VirtualMachine *vm, const char *name) {
    if (!vm || !name)
        return -1;

    // Exact match only: compare using the cached name_len to avoid repeated
    // strlen. (#164)
    //
    // This used to also fall back to stripping a trailing digit suffix and
    // matching a registered *variadic* base name (e.g. "printf2" ->
    // "printf"), intended to support libc's "overloaded" names like
    // execl/execle/execlp resolving through one variadic slot. That
    // fallback wasn't scoped to those names -- any guest-defined function
    // whose name happened to end in a digit and match a registered
    // variadic base name (a guest's own `printf2`, `open2`, etc.) was
    // silently rebound to the host function instead of the guest's own
    // definition (#876). Removed: nothing in the standard headers or
    // stdlib declares a digit-suffixed name over a variadic base, so
    // there's nothing left relying on it.
    size_t len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name_len == len &&
            memcmp(vm->compiler.ffi_table[i].name, name, len) == 0) {
            return i;
        }
    }

    return -1;
}

const char *obj_external_name(Obj *obj) {
    return obj && obj->asm_label ? obj->asm_label : obj ? obj->name : NULL;
}

static bool is_host_family_name(const char *name) {
    static const char *const family[] = {
#define ROW(n) #n,
        CCCC_HOST_FFI_FAMILY(ROW) CCCC_HOST_FFI_FAMILY_LINUX(ROW)
#undef ROW
    };
    for (size_t i = 0; i < sizeof family / sizeof *family; i++)
        if (strcmp(name, family[i]) == 0)
            return true;
    return false;
}

// FFI slot a call to `obj` binds to. Under --use-system-headers a translated
// native (host_ffi_family.h) declared by a real, non-bundled header binds to
// its `__cccc_host_` passthrough, since that program already holds the host's
// numbers. The first declaration decides (Obj.tok), so a user prototype ahead
// of the header counts as non-bundled. Name-only lookups (find_ffi_function)
// stay canonical.
int ffi_index_for_obj(VirtualMachine *vm, Obj *obj) {
    const char *name = obj_external_name(obj);
    if (!name)
        return -1;
#ifdef __APPLE__
    // SDK headers spell asm labels with the Mach-O underscore
    // (`__asm("_confstr")`).
    if (obj->asm_label && name[0] == '_' && find_ffi_function(vm, name) < 0)
        name++;
#endif
    if (vm->compiler.use_system_headers && obj->tok && obj->tok->file &&
        is_host_family_name(name) &&
        !cc_file_is_cccc_bundled(vm, obj->tok->file->name)) {
        char host_name[64];
        snprintf(host_name, sizeof host_name, CCCC_HOST_FFI_PREFIX "%s", name);
        return find_ffi_function(vm, host_name);
    }
    return find_ffi_function(vm, name);
}

// A guest program can define its own function whose name happens to match
// a registered FFI symbol -- e.g. `int printf(const char *fmt, ...) { ... }`
// wrapping the real one. find_ffi_function's exact-name match doesn't know
// about the guest's own definition, so calls to that name were compiled as
// CALLF to the *host* printf instead of CALL to the guest's own body,
// silently calling the wrong code (#880). A bare declaration (the ordinary
// libc case -- no body) must still resolve to FFI; only a body wins.
int ffi_index_for_callee(VirtualMachine *vm, Obj *callee) {
    if (callee && callee->body)
        return -1;
    return ffi_index_for_obj(vm, callee);
}

static Obj *find_global_obj(Obj *prog, const char *name) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->name && strlen(obj->name) == strlen(name) &&
            strncmp(obj->name, name, strlen(name)) == 0)
            return obj;
    }
    return NULL;
}

bool is_extern_func_name(Node *node, const char *name) {
    if (!node || node->kind != ND_VAR || !node->var ||
        !node->var->is_function || node->var->is_definition || !node->var->name)
        return false;
    return strlen(node->var->name) == strlen(name) &&
           memcmp(node->var->name, name, strlen(name)) == 0;
}

static void add_data_reloc(VirtualMachine *vm, long long data_offset,
                           int target_segment, long long target_offset,
                           long long addend) {
    PATCH_GROW(vm, data_relocs, num_data_relocs, data_relocs_cap);
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].data_offset =
        data_offset;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_segment =
        target_segment;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_offset =
        target_offset;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].addend = addend;
    vm->compiler.num_data_relocs++;
}

static void add_tls_reloc(VirtualMachine *vm, long long tls_offset,
                          int target_segment, long long target_offset,
                          long long addend) {
    PATCH_GROW(vm, tls_relocs, num_tls_relocs, tls_relocs_cap);
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].tls_offset =
        tls_offset;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_segment =
        target_segment;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_offset =
        target_offset;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].addend = addend;
    vm->compiler.num_tls_relocs++;
}

// Persistent label map -- declared in codegen_internal.h (shared with
// codegen_call.c and codegen_func.c); defined here.
GlobalLabelEntry *global_label_map  = NULL;
int               num_global_labels = 0;
int               global_labels_cap = 0;

void apply_global_relocations(VirtualMachine *vm, Obj *prog, Obj *stop) {
    for (Obj *var = prog; var != stop; var = var->next) {
        if (var->is_function || var->reuse_of)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid global relocation");

            long long target_offset;
            long long value;
            int       segment;

            Obj      *target = find_global_obj(prog, *rel->label);
            if (target && target->reuse_of)
                target = reuse_root(target);
            if (!target) {
                // Not a global object — try the persistent label map.  This
                // handles &&label stored in a static/global initialiser, where
                // the label lives in the text segment rather than the data
                // segment (#573).
                Pc label_pc = 0;
                for (int li = 0; li < num_global_labels; li++) {
                    if (strcmp(global_label_map[li].name, *rel->label) == 0) {
                        label_pc = global_label_map[li].offset;
                        break;
                    }
                }
                if (!label_pc)
                    error("undefined relocation target: %s", *rel->label);
                segment       = 1;
                target_offset = cc_pc_to_byte_offset(label_pc);
                value         = target_offset + rel->addend;
            } else if (target->is_function) {
                Pc stub = 0;
                if (!target->body) {
                    // Undefined function: if it is an FFI/extern function,
                    // store the FFI dispatch token (CCCC_FFI_TOKEN_BASE - idx)
                    // directly, mirroring the runtime function-address path
                    // (CALLN/JMPI recognise the token).  This is how static
                    // initialisers that take the address of a libc/POSIX
                    // function resolve, e.g. SQLite's unix VFS structs full of
                    // { close, read, write, ... }
                    // (#589).  The token is segment-independent, so no data/tls
                    // reloc is recorded (it survives .c4 round-trips verbatim,
                    // exactly like the text-segment FFI case).
                    int ffi_idx = ffi_index_for_obj(vm, target);
                    if (ffi_idx < 0) {
                        // VM-lowered intrinsics (signal, longjmp, dlopen...)
                        // point at their stub instead.
                        stub = cc_intrinsic_stub_addr(vm, target);
                        if (!stub &&
                            (obj_is_reserved_builtin(target, "setjmp") ||
                             obj_is_reserved_builtin(target, "_setjmp") ||
                             obj_is_reserved_builtin(target, "sigsetjmp")))
                            error("cannot take the address of '%s'",
                                  target->name);
                        if (!stub)
                            error("unsupported relocation to undefined "
                                  "function: %s",
                                  target->name);
                    } else {
                        long long slot  = var->offset + rel->offset;
                        long long token = CCCC_FFI_TOKEN_BASE - ffi_idx;
                        if (var->is_tls)
                            *(long long *)(vm->tls_template + slot) = token;
                        else
                            *(long long *)(vm->data_seg + slot) = token;
                        continue;
                    }
                }
                segment       = 1;
                target_offset = cc_pc_to_byte_offset(
                    stub ? stub : (Pc)fn_canonical(target)->code_addr);
                value = target_offset + rel->addend;
            } else {
                // #957: a static initializer taking &g needs the same
                // defined-or-deferred check as an ordinary reference (see
                // the fourth pass in gen()) -- find_global_obj resolves by
                // name regardless of whether target is ever defined, so
                // without this an extern-declared-but-never-defined global
                // silently gets an address into an inert zero slot.
                if (!target->is_definition && !target->is_tentative &&
                    !target->init_data && !vm->compiler.compile_only)
                    error("undefined global: %s", target->name);
                segment       = 0;
                target_offset = target->offset;
                value = (long long)(vm->data_seg + target_offset + rel->addend);
            }

            long long slot_offset = var->offset + rel->offset;
            if (var->is_tls) {
                // TLS pointer initialiser: patch into tls_template and record
                // the reloc so it can be re-applied after .c4 load (#493).
                *(long long *)(vm->tls_template + slot_offset) = value;
                add_tls_reloc(vm, slot_offset, segment, target_offset,
                              rel->addend);
            } else {
                *(long long *)(vm->data_seg + slot_offset) = value;
                add_data_reloc(vm, slot_offset, segment, target_offset,
                               rel->addend);
            }
        }
    }
}

Obj *fn_canonical(Obj *fn) {
    while (fn->reload_of)
        fn = fn->reload_of;
    return fn;
}

Obj *reuse_root(Obj *obj) {
    while (obj->reuse_of)
        obj = obj->reuse_of;
    return obj;
}

// A name two static functions share maps to this instead of either of them.
static Obj ambiguous_static_def;

void add_static_function_def(HashMap *static_defs, Obj *fn) {
    Obj *prev = hashmap_get(static_defs, fn->name);
    hashmap_put(static_defs, fn->name, prev ? &ambiguous_static_def : fn);
}

// A static target is resolved by identity. A body-less static prototype (code
// macro-generated against the TU's own static function names it that way)
// falls back to the unique static definition of that name.
Obj *find_function_definition_for_patch(HashMap *fn_defs, HashMap *static_defs,
                                        Obj *target) {
    if (target->reuse_of)
        return fn_canonical(reuse_root(target));
    Obj *def;
    if (target->is_static && target->body)
        def = target;
    else if (target->is_static) {
        def = hashmap_get(static_defs, target->name);
        if (def == &ambiguous_static_def)
            def = NULL;
    } else
        def = hashmap_get(fn_defs, target->name);
    return def ? fn_canonical(def) : NULL;
}

void add_debug_symbol(VirtualMachine *vm, char *name, long long offset,
                      Type *ty, int is_local, Obj *owner_fn, int align_log) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !name || !*name)
        return;
    if (vm->dbg.num_debug_symbols == vm->dbg.debug_symbols_cap) {
        vm->dbg.debug_symbols_cap =
            vm->dbg.debug_symbols_cap ? vm->dbg.debug_symbols_cap * 2 : 256;
        vm->dbg.debug_symbols =
            realloc(vm->dbg.debug_symbols,
                    (size_t)vm->dbg.debug_symbols_cap * sizeof(DebugSymbol));
        if (!vm->dbg.debug_symbols)
            error("out of memory for debug symbols");
    }

    DebugSymbol *sym = &vm->dbg.debug_symbols[vm->dbg.num_debug_symbols++];
    sym->name        = name;
    sym->offset      = offset;
    sym->ty          = ty;
    sym->is_local    = is_local;
    sym->scope_depth = 0;
    sym->owner_fn    = owner_fn;
    sym->align_log   = align_log;
}
// ========== Register Allocator ==========
// Simple bitmap allocator for temporary registers T0-T10

// PLACEHOLDER: file-scope statics (temp_reg_in_use, num_label_defs,
// num_label_patches, label_defs[], label_patches[]) are shared across all
// CCCC instances compiled in the same process. Move them onto the CCCC/Compiler
// struct as part of the thread-safety work tracked in ticket #139 so two CCCC
// instances (or two threads) can compile in parallel.
// Ticket: https://todo.sr.ht/~takeiteasy/cccc/161
static unsigned int temp_reg_in_use = 0;

static const int    temp_reg_map[]  = {REG_T0, REG_T1, REG_T2, REG_T3,
                                       REG_T4, REG_T5, REG_T6, REG_T7,
                                       REG_T8, REG_T9, REG_T10};
#define NUM_TEMP_REGS 11

int alloc_temp_reg(void) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (!(temp_reg_in_use & (1 << i))) {
            temp_reg_in_use |= (1 << i);
            return temp_reg_map[i];
        }
    }
    error("codegen: out of temporary registers");
    return -1;
}

void free_temp_reg(int reg) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (temp_reg_map[i] == reg) {
            temp_reg_in_use &= ~(1 << i);
            return;
        }
    }
}

// Mark a specific register as in-use (needed after function calls reset temps)
void mark_temp_reg_used(int reg) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (temp_reg_map[i] == reg) {
            temp_reg_in_use |= (1 << i);
            return;
        }
    }
}

void reset_temp_regs(void) {
    temp_reg_in_use = 0;
}

// Number of temp registers currently free. Used by the binary-op codegen to
// decide when to spill the LHS to the stack instead of holding a live temp
// across the RHS recursion (ticket #587 — bounds peak register use on deeply
// nested / right-leaning expression trees).
int temp_regs_free(void) {
    int used = 0;
    for (int i = 0; i < NUM_TEMP_REGS; i++)
        if (temp_reg_in_use & (1 << i))
            used++;
    return NUM_TEMP_REGS - used;
}

// When free temps drop to this many, the binary-op path stops reserving a
// register for the RHS result (which would stay live across the RHS subtree
// recursion → O(depth) peak) and instead spills the LHS to the stack, reusing
// dest_reg for the RHS. Leaves headroom for ops that need 1-2 temps at once
// (e.g. the float push/pop branch). TEMP_REG_SPILL_THRESHOLD itself is
// declared in codegen_internal.h (used from codegen_expr.c too).

// ========== (removed) Scalar/FP Local Promotion + Restrict-Value Cache ======
//
// #1214 removed the VM bytecode optimiser; #1219 removed what it left behind.
// Local-register promotion (#249), the restrict-value cache (#654 / #750),
// the restrict-derived-locals analysis and indexed-deref extraction were all
// opt_level >= 2 codegen optimisations with no counterpart now that the VM
// has a single configuration.
