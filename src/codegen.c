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

#include "./internal.h"
#if !defined(_WIN32)
#include <sys/wait.h>
#endif

// Grow a dynamic patch table by 2x when full (initial capacity 256).
// 'field' is the array pointer member, 'nf' is the count, 'cf' is the cap.
#define PATCH_GROW(vm, field, nf, cf)                                       \
    do {                                                                    \
        if ((vm)->compiler.nf >= (vm)->compiler.cf) {                       \
            int _nc = (vm)->compiler.cf ? (vm)->compiler.cf * 2 : 256;      \
            void *_p = realloc((vm)->compiler.field,                        \
                               (size_t)_nc * sizeof(*(vm)->compiler.field)); \
            if (!_p) error("out of memory growing patch table");            \
            (vm)->compiler.field = _p;                                      \
            (vm)->compiler.cf = _nc;                                        \
        }                                                                   \
    } while (0)

// ========== FFI Helper ==========

static int find_ffi_function(VirtualMachine *vm, const char *name) {
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

static const char *obj_external_name(Obj *obj) {
    return obj && obj->asm_label ? obj->asm_label : obj ? obj->name : NULL;
}

// FFI resolution for a call/tail-call *callee*, as opposed to a bare name
// lookup (find_ffi_function above, still used directly by the reloc/patch
// passes and by runtime-helper lookups that have no guest Obj at all).
//
// A guest program can define its own function whose name happens to match
// a registered FFI symbol -- e.g. `int printf(const char *fmt, ...) { ... }`
// wrapping the real one. find_ffi_function's exact-name match doesn't know
// about the guest's own definition, so calls to that name were compiled as
// CALLF to the *host* printf instead of CALL to the guest's own body,
// silently calling the wrong code (#880). A bare declaration (the ordinary
// libc case -- no body) must still resolve to FFI; only a body wins.
static int ffi_index_for_callee(VirtualMachine *vm, Obj *callee) {
    if (callee && callee->body)
        return -1;
    return find_ffi_function(vm, obj_external_name(callee));
}

static Obj *find_global_obj(Obj *prog, const char *name) {
    for (Obj *obj = prog; obj; obj = obj->next) {
        if (obj->name && strlen(obj->name) == strlen(name) &&
            strncmp(obj->name, name, strlen(name)) == 0)
            return obj;
    }
    return NULL;
}

static bool is_extern_func_name(Node *node, const char *name) {
    if (!node || node->kind != ND_VAR || !node->var || !node->var->is_function ||
        node->var->is_definition || !node->var->name)
        return false;
    return strlen(node->var->name) == strlen(name) &&
           memcmp(node->var->name, name, strlen(name)) == 0;
}

static void add_data_reloc(VirtualMachine *vm, long long data_offset, int target_segment,
                           long long target_offset, long long addend) {
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

static void add_tls_reloc(VirtualMachine *vm, long long tls_offset, int target_segment,
                          long long target_offset, long long addend) {
    PATCH_GROW(vm, tls_relocs, num_tls_relocs, tls_relocs_cap);
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].tls_offset    = tls_offset;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_segment = target_segment;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].target_offset  = target_offset;
    vm->compiler.tls_relocs[vm->compiler.num_tls_relocs].addend         = addend;
    vm->compiler.num_tls_relocs++;
}

// Persistent label map: records every label defined across all functions during
// a single gen() call.  Unlike label_defs[] (per-function, reset each time)
// this map survives until apply_global_relocations() has run, so &&label
// stored in static/global initialisers can be resolved to their text offsets
// (#573).  All .L..N names are globally unique (unique_name_counter), so there
// are no cross-function clashes.
typedef struct { char *name; Pc offset; } GlobalLabelEntry;
static GlobalLabelEntry *global_label_map = NULL;
static int num_global_labels = 0;
static int global_labels_cap = 0;

static void apply_global_relocations(VirtualMachine *vm, Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid global relocation");

            long long target_offset;
            long long value;
            int segment;

            Obj *target = find_global_obj(prog, *rel->label);
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
                segment      = 1;
                target_offset = cc_pc_to_byte_offset(label_pc);
                value        = target_offset + rel->addend;
            } else if (target->is_function) {
                if (!target->body) {
                    // Undefined function: if it is an FFI/extern function, store
                    // the FFI dispatch token (CCCC_FFI_TOKEN_BASE - idx) directly,
                    // mirroring the runtime function-address path (CALLN/JMPI
                    // recognise the token).  This is how static initialisers that
                    // take the address of a libc/POSIX function resolve, e.g.
                    // SQLite's unix VFS structs full of { close, read, write, ... }
                    // (#589).  The token is segment-independent, so no data/tls
                    // reloc is recorded (it survives .c4 round-trips verbatim,
                    // exactly like the text-segment FFI case).
                    int ffi_idx = find_ffi_function(vm, obj_external_name(target));
                    if (ffi_idx < 0)
                        error("unsupported relocation to undefined function: %s",
                              target->name);
                    long long slot = var->offset + rel->offset;
                    long long token = CCCC_FFI_TOKEN_BASE - ffi_idx;
                    if (var->is_tls)
                        *(long long *)(vm->tls_template + slot) = token;
                    else
                        *(long long *)(vm->data_seg + slot) = token;
                    continue;
                }
                segment      = 1;
                target_offset = cc_pc_to_byte_offset((Pc)target->code_addr);
                value        = target_offset + rel->addend;
            } else {
                segment      = 0;
                target_offset = target->offset;
                value        = (long long)(vm->data_seg + target_offset + rel->addend);
            }

            long long slot_offset = var->offset + rel->offset;
            if (var->is_tls) {
                // TLS pointer initialiser: patch into tls_template and record
                // the reloc so it can be re-applied after .c4 load (#493).
                *(long long *)(vm->tls_template + slot_offset) = value;
                add_tls_reloc(vm, slot_offset, segment, target_offset, rel->addend);
            } else {
                *(long long *)(vm->data_seg + slot_offset) = value;
                add_data_reloc(vm, slot_offset, segment, target_offset, rel->addend);
            }
        }
    }
}

static Obj *find_function_definition_for_patch(HashMap *fn_defs, Obj *target) {
    if (target->is_static && target->body)
        return target;

    return hashmap_get(fn_defs, target->name);
}

static void add_debug_symbol(VirtualMachine *vm, char *name, long long offset, Type *ty,
                             int is_local, Obj *owner_fn) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !name || !*name)
        return;
    if (vm->dbg.num_debug_symbols >= MAX_DEBUG_SYMBOLS)
        return;

    DebugSymbol *sym = &vm->dbg.debug_symbols[vm->dbg.num_debug_symbols++];
    sym->name = name;
    sym->offset = offset;
    sym->ty = ty;
    sym->is_local = is_local;
    sym->scope_depth = 0;
    sym->owner_fn = owner_fn;
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

static const int temp_reg_map[] = {REG_T0, REG_T1, REG_T2, REG_T3,
                                   REG_T4, REG_T5, REG_T6, REG_T7,
                                   REG_T8, REG_T9, REG_T10};
#define NUM_TEMP_REGS 11

static Obj *belongs_to_outer_function(Obj *current_fn, Obj *var);

static int alloc_temp_reg(void) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (!(temp_reg_in_use & (1 << i))) {
            temp_reg_in_use |= (1 << i);
            return temp_reg_map[i];
        }
    }
    error("codegen: out of temporary registers");
    return -1;
}

static void free_temp_reg(int reg) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (temp_reg_map[i] == reg) {
            temp_reg_in_use &= ~(1 << i);
            return;
        }
    }
}

// Mark a specific register as in-use (needed after function calls reset temps)
static void mark_temp_reg_used(int reg) {
    for (int i = 0; i < NUM_TEMP_REGS; i++) {
        if (temp_reg_map[i] == reg) {
            temp_reg_in_use |= (1 << i);
            return;
        }
    }
}

static void reset_temp_regs(void) { temp_reg_in_use = 0; }

// Number of temp registers currently free. Used by the binary-op codegen to
// decide when to spill the LHS to the stack instead of holding a live temp
// across the RHS recursion (ticket #587 — bounds peak register use on deeply
// nested / right-leaning expression trees).
static int temp_regs_free(void) {
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
// (e.g. the float push/pop branch).
#define TEMP_REG_SPILL_THRESHOLD 2

// ========== Scalar Local Promotion (#249) ==========

typedef struct {
    Obj *var;
    int score;
    bool address_escapes;
} PromotionCandidate;

static int promoted_local_index(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.promoted_count; i++)
        if (vm->compiler.promoted_locals[i] == var)
            return i;
    return -1;
}

static bool is_promoted_local(VirtualMachine *vm, Obj *var) {
    return promoted_local_index(vm, var) >= 0;
}

static int promoted_local_reg(VirtualMachine *vm, Obj *var) {
    int idx = promoted_local_index(vm, var);
    return idx >= 0 ? vm->compiler.promoted_regs[idx] : -1;
}

static void promotion_alias_reset(VirtualMachine *vm) {
    vm->compiler.promotion_alias_count = 0;
}

static void promotion_alias_add(VirtualMachine *vm, Obj *alias, Obj *target) {
    if (!alias || !target || vm->compiler.promotion_alias_count >= 16)
        return;
    for (int i = 0; i < vm->compiler.promotion_alias_count; i++) {
        if (vm->compiler.promotion_alias_vars[i] == alias) {
            vm->compiler.promotion_alias_targets[i] = target;
            return;
        }
    }
    int idx = vm->compiler.promotion_alias_count++;
    vm->compiler.promotion_alias_vars[idx] = alias;
    vm->compiler.promotion_alias_targets[idx] = target;
}

static Obj *promotion_alias_target(VirtualMachine *vm, Obj *alias) {
    for (int i = 0; i < vm->compiler.promotion_alias_count; i++)
        if (vm->compiler.promotion_alias_vars[i] == alias)
            return vm->compiler.promotion_alias_targets[i];
    return NULL;
}

static Obj *promoted_deref_target(VirtualMachine *vm, Node *node) {
    if (!node || node->kind != ND_DEREF || !node->lhs ||
        node->lhs->kind != ND_VAR)
        return NULL;
    return promotion_alias_target(vm, node->lhs->var);
}

static bool is_scalar_promotion_type(Type *ty) {
    if (!ty || ty->is_volatile || ty->size > 8)
        return false;
    switch (ty->kind) {
    case TY_BOOL:
    case TY_CHAR:
    case TY_SHORT:
    case TY_INT:
    case TY_LONG:
    case TY_ENUM:
    case TY_PTR:
    case TY_BITINT:
        return true;
    default:
        return false;
    }
}

static bool is_float_promotion_type(Type *ty) {
    if (!ty || ty->is_volatile || ty->size > 8)
        return false;
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE;
}

static bool promotion_candidate_ok(VirtualMachine *vm, Obj *fn, Obj *var) {
    if (!var || !var->is_local || var->is_block_var || var->is_captured ||
        var->is_static || var == fn->va_area || var == fn->alloca_bottom)
        return false;
    if (!is_scalar_promotion_type(var->ty))
        return false;
    if (belongs_to_outer_function(fn, var))
        return false;
    if (var->name && strncmp(var->name, "__static_link", 14) == 0)
        return false;
    // Variables with cleanup functions have their address taken implicitly by
    // the cleanup call mechanism — they must not be promoted to a register.
    if (var->cleanup_fn)
        return false;
    return true;
}

static bool is_fp_promotion_candidate_ok(VirtualMachine *vm, Obj *fn, Obj *var) {
    if (!var || !var->is_local || var->is_block_var || var->is_captured ||
        var->is_static || var == fn->va_area || var == fn->alloca_bottom)
        return false;
    if (!is_float_promotion_type(var->ty))
        return false;
    if (belongs_to_outer_function(fn, var))
        return false;
    if (var->name && strncmp(var->name, "__static_link", 14) == 0)
        return false;
    // Variables with cleanup functions have their address taken implicitly —
    // don't promote them to a register.
    if (var->cleanup_fn)
        return false;
    return true;
}

static PromotionCandidate *promotion_find_candidate(PromotionCandidate *cands,
                                                     int count, Obj *var) {
    for (int i = 0; i < count; i++)
        if (cands[i].var == var)
            return &cands[i];
    return NULL;
}

static bool is_synthetic_addr_assignment(Node *parent, Node *addr_node) {
    if (!parent || parent->kind != ND_ASSIGN || parent->rhs != addr_node)
        return false;
    if (!parent->lhs || parent->lhs->kind != ND_VAR || !parent->lhs->var)
        return false;
    Obj *lhs = parent->lhs->var;
    return lhs->is_local && (!lhs->name || lhs->name[0] == '\0');
}

static void collect_promotion_candidates(VirtualMachine *vm, Obj *fn, Node *node,
                                         Node *parent,
                                         PromotionCandidate *cands, int count,
                                         int loop_depth) {
    // Walk the sibling chain iteratively.  Recursing into node->next here
    // would re-process the remainder of the chain once per sibling, since
    // every recursive call repeats the same node->next traversal — that is
    // O(2^N) in the number of statements.  Iterating the chain and recursing
    // only into children keeps this linear.
    for (; node; node = node->next) {
        if (node->kind == ND_VAR && node->var) {
            PromotionCandidate *cand =
                promotion_find_candidate(cands, count, node->var);
            if (cand)
                cand->score += 1 + loop_depth * 4;
        }

        if (node->kind == ND_ADDR && node->lhs && node->lhs->kind == ND_VAR &&
            node->lhs->var) {
            PromotionCandidate *cand =
                promotion_find_candidate(cands, count, node->lhs->var);
            if (cand && !is_synthetic_addr_assignment(parent, node))
                cand->address_escapes = true;
        }

        int child_loop_depth =
            loop_depth + (node->kind == ND_FOR || node->kind == ND_DO);
        collect_promotion_candidates(vm, fn, node->lhs, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->rhs, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->cond, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->then, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->els, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->init, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->inc, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->body, node, cands, count,
                                     child_loop_depth);
        // ND_OVERFLOW_ARITH stores its result pointer (&var) in cas_addr,
        // which isn't reached via lhs/rhs/args - without this, a promoted
        // local's address-escape via ckd_add/sub/mul goes undetected and
        // the promoted register goes stale after IOVFL stores through it.
        collect_promotion_candidates(vm, fn, node->cas_addr, node, cands, count,
                                     child_loop_depth);
        // ND_CAS (__builtin_compare_and_swap) passes &cas_old to ACAS, so the
        // expected-value variable's address escapes through cas_old.  Walk
        // cas_old and cas_new so that any ND_ADDR inside them marks the
        // target variable as address-escaping and prevents its promotion.
        collect_promotion_candidates(vm, fn, node->cas_old, node, cands, count,
                                     child_loop_depth);
        collect_promotion_candidates(vm, fn, node->cas_new, node, cands, count,
                                     child_loop_depth);
        for (Node *arg = node->args; arg; arg = arg->next)
            collect_promotion_candidates(vm, fn, arg, node, cands, count,
                                         child_loop_depth);
    }
}

static int promotion_candidate_cmp(const void *a, const void *b) {
    const PromotionCandidate *ca = (const PromotionCandidate *)a;
    const PromotionCandidate *cb = (const PromotionCandidate *)b;
    if (ca->address_escapes != cb->address_escapes)
        return ca->address_escapes ? 1 : -1;
    return cb->score - ca->score;
}

static void prepare_local_promotion(VirtualMachine *vm, Obj *fn, int base_stack_size) {
    vm->compiler.promoted_count = 0;
    promotion_alias_reset(vm);
    memset(vm->compiler.promoted_locals, 0, sizeof(vm->compiler.promoted_locals));
    memset(vm->compiler.promoted_regs, 0, sizeof(vm->compiler.promoted_regs));
    memset(vm->compiler.promoted_save_offsets, 0,
           sizeof(vm->compiler.promoted_save_offsets));
    memset(vm->compiler.promoted_dirty, 0, sizeof(vm->compiler.promoted_dirty));

    if (vm->compiler.opt_level < 2 || (vm->flags & CCCC_ENABLE_DEBUGGER))
        return;

    int local_count = 0;
    for (Obj *var = fn->locals; var; var = var->next)
        if (promotion_candidate_ok(vm, fn, var))
            local_count++;
    if (local_count == 0)
        return;

    PromotionCandidate *cands =
        calloc((size_t)local_count, sizeof(PromotionCandidate));
    if (!cands)
        error("out of memory");
    int idx = 0;
    for (Obj *var = fn->locals; var; var = var->next)
        if (promotion_candidate_ok(vm, fn, var))
            cands[idx++].var = var;

    collect_promotion_candidates(vm, fn, fn->body, NULL, cands, local_count, 0);
    qsort(cands, (size_t)local_count, sizeof(PromotionCandidate),
          promotion_candidate_cmp);

    // Only use S0-S3 for scalar promotion; S4-S7 are reserved for the restrict cache.
    static const int sregs[] = {REG_S0, REG_S1, REG_S2, REG_S3};
    for (int i = 0; i < local_count && vm->compiler.promoted_count < 4; i++) {
        if (cands[i].address_escapes || cands[i].score < 3)
            continue;
        int p = vm->compiler.promoted_count++;
        vm->compiler.promoted_locals[p] = cands[i].var;
        vm->compiler.promoted_regs[p] = sregs[p];
        vm->compiler.promoted_save_offsets[p] = -(base_stack_size + p + 1);
    }
    free(cands);
}

static void prepare_fp_local_promotion(VirtualMachine *vm, Obj *fn, int base_stack_size) {
    memset(vm->compiler.fp_promoted_locals, 0, sizeof(vm->compiler.fp_promoted_locals));
    memset(vm->compiler.fp_promoted_regs, 0, sizeof(vm->compiler.fp_promoted_regs));
    memset(vm->compiler.fp_promoted_save_offsets, 0,
           sizeof(vm->compiler.fp_promoted_save_offsets));
    memset(vm->compiler.fp_promoted_dirty, 0, sizeof(vm->compiler.fp_promoted_dirty));
    vm->compiler.fp_promoted_count = 0;

    if (vm->compiler.opt_level < 2 || (vm->flags & CCCC_ENABLE_DEBUGGER))
        return;

    int local_count = 0;
    for (Obj *var = fn->locals; var; var = var->next)
        if (is_fp_promotion_candidate_ok(vm, fn, var))
            local_count++;
    if (local_count == 0)
        return;

    PromotionCandidate *cands =
        calloc((size_t)local_count, sizeof(PromotionCandidate));
    if (!cands)
        error("out of memory");
    int idx = 0;
    for (Obj *var = fn->locals; var; var = var->next)
        if (is_fp_promotion_candidate_ok(vm, fn, var))
            cands[idx++].var = var;

    collect_promotion_candidates(vm, fn, fn->body, NULL, cands, local_count, 0);
    qsort(cands, (size_t)local_count, sizeof(PromotionCandidate),
          promotion_candidate_cmp);

    static const int fsregs[] = {FREG_S0, FREG_S1, FREG_S2, FREG_S3};
    for (int i = 0; i < local_count && vm->compiler.fp_promoted_count < 4; i++) {
        if (cands[i].address_escapes || cands[i].score < 3)
            continue;
        int q = vm->compiler.fp_promoted_count++;
        vm->compiler.fp_promoted_locals[q] = cands[i].var;
        vm->compiler.fp_promoted_regs[q] = fsregs[q];
        // Save slots come after the integer promoted save slots.
        vm->compiler.fp_promoted_save_offsets[q] =
            -(base_stack_size + vm->compiler.promoted_count + q + 1);
    }
    free(cands);
}

// ========== Restrict-param Deref Cache (#267) and Derived-Local Analysis (#269) ==========

// Forward declarations for helpers used by the restrict cache
static void emit_mov3(VirtualMachine *vm, int rd, int rs);
static void emit_local_store(VirtualMachine *vm, Type *ty, int rd_val, long long offset);
static void emit_local_load(VirtualMachine *vm, Type *ty, int rd, long long offset);
static void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr);
static void emit_normalize_promoted_scalar(VirtualMachine *vm, Type *ty, int reg);
static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);
static inline bool is_wide_bitint(Type *ty);

// Evaluate a constant byte offset from a node (ND_NUM or ND_MUL(ND_NUM, ND_NUM)).
// Returns true and sets *out on success. Strips ND_CAST wrappers.
static bool eval_const_byte_offset(Node *n, long *out) {
    while (n && n->kind == ND_CAST) n = n->lhs;
    if (!n) return false;
    if (n->kind == ND_NUM) { *out = (long)n->val; return true; }
    if (n->kind == ND_MUL) {
        Node *ml = n->lhs, *mr = n->rhs;
        while (ml && ml->kind == ND_CAST) ml = ml->lhs;
        while (mr && mr->kind == ND_CAST) mr = mr->lhs;
        if (ml && mr && ml->kind == ND_NUM && mr->kind == ND_NUM) {
            *out = (long)(ml->val * mr->val);
            return true;
        }
    }
    return false;
}

// Extract (restrict_param, byte_offset) from a raw pointer expression.
// Used by the pre-pass to detect `int *q = p ± const` assignments.
// Sets *out_var_offset=true when the offset is non-constant (derivation known
// only for invalidation purposes, not for cache slot keying).
// Only resolves direct restrict params (single-hop; derived locals not yet mapped).
static bool restrict_extract_base_offset(Node *expr, Obj **out_param,
                                          long *out_byte_offset,
                                          bool *out_var_offset) {
    while (expr && expr->kind == ND_CAST) expr = expr->lhs;
    if (!expr) return false;

    // Pattern: plain param
    if (expr->kind == ND_VAR) {
        Obj *var = expr->var;
        if (!var || !var->is_param || !var->ty || var->ty->kind != TY_PTR ||
            !var->ty->is_restrict || !var->ty->base ||
            !is_scalar_promotion_type(var->ty->base))
            return false;
        *out_param = var;
        *out_byte_offset = 0;
        *out_var_offset = false;
        return true;
    }

    // Pattern: param +/- offset
    if (expr->kind != ND_ADD && expr->kind != ND_SUB) return false;
    bool is_sub = (expr->kind == ND_SUB);

    // For ADD try both orderings; for SUB only (ptr - off)
    Node *sides[2][2] = {{expr->lhs, expr->rhs}, {expr->rhs, expr->lhs}};
    int n_sides = is_sub ? 1 : 2;
    for (int s = 0; s < n_sides; s++) {
        Node *ptr_node = sides[s][0], *off_node = sides[s][1];
        while (ptr_node && ptr_node->kind == ND_CAST) ptr_node = ptr_node->lhs;
        while (off_node && off_node->kind == ND_CAST) off_node = off_node->lhs;
        if (!ptr_node || !off_node || ptr_node->kind != ND_VAR) continue;
        Obj *var = ptr_node->var;
        if (!var || !var->is_param || !var->ty || var->ty->kind != TY_PTR ||
            !var->ty->is_restrict || !var->ty->base ||
            !is_scalar_promotion_type(var->ty->base))
            continue;
        long byte_off = 0;
        bool is_var = !eval_const_byte_offset(off_node, &byte_off);
        if (is_sub && !is_var) byte_off = -byte_off;
        *out_param = var;
        *out_byte_offset = is_var ? 0 : byte_off;
        *out_var_offset = is_var;
        return true;
    }
    return false;
}

// ---- Pre-pass: collect derived locals (#269) ----

// Per-candidate state collected during the pre-pass AST walk.
#define MAX_DERIVED_CANDS 24
typedef struct {
    Obj  *var;
    Obj  *base_param;
    long  byte_offset;
    bool  var_offset;
    int   assign_count;
    bool  addr_taken;
} DerivedCand;

static DerivedCand *derived_cand_find_or_create(DerivedCand *cands, int *nc, Obj *var) {
    for (int i = 0; i < *nc; i++)
        if (cands[i].var == var) return &cands[i];
    if (*nc >= MAX_DERIVED_CANDS) return NULL;
    DerivedCand *c = &cands[(*nc)++];
    c->var = var;
    c->base_param = NULL;
    c->byte_offset = 0;
    c->var_offset = false;
    c->assign_count = 0;
    c->addr_taken = false;
    return c;
}

static void restrict_derived_walk(Node *node,
                                   DerivedCand *cands, int *nc,
                                   bool *param_reassigned,
                                   Obj **rparams, int np) {
    // Walk the sibling chain iteratively — recursing into node->next while
    // also recursing into children would re-process the chain remainder once
    // per sibling, i.e. O(2^N) in statement count.  Iterate siblings and
    // recurse only into children to stay linear.
    for (; node; node = node->next) {
        // Detect &q (address-of a local pointer → mark addr_taken)
        if (node->kind == ND_ADDR && node->lhs && node->lhs->kind == ND_VAR) {
            Obj *v = node->lhs->var;
            if (v && v->is_local && !v->is_param) {
                DerivedCand *c = derived_cand_find_or_create(cands, nc, v);
                if (c) c->addr_taken = true;
            }
        }

        // Detect assignments: lhs = rhs
        if (node->kind == ND_ASSIGN && node->lhs && node->lhs->kind == ND_VAR) {
            Obj *lv = node->lhs->var;
            if (lv) {
                // Restrict param being reassigned → mark it
                for (int i = 0; i < np; i++)
                    if (rparams[i] == lv) { param_reassigned[i] = true; break; }

                // Local pointer-to-scalar candidate
                if (lv->is_local && !lv->is_param && lv->ty &&
                    lv->ty->kind == TY_PTR && lv->ty->base &&
                    is_scalar_promotion_type(lv->ty->base)) {
                    DerivedCand *c = derived_cand_find_or_create(cands, nc, lv);
                    if (c) {
                        if (c->assign_count == 0)
                            restrict_extract_base_offset(node->rhs, &c->base_param,
                                                         &c->byte_offset, &c->var_offset);
                        c->assign_count++;
                    }
                }
            }
        }

        restrict_derived_walk(node->lhs, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->rhs, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->cond, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->then, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->els, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->init, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->inc, cands, nc, param_reassigned, rparams, np);
        restrict_derived_walk(node->body, cands, nc, param_reassigned, rparams, np);
        for (Node *arg = node->args; arg; arg = arg->next)
            restrict_derived_walk(arg, cands, nc, param_reassigned, rparams, np);
    }
}

static void collect_restrict_derived_locals(VirtualMachine *vm, Obj *fn) {
    vm->compiler.restrict_derived_count = 0;
    memset(vm->compiler.restrict_derived_vars, 0,
           sizeof(vm->compiler.restrict_derived_vars));

    // Gather all restrict pointer params (at most 8 in CCCC's ABI)
    Obj *rparams[8];
    bool param_reassigned[8];
    int np = 0;
    for (Obj *p = fn->params; p && np < 8; p = p->next)
        if (p->ty && p->ty->kind == TY_PTR && p->ty->is_restrict) {
            rparams[np] = p;
            param_reassigned[np] = false;
            np++;
        }
    if (np == 0) return;

    DerivedCand cands[MAX_DERIVED_CANDS];
    int nc = 0;
    restrict_derived_walk(fn->body, cands, &nc, param_reassigned, rparams, np);

    for (int i = 0; i < nc; i++) {
        DerivedCand *c = &cands[i];
        if (c->assign_count != 1) continue;
        if (c->addr_taken) continue;
        if (!c->base_param) continue;
        // Bail if the base restrict param was reassigned anywhere in the function
        bool base_bad = false;
        for (int j = 0; j < np; j++)
            if (rparams[j] == c->base_param && param_reassigned[j]) {
                base_bad = true; break;
            }
        if (base_bad) continue;
        if (vm->compiler.restrict_derived_count >= MAX_RESTRICT_DERIVED) break;
        int idx = vm->compiler.restrict_derived_count++;
        vm->compiler.restrict_derived_vars[idx]       = c->var;
        vm->compiler.restrict_derived_params[idx]     = c->base_param;
        vm->compiler.restrict_derived_offsets[idx]    = c->byte_offset;
        vm->compiler.restrict_derived_var_offset[idx] = c->var_offset;
    }
}

// Codegen fusions below (indexed load/store fusion, restrict memcpy-loop
// lowering) elide a load/store and therefore bypass emit_load_ex/
// emit_store_ex's CHKP3/CHKT3 emission entirely. Any flag whose safety check
// rides on those opcodes must disable the fusion, or a build enabling that
// flag alone (without the others) silently loses coverage at opt_level >= 2
// (#654). The restrict-value cache (below) is the exception: rather than
// disabling it wholesale, its cache-hit path re-derives the address and
// emits the same checks itself via emit_load_safety_checks (#750).
#define CCCC_FUSION_UNSAFE_FLAGS \
    (CCCC_POINTER_CHECKS | CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK | \
     CCCC_TYPE_CHECKS)

// Forward-declared: the restrict-cache hit path (below) needs this before its
// real definition, which sits next to emit_load_ex for locality.
static void emit_load_safety_checks(VirtualMachine *vm, Type *ty, int rs_addr,
                                     bool dangling_check);

// Look up a variable in the restrict derivation map. Returns index or -1.
static int restrict_derived_find(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.restrict_derived_count; i++)
        if (vm->compiler.restrict_derived_vars[i] == var) return i;
    return -1;
}

// ---- Cache setup ----

static void prepare_restrict_cache(VirtualMachine *vm, Obj *fn, int base_stack_size) {
    vm->compiler.restrict_cache_count = 0;
    vm->compiler.restrict_cache_capacity = 0;
    memset(vm->compiler.restrict_cache_params, 0,
           sizeof(vm->compiler.restrict_cache_params));
    memset(vm->compiler.restrict_cache_offsets, 0,
           sizeof(vm->compiler.restrict_cache_offsets));
    memset(vm->compiler.restrict_cache_regs, 0,
           sizeof(vm->compiler.restrict_cache_regs));
    memset(vm->compiler.restrict_cache_save_offsets, 0,
           sizeof(vm->compiler.restrict_cache_save_offsets));
    memset(vm->compiler.restrict_cache_valid, 0,
           sizeof(vm->compiler.restrict_cache_valid));
    vm->compiler.restrict_derived_count = 0;
    memset(vm->compiler.restrict_derived_vars, 0,
           sizeof(vm->compiler.restrict_derived_vars));

    // #654 found that restrict_cache_handle_deref's cache-hit path elided
    // the load entirely, which skipped CHKP3/CHKT3 for pattern 1 (*p on a
    // restrict scalar param) with no other guard in the cache-hit path. The
    // stopgap fix disabled the cache wholesale under any safety flag.
    //
    // #750: re-enabled here, with the cache-hit path in
    // restrict_cache_handle_deref re-deriving the address and running
    // CHKP3/CHKT3 itself (via emit_load_safety_checks). This is not just
    // defence-in-depth against a hypothetical: #754 (the invalidation-gap
    // fix, see the ND_FUNCALL entry in gen_expr) closed the two known ways a
    // cache entry could go stale without the compiler's own bookkeeping
    // noticing, but a hit-site check that doesn't depend on that bookkeeping
    // being complete catches classes of bug that #654/#754 didn't anticipate
    // either. Two tests demonstrate it firing on paths the invalidation
    // fix alone does not cover:
    // tests/test_type_check_restrict_cache_hit_error.c (a store through p at
    // a different pointee type re-stamps the real heap type shadow without
    // invalidating the cache entry -- only the hit-site CHKT3 catches the
    // resulting type mismatch) and
    // tests/test_restrict_cache_argument_fill_invalidation.c's CHKP3
    // counterpart pattern (a callee mutates the pointee through an aliasing
    // non-restrict parameter -- the hit-site check still observes live
    // state even when cache bookkeeping would say "hit").
    // Throughput-wise the re-enable is a wash, not a win: measured -1.1%
    // total opcodes and flat wall-clock time in a best-case
    // straight-line-deref microbench under -3 --optimize=3 (LDR_W 10M -> 4M,
    // but the address re-derivation and checks cost roughly what the
    // eliminated load saves). It ships for the safety property, not
    // throughput -- see #750's resolution comment.
    if (vm->compiler.opt_level < 2 || (vm->flags & CCCC_ENABLE_DEBUGGER))
        return;

    // Only activate when there is at least one restrict scalar pointer param.
    bool has_restrict = false;
    for (Obj *param = fn->params; param; param = param->next) {
        if (param->ty && param->ty->kind == TY_PTR && param->ty->is_restrict &&
            param->ty->base && is_scalar_promotion_type(param->ty->base)) {
            has_restrict = true;
            break;
        }
    }
    if (!has_restrict)
        return;

    collect_restrict_derived_locals(vm, fn);

    // Pre-reserve all MAX_RESTRICT_CACHE slots (S4-S7 and their stack save slots)
    // so lazy binding can claim any slot without growing the frame mid-function.
    // Cache entries are bound on first deref access (count starts at 0).
    static const int rcregs[] = {REG_S4, REG_S5, REG_S6, REG_S7};
    for (int q = 0; q < MAX_RESTRICT_CACHE; q++) {
        vm->compiler.restrict_cache_regs[q] = rcregs[q];
        // Save slots are placed after both integer and FP promoted-local save slots.
        vm->compiler.restrict_cache_save_offsets[q] =
            -(base_stack_size + vm->compiler.promoted_count
              + vm->compiler.fp_promoted_count + q + 1);
    }
    vm->compiler.restrict_cache_capacity = MAX_RESTRICT_CACHE;
}

static int restrict_cache_find(VirtualMachine *vm, Obj *param, long byte_offset) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        if (vm->compiler.restrict_cache_params[i] == param &&
            vm->compiler.restrict_cache_offsets[i] == byte_offset)
            return i;
    return -1;
}

// Invalidate all restrict cache entries (called at control-flow join points).
static void restrict_cache_invalidate_all(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        vm->compiler.restrict_cache_valid[i] = false;
}

// Invalidate all cache entries for a restrict param (all cached offsets for it).
static void restrict_cache_invalidate_param(VirtualMachine *vm, Obj *param) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        if (vm->compiler.restrict_cache_params[i] == param)
            vm->compiler.restrict_cache_valid[i] = false;
}

// Bind next free cache slot to (param, byte_offset). Returns index or -1 if full.
static int restrict_cache_alloc(VirtualMachine *vm, Obj *param, long byte_offset) {
    if (vm->compiler.restrict_cache_count >= MAX_RESTRICT_CACHE)
        return -1;
    int idx = vm->compiler.restrict_cache_count++;
    vm->compiler.restrict_cache_params[idx] = param;
    vm->compiler.restrict_cache_offsets[idx] = byte_offset;
    vm->compiler.restrict_cache_valid[idx] = false;
    return idx;
}

// Extract (restrict_param, byte_offset) from a ND_DEREF node.
// Handles *p (offset 0) and p[const] (constant element index).
// Also handles *q and q[const] where q is a derived local (see #269).
// Returns true and sets out_param/out_byte_offset on success.
static bool restrict_const_deref_extract(VirtualMachine *vm, Node *node,
                                         Obj **out_param, long *out_byte_offset) {
    if (!node || node->kind != ND_DEREF || !node->lhs)
        return false;
    Node *addr = node->lhs;
    while (addr && addr->kind == ND_CAST)
        addr = addr->lhs;
    if (!addr)
        return false;

    // Pattern 1: *p or *q where q is a derived local
    if (addr->kind == ND_VAR) {
        Obj *var = addr->var;
        if (var && var->is_param && var->ty && var->ty->kind == TY_PTR &&
            var->ty->is_restrict && var->ty->base &&
            is_scalar_promotion_type(var->ty->base)) {
            *out_param = var;
            *out_byte_offset = 0;
            return true;
        }
        // Derived local: *q where q = p + const
        int di = restrict_derived_find(vm, var);
        if (di >= 0 && !vm->compiler.restrict_derived_var_offset[di]) {
            *out_param = vm->compiler.restrict_derived_params[di];
            *out_byte_offset = vm->compiler.restrict_derived_offsets[di];
            return true;
        }
        return false;
    }

    // Pattern 2: p[const] → *(p + k*elem_size) or *(p + byte_off)
    // Also handles q[const] where q is a derived local.
    // Requires opt_level >= 2. Unlike match_indexed_addr (indexed load/store
    // fusion, which elides the check with no recourse), this function only
    // feeds the restrict cache, whose deref-side caller re-derives the
    // address and runs CHKP3/CHKT3 itself on a cache hit (#750); the
    // store-side caller (restrict_cache_handle_store) writes through to the
    // real store, which already goes through emit_store_ex's checks. So no
    // CCCC_FUSION_UNSAFE_FLAGS gate is needed here.
    if (addr->kind != ND_ADD || vm->compiler.opt_level < 2)
        return false;

    // Try both orderings: (ptr + const) and (const + ptr)
    Node *sides[2][2] = {{addr->lhs, addr->rhs}, {addr->rhs, addr->lhs}};
    for (int s = 0; s < 2; s++) {
        Node *ptr_node = sides[s][0];
        Node *off_node = sides[s][1];
        while (ptr_node && ptr_node->kind == ND_CAST) ptr_node = ptr_node->lhs;
        while (off_node && off_node->kind == ND_CAST) off_node = off_node->lhs;
        if (!ptr_node || !off_node || ptr_node->kind != ND_VAR)
            continue;
        Obj *var = ptr_node->var;

        // Resolve: is var a restrict param or a derived local?
        Obj *base_param = NULL;
        long base_off = 0;
        if (var && var->is_param && var->ty && var->ty->kind == TY_PTR &&
            var->ty->is_restrict && var->ty->base &&
            is_scalar_promotion_type(var->ty->base)) {
            base_param = var;
            base_off = 0;
        } else {
            int di = restrict_derived_find(vm, var);
            if (di < 0 || vm->compiler.restrict_derived_var_offset[di]) continue;
            base_param = vm->compiler.restrict_derived_params[di];
            base_off = vm->compiler.restrict_derived_offsets[di];
        }

        // Evaluate the constant element offset
        long elem_off = 0;
        if (!eval_const_byte_offset(off_node, &elem_off))
            continue;
        *out_param = base_param;
        *out_byte_offset = base_off + elem_off;
        return true;
    }
    return false;
}

// Walk a pointer expression to find its restrict-param root (for indexed stores).
// Also resolves derived locals (q = p + k maps q → p).
// Returns the restrict param Obj if found, NULL otherwise.
static Obj *restrict_root_param_of_ptr(VirtualMachine *vm, Node *ptr) {
    while (ptr) {
        if (ptr->kind == ND_CAST) { ptr = ptr->lhs; continue; }
        if (ptr->kind == ND_VAR) {
            Obj *var = ptr->var;
            if (var && var->is_param && var->ty && var->ty->kind == TY_PTR &&
                var->ty->is_restrict)
                return var;
            // Derived local: *q where q = p + k → root is p
            int di = restrict_derived_find(vm, var);
            if (di >= 0) return vm->compiler.restrict_derived_params[di];
            return NULL;
        }
        if (ptr->kind == ND_ADD || ptr->kind == ND_SUB) {
            Obj *r = restrict_root_param_of_ptr(vm, ptr->lhs);
            if (r) return r;
            return restrict_root_param_of_ptr(vm, ptr->rhs);
        }
        return NULL;
    }
    return NULL;
}

// Called on ND_DEREF to check/populate the restrict cache.
// Handles *restrict_param and restrict_param[const] patterns.
// Returns true and emits a register copy or a load+cache-fill on hit/miss.
static bool restrict_cache_handle_deref(VirtualMachine *vm, Node *node,
                                        int dest_reg) {
    if (vm->compiler.restrict_cache_capacity == 0)
        return false;

    Obj *param;
    long byte_off;
    if (!restrict_const_deref_extract(vm, node, &param, &byte_off))
        return false;

    int idx = restrict_cache_find(vm, param, byte_off);
    if (idx < 0) {
        idx = restrict_cache_alloc(vm, param, byte_off);
        if (idx < 0)
            return false; // all slots bound to other (param,offset) pairs
    }

    int cache_reg = vm->compiler.restrict_cache_regs[idx];

    if (vm->compiler.restrict_cache_valid[idx]) {
        // Cache hit: the value is already in cache_reg, so no load is emitted
        // and emit_load_ex's checks never run for this access. Under any
        // CCCC_FUSION_UNSAFE_FLAGS flag, re-derive the address purely to run
        // those checks against it (#750) -- emitted unconditionally, even
        // when dest_reg == REG_ZERO (a discarded-value deref like `*p;` must
        // still trap on a dangling/mistyped access).
        if (vm->flags & CCCC_FUSION_UNSAFE_FLAGS) {
            int r_addr = alloc_temp_reg();
            gen_expr(vm, node->lhs, r_addr);
            emit_load_safety_checks(vm, node->ty, r_addr, true);
            free_temp_reg(r_addr);
        }
        // Use the S-reg directly for the value itself.
        if (dest_reg != REG_ZERO && dest_reg != cache_reg)
            emit_mov3(vm, dest_reg, cache_reg);
        return true;
    }

    // Cache miss: load the value at (param + byte_off) into cache_reg, mark valid
    int r_addr = alloc_temp_reg();
    gen_expr(vm, node->lhs, r_addr); // evaluates the full address (p or p+offset)
    emit_load(vm, node->ty, cache_reg, r_addr);
    free_temp_reg(r_addr);
    vm->compiler.restrict_cache_valid[idx] = true;

    if (dest_reg != REG_ZERO && dest_reg != cache_reg)
        emit_mov3(vm, dest_reg, cache_reg);
    return true;
}

// The write-through in restrict_cache_handle_store() below stamps the whole
// cached slot with val_reg, normalized to the *param's* declared pointee
// type (param->ty->base -- the type every cache entry is filled and
// normalized against, see restrict_cache_handle_deref() and the
// emit_normalize_promoted_scalar() call below). That is only correct when
// the store itself covers exactly the bytes the entry tracks: a narrower
// store (`*(char *)p = c` through an `int *restrict p`) leaves the other
// bytes of the int intact in real memory, but write-through would splat the
// zero/sign-extended byte over all four (#757). A wider store through a
// smaller-typed restrict pointer would conversely spill into bytes owned by
// a different (param, offset) cache entry. Neither case is expressible as a
// single register copy, so require an exact type match between the store's
// own (possibly cast) pointee type and the param's declared pointee type,
// and invalidate the whole param instead of writing through on a mismatch.
static bool restrict_cache_store_type_matches(Type *store_ty, Type *cached_ty) {
    if (!store_ty || !cached_ty)
        return false;
    return store_ty->size == cached_ty->size &&
           is_flonum(store_ty) == is_flonum(cached_ty) &&
           store_ty->is_unsigned == cached_ty->is_unsigned;
}

// Called after a store through a pointer expression.
// If the store goes through a restrict param, update or invalidate cache entries.
// If the store goes through a non-restrict pointer, do nothing (restrict contract).
static void restrict_cache_handle_store(VirtualMachine *vm, Node *lhs, int val_reg) {
    if (vm->compiler.restrict_cache_capacity == 0)
        return;
    if (!lhs || lhs->kind != ND_DEREF || !lhs->lhs)
        return;

    // *p = val or p[const] = val: write-through the specific (param, offset) entry.
    Obj *param;
    long byte_off;
    if (restrict_const_deref_extract(vm, lhs, &param, &byte_off)) {
        if (!restrict_cache_store_type_matches(lhs->ty, param->ty->base)) {
            // Type-punned store through this restrict param (narrowing,
            // widening, or int/float mismatch, #757): the cached slot(s) no
            // longer reflect real memory. Invalidate every entry for this
            // param rather than just this (param, offset) slot -- a wider
            // store at this offset can corrupt bytes tracked by a
            // *different* entry too.
            restrict_cache_invalidate_param(vm, param);
            return;
        }
        int idx = restrict_cache_find(vm, param, byte_off);
        if (idx >= 0) {
            int cache_reg = vm->compiler.restrict_cache_regs[idx];
            if (cache_reg != val_reg)
                emit_mov3(vm, cache_reg, val_reg);
            // Truncate to pointee width (e.g. char *restrict: cache holds byte).
            emit_normalize_promoted_scalar(vm, param->ty->base, cache_reg);
            vm->compiler.restrict_cache_valid[idx] = true;
        }
        return;
    }

    // p[var] or derived pointer store: find the restrict param root and invalidate
    // all of its cached offsets (we don't know which byte_offset was hit).
    Obj *base = restrict_root_param_of_ptr(vm, lhs->lhs);
    if (base) {
        restrict_cache_invalidate_param(vm, base);
        return;
    }
    // Unknown base (e.g. int *r = p; *r = x): conservatively invalidate everything.
    restrict_cache_invalidate_all(vm);
}

static void emit_save_restrict_cache_regs(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        emit_local_store(vm, ty_long, vm->compiler.restrict_cache_regs[i],
                         vm->compiler.restrict_cache_save_offsets[i]);
}

static void emit_restore_restrict_cache_regs(VirtualMachine *vm) {
    for (int i = vm->compiler.restrict_cache_count - 1; i >= 0; i--)
        emit_local_load(vm, ty_long, vm->compiler.restrict_cache_regs[i],
                        vm->compiler.restrict_cache_save_offsets[i]);
}

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

static bool contains_funcall(Node *node) {
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

static bool contains_self_call(Node *node, Obj *fn) {
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
static bool cast_is_repr_noop(Type *to, Type *from) {
    long long k1 = return_repr_key(to);
    long long k2 = return_repr_key(from);
    return k1 >= 0 && k1 == k2;
}

// Return true when `expr` is a tail-call candidate: a direct, in-VM,
// non-variadic, non-nested, non-noreturn, non-struct-returning call with ≤8 args.
// The caller is responsible for the opt_level >= 1 and inline_exit_name guards.
static bool can_emit_tail_call(VirtualMachine *vm, Node *expr) {
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
static int count_ast_nodes(Node *node) {
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
static bool contains_unsupported_control_flow(Node *node) {
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
static void replace_locals_in_ast(Node *node, Obj **orig, Obj **map, int count) {
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
static int var_stack_slots(Obj *var) {
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
    n->goto_next = NULL;
    n->case_next = NULL;
    n->default_case = NULL;
    n->init_tail = NULL;
    return n;
}

static Node *clone_subst(VirtualMachine *vm, Node *src, Obj *params, Node *args) {
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

static void reset_labels(void) {
    num_label_defs = 0;
    num_label_patches = 0;
}

// Define a label at the current position
static void define_label(VirtualMachine *vm, char *name) {
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
static void add_label_patch(char *name, Pc patch_location,
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
static void patch_labels(VirtualMachine *vm) {
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

// ========== Emit Helpers ==========

static void emit_word(VirtualMachine *vm, InstrWord word) {
    if (!vm || !vm->text_seg)
        error("codegen: text segment not initialized");
    if (vm->text_ptr + 1 >= (Pc)(vm->text_committed / sizeof(InstrWord))) {
        if (vm_text_ensure_count(vm, vm->text_ptr + 2) != 0)
            error("codegen: text segment overflow (limit: %d instructions)",
                  vm->poolsize_max);
    }
    vm->text_seg[++vm->text_ptr] = word;
}

static Pc emit_word_ptr(VirtualMachine *vm) {
    if (!vm || !vm->text_seg)
        error("codegen: text segment not initialized");
    if (vm->text_ptr + 1 >= (Pc)(vm->text_committed / sizeof(InstrWord))) {
        if (vm_text_ensure_count(vm, vm->text_ptr + 2) != 0)
            error("codegen: text segment overflow (limit: %d instructions)",
                  vm->poolsize_max);
    }
    return ++vm->text_ptr;
}

static Pc emit_i64(VirtualMachine *vm, long long val) {
    Pc loc = emit_word_ptr(vm);
    vm->text_seg[loc] = cc_i64_lo(val);
    emit_word(vm, cc_i64_hi(val));
    return loc;
}

static void check_data_capacity(VirtualMachine *vm, long long needed) {
    if (vm_data_ensure(vm, needed) != 0)
        error("codegen: data segment overflow (limit: %d bytes)", vm->poolsize_max);
}

// Grow tls_template to hold at least `needed` bytes.
static void check_tls_capacity(VirtualMachine *vm, size_t needed) {
    if (needed <= vm->tls_template_cap)
        return;
    size_t new_cap = vm->tls_template_cap ? vm->tls_template_cap * 2 : 256;
    while (new_cap < needed)
        new_cap *= 2;
    char *p = realloc(vm->tls_template, new_cap);
    if (!p)
        error("codegen: TLS template allocation failed");
    if (new_cap > vm->tls_template_cap)
        memset(p + vm->tls_template_cap, 0, new_cap - vm->tls_template_cap);
    vm->tls_template = p;
    vm->tls_template_cap = new_cap;
}

static void emit(VirtualMachine *vm, int instruction) {
    emit_word(vm, instruction);
}

static void emit_with_arg(VirtualMachine *vm, int instruction, long long arg) {
    emit_word(vm, instruction);
    emit_i64(vm, arg);
}

// 3-register ops: [OP] [rd:8|rs1:8|rs2:8|unused:40]
static void emit_rrr(VirtualMachine *vm, int op, int rd, int rs1, int rs2) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRR(rd, rs1, rs2));
}

// 2-register ops: [OP] [rd:8|rs1:8|unused:48]
static void emit_rr(VirtualMachine *vm, int op, int rd, int rs1) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RR(rd, rs1));
}

// 3-register + 8-bit "scale" ops: [OP] [rd:8|rs1:8|rs2:8|scale:8|unused:32].
// Used by the wide-vector opcodes (#722) to carry a runtime lane count/byte
// width alongside up to 3 register operands -- see the SIMD opcode block in
// cccc.h for which family uses which field.
static void emit_rrrs(VirtualMachine *vm, int op, int rd, int rs1, int rs2, int scale) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRS(rd, rs1, rs2, scale));
}

// 2-register + 8-bit "scale" ops: rs2 is unused (0) -- the vector ops that
// only take one source register (VLDR/VSTR/VSPLAT/VNEG/VNOT/VCVT) still need
// the scale byte, so they reuse the RRRS encoding with rs2 left unread by
// the VM handler (mirrors how VEXTRACT_*/VINSERT_* already ignore rs2).
static void emit_rrs(VirtualMachine *vm, int op, int rd, int rs1, int scale) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRS(rd, rs1, 0, scale));
}

// 1-register + immediate: [OP] [rd:8|unused:24] [imm:64]
static Pc emit_ri(VirtualMachine *vm, int op, int rd, long long imm) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_R(rd));
    return emit_i64(vm, imm);
}

// Register + register + immediate: [OP] [rd:8|rs:8|unused:16] [imm:64]
static Pc emit_rri(VirtualMachine *vm, int op, int rd, int rs, long long imm) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RR(rd, rs));
    return emit_i64(vm, imm);
}

static Pc emit_rrrs_i(VirtualMachine *vm, int op, int rd, int base, int index,
                          int scale, long long offset) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRRS(rd, base, index, scale));
    return emit_i64(vm, offset);
}

// Float 3-register ops
static void emit_frrr(VirtualMachine *vm, int op, int rd, int rs1, int rs2) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RRR(rd, rs1, rs2));
}

// Float 2-register ops
static void emit_frr(VirtualMachine *vm, int op, int rd, int rs1) {
    emit_word(vm, op);
    emit_word(vm, ENCODE_RR(rd, rs1));
}

// ========== Specific Emit Helpers ==========

// LI3: rd = immediate
static Pc emit_li3(VirtualMachine *vm, int rd, long long imm) {
    return emit_ri(vm, LI3, rd, imm);
}

static Pc emit_lda3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LDA3, rd, offset);
}

static Pc emit_ldtls3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LDTLS3, rd, offset);
}

static Pc emit_lta3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LTA3, rd, offset);
}

// LEA3: rd = bp + offset. `skip_record` sets LEA3_NO_RECORD (#676), telling
// op_LEA3_fn to skip its vm->stack_ptr_epochs write for this address -- pass
// true only when the result is proven never to escape its creating frame
// (see docs/SAFETY.md and the mark_addr_escapes pass in parse.c).
static Pc emit_lea3_ex(VirtualMachine *vm, int rd, long long offset, bool skip_record) {
    emit_word(vm, LEA3);
    emit_word(vm, ENCODE_R(rd) | (skip_record ? LEA3_NO_RECORD : 0));
    return emit_i64(vm, offset);
}

// LEA3: rd = bp + offset. Default: recorded (safe) -- see emit_lea3_ex.
static Pc emit_lea3(VirtualMachine *vm, int rd, long long offset) {
    return emit_lea3_ex(vm, rd, offset, false);
}

// STKTAG: tag [bp+offset, bp+offset+size) with the current frame's epoch,
// for interior dangling-pointer resolution (#675). See docs/SAFETY.md.
static Pc emit_stktag(VirtualMachine *vm, long long offset, long long size) {
    emit_word(vm, STKTAG);
    emit_word(vm, 0); // unused (no register operand)
    emit_i64(vm, offset);
    return emit_i64(vm, size);
}

// LEA3 for a local Obj's own base address, e.g. `&var`/array-or-struct
// base materialization -- skips recording iff #676's escape analysis
// proved `var`'s address never escapes its creating frame. When `var` is
// an escaping array/struct/union, also emits STKTAG (#675) so an interior
// pointer derived from this base at a runtime offset (e.g. &arr[i] for
// non-constant i, which never itself passes through LEA3 as a single
// recorded address) can still be resolved back to this base's epoch at
// CHKP3 time. Scalars need no interior resolution -- their one address is
// already covered exactly by stack_ptr_epochs.
//
// Also records, on vm->compiler, whether the *current function* needs its
// own frame epoch pushed (#703): STKTAG for an escaping aggregate, or a
// recorded LEA3 for an escaping scalar. gen_function patches these into the
// ENT3 masks word once the body is done. Deliberately keyed off what this
// function actually emits (params included, via the same emit_lea3_var
// path) rather than a fn->locals-only pre-scan, which would miss an
// escaping aggregate *parameter*.
static Pc emit_lea3_var(VirtualMachine *vm, int rd, Obj *var) {
    bool escaping_agg = var->addr_escapes &&
        (var->ty->kind == TY_ARRAY || var->ty->kind == TY_STRUCT ||
         var->ty->kind == TY_UNION);
    // A TY_VECTOR local is always STKTAG'd, regardless of what escape
    // analysis (mark_addr_escapes, #676) proved (#727). Structs/arrays only
    // need STKTAG when addr_escapes is set because a *non*-escaping
    // struct/array is never read back through its own address -- member
    // access on a value that can't escape has no reason to re-derive an
    // lvalue. Vectors break that assumption: element access (`v[i]`) always
    // lowers through gen_addr()+VLDR (this file's #714/#722 comment: "a
    // vector local ... lives in a memory slot ... exactly like a small
    // struct"), even for a vector proven never to leave its frame. Without
    // STKTAG, only the LEA3-recorded base offset is coverable at all, and
    // for a non-escaping vector even the base isn't recorded (skip_record is
    // still true below, matching struct/array's own policy) -- so *every*
    // lane read of a non-escaping vector falls to stack_interval_stab.
    // STKTAG-ing the whole vector extent with the current (live) frame's
    // epoch on every access ensures stack_interval_stab's prefer-live
    // resolution always has this vector's own live range to prefer over any
    // dead sibling frame's STKTAG range that happens to physically overlap
    // it (e.g. a prior variadic call's own dead va_list).
    //
    // Deliberately does NOT also force skip_record=false (exact
    // stack_ptr_epochs recording) for a non-escaping vector: exact-recording
    // the base would tag that one absolute address with this frame's epoch,
    // and once this frame returns that tag goes stale -- unlike the STKTAG
    // interval, a stale *exact* tag has no prefer-live protection (layer 2
    // is a single last-write-wins hashmap slot, not a set of overlapping
    // candidates), so an unrelated dereference by any later sibling frame
    // that happens to reuse the same physical address (e.g. a stack-spilled
    // variadic argument slot, which is address-passed rather than declared
    // as a var) would spuriously collide with it. The STKTAG interval alone
    // -- which layer 3 resolves soundly via prefer-live -- is enough to
    // cover every offset of this vector, including offset 0.
    bool vector_agg = var->ty->kind == TY_VECTOR;
    Pc pc = emit_lea3_ex(vm, rd, var->offset, !var->addr_escapes);
    if (escaping_agg || vector_agg) {
        emit_stktag(vm, var->offset, var->ty->size);
        vm->compiler.frame_has_esc_agg = true;
    } else if (var->addr_escapes) {
        vm->compiler.frame_has_esc_scalar = true;
    }
    return pc;
}

// LEA3 for compiler-internal bookkeeping addresses (static links, block
// descriptors, closure captures, memcpy/cleanup scratch) that never
// correspond to a user-visible `&local` and are proven, by construction,
// never to escape their creating frame -- always skip recording (#676).
static Pc emit_lea3_internal(VirtualMachine *vm, int rd, long long offset) {
    return emit_lea3_ex(vm, rd, offset, true);
}

// ADDI3: rd = rs + immediate
static Pc emit_addi3(VirtualMachine *vm, int rd, int rs, long long imm) {
    return emit_rri(vm, ADDI3, rd, rs, imm);
}

// MOV3: rd = rs
static void emit_mov3(VirtualMachine *vm, int rd, int rs) {
    emit_rrr(vm, MOV3, rd, rs, 0);
}

static void emit_fmov3(VirtualMachine *vm, int rd, int rs) {
    emit_frr(vm, FMOV3, rd, rs);
}

static void emit_fround_f32(VirtualMachine *vm, int rd, int rs) {
    emit_frr(vm, FROUND_F32, rd, rs);
}

static int fop_for_type(Type *ty, int f64_op) {
    if (!ty || ty->kind != TY_FLOAT)
        return f64_op;
    switch (f64_op) {
    case FADD3: return FADD3_F32;
    case FSUB3: return FSUB3_F32;
    case FMUL3: return FMUL3_F32;
    case FDIV3: return FDIV3_F32;
    case FNEG3: return FNEG3_F32;
    case FEQ3: return FEQ3_F32;
    case FNE3: return FNE3_F32;
    case FLT3: return FLT3_F32;
    case FLE3: return FLE3_F32;
    case FGT3: return FGT3_F32;
    case FGE3: return FGE3_F32;
    case I2F3: return I2F3_F32;
    case F2I3: return F2I3_F32;
    case U2F3: return U2F3_F32;
    case F2U3: return F2U3_F32;
    case FR2R: return FR2R_F32;
    case R2FR: return R2FR_F32;
    default: return f64_op;
    }
}

// #780: an unsigned 64-bit integer type needs the dedicated U2F3/F2U3
// opcode pair, not I2F3/F2I3 -- the latter treat the register as a signed
// 64-bit value, which is wrong for both directions once the value's high
// bit is set (float->int saturates against the wrong range; int->float
// reads a negative value). Narrower unsigned types are unaffected: they're
// already zero-extended in the register (so I2F3 is correct) and F2I3's
// signed saturation covers their full range (so a same-width ZX/SX after
// F2I3 is correct too) -- only size-8 needs the new opcodes.
static bool is_u64_int(Type *ty) {
    return ty && is_integer(ty) && ty->is_unsigned && ty->size == 8;
}

// FREG_A0..A7 alias REG_A0..A7 by raw register number (regs[] and fregs[] are
// separate storage, but share index numbers). Many gen_expr paths legitimately
// reuse their destination register *number* as an integer scratch while
// producing a float result -- deref/member address computation, int->float
// cast source, ternary condition -- so evaluating a float call argument
// directly into FREG_A0+i would let that integer scratch clobber a
// live REG_A0+i already holding a marshalled argument (e.g. printf's format
// pointer). Evaluate into a caller-saved temp-numbered float register instead:
// its aliased integer slot is a free temp, never a live argument register.
// Caller moves the result out (FR2R/emit_fmov3) and frees the temp. (#712)
static int gen_flonum_arg_to_scratch(VirtualMachine *vm, Node *arg) {
    int r = alloc_temp_reg();
    gen_expr(vm, arg, r);
    return r;
}

// Load operations based on type
static void emit_bitint_trunc(VirtualMachine *vm, Type *ty, int reg);
static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);

static bool is_zero_size_aggregate(Type *ty) {
    return ty && ty->size == 0 &&
           (ty->kind == TY_STRUCT || ty->kind == TY_UNION);
}

// True for _BitInt(N) with N > 64 — multi-word, address-based storage.
static inline bool is_wide_bitint(Type *ty) {
    return ty && ty->kind == TY_BITINT && ty->bit_width > 64;
}

// Emit CALLF for a named wide-bitint helper with `nargs` integer args already
// loaded into REG_A0..REG_A{nargs-1}.  Returns false if function not found.
static bool emit_wide_helper(VirtualMachine *vm, const char *name, int nargs) {
    int ffi_idx = find_ffi_function(vm, name);
    if (ffi_idx < 0) {
        // Should never happen if wide_bitint.c is compiled in.
        error("wide _BitInt runtime helper '%s' not registered", name);
        return false;
    }
    emit(vm, CALLF);
    emit_word(vm, ffi_idx);
    emit_word(vm, nargs);
    emit_i64(vm, 0); // double_arg_mask
    emit_i64(vm, 0); // float_arg_mask
    restrict_cache_invalidate_all(vm);
    reset_temp_regs();
    return true;
}

// Emit a dedicated WIDE_* opcode (args already loaded into REG_A0..A5) for
// the hot wide-bitint ops (#456) — same args/clobber contract as
// emit_wide_helper's CALLF, just without the FFI marshalling overhead.
static void emit_wide_op(VirtualMachine *vm, int op) {
    emit(vm, op);
    restrict_cache_invalidate_all(vm);
}

// Allocate a fresh stack slot for a wide _BitInt intermediate result.
// Returns the bp-relative offset (negative).  words = number of 64-bit words.
// Wide _BitInt storage is written via a raw pointer (CALLF helper or a
// WIDE_* opcode), never through STR_LOCAL, so no MARKI/MARKW liveness mark
// is emitted for it. This is intentional: wide _BitInt is address-based like
// structs/unions, so the ND_VAR read-side instrumentation guard excludes it
// (mirroring TY_STRUCT/UNION), keeping both sides symmetric. Fixed in #457.
static long long alloc_wide_bitint_temp(VirtualMachine *vm, int words) {
    vm->compiler.ent3_extra_stack += words;
    return -(long long)(vm->compiler.ent3_base_stack + vm->compiler.ent3_extra_stack);
}

// Allocate a fresh stack slot for a _Decimal32/64/128 intermediate result
// (#402). Reuses alloc_wide_bitint_temp's per-function scratch pool,
// rounding up to whole 64-bit words (4 bytes for _Decimal32 still costs a
// full word -- same tradeoff wide _BitInt already makes for narrow widths).
// Same address-based rationale applies: written via DADD/DSUB/... or the
// BID shim, never through STR_LOCAL, so no MARKI/MARKW mark is emitted and
// the ND_VAR read-side uninit guard must exclude decimal (see is_decimal
// checks alongside is_wide_bitint below).
static long long alloc_decimal_temp(VirtualMachine *vm, int bytes) {
    return alloc_wide_bitint_temp(vm, (bytes + 7) / 8);
}

// Materialize a vector-typed argument into a fresh frame scratch slot sized
// to the argument's own width and load its address into addr_reg, ready to
// pass like a struct-by-value arg (#714). Unlike a struct arg -- whose
// address is the caller's own addressable storage -- a vector expression is
// a value living in a vregs[] register (gen_vector_expr), so it must be
// copied out to memory before its address can be handed to the callee.
// Reuses alloc_wide_bitint_temp's per-function scratch allocator (same ENT3
// extra-stack pool as wide _BitInt temporaries), sized to arg->ty->size/8
// words -- 2/4/8 words for the 16/32/64-byte vectors #722 supports.
static void gen_vector_arg_ptr(VirtualMachine *vm, Node *arg, int addr_reg) {
    int v = alloc_temp_reg();
    gen_expr(vm, arg, v); // vector value -> vregs[v]
    mark_temp_reg_used(v);
    int bytes = arg->ty->size;
    long long off = alloc_wide_bitint_temp(vm, bytes / 8);
    emit_lea3(vm, addr_reg, off); // address escapes to the callee -- record it
    if (vm->flags & CCCC_POINTER_CHECKS)
        emit_rr(vm, CHKP3, addr_reg, 0);
    emit_rrs(vm, VSTR, v, addr_reg, bytes);
    free_temp_reg(v);
}

// Pass a _Decimal32/64/128 variadic argument by pointer to a caller-frame
// scratch copy, mirroring gen_vector_arg_ptr just above (#714/#721) --
// exactly one 8-byte slot regardless of the value's 4/8/16 byte width, which
// <stdarg.h>'s va_arg dereferences via __builtin_classify_type. A FIXED
// decimal param doesn't need this: gen_expr already yields the address of
// the source value directly (decimal is address-based throughout codegen --
// see alloc_decimal_temp's comment), and the callee only ever reads back
// exactly the declared width. A variadic reader, though, picks its load
// width from the %Hf/%Df/%DDf modifier in a printf-style format string,
// which the compiler cannot generally correlate with the argument's actual
// width (validate_format_call only catches a literal-format-string mismatch,
// and only warns). So this always allocates and copies the full 16 bytes,
// independent of arg->ty->size: a width mismatch then reads uninitialized-
// but-in-bounds bytes from a fully-sized scratch object, never past it.
static void gen_decimal_arg_ptr(VirtualMachine *vm, Node *arg, int addr_reg) {
    int r_src = alloc_temp_reg();
    gen_expr(vm, arg, r_src); // decimal value -> address of source
    mark_temp_reg_used(r_src);
    long long off = alloc_decimal_temp(vm, 16); // always full width, not arg->ty->size
    emit_lea3(vm, addr_reg, off); // address escapes to the callee -- record it
    if (vm->flags & CCCC_POINTER_CHECKS) {
        emit_rr(vm, CHKP3, addr_reg, 0);
        emit_rr(vm, CHKP3, r_src, 0);
    }
    // Raw word-at-a-time copy of arg->ty->size bytes (4, 8, or 16), NOT
    // MCPY: MCPY hard-codes REG_A0/A1/A2, which this function runs
    // interleaved with the call's own argument-register evaluation loop and
    // would clobber earlier-placed argument registers.
    int r_tmp = alloc_temp_reg();
    int r_s = alloc_temp_reg();
    int r_d = alloc_temp_reg();
    emit_mov3(vm, r_s, r_src);
    emit_mov3(vm, r_d, addr_reg);
    if (arg->ty->size == 4) {
        emit_rr(vm, LDR_W, r_tmp, r_s);
        emit_rr(vm, STR_W, r_tmp, r_d);
    } else {
        int words = arg->ty->size / 8;
        for (int i = 0; i < words; i++) {
            emit_rr(vm, LDR_D, r_tmp, r_s);
            emit_rr(vm, STR_D, r_tmp, r_d);
            if (i + 1 < words) {
                emit_addi3(vm, r_s, r_s, 8);
                emit_addi3(vm, r_d, r_d, 8);
            }
        }
    }
    free_temp_reg(r_d);
    free_temp_reg(r_s);
    free_temp_reg(r_tmp);
    free_temp_reg(r_src);
}

static void gen_zero_size_arg(VirtualMachine *vm, Node *arg, int dest_reg) {
    gen_expr(vm, arg, REG_ZERO);
    emit_li3(vm, dest_reg, 0);
}

// Unary negate (-x) or bitwise-complement (~x) of a wide _BitInt, via its
// runtime helper(dst, a, words, width). The operand is address-based (like
// the binary wide ops), so gen_expr yields the source address; the result is
// written into a fresh stack temp whose address is returned in dest_reg.
static void gen_wide_bitint_unary(VirtualMachine *vm, Node *node, int dest_reg,
                                  const char *helper) {
    Type *ty = node->ty;
    int words = ty->size / 8;
    int r_src = alloc_temp_reg();
    gen_expr(vm, node->lhs, r_src); // wide operand → address
    long long dst_offset = node->ret_buffer
                               ? (long long)node->ret_buffer->offset
                               : alloc_wide_bitint_temp(vm, words);
    emit_lea3(vm, REG_A0, dst_offset);
    emit_mov3(vm, REG_A1, r_src);
    emit_li3(vm, REG_A2, words);
    emit_li3(vm, REG_A3, ty->bit_width);
    if (vm->flags & CCCC_POINTER_CHECKS) {
        emit_rr(vm, CHKP3, REG_A0, 0);
        emit_rr(vm, CHKP3, REG_A1, 0);
    }
    emit_wide_helper(vm, helper, 4);
    emit_lea3(vm, dest_reg, dst_offset);
    free_temp_reg(r_src);
}

// Generate `node` as a scalar 0/1 truth value in dest_reg for boolean contexts
// (if/while/for/?: conditions, &&, ||, casts to _Bool). For ordinary scalars
// the value the branch ops test is already correct; wide _BitInt operands are
// address-based, so OR-reduce their words to a single 0/1 via the runtime.
// _Decimal32/64/128 (#402) is likewise address-based -- test != 0 via DCMP
// against a zero literal of the same width (C truthiness: nonzero is true,
// including -0, which DCMP's quiet_equal already treats as == 0).
static void gen_cond_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    gen_expr(vm, node, dest_reg);
    if (is_wide_bitint(node->ty)) {
        emit_mov3(vm, REG_A0, dest_reg); // address of the wide value
        emit_li3(vm, REG_A1, node->ty->size / 8);
        emit_wide_helper(vm, "__cccc_bitint_nonzero", 2);
        emit_mov3(vm, dest_reg, REG_A0);
    } else if (is_decimal(node->ty)) {
        int w = dec_width_code(node->ty);
        unsigned char zero_bits[16] = {0};
        long long offset = vm->data_ptr - vm->data_seg;
        offset = (offset + (node->ty->align - 1)) & ~(long long)(node->ty->align - 1);
        vm->data_ptr = vm->data_seg + offset;
        check_data_capacity(vm, offset + node->ty->size);
        if (!cccc_dec_encode_literal("0", w, zero_bits))
            error_tok(vm, node->tok,
                      "_Decimal requires a build with CCCC_HAS_DECIMAL=1");
        memcpy(vm->data_ptr, zero_bits, (size_t)node->ty->size);
        vm->data_ptr += node->ty->size;

        int r_zero = alloc_temp_reg();
        emit_lda3(vm, r_zero, offset);
        emit_mov3(vm, REG_A0, dest_reg);
        emit_mov3(vm, REG_A1, r_zero);
        emit_li3(vm, REG_A2, w);
        emit_wide_op(vm, DCMP); // A0 = 0=EQ/1=LT/2=GT/3=UNORDERED
        int tmp = alloc_temp_reg();
        emit_li3(vm, tmp, 0);
        emit_rrr(vm, SNE3, dest_reg, REG_A0, tmp); // nonzero iff code != EQ(0)
        free_temp_reg(tmp);
        free_temp_reg(r_zero);
    }
}

// dangling_check gates only the CHKP3 (pointer/dangling) emission -- CHKT3
// (type checks, an orthogonal feature) is untouched. Pass false only when the
// address is known at compile time to be a bp-relative local-frame address
// (see addr_is_local_frame, #740), never for an address that could hold an
// arbitrary/stale pointer value.
//
// vm->compiler.in_union_member_access (#653) additionally gates CHKT3 here:
// a union member load must never be checked against the shadow -- legal
// union punning (write one member, read another) would otherwise
// false-positive -- so no CHKT3 is emitted at all for a union load.
//
// Shared with restrict_cache_handle_deref's cache-hit path (#750), which
// re-derives rs_addr purely to run these checks -- forward-declared near
// CCCC_FUSION_UNSAFE_FLAGS since that caller sits earlier in the file.
static void emit_load_safety_checks(VirtualMachine *vm, Type *ty, int rs_addr,
                                     bool dangling_check) {
    if (dangling_check && (vm->flags & CCCC_POINTER_CHECKS))
        emit_rr(vm, CHKP3, rs_addr, 0);
    if ((vm->flags & CCCC_TYPE_CHECKS) && !vm->compiler.in_union_member_access)
        emit_rri(vm, CHKT3, rs_addr, CHKT3_MODE_CHECK,
                 ((long long)ty->size << 8) | (long long)ty->kind);
}

static void emit_load_ex(VirtualMachine *vm, Type *ty, int rd, int rs_addr, bool dangling_check) {
    emit_load_safety_checks(vm, ty, rs_addr, dangling_check);
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_rr(vm, LDR_B, rd, rs_addr);
        if (ty->is_unsigned || ty->kind == TY_BOOL)
            emit_rr(vm, ZX1, rd, rd);
    } else if (ty->kind == TY_SHORT) {
        emit_rr(vm, LDR_H, rd, rs_addr);
        if (ty->is_unsigned)
            emit_rr(vm, ZX2, rd, rd);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_rr(vm, LDR_W, rd, rs_addr);
        if (ty->is_unsigned)
            emit_rr(vm, ZX4, rd, rd);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1) {
            emit_rr(vm, LDR_B, rd, rs_addr);
            if (ty->is_unsigned) emit_rr(vm, ZX1, rd, rd);
        } else if (ty->size == 2) {
            emit_rr(vm, LDR_H, rd, rs_addr);
            if (ty->is_unsigned) emit_rr(vm, ZX2, rd, rd);
        } else {
            emit_rr(vm, LDR_D, rd, rs_addr);
        }
    } else if (ty->kind == TY_BITINT) {
        if (is_wide_bitint(ty)) {
            // Wide _BitInt: address-based. rd already holds the address (rs_addr).
            // Just move address if they differ; no value load needed.
            if (rd != rs_addr) emit_mov3(vm, rd, rs_addr);
        } else if (ty->size == 1) {
            emit_rr(vm, LDR_B, rd, rs_addr);
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, rd, rd);
        } else if (ty->size == 2) {
            emit_rr(vm, LDR_H, rd, rs_addr);
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, rd, rd);
        } else if (ty->size == 4) {
            emit_rr(vm, LDR_W, rd, rs_addr);
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, rd, rd);
        } else {
            emit_rr(vm, LDR_D, rd, rs_addr);
        }
        if (!is_wide_bitint(ty))
            emit_bitint_trunc(vm, ty, rd);
    } else if (is_decimal(ty)) {
        // _Decimal32/64/128 (#402): address-based, same as wide _BitInt.
        // rd already holds the address (rs_addr); no value load needed.
        if (rd != rs_addr) emit_mov3(vm, rd, rs_addr);
    } else if (is_flonum(ty)) {
        emit_rr(vm, ty->kind == TY_FLOAT ? FLDR_F32 : FLDR, rd, rs_addr);
    } else {
        emit_rr(vm, LDR_D, rd, rs_addr);
    }
}

static void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr) {
    emit_load_ex(vm, ty, rd, rs_addr, true);
}

typedef struct {
    Node *base;
    Node *index;
    int scale;
    long long offset;
} IndexedAddr;

static Node *strip_index_casts(Node *node) {
    while (node && node->kind == ND_CAST)
        node = node->lhs;
    return node;
}

// Returns true if the expression subtree contains a function call.
// Used to guard indexed addressing: calls clobber all temp registers
// (caller-saved), so a base address held in r_base across a call in
// the index expression will be corrupted at runtime.
static bool expr_has_call(Node *node) {
    if (!node) return false;
    if (node->kind == ND_FUNCALL) return true;
    return expr_has_call(node->lhs) || expr_has_call(node->rhs) ||
           expr_has_call(node->cond) || expr_has_call(node->then) ||
           expr_has_call(node->els) || expr_has_call(node->body);
}

static bool is_index_scale(Node *node, Node **index, int *scale) {
    node = strip_index_casts(node);
    if (!node || node->kind != ND_MUL)
        return false;
    Node *lhs = strip_index_casts(node->lhs);
    Node *rhs = strip_index_casts(node->rhs);
    if (rhs && rhs->kind == ND_NUM && rhs->val > 0 && rhs->val <= 255) {
        *index = strip_index_casts(lhs);
        if (!*index)
            return false;
        *scale = (int)rhs->val;
        return true;
    }
    if (lhs && lhs->kind == ND_NUM && lhs->val > 0 && lhs->val <= 255) {
        *index = strip_index_casts(rhs);
        if (!*index)
            return false;
        *scale = (int)lhs->val;
        return true;
    }
    return false;
}

static bool match_indexed_addr(VirtualMachine *vm, Node *addr, IndexedAddr *out) {
    addr = strip_index_casts(addr);
    if (!addr || addr->kind != ND_ADD || vm->compiler.opt_level < 2)
        return false;
    if (vm->flags & CCCC_FUSION_UNSAFE_FLAGS)
        return false;

    Node *lhs = strip_index_casts(addr->lhs);
    Node *rhs = strip_index_casts(addr->rhs);
    if (!lhs || !rhs)
        return false;
    Node *index = NULL;
    int scale = 0;
    if (is_index_scale(rhs, &index, &scale)) {
        out->base = lhs;
        out->index = index;
        out->scale = scale;
        out->offset = 0;
        return true;
    }
    if (is_index_scale(lhs, &index, &scale)) {
        out->base = rhs;
        out->index = index;
        out->scale = scale;
        out->offset = 0;
        return true;
    }
    return false;
}

static int indexed_load_op(Type *ty) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL)
        return LDR_INDEX_B;
    if (ty->kind == TY_SHORT)
        return LDR_INDEX_H;
    if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4))
        return LDR_INDEX_W;
    if (ty->kind == TY_ENUM) {
        if (ty->size == 1) return LDR_INDEX_B;
        if (ty->size == 2) return LDR_INDEX_H;
        return LDR_INDEX_D;
    }
    if (ty->kind == TY_BITINT && !is_wide_bitint(ty)) {
        if (ty->size == 1) return LDR_INDEX_B;
        if (ty->size == 2) return LDR_INDEX_H;
        if (ty->size == 4) return LDR_INDEX_W;
        return LDR_INDEX_D;
    }
    if (ty->kind == TY_FLOAT)
        return FLDR_INDEX_F32;
    if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE)
        return FLDR_INDEX;
    return LDR_INDEX_D;
}

static int indexed_store_op(Type *ty) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL)
        return STR_INDEX_B;
    if (ty->kind == TY_SHORT)
        return STR_INDEX_H;
    if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4))
        return STR_INDEX_W;
    if (ty->kind == TY_ENUM) {
        if (ty->size == 1) return STR_INDEX_B;
        if (ty->size == 2) return STR_INDEX_H;
        return STR_INDEX_D;
    }
    if (ty->kind == TY_BITINT && !is_wide_bitint(ty)) {
        if (ty->size == 1) return STR_INDEX_B;
        if (ty->size == 2) return STR_INDEX_H;
        if (ty->size == 4) return STR_INDEX_W;
        return STR_INDEX_D;
    }
    if (ty->kind == TY_FLOAT)
        return FSTR_INDEX_F32;
    if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE)
        return FSTR_INDEX;
    return STR_INDEX_D;
}

// After computing an array index into `reg`, an *unsigned* index narrower than
// 64 bits may carry garbage in its high bits: intermediate unsigned arithmetic
// results are not truncated to the type width (e.g. the post-increment `n++`
// lowering `(unsigned)((n += 1) - 1)` evaluates `1 + 0xFFFFFFFF == 0x100000000`
// in a 64-bit register), and match_indexed_addr strips the widening cast that
// would otherwise zero-extend — i.e. truncate — the value before it is used as
// a byte offset. Re-apply that zero-extension here. Signed indices are
// sign-correct straight from their loads/arithmetic, so they need no fixup. (#581)
static void emit_index_normalize(VirtualMachine *vm, int reg, Type *ty) {
    if (!ty || !ty->is_unsigned || !is_integer(ty) || ty->kind == TY_BITINT)
        return;
    if (ty->size == 1)      emit_rr(vm, ZX1, reg, reg);
    else if (ty->size == 2) emit_rr(vm, ZX2, reg, reg);
    else if (ty->size == 4) emit_rr(vm, ZX4, reg, reg);
    // size 8 (unsigned long / size_t): already full width.
}

static bool emit_indexed_load_if_possible(VirtualMachine *vm, Node *node, int dest_reg) {
    if (!node || node->kind != ND_DEREF || !node->lhs ||
        node->ty->kind == TY_ARRAY || node->ty->kind == TY_STRUCT ||
        node->ty->kind == TY_UNION || node->ty->kind == TY_COMPLEX ||
        is_wide_bitint(node->ty) || is_decimal(node->ty)) // #402: address-based
        return false;
    IndexedAddr idx = {};
    if (!match_indexed_addr(vm, node->lhs, &idx))
        return false;
    if (expr_has_call(idx.base) || expr_has_call(idx.index))
        return false;
    int r_base = alloc_temp_reg();
    gen_expr(vm, idx.base, r_base);
    mark_temp_reg_used(r_base);
    int r_index = alloc_temp_reg();
    gen_expr(vm, idx.index, r_index);
    emit_index_normalize(vm, r_index, idx.index->ty);
    emit_rrrs_i(vm, indexed_load_op(node->ty), dest_reg, r_base, r_index,
                idx.scale, idx.offset);
    if (!is_flonum(node->ty)) {
        if (node->ty->kind == TY_BOOL || node->ty->kind == TY_CHAR) {
            if (node->ty->is_unsigned || node->ty->kind == TY_BOOL)
                emit_rr(vm, ZX1, dest_reg, dest_reg);
        } else if (node->ty->kind == TY_SHORT) {
            if (node->ty->is_unsigned)
                emit_rr(vm, ZX2, dest_reg, dest_reg);
        } else if (node->ty->kind == TY_INT ||
                   (node->ty->kind == TY_ENUM && node->ty->size == 4)) {
            if (node->ty->is_unsigned)
                emit_rr(vm, ZX4, dest_reg, dest_reg);
        } else if (node->ty->kind == TY_BITINT) {
            if (node->ty->is_unsigned) {
                if (node->ty->size == 1) emit_rr(vm, ZX1, dest_reg, dest_reg);
                else if (node->ty->size == 2) emit_rr(vm, ZX2, dest_reg, dest_reg);
                else if (node->ty->size == 4) emit_rr(vm, ZX4, dest_reg, dest_reg);
            } else {
                if (node->ty->size == 1) emit_rr(vm, SX1, dest_reg, dest_reg);
                else if (node->ty->size == 2) emit_rr(vm, SX2, dest_reg, dest_reg);
                else if (node->ty->size == 4) emit_rr(vm, SX4, dest_reg, dest_reg);
            }
            emit_bitint_trunc(vm, node->ty, dest_reg);
        }
    }
    free_temp_reg(r_index);
    free_temp_reg(r_base);
    return true;
}

static bool emit_indexed_store_if_possible(VirtualMachine *vm, Node *lhs, Type *ty,
                                           int value_reg) {
    if (!lhs || lhs->kind != ND_DEREF || !lhs->lhs ||
        ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_COMPLEX ||
        is_wide_bitint(ty) || is_decimal(ty)) // #402: address-based
        return false;
    IndexedAddr idx = {};
    if (!match_indexed_addr(vm, lhs->lhs, &idx))
        return false;
    if (expr_has_call(idx.base) || expr_has_call(idx.index))
        return false;
    int r_base = alloc_temp_reg();
    gen_expr(vm, idx.base, r_base);
    mark_temp_reg_used(r_base);
    int r_index = alloc_temp_reg();
    gen_expr(vm, idx.index, r_index);
    emit_index_normalize(vm, r_index, idx.index->ty);
    emit_rrrs_i(vm, indexed_store_op(ty), value_reg, r_base, r_index,
                idx.scale, idx.offset);
    free_temp_reg(r_index);
    free_temp_reg(r_base);
    return true;
}

// Fused load from bp-relative local slot — replaces LEA3+LDR
static void emit_local_load(VirtualMachine *vm, Type *ty, int rd, long long offset) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_ri(vm, LDR_LOCAL_B, rd, offset);
        if (ty->is_unsigned || ty->kind == TY_BOOL)
            emit_rr(vm, ZX1, rd, rd);
    } else if (ty->kind == TY_SHORT) {
        emit_ri(vm, LDR_LOCAL_H, rd, offset);
        if (ty->is_unsigned)
            emit_rr(vm, ZX2, rd, rd);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_ri(vm, LDR_LOCAL_W, rd, offset);
        if (ty->is_unsigned)
            emit_rr(vm, ZX4, rd, rd);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1) {
            emit_ri(vm, LDR_LOCAL_B, rd, offset);
            if (ty->is_unsigned) emit_rr(vm, ZX1, rd, rd);
        } else if (ty->size == 2) {
            emit_ri(vm, LDR_LOCAL_H, rd, offset);
            if (ty->is_unsigned) emit_rr(vm, ZX2, rd, rd);
        } else {
            emit_ri(vm, LDR_LOCAL_D, rd, offset);
        }
    } else if (ty->kind == TY_BITINT) {
        if (ty->size == 1) {
            emit_ri(vm, LDR_LOCAL_B, rd, offset);
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, rd, rd);
        } else if (ty->size == 2) {
            emit_ri(vm, LDR_LOCAL_H, rd, offset);
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, rd, rd);
        } else if (ty->size == 4) {
            emit_ri(vm, LDR_LOCAL_W, rd, offset);
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, rd, rd);
        } else {
            emit_ri(vm, LDR_LOCAL_D, rd, offset);
        }
        emit_bitint_trunc(vm, ty, rd);
    } else if (ty->kind == TY_FLOAT) {
        emit_ri(vm, FLDR_LOCAL_F32, rd, offset);
    } else if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
        emit_ri(vm, FLDR_LOCAL, rd, offset);
    } else {
        emit_ri(vm, LDR_LOCAL_D, rd, offset);
    }
}

// Fused store to bp-relative local slot — replaces LEA3+STR
static void emit_local_store(VirtualMachine *vm, Type *ty, int rd_val, long long offset) {
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_ri(vm, STR_LOCAL_B, rd_val, offset);
    } else if (ty->kind == TY_SHORT) {
        emit_ri(vm, STR_LOCAL_H, rd_val, offset);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_ri(vm, STR_LOCAL_W, rd_val, offset);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1)       emit_ri(vm, STR_LOCAL_B, rd_val, offset);
        else if (ty->size == 2)  emit_ri(vm, STR_LOCAL_H, rd_val, offset);
        else                     emit_ri(vm, STR_LOCAL_D, rd_val, offset);
    } else if (ty->kind == TY_BITINT) {
        if (ty->size == 1)       emit_ri(vm, STR_LOCAL_B, rd_val, offset);
        else if (ty->size == 2)  emit_ri(vm, STR_LOCAL_H, rd_val, offset);
        else if (ty->size == 4)  emit_ri(vm, STR_LOCAL_W, rd_val, offset);
        else                     emit_ri(vm, STR_LOCAL_D, rd_val, offset);
    } else if (ty->kind == TY_FLOAT) {
        emit_ri(vm, FSTR_LOCAL_F32, rd_val, offset);
    } else if (ty->kind == TY_DOUBLE || ty->kind == TY_LDOUBLE) {
        emit_ri(vm, FSTR_LOCAL, rd_val, offset);
    } else {
        emit_ri(vm, STR_LOCAL_D, rd_val, offset);
    }
}

static void emit_normalize_promoted_scalar(VirtualMachine *vm, Type *ty, int reg) {
    if (!ty)
        return;
    if (ty->kind == TY_BOOL || ty->kind == TY_CHAR) {
        emit_rr(vm, ty->is_unsigned || ty->kind == TY_BOOL ? ZX1 : SX1, reg, reg);
    } else if (ty->kind == TY_SHORT) {
        emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, reg, reg);
    } else if (ty->kind == TY_INT ||
               (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, reg, reg);
    } else if (ty->kind == TY_BITINT) {
        if (ty->size == 1)
            emit_rr(vm, ty->is_unsigned ? ZX1 : SX1, reg, reg);
        else if (ty->size == 2)
            emit_rr(vm, ty->is_unsigned ? ZX2 : SX2, reg, reg);
        else if (ty->size == 4)
            emit_rr(vm, ty->is_unsigned ? ZX4 : SX4, reg, reg);
        emit_bitint_trunc(vm, ty, reg);
    }
}

static void emit_promoted_read(VirtualMachine *vm, Obj *var, int dest_reg) {
    int preg = promoted_local_reg(vm, var);
    if (preg < 0 || dest_reg == REG_ZERO)
        return;
    emit_mov3(vm, dest_reg, preg);
}

static void emit_promoted_write(VirtualMachine *vm, Obj *var, int value_reg) {
    int idx = promoted_local_index(vm, var);
    if (idx < 0)
        return;
    int preg = vm->compiler.promoted_regs[idx];
    if (preg != value_reg)
        emit_mov3(vm, preg, value_reg);
    emit_normalize_promoted_scalar(vm, var->ty, preg);
    vm->compiler.promoted_dirty[idx] = true;
}

static void emit_flush_promoted_locals(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.promoted_count; i++) {
        if (!vm->compiler.promoted_dirty[i])
            continue;
        Obj *var = vm->compiler.promoted_locals[i];
        emit_local_store(vm, var->ty, vm->compiler.promoted_regs[i],
                         var->offset);
        vm->compiler.promoted_dirty[i] = false;
    }
}

static void emit_save_promoted_registers(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.promoted_count; i++)
        emit_local_store(vm, ty_long, vm->compiler.promoted_regs[i],
                         vm->compiler.promoted_save_offsets[i]);
}

static void emit_restore_promoted_registers(VirtualMachine *vm) {
    for (int i = vm->compiler.promoted_count - 1; i >= 0; i--)
        emit_local_load(vm, ty_long, vm->compiler.promoted_regs[i],
                        vm->compiler.promoted_save_offsets[i]);
}

static void emit_init_promoted_params(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.promoted_count; i++) {
        Obj *var = vm->compiler.promoted_locals[i];
        if (var->is_param)
            emit_local_load(vm, var->ty, vm->compiler.promoted_regs[i],
                            var->offset);
    }
}

// ---- FP local promotion helpers (#461) ----

static int fp_promoted_local_index(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++)
        if (vm->compiler.fp_promoted_locals[i] == var)
            return i;
    return -1;
}

static bool is_fp_promoted_local(VirtualMachine *vm, Obj *var) {
    return fp_promoted_local_index(vm, var) >= 0;
}

static int fp_promoted_local_reg(VirtualMachine *vm, Obj *var) {
    int idx = fp_promoted_local_index(vm, var);
    return idx >= 0 ? vm->compiler.fp_promoted_regs[idx] : -1;
}

static void emit_fp_promoted_read(VirtualMachine *vm, Obj *var, int dest_reg) {
    int preg = fp_promoted_local_reg(vm, var);
    if (preg < 0 || dest_reg == REG_ZERO)
        return;
    emit_fmov3(vm, dest_reg, preg);
}

static void emit_fp_promoted_write(VirtualMachine *vm, Obj *var, int value_reg) {
    int idx = fp_promoted_local_index(vm, var);
    if (idx < 0)
        return;
    int preg = vm->compiler.fp_promoted_regs[idx];
    if (preg != value_reg)
        emit_fmov3(vm, preg, value_reg);
    vm->compiler.fp_promoted_dirty[idx] = true;
}

static void emit_flush_fp_promoted_locals(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++) {
        if (!vm->compiler.fp_promoted_dirty[i])
            continue;
        Obj *var = vm->compiler.fp_promoted_locals[i];
        emit_local_store(vm, var->ty, vm->compiler.fp_promoted_regs[i], var->offset);
        vm->compiler.fp_promoted_dirty[i] = false;
    }
}

static void emit_save_fp_promoted_registers(VirtualMachine *vm) {
    // Save as flat double — fregs[] holds doubles after detag (#460).
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++)
        emit_ri(vm, FSTR_LOCAL, vm->compiler.fp_promoted_regs[i],
                vm->compiler.fp_promoted_save_offsets[i]);
}

static void emit_restore_fp_promoted_registers(VirtualMachine *vm) {
    for (int i = vm->compiler.fp_promoted_count - 1; i >= 0; i--)
        emit_ri(vm, FLDR_LOCAL, vm->compiler.fp_promoted_regs[i],
                vm->compiler.fp_promoted_save_offsets[i]);
}

static void emit_init_fp_promoted_params(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.fp_promoted_count; i++) {
        Obj *var = vm->compiler.fp_promoted_locals[i];
        if (var->is_param)
            emit_local_load(vm, var->ty, vm->compiler.fp_promoted_regs[i],
                            var->offset);
    }
}

// Store operations based on type. dangling_check: see emit_load_ex above.
//
// vm->compiler.in_union_member_access (#653): a union member store must
// not stamp the accessed range with that member's type -- a later access
// through a *different* member of the same union is legal punning, not a
// bug -- so this emits CHKT3 in "clear" mode instead of "stamp" mode,
// erasing rather than establishing effective-type info for the range.
static void emit_store_ex(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr, bool dangling_check) {
    if (dangling_check && (vm->flags & CCCC_POINTER_CHECKS))
        emit_rr(vm, CHKP3, rs_addr, 0);
    if (vm->flags & CCCC_TYPE_CHECKS) {
        int mode = vm->compiler.in_union_member_access ? CHKT3_MODE_CLEAR : CHKT3_MODE_STAMP;
        emit_rri(vm, CHKT3, rs_addr, mode,
                 ((long long)ty->size << 8) | (long long)ty->kind);
    }
    if (ty->kind == TY_CHAR || ty->kind == TY_BOOL) {
        emit_rr(vm, STR_B, rd_val, rs_addr);
    } else if (ty->kind == TY_SHORT) {
        emit_rr(vm, STR_H, rd_val, rs_addr);
    } else if (ty->kind == TY_INT || (ty->kind == TY_ENUM && ty->size == 4)) {
        emit_rr(vm, STR_W, rd_val, rs_addr);
    } else if (ty->kind == TY_ENUM) {
        if (ty->size == 1)       emit_rr(vm, STR_B, rd_val, rs_addr);
        else if (ty->size == 2)  emit_rr(vm, STR_H, rd_val, rs_addr);
        else                     emit_rr(vm, STR_D, rd_val, rs_addr);
    } else if (ty->kind == TY_BITINT) {
        if (is_wide_bitint(ty)) {
            // rd_val holds the source address; rs_addr is the destination.
            emit_mov3(vm, REG_A0, rs_addr);
            emit_mov3(vm, REG_A1, rd_val);
            emit_li3(vm, REG_A2, ty->size);
            emit(vm, MCPY);
        } else if (ty->size == 1) {
            emit_rr(vm, STR_B, rd_val, rs_addr);
        } else if (ty->size == 2) {
            emit_rr(vm, STR_H, rd_val, rs_addr);
        } else if (ty->size == 4) {
            emit_rr(vm, STR_W, rd_val, rs_addr);
        } else {
            emit_rr(vm, STR_D, rd_val, rs_addr);
        }
    } else if (is_decimal(ty)) {
        // _Decimal32/64/128 (#402): rd_val holds the source address (like
        // wide _BitInt); rs_addr is the destination. Plain memcpy.
        emit_mov3(vm, REG_A0, rs_addr);
        emit_mov3(vm, REG_A1, rd_val);
        emit_li3(vm, REG_A2, ty->size);
        emit(vm, MCPY);
    } else if (is_flonum(ty)) {
        emit_rr(vm, ty->kind == TY_FLOAT ? FSTR_F32 : FSTR, rd_val, rs_addr);
    } else {
        emit_rr(vm, STR_D, rd_val, rs_addr);
    }
}

static void emit_store(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr) {
    emit_store_ex(vm, ty, rd_val, rs_addr, true);
}

// Truncate register to _BitInt(N) value semantics via shift pair.
// Uses 64-bit shifts: SHL by (64-N) then SHR by (64-N) to mask to N bits.
// For N==64 the shift is 0 and ops are no-ops.
static void emit_bitint_trunc(VirtualMachine *vm, Type *ty, int reg) {
    if (ty->bit_width >= 64)
        return;
    int shift = 64 - ty->bit_width;
    int tmp = alloc_temp_reg();
    emit_li3(vm, tmp, shift);
    emit_rrr(vm, SHL3, reg, reg, tmp);
    emit_rrr(vm, ty->is_unsigned ? USHR3 : SHR3, reg, reg, tmp);
    free_temp_reg(tmp);
}

// JZ3: if rs == 0, jump (returns patch location)
static Pc emit_jz3(VirtualMachine *vm, int rs) {
    emit(vm, JZ3);
    emit_word(vm, ENCODE_R(rs));
    Pc patch = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    // Branch creates a control-flow split; invalidate restrict cache for the fall-through path.
    restrict_cache_invalidate_all(vm);
    return patch;
}

// JNZ3: if rs != 0, jump (returns patch location)
static Pc emit_jnz3(VirtualMachine *vm, int rs) {
    emit(vm, JNZ3);
    emit_word(vm, ENCODE_R(rs));
    Pc patch = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    // Branch creates a control-flow split; invalidate restrict cache for the fall-through path.
    restrict_cache_invalidate_all(vm);
    return patch;
}

typedef struct {
    Node *node;
    long begin;
    long end;
    Pc table_entry;
    Pc *patches;
    int num_patches;
    int cap_patches;
} SwitchCasePatch;

typedef struct {
    Pc *items;
    int len;
    int cap;
} PatchList;

static void add_patch_to_list(PatchList *list, Pc patch) {
    if (list->len == list->cap) {
        int new_cap = list->cap ? list->cap * 2 : 16;
        Pc *items = realloc(list->items, sizeof(Pc) * new_cap);
        if (!items)
            error("out of memory");
        list->items = items;
        list->cap = new_cap;
    }
    list->items[list->len++] = patch;
}

static void add_case_patch(SwitchCasePatch *entry, Pc patch) {
    if (entry->num_patches == entry->cap_patches) {
        int new_cap = entry->cap_patches ? entry->cap_patches * 2 : 2;
        Pc *patches = realloc(entry->patches, sizeof(Pc) * new_cap);
        if (!patches)
            error("out of memory");
        entry->patches = patches;
        entry->cap_patches = new_cap;
    }
    entry->patches[entry->num_patches++] = patch;
}

static SwitchCasePatch *find_switch_case(SwitchCasePatch *cases, int num_cases,
                                         Node *node) {
    for (int i = 0; i < num_cases; i++)
        if (cases[i].node == node)
            return &cases[i];
    return NULL;
}

static int compare_switch_cases(const void *a, const void *b) {
    const SwitchCasePatch *ca = a;
    const SwitchCasePatch *cb = b;
    if (ca->begin < cb->begin)
        return -1;
    if (ca->begin > cb->begin)
        return 1;
    return 0;
}

static SwitchCasePatch *collect_switch_cases(Node *node, int *num_cases,
                                             long *min_case, long *max_case,
                                             long *covered_values) {
    int cap = 16;
    SwitchCasePatch *cases = calloc(cap, sizeof(SwitchCasePatch));
    if (!cases)
        error("out of memory");

    *num_cases = 0;
    *covered_values = 0;
    for (Node *n = node->case_next; n; n = n->case_next) {
        if (*num_cases == cap) {
            cap *= 2;
            SwitchCasePatch *new_cases =
                realloc(cases, sizeof(SwitchCasePatch) * cap);
            if (!new_cases)
                error("out of memory");
            memset(new_cases + *num_cases, 0,
                   sizeof(SwitchCasePatch) * (cap - *num_cases));
            cases = new_cases;
        }

        long begin = n->begin;
        long end = n->end;
        cases[*num_cases].node = n;
        cases[*num_cases].begin = begin;
        cases[*num_cases].end = end;
        cases[*num_cases].table_entry = CCCC_INVALID_PC;

        if (*num_cases == 0 || begin < *min_case)
            *min_case = begin;
        if (*num_cases == 0 || end > *max_case)
            *max_case = end;
        *covered_values += end - begin + 1;
        (*num_cases)++;
    }

    qsort(cases, *num_cases, sizeof(SwitchCasePatch), compare_switch_cases);
    return cases;
}

static void free_switch_cases(SwitchCasePatch *cases, int num_cases) {
    if (!cases)
        return;
    for (int i = 0; i < num_cases; i++)
        free(cases[i].patches);
    free(cases);
}

static void emit_sparse_switch_tree(VirtualMachine *vm, SwitchCasePatch *cases, int lo,
                                    int hi, int r_val, int r_cmp,
                                    PatchList *fail_patches) {
    if (lo > hi) {
        emit(vm, JMP);
        add_patch_to_list(fail_patches, emit_word_ptr(vm));
        return;
    }

    int mid = lo + (hi - lo) / 2;
    emit_li3(vm, r_cmp, cases[mid].begin);
    emit_rrr(vm, SLT3, r_cmp, r_val, r_cmp);
    Pc left_patch = emit_jnz3(vm, r_cmp);

    emit_li3(vm, r_cmp, cases[mid].end);
    emit_rrr(vm, SGT3, r_cmp, r_val, r_cmp);
    add_case_patch(&cases[mid], emit_jz3(vm, r_cmp));

    emit_sparse_switch_tree(vm, cases, mid + 1, hi, r_val, r_cmp, fail_patches);
    vm->text_seg[left_patch] = vm->text_ptr + 1;
    emit_sparse_switch_tree(vm, cases, lo, mid - 1, r_val, r_cmp, fail_patches);
}

// PSH3: push register value onto stack
static void emit_psh3(VirtualMachine *vm, int rs) {
    emit(vm, PSH3);
    emit_word(vm, ENCODE_R(rs));
}

// POP3: pop stack value into register
static void emit_pop3(VirtualMachine *vm, int rd) {
    emit(vm, POP3);
    emit_word(vm, ENCODE_R(rd));
}

// ========== Cleanup Scope Stack ==========
// Per-block stack tracking vars with __attribute__((cleanup(fn))).
// Parallels the parse-time cleanup_scope_depth. Shared file-scope state
// (see ticket #139 note above about thread safety).

typedef struct CleanupScopeEntry {
    CleanupVar *vars;              // LIFO-ordered cleanup vars for this scope
    int depth;                     // parse-time cleanup_scope_depth of this scope
    struct CleanupScopeEntry *outer;
} CleanupScopeEntry;

static CleanupScopeEntry *g_cleanup_scope = NULL;

// Emit address-of a local variable into dest_reg.
static void emit_local_addr(VirtualMachine *vm, Obj *var, int dest_reg) {
    emit_lea3(vm, dest_reg, var->offset);
}

// Call cv->cleanup_fn(&cv->var). The cleanup fn returns void so REG_A0 is clobbered.
// Uses the standard CALL+patch mechanism (same as ND_FUNCALL for CCCC functions).
static void emit_one_cleanup(VirtualMachine *vm, CleanupVar *cv) {
    int r_addr = alloc_temp_reg();
    emit_local_addr(vm, cv->var, r_addr);
    emit_mov3(vm, REG_A0, r_addr);
    free_temp_reg(r_addr);
    emit(vm, CALL);
    Pc patch = emit_word_ptr(vm);
    vm->text_seg[patch] = 0;
    PATCH_GROW(vm, call_patches, num_call_patches, call_patches_cap);
    vm->compiler.call_patches[vm->compiler.num_call_patches].location = patch;
    vm->compiler.call_patches[vm->compiler.num_call_patches].function = cv->cleanup_fn;
    vm->compiler.num_call_patches++;
    reset_temp_regs();
}

// Emit cleanup calls for one scope's vars (iterate in stored order = LIFO).
static void emit_scope_cleanups(VirtualMachine *vm, CleanupScopeEntry *scope) {
    for (CleanupVar *cv = scope->vars; cv; cv = cv->next)
        emit_one_cleanup(vm, cv);
}

// Emit cleanup calls for all active scopes with depth > target_depth (innermost first).
static void emit_cleanups_to_depth(VirtualMachine *vm, int target_depth) {
    for (CleanupScopeEntry *s = g_cleanup_scope; s && s->depth > target_depth; s = s->outer)
        emit_scope_cleanups(vm, s);
}

// ========== Forward Declarations ==========

static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);
static void gen_complex_expr(VirtualMachine *vm, Node *node, int real_reg, int imag_reg);
static void gen_stmt(VirtualMachine *vm, Node *node);
static void gen_addr(VirtualMachine *vm, Node *node, int dest_reg);

// ========== Inline Assembly Passthru ==========

// Compile `asm_str` into a shared library exporting `sym_name` and return the
// resolved function pointer.  The .so file is unlinked immediately after
// dlopen so the library lives only in memory.  Calls error() on failure.
#if !defined(_WIN32)
static void *compile_asm_to_funcptr(const char *sym_name, const char *asm_str) {
    // Create temp source file path (use mkstemp + rename to get .c extension)
    char src_template[] = "/tmp/cccc-asm-XXXXXX";
    int src_fd = mkstemp(src_template);
    if (src_fd < 0)
        error("--asm-passthru: failed to create temp source file");
    close(src_fd);
    size_t src_len = strlen(src_template) + 3;
    char *src_path = malloc(src_len);
    if (!src_path)
        error("--asm-passthru: malloc failed");
    snprintf(src_path, src_len, "%s.c", src_template);
    if (rename(src_template, src_path) != 0) {
        unlink(src_template);
        free(src_path);
        error("--asm-passthru: failed to rename temp source file");
    }

    // Write C wrapper file with escaped asm string
    FILE *f = fopen(src_path, "w");
    if (!f) {
        unlink(src_path);
        free(src_path);
        error("--asm-passthru: failed to write temp source file");
    }
    fprintf(f, "void %s() { asm(\"", sym_name);
    for (const char *p = asm_str; *p; p++) {
        if (*p == '\\')
            fputs("\\\\", f);
        else if (*p == '"')
            fputs("\\\"", f);
        else if (*p == '\n')
            fputs("\\n", f);
        else
            fputc(*p, f);
    }
    fprintf(f, "\"); }\n");
    fclose(f);

    // Find system C compiler
    char *cc = cccc_find_native_cc();
    if (!cc) {
        unlink(src_path);
        free(src_path);
        error("--asm-passthru: no native C compiler found");
    }

    // Create temp output path for shared library
    char so_template[] = "/tmp/cccc-asm-XXXXXX";
    int so_fd = mkstemp(so_template);
    if (so_fd < 0) {
        unlink(src_path);
        free(src_path);
        free(cc);
        error("--asm-passthru: failed to create temp output file");
    }
    close(so_fd);
    size_t so_len = strlen(so_template) + 7;
    char *so_path = malloc(so_len);
    if (!so_path) {
        unlink(src_path);
        free(src_path);
        free(cc);
        unlink(so_template);
        error("--asm-passthru: malloc failed");
    }
#if defined(__APPLE__)
    snprintf(so_path, so_len, "%s.dylib", so_template);
#else
    snprintf(so_path, so_len, "%s.so", so_template);
#endif
    if (rename(so_template, so_path) != 0) {
        unlink(src_path); unlink(so_template);
        free(src_path); free(so_path); free(cc);
        error("--asm-passthru: failed to rename temp output file");
    }

    // Compile source to shared library
    pid_t pid = fork();
    if (pid < 0) {
        unlink(src_path); unlink(so_path);
        free(src_path); free(so_path); free(cc);
        error("--asm-passthru: fork failed");
    }
    if (pid == 0) {
#if defined(__APPLE__)
        execlp(cc, cc, "-shared", "-o", so_path, src_path, (char *)NULL);
#else
        execlp(cc, cc, "-shared", "-fPIC", "-o", so_path, src_path, (char *)NULL);
#endif
        _exit(127);
    }
    int status;
    waitpid(pid, &status, 0);
    unlink(src_path);
    free(src_path);
    free(cc);

    if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        unlink(so_path);
        free(so_path);
        error("--asm-passthru: native compilation failed");
    }

    // dlopen the shared library
    void *handle = dlopen(so_path, RTLD_NOW | RTLD_LOCAL);
    if (!handle) {
        unlink(so_path);
        free(so_path);
        error("--asm-passthru: dlopen failed: %s", dlerror());
    }

    // dlsym the function
    void *func_ptr = dlsym(handle, sym_name);
    if (!func_ptr) {
        dlclose(handle);
        unlink(so_path);
        free(so_path);
        error("--asm-passthru: dlsym failed: %s", dlerror());
    }

    // Unlink the .so now that it's loaded in memory
    unlink(so_path);
    free(so_path);
    return func_ptr;
}
#endif // !_WIN32

static void cccc_default_asm_passthru(VirtualMachine *vm, const char *asm_str) {
#if !defined(_WIN32)
    static int asm_counter = 0;
    char sym_name[128];
    int n = asm_counter++;
    snprintf(sym_name, sizeof(sym_name), "__cccc_asm_passthru_%d", n);

    void *func_ptr = compile_asm_to_funcptr(sym_name, asm_str);

    // Register as FFI function (0 args, no double return)
    cc_register_cfunc(vm, sym_name, func_ptr, 0, 0);

    // Find FFI index and annotate for .c4 rehydration
    int ffi_idx = find_ffi_function(vm, sym_name);
    if (ffi_idx < 0)
        error("--asm-passthru: FFI registration failed");
    vm->compiler.ffi_table[ffi_idx].is_asm_passthru = 1;
    vm->compiler.ffi_table[ffi_idx].asm_src = strdup(asm_str);

    // Emit CALLF with 0 args
    emit(vm, CALLF);
    emit_word(vm, ffi_idx);
    emit_word(vm, 0);
    emit_i64(vm, 0);
    emit_i64(vm, 0);
#else
    (void)asm_str;
    error("--asm-passthru is not supported on Windows");
#endif
}

// Recompile any asm-passthru FFI entries whose func_ptr was lost during .c4
// serialization.  Called from the .c4 load path after stdlib/library resolution.
// Respects the FFI allow/deny policy; denied entries are left with func_ptr=NULL
// (CALLF will emit the "not resolved" error at execution time).
// Returns 0 on success, -1 on hard failure.
int cc_rehydrate_asm_passthru(VirtualMachine *vm) {
#if !defined(_WIN32)
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        ForeignFunc *ff = &vm->compiler.ffi_table[i];
        if (!ff->is_asm_passthru || ff->func_ptr)
            continue;

        // Respect disable_all_ffi
        if (vm->disable_all_ffi)
            continue;

        // Respect allow list: if set, symbol must appear in it
        if (vm->ffi_allow_count > 0 &&
            !cccc_ffi_name_in_list(vm->ffi_allow_list, vm->ffi_allow_count,
                                   ff->name))
            continue;

        // Respect deny list: if no allow list and symbol is denied, skip
        if (vm->ffi_allow_count == 0 &&
            cccc_ffi_name_in_list(vm->ffi_deny_list, vm->ffi_deny_count,
                                  ff->name))
            continue;

        if (!ff->asm_src) {
            fprintf(stderr,
                    "error: asm-passthru FFI entry '%s' has no source to "
                    "rehydrate\n",
                    ff->name ? ff->name : "(null)");
            return -1;
        }

        ff->func_ptr = compile_asm_to_funcptr(ff->name, ff->asm_src);
    }
    return 0;
#else
    (void)vm;
    return 0;
#endif
}

// ========== Nested Function Helpers ==========

// Find the __static_link local variable in a nested function's locals
static Obj *find_static_link_var(Obj *fn) {
    if (!fn || !fn->is_nested)
        return NULL;
    for (Obj *var = fn->locals; var; var = var->next) {
        if (strncmp(var->name, "__static_link", sizeof("__static_link")) == 0)
            return var;
    }
    return NULL;
}

// Return the index of var in block_fn->captures (0-based), or -1 if not captured.
// Descriptor slot = (index + 1) * 8 so that slot 0 (offset 0) stays the invoke ptr.
static int find_capture_index(Obj *block_fn, Obj *var) {
    for (int i = 0; i < block_fn->num_captures; i++)
        if (block_fn->captures[i] == var) return i;
    return -1;
}

// Ensure parent->local_set is populated with all its locals and params.
// Keyed by (long long)(intptr_t)var so membership is O(1). (#165)
static void ensure_local_set(Obj *parent) {
    if (parent->local_set_built)
        return;
    for (Obj *local = parent->locals; local; local = local->next)
        hashmap_put_int(&parent->local_set, (long long)(intptr_t)local, local);
    for (Obj *param = parent->params; param; param = param->next)
        hashmap_put_int(&parent->local_set, (long long)(intptr_t)param, param);
    parent->local_set_built = true;
}

// Check if a variable belongs to an outer (enclosing) function.
// Returns the owning function, or NULL if it belongs to the current function.
// O(depth) using per-function lazy hash sets instead of O(depth * locals). (#165)
static Obj *belongs_to_outer_function(Obj *current_fn, Obj *var) {
    if (!current_fn || !current_fn->is_nested || !var || !var->is_local)
        return NULL;

    for (Obj *parent = current_fn->parent_fn; parent;
         parent = parent->parent_fn) {
        ensure_local_set(parent);
        if (hashmap_get_int(&parent->local_set, (long long)(intptr_t)var))
            return parent;
    }
    return NULL;
}

// Calculate how many static chain links to follow to reach the owning function
static int calculate_chain_depth(Obj *current_fn, Obj *owner_fn) {
    int depth = 0;
    for (Obj *fn = current_fn; fn && fn != owner_fn; fn = fn->parent_fn) {
        depth++;
    }
    return depth;
}

// Returns true when a ND_VAR node can be loaded/stored with a fused
// LDR_LOCAL/STR_LOCAL opcode instead of a LEA3+LDR/STR pair.
// Requires: node->kind == ND_VAR.
static bool is_simple_local_scalar(VirtualMachine *vm, Node *node) {
    if (!node->var->is_local)
        return false;
    if (node->var->is_block_var)
        return false;
    // A variable captured by the enclosing block lives in the block descriptor
    // (reached via __static_link), not at its own frame offset.  It must go
    // through gen_addr's capture path, never the fused direct-frame load.
    if (vm->compiler.current_fn && vm->compiler.current_fn->is_block &&
        find_capture_index(vm->compiler.current_fn, node->var) >= 0)
        return false;
    if (belongs_to_outer_function(vm->compiler.current_fn, node->var))
        return false;
    // Volatile locals must go through the generic LEA3+LDR/STR path so that
    // watchpoint checks fire on every access (C11 §6.7.3p7).
    if (node->ty->is_volatile)
        return false;
    if (node->var->is_param &&
        (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION))
        return false;
    return node->ty->kind != TY_ARRAY &&
           node->ty->kind != TY_STRUCT &&
           node->ty->kind != TY_UNION &&
           node->ty->kind != TY_COMPLEX &&
           !is_wide_bitint(node->ty) &&
           !is_decimal(node->ty); // #402: address-based, same as wide _BitInt
}

// True iff gen_addr(node) is guaranteed to produce a bp-relative address of
// the CURRENT function's own live frame -- i.e. built entirely through
// emit_lea3_var's plain local-offset branch, with no intervening pointer
// *value* load (captured-var descriptor, static-link chain, by-pointer
// aggregate param, __block heap wrapper, or an actual ND_DEREF). Mirrors the
// base-case conditions in gen_addr's ND_VAR handling (see above) and
// is_simple_local_scalar's predicate cluster, generalized to aggregates and
// to member-access chains of arbitrary depth (#740).
//
// Deliberately NOT gated on var->addr_escapes: accessing your own local from
// within your own still-running frame is safe whether or not its address
// escapes elsewhere -- escaping only matters when a *different* frame
// dereferences a pointer *value* it was handed, which is exactly the
// ND_DEREF case below (always declined, stays CHKP3-checked).
//
// #740: a non-escaping local struct/union's member access (t.a) always falls
// to gen_addr()+emit_load/emit_store like a scalar dereference, since
// is_simple_local_scalar above excludes aggregates from the fused frame-load
// path. Without this classifier, CHKP3 unconditionally runs on that freshly
// computed bp+offset address -- and can find a stale stack_ptr_epochs/
// stack_intervals tag left by an unrelated dead sibling frame's own escaping
// local that happened to reuse the same physical stack slot, false-positivin
// a dangling-pointer report on a plainly-live access to the current frame's
// own memory. <stdarg.h>'s va_arg/va_start hit this same pattern (ap.reg_ptr,
// ap.stack_ptr are ND_MEMBER on a local va_list), which is why the original
// #740 ticket's va_arg-specific diagnosis was actually one instance of this
// more general bug, not a vector/variadic-specific mechanism.
//
// Scoped to struct/union member chains only, not array/vector indexing
// (arr[i] on a local array reaches the same unchecked emit_load/store surface
// under -3, since match_indexed_addr's fused fast path is disabled whenever
// CCCC_POINTER_CHECKS is set -- but distinguishing an array/vector-decay base
// from a pointer-variable base in that ND_ADD indexing chain needs its own
// classifier and has no known reproducer yet; tracked separately).
// Is `node` a member access directly on a union expression (`u.m`, not a
// struct nested inside a union or vice versa -- only the immediate parent
// matters, since that's the object whose bytes might legally carry more
// than one C11 effective type)? Used to gate CHKT3 emission around union
// member loads/stores so legal punning doesn't false-positive (#653).
static bool is_union_member_access(Node *node) {
    return node && node->kind == ND_MEMBER && node->lhs && node->lhs->ty &&
           node->lhs->ty->kind == TY_UNION;
}

static bool addr_is_local_frame(VirtualMachine *vm, Node *node) {
    switch (node->kind) {
    case ND_VAR: {
        Obj *var = node->var;
        if (var->is_function || !var->is_local || var->is_block_var)
            return false;
        Obj *current_fn = vm->compiler.current_fn;
        if (current_fn && current_fn->is_block &&
            find_capture_index(current_fn, var) >= 0)
            return false;
        if (belongs_to_outer_function(current_fn, var))
            return false;
        if (var->is_param && (var->ty->kind == TY_STRUCT || var->ty->kind == TY_UNION ||
                               var->ty->kind == TY_VECTOR || is_wide_bitint(var->ty) ||
                               is_decimal(var->ty))) // #402: passed by pointer too
            return false;
        return true;
    }
    case ND_MEMBER:
        return addr_is_local_frame(vm, node->lhs);
    case ND_COMMA:
        return addr_is_local_frame(vm, node->rhs);
    default:
        return false; // ND_DEREF (pointer value) and everything else: keep checked
    }
}

// ========== Safety Instrumentation Helpers ==========

// Register stack variable metadata for runtime instrumentation.
// Keyed by (scope_id, offset) via stack_var_meta_key(), not offset alone --
// two different functions whose locals land at the same bp-relative offset
// (the common case) must not collide in the table (#671).
static void add_stack_var_meta(VirtualMachine *vm, const char *name, long long offset,
                               Type *ty, int scope_id) {
    if (!(vm->flags & CCCC_STACK_INSTR))
        return;
    StackVarMeta *meta = calloc(1, sizeof(StackVarMeta));
    if (!meta)
        error("failed to allocate stack variable metadata");
    meta->name     = (char *)name;
    meta->offset   = offset;
    meta->ty       = ty;
    meta->scope_id = scope_id;
    meta->is_alive = 0;
    hashmap_put_int(&vm->stack_var_meta, stack_var_meta_key(scope_id, offset), meta);
}

// Emit SCOPEIN for scope_id.
static void emit_scopein(VirtualMachine *vm, int scope_id) {
    emit(vm, SCOPEIN);
    emit_word(vm, scope_id);
}

// Emit SCOPEOUT for scope_id.
static void emit_scopeout(VirtualMachine *vm, int scope_id) {
    emit(vm, SCOPEOUT);
    emit_word(vm, scope_id);
}

// Emit CHKI (check initialized) for local at bp+offset.
static void emit_chki(VirtualMachine *vm, long long offset) {
    emit(vm, CHKI);
    emit_i64(vm, offset);
}

// Emit MARKI (mark initialized) for local at bp+offset.
static void emit_marki(VirtualMachine *vm, long long offset) {
    emit(vm, MARKI);
    emit_i64(vm, offset);
}

// Emit CHKL (check liveness) for local at bp+offset, declared in
// vm->current_function_scope_id. The runtime liveness check itself is keyed
// by actual address (bp+offset); the scope_id operand is only used to look
// up the declaration record (name/type) for the error message when the
// check fails (#671) -- see stack_var_meta_key().
static void emit_chkl(VirtualMachine *vm, long long offset) {
    emit(vm, CHKL);
    emit_i64(vm, offset);
    emit_word(vm, vm->current_function_scope_id);
}

// Emit MARKR (mark read) for local at bp+offset. No scope_id operand needed:
// the runtime looks this up by address (bp+offset) in vm->stack_var_active,
// which directly yields the correct StackVarMeta* for whichever activation
// is currently live at that address.
static void emit_markr(VirtualMachine *vm, long long offset) {
    emit(vm, MARKR);
    emit_i64(vm, offset);
}

// Emit MARKW (mark write) for local at bp+offset. See emit_markr() re: no
// scope_id operand needed.
static void emit_markw(VirtualMachine *vm, long long offset) {
    emit(vm, MARKW);
    emit_i64(vm, offset);
}

// Emit MARKP (mark provenance).
// rs_ptr and rs_base hold the pointer and its allocation base.
static void emit_markp(VirtualMachine *vm, int rs_ptr, int rs_base, int origin_type,
                       size_t size) {
    emit(vm, MARKP);
    emit_word(vm, ENCODE_RR(rs_ptr, rs_base));
    emit_word(vm, origin_type);
    emit_i64(vm, (long long)size);
}

// ========== Address Generation ==========

// Generate address of an lvalue into dest_reg
static void gen_addr(VirtualMachine *vm, Node *node, int dest_reg) {
    switch (node->kind) {
    case ND_VAR:
        if (node->var->is_function) {
            // Function address - emit placeholder and record patch
            Pc addr_loc = emit_lta3(vm, dest_reg, 0); // Placeholder

            PATCH_GROW(vm, func_addr_patches, num_func_addr_patches, func_addr_patches_cap);
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .location = addr_loc;
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .function = node->var;
            vm->compiler.num_func_addr_patches++;
        } else if (node->var->is_local) {
            // Check if this is a captured variable accessed from within a block
            Obj *current_fn = vm->compiler.current_fn;

            int cap_idx = (current_fn && current_fn->is_block)
                          ? find_capture_index(current_fn, node->var) : -1;
            if (cap_idx >= 0) {
                // Access captured variable from block descriptor via __static_link.
                // Offset is computed per-block to avoid collisions when the same
                // variable is captured at different positions in nested descriptors.
                Obj *static_link = find_static_link_var(current_fn);
                if (!static_link)
                    error("block function missing __static_link");
                int cap_offset = (cap_idx + 2) * 8; // skip invoke(0) and size(1) slots
                // Compiler-internal: this LEA3 only materializes
                // __static_link's own slot address to immediately load the
                // descriptor pointer out of it (#676) -- the slot address
                // itself never survives past the next instruction.
                emit_lea3_internal(vm, dest_reg, static_link->offset); // &__static_link
                emit_rr(vm, LDR_D, dest_reg, dest_reg);       // Load descriptor ptr
                emit_addi3(vm, dest_reg, dest_reg, cap_offset);
                if (node->var->is_block_var)
                    emit_rr(vm, LDR_D, dest_reg, dest_reg); // heap ptr from slot
            } else {
                // Check if this variable belongs to an outer function (nested
                // function access)
                Obj *owner_fn =
                    belongs_to_outer_function(current_fn, node->var);

                if (owner_fn) {
                    // Accessing outer function's variable via static chain
                    // 1. Load __static_link from current function's frame
                    Obj *static_link = find_static_link_var(current_fn);
                    if (!static_link) {
                        error("nested function missing __static_link");
                    }
                    // Compiler-internal: slot address only feeds the
                    // immediate load below (#676).
                    emit_lea3_internal(vm, dest_reg,
                              static_link->offset); // &__static_link
                    emit_rr(vm, LDR_D, dest_reg,
                            dest_reg); // Load static_link (parent's bp)

                    // 2. Walk the chain for multi-level nesting
                    int depth = calculate_chain_depth(current_fn, owner_fn);
                    // First link already loaded above, so start from 1
                    for (int i = 1; i < depth; i++) {
                        // Each parent also has __static_link at offset -1 (8
                        // bytes below bp) parent's __static_link is at
                        // parent_bp + (-1 * 8) = parent_bp - 8
                        emit_addi3(vm, dest_reg, dest_reg,
                                   -8); // parent's __static_link slot
                        emit_rr(vm, LDR_D, dest_reg,
                                dest_reg); // Load grandparent's bp
                    }

                    // 3. Now dest_reg contains owner_fn's bp, add variable's
                    // offset Variable offsets are in slots, so multiply by 8
                    // bytes
                    emit_addi3(vm, dest_reg, dest_reg, node->var->offset * 8);
                } else {
                    // Normal local variable access
                    // For struct/union (and vector, #714) parameters, the
                    // slot contains a pointer to the value. We need to load
                    // that pointer, not the slot address.
                    if (node->var->is_param && (node->ty->kind == TY_STRUCT ||
                                                node->ty->kind == TY_UNION ||
                                                node->ty->kind == TY_VECTOR ||
                                                is_wide_bitint(node->ty) ||
                                                is_decimal(node->ty))) {
                        // Compiler-internal: slot address only feeds the
                        // immediate load below (#676), not the struct's own
                        // data address.
                        emit_lea3_internal(vm, dest_reg,
                                  node->var->offset); // Slot address
                        emit_rr(vm, LDR_D, dest_reg,
                                dest_reg); // Load pointer from slot
                    } else if (node->var->is_block_var) {
                        // __block variable: slot contains pointer to
                        // heap-allocated wrapper. Compiler-internal: slot
                        // address only feeds the immediate load (#676).
                        emit_lea3_internal(vm, dest_reg,
                                  node->var->offset); // Slot address
                        emit_rr(vm, LDR_D, dest_reg,
                                dest_reg); // Load heap pointer from slot
                        // dest_reg now points to actual storage on heap
                    } else {
                        // The address returned here IS this var's own base
                        // address, handed to whatever wanted it (&var,
                        // array/struct decay, member/subscript base, ...) --
                        // skip recording iff #676's escape analysis proved
                        // it never leaves this frame.
                        emit_lea3_var(vm, dest_reg, node->var);
                    }
                }
            }
        } else {
            // Global variable (TLS or shared)
            if (node->var->is_tls)
                emit_ldtls3(vm, dest_reg, node->var->offset);
            else
                emit_lda3(vm, dest_reg, node->var->offset);
        }
        return;

    case ND_DEREF:
        // Address of *ptr is just ptr
        gen_expr(vm, node->lhs, dest_reg);
        return;

    case ND_MEMBER:
        // Address of struct.member = &struct + member_offset
        gen_addr(vm, node->lhs, dest_reg);
        if (node->member->offset != 0) {
            emit_addi3(vm, dest_reg, dest_reg, node->member->offset);
        }
        return;

    case ND_COMMA:
        gen_expr(vm, node->lhs, REG_ZERO); // Discard result
        gen_addr(vm, node->rhs, dest_reg);
        return;

    case ND_VLA_PTR:
        // VLA: get the address of the pointer variable itself (for storing into
        // it) NOT the pointer value - that's for gen_expr when accessing the
        // array
        if (node->var->is_local) {
            emit_lea3(vm, dest_reg,
                      node->var->offset); // Address of the pointer variable
        } else {
            error_tok(vm, node->tok, "VLA must be local");
        }
        return;

    default:
        error_tok(vm, node->tok, "not an lvalue");
    }
}

static int complex_part_offset(Type *ty) {
    return ty && ty->kind == TY_COMPLEX && ty->base ? ty->base->size : 8;
}

static int complex_load_op(Type *ty) {
    return ty && ty->kind == TY_COMPLEX && ty->base &&
           ty->base->kind == TY_FLOAT ? FLDR_F32 : FLDR;
}

static int complex_store_op(Type *ty) {
    return ty && ty->kind == TY_COMPLEX && ty->base &&
           ty->base->kind == TY_FLOAT ? FSTR_F32 : FSTR;
}

static void emit_float_zero(VirtualMachine *vm, int freg) {
    int r_zero = alloc_temp_reg();
    emit_li3(vm, r_zero, 0);
    emit_rr(vm, I2F3, freg, r_zero);
    free_temp_reg(r_zero);
}

static void emit_complex_load(VirtualMachine *vm, Type *ty, int real_reg, int imag_reg,
                              int addr_reg) {
    int op = complex_load_op(ty);
    emit_rr(vm, op, real_reg, addr_reg);
    int r_imag_addr = alloc_temp_reg();
    emit_addi3(vm, r_imag_addr, addr_reg, complex_part_offset(ty));
    emit_rr(vm, op, imag_reg, r_imag_addr);
    free_temp_reg(r_imag_addr);
}

static void emit_complex_store(VirtualMachine *vm, Type *ty, int real_reg, int imag_reg,
                               int addr_reg) {
    int op = complex_store_op(ty);
    emit_rr(vm, op, real_reg, addr_reg);
    int r_imag_addr = alloc_temp_reg();
    emit_addi3(vm, r_imag_addr, addr_reg, complex_part_offset(ty));
    emit_rr(vm, op, imag_reg, r_imag_addr);
    free_temp_reg(r_imag_addr);
}

static void gen_complex_expr(VirtualMachine *vm, Node *node, int real_reg, int imag_reg) {
    if (!node)
        error("codegen: null complex expression node");

    if (!is_complex(node->ty)) {
        gen_expr(vm, node, real_reg);
        emit_float_zero(vm, imag_reg);
        return;
    }

    switch (node->kind) {
    case ND_COMPLEX:
        if (node->val == 0) {
            gen_expr(vm, node->lhs, real_reg);
            gen_expr(vm, node->rhs, imag_reg);
            return;
        }
        if (node->val == 3) {
            gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
            emit_frr(vm, fop_for_type(node->ty->base, FNEG3), imag_reg,
                     imag_reg);
            return;
        }
        break;
    case ND_CAST:
        if (is_complex(node->lhs->ty)) {
            gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
        } else {
            gen_expr(vm, node->lhs, real_reg);
            if (!is_flonum(node->lhs->ty))
                emit_rr(vm,
                        fop_for_type(node->ty->base,
                                     is_u64_int(node->lhs->ty) ? U2F3 : I2F3),
                        real_reg, real_reg);
            emit_float_zero(vm, imag_reg);
        }
        if (node->ty->base && node->ty->base->kind == TY_FLOAT) {
            emit_fround_f32(vm, real_reg, real_reg);
            emit_fround_f32(vm, imag_reg, imag_reg);
        }
        return;
    case ND_VAR:
    case ND_DEREF:
    case ND_MEMBER: {
        int r_addr = alloc_temp_reg();
        gen_addr(vm, node, r_addr);
        emit_complex_load(vm, node->ty, real_reg, imag_reg, r_addr);
        free_temp_reg(r_addr);
        return;
    }
    case ND_ASSIGN: {
        gen_complex_expr(vm, node->rhs, real_reg, imag_reg);
        int r_addr = alloc_temp_reg();
        gen_addr(vm, node->lhs, r_addr);
        emit_complex_store(vm, node->ty, real_reg, imag_reg, r_addr);
        free_temp_reg(r_addr);
        return;
    }
    case ND_COMMA:
        gen_expr(vm, node->lhs, REG_ZERO);
        gen_complex_expr(vm, node->rhs, real_reg, imag_reg);
        return;
    case ND_NEG:
        gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
        emit_frr(vm, fop_for_type(node->ty->base, FNEG3), real_reg, real_reg);
        emit_frr(vm, fop_for_type(node->ty->base, FNEG3), imag_reg, imag_reg);
        return;
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV: {
        int br = REG_T5;
        int bi = REG_T6;
        int t0 = REG_T7;
        int t1 = REG_T8;
        int t2 = REG_T9;
        gen_complex_expr(vm, node->lhs, real_reg, imag_reg);
        gen_complex_expr(vm, node->rhs, br, bi);

        int fadd = fop_for_type(node->ty->base, FADD3);
        int fsub = fop_for_type(node->ty->base, FSUB3);
        int fmul = fop_for_type(node->ty->base, FMUL3);
        int fdiv = fop_for_type(node->ty->base, FDIV3);

        if (node->kind == ND_ADD) {
            emit_frrr(vm, fadd, real_reg, real_reg, br);
            emit_frrr(vm, fadd, imag_reg, imag_reg, bi);
        } else if (node->kind == ND_SUB) {
            emit_frrr(vm, fsub, real_reg, real_reg, br);
            emit_frrr(vm, fsub, imag_reg, imag_reg, bi);
        } else if (node->kind == ND_MUL) {
            emit_frrr(vm, fmul, t0, real_reg, br);
            emit_frrr(vm, fmul, t1, imag_reg, bi);
            emit_frrr(vm, fsub, t0, t0, t1);
            emit_frrr(vm, fmul, t1, real_reg, bi);
            emit_frrr(vm, fmul, imag_reg, imag_reg, br);
            emit_frrr(vm, fadd, imag_reg, t1, imag_reg);
            emit_fmov3(vm, real_reg, t0);
        } else {
            emit_frrr(vm, fmul, t0, br, br);
            emit_frrr(vm, fmul, t1, bi, bi);
            emit_frrr(vm, fadd, t0, t0, t1);
            emit_frrr(vm, fmul, t1, real_reg, br);
            emit_frrr(vm, fmul, t2, imag_reg, bi);
            emit_frrr(vm, fadd, t1, t1, t2);
            emit_frrr(vm, fdiv, t1, t1, t0);
            emit_frrr(vm, fmul, imag_reg, imag_reg, br);
            emit_frrr(vm, fmul, t2, real_reg, bi);
            emit_frrr(vm, fsub, imag_reg, imag_reg, t2);
            emit_frrr(vm, fdiv, imag_reg, imag_reg, t0);
            emit_fmov3(vm, real_reg, t1);
        }

        if (node->ty->base && node->ty->base->kind == TY_FLOAT) {
            emit_fround_f32(vm, real_reg, real_reg);
            emit_fround_f32(vm, imag_reg, imag_reg);
        }
        return;
    }
    default:
        break;
    }

    error_tok(vm, node->tok, "unsupported complex expression");
}

// ========== GNU vector_size Expressions (tracker #72) ==========
//
// A vector local/global lives in a memory slot aligned/sized to its own
// width (16/32/64 bytes), exactly like a small struct (gen_addr() already
// handles this generically -- no frame-layout changes needed). The *value*
// of a vector expression, however, flows through a vreg index in dest_reg,
// mirroring how FReg-typed expressions flow through fregs[] even though
// float locals also live in memory: VLDR/VSTR move between the slot and a
// vreg around each operation, carrying the value's byte width in the
// instruction operand.
//
// tracker #715 adds: bitwise &|^~ (integer lanes), integer lane / and %
// (per-lane trap on zero divisor / MIN-over-(-1)), comparisons ==/!=/</<=
// (GCC per-lane all-ones/all-zero SIGNED mask; >/>= are parsed as swapped
// </<=), GNU vector ?: select, and __builtin_convertvector.
//
// tracker #722 widens the substrate from 128-bit only to 128/256/512-bit
// (16/32/64-byte vectors), carrying the runtime lane count / byte width in
// the instruction operand instead of baking a fixed count into each opcode.
//
// Still deliberately out of scope (see follow-up tickets): __builtin_shuffle
// with a non-constant (runtime) index mask (the constant-mask form is
// supported, lowered without a new opcode).

typedef enum { VLANE_F64, VLANE_F32, VLANE_I64, VLANE_I32, VLANE_I16, VLANE_I8 } VecLaneFamily;

static VecLaneFamily vector_lane_family(Type *elem) {
    if (is_flonum(elem))
        return elem->kind == TY_FLOAT ? VLANE_F32 : VLANE_F64;
    switch (elem->size) {
    case 1:  return VLANE_I8;
    case 2:  return VLANE_I16;
    case 4:  return VLANE_I32;
    default: return VLANE_I64;
    }
}

static int vector_binop_for(NodeKind kind, VecLaneFamily fam) {
    static const int table[6][4] = {
        // ADD          SUB          MUL          DIV (int lanes: the
        //                                             trapping VDIV_I* also
        //                                             used by vector_modop_for)
        { VADD_F64,  VSUB_F64,  VMUL_F64,  VDIV_F64 },   // VLANE_F64
        { VADD_F32,  VSUB_F32,  VMUL_F32,  VDIV_F32 },   // VLANE_F32
        { VADD_I64,  VSUB_I64,  VMUL_I64,  VDIV_I64 },   // VLANE_I64
        { VADD_I32,  VSUB_I32,  VMUL_I32,  VDIV_I32 },   // VLANE_I32
        { VADD_I16,  VSUB_I16,  VMUL_I16,  VDIV_I16 },   // VLANE_I16
        { VADD_I8,   VSUB_I8,   VMUL_I8,   VDIV_I8  },   // VLANE_I8
    };
    int col = kind == ND_ADD ? 0 : kind == ND_SUB ? 1 : kind == ND_MUL ? 2 : 3;
    int op = table[fam][col];
    if (op < 0)
        error("codegen: unsupported vector binary op (should have been "
              "rejected in add_type)");
    return op;
}

// Integer-lane-only modulo (tracker #715); add_type rejects float-lane %.
static int vector_modop_for(VecLaneFamily fam) {
    switch (fam) {
    case VLANE_I64: return VMOD_I64;
    case VLANE_I32: return VMOD_I32;
    case VLANE_I16: return VMOD_I16;
    case VLANE_I8:  return VMOD_I8;
    default:
        error("codegen: unsupported vector modulo (should have been "
              "rejected in add_type)");
        return -1;
    }
}

// Bitwise (tracker #715): width-agnostic -- no per-lane-family variants
// needed (VM handler loops over the operand-carried word count). add_type
// rejects float-lane &|^~.
static int vector_bitop_for(NodeKind kind) {
    switch (kind) {
    case ND_BITAND: return VAND;
    case ND_BITOR:  return VOR;
    case ND_BITXOR: return VXOR;
    default:
        error("codegen: unsupported vector bitwise op");
        return -1;
    }
}

static int vector_negop_for(VecLaneFamily fam) {
    static const int table[6] = { VNEG_F64, VNEG_F32, VNEG_I64,
                                   VNEG_I32, VNEG_I16, VNEG_I8 };
    return table[fam];
}

static int vector_splatop_for(VecLaneFamily fam) {
    static const int table[6] = { VSPLAT_F64, VSPLAT_F32, VSPLAT_I64,
                                   VSPLAT_I32, VSPLAT_I16, VSPLAT_I8 };
    return table[fam];
}

// Comparisons (tracker #715): GCC semantics -- per-lane all-ones/all-zero
// SIGNED mask. `is_uns` selects the unsigned-view VCLTU/VCLEU variants for
// ordered comparisons on integer lanes (EQ/NE are sign-independent; float
// lanes are always signed-ordered, `is_uns` is ignored for VLANE_F64/F32).
static int vector_cmpop_for(NodeKind kind, VecLaneFamily fam, bool is_uns) {
    static const int table[6][4] = {
        // EQ         NE         LT (signed)   LE (signed)
        { VCEQ_F64, VCNE_F64, VCLT_F64, VCLE_F64 },
        { VCEQ_F32, VCNE_F32, VCLT_F32, VCLE_F32 },
        { VCEQ_I64, VCNE_I64, VCLT_I64, VCLE_I64 },
        { VCEQ_I32, VCNE_I32, VCLT_I32, VCLE_I32 },
        { VCEQ_I16, VCNE_I16, VCLT_I16, VCLE_I16 },
        { VCEQ_I8,  VCNE_I8,  VCLT_I8,  VCLE_I8  },
    };
    static const int unsigned_table[6][2] = {
        // LT (unsigned)   LE (unsigned) -- only meaningful for int families
        { -1, -1 }, { -1, -1 },
        { VCLTU_I64, VCLEU_I64 },
        { VCLTU_I32, VCLEU_I32 },
        { VCLTU_I16, VCLEU_I16 },
        { VCLTU_I8,  VCLEU_I8  },
    };
    int col = kind == ND_EQ ? 0 : kind == ND_NE ? 1 : kind == ND_LT ? 2 : 3;
    if (is_uns && col >= 2) {
        int op = unsigned_table[fam][col - 2];
        if (op >= 0)
            return op;
    }
    return table[fam][col];
}

// Select (tracker #715): VSEL_{8,16,32,64} by lane byte width.
static int vector_selop_for(int lane_bytes) {
    switch (lane_bytes) {
    case 1: return VSEL_8;
    case 2: return VSEL_16;
    case 4: return VSEL_32;
    default: return VSEL_64;
    }
}

static void gen_addr(VirtualMachine *vm, Node *node, int dest_reg);

static void gen_vector_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    switch (node->kind) {
    case ND_VAR:
    case ND_DEREF:
    case ND_MEMBER: {
        // Vector lvalue read: address, then a full-width load.
        int r_addr = alloc_temp_reg();
        gen_addr(vm, node, r_addr);
        emit_rrs(vm, VLDR, dest_reg, r_addr, node->ty->size);
        free_temp_reg(r_addr);
        return;
    }
    case ND_ASSIGN: {
        int r_val = dest_reg == REG_ZERO ? alloc_temp_reg() : dest_reg;
        gen_expr(vm, node->rhs, r_val);
        mark_temp_reg_used(r_val);
        int r_addr = alloc_temp_reg();
        gen_addr(vm, node->lhs, r_addr);
        emit_rrs(vm, VSTR, r_val, r_addr, node->ty->size);
        free_temp_reg(r_addr);
        if (dest_reg == REG_ZERO)
            free_temp_reg(r_val);
        return;
    }
    case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD: {
        // usual_arith_conv() already cast both operands to this node's
        // (vector) type, so both sides are vector-valued here.
        VecLaneFamily fam = vector_lane_family(node->ty->base);
        int r_lhs = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_lhs);
        mark_temp_reg_used(r_lhs);
        int r_rhs = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_rhs);
        int op = node->kind == ND_MOD ? vector_modop_for(fam)
                                       : vector_binop_for(node->kind, fam);
        emit_rrrs(vm, op, dest_reg, r_lhs, r_rhs, node->ty->vec_len);
        free_temp_reg(r_rhs);
        free_temp_reg(r_lhs);
        return;
    }
    case ND_BITAND: case ND_BITOR: case ND_BITXOR: {
        int r_lhs = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_lhs);
        mark_temp_reg_used(r_lhs);
        int r_rhs = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_rhs);
        emit_rrrs(vm, vector_bitop_for(node->kind), dest_reg, r_lhs, r_rhs,
                  node->ty->size / 8);
        free_temp_reg(r_rhs);
        free_temp_reg(r_lhs);
        return;
    }
    case ND_BITNOT: {
        int r_src = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_src);
        emit_rrs(vm, VNOT, dest_reg, r_src, node->ty->size / 8);
        free_temp_reg(r_src);
        return;
    }
    case ND_EQ: case ND_NE: case ND_LT: case ND_LE: {
        // node->ty is the signed-integer mask type (vector_mask_type);
        // node->lhs->ty is the still-intact operand vector type, which is
        // what determines the lane family/signedness to compare.
        VecLaneFamily fam = vector_lane_family(node->lhs->ty->base);
        bool is_uns = node->lhs->ty->base->is_unsigned;
        int r_lhs = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_lhs);
        mark_temp_reg_used(r_lhs);
        int r_rhs = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_rhs);
        emit_rrrs(vm, vector_cmpop_for(node->kind, fam, is_uns), dest_reg,
                  r_lhs, r_rhs, node->lhs->ty->vec_len);
        free_temp_reg(r_rhs);
        free_temp_reg(r_lhs);
        return;
    }
    case ND_COND: {
        if (!is_vector(node->cond->ty)) {
            // Ordinary C ternary with vector arms (tracker #715): the
            // condition is a plain scalar, so the whole vector value is
            // selected by a runtime branch -- standard C, not the GNU
            // per-lane extension below. Identical shape to the generic
            // (scalar/struct) ND_COND codegen elsewhere in this switch.
            int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
            mark_temp_reg_used(r_cond);
            gen_cond_expr(vm, node->cond, r_cond);
            Pc jz_else = emit_jz3(vm, r_cond);
            if (r_cond != dest_reg) free_temp_reg(r_cond);

            gen_expr(vm, node->then, dest_reg);
            emit(vm, JMP);
            Pc jmp_end = emit_word_ptr(vm);

            vm->text_seg[jz_else] = vm->text_ptr + 1;
            gen_expr(vm, node->els, dest_reg);
            vm->text_seg[jmp_end] = vm->text_ptr + 1;
            return;
        }
        // GNU per-lane vector ?: select (tracker #715): rd is pre-loaded
        // with the else-arm, then VSEL_w overwrites only the lanes where
        // cond is nonzero (see op_VSEL_*_fn in ops.c -- a read-modify-write
        // on rd, like VINSERT_*, safe under the optimizer's fully-opaque
        // treatment of vector opcodes).
        int r_dest = dest_reg == REG_ZERO ? alloc_temp_reg() : dest_reg;
        gen_expr(vm, node->els, r_dest);
        mark_temp_reg_used(r_dest);
        int r_cond = alloc_temp_reg();
        gen_expr(vm, node->cond, r_cond);
        mark_temp_reg_used(r_cond);
        int r_then = alloc_temp_reg();
        gen_expr(vm, node->then, r_then);
        emit_rrrs(vm, vector_selop_for(node->ty->base->size), r_dest, r_cond,
                  r_then, node->ty->vec_len);
        free_temp_reg(r_then);
        free_temp_reg(r_cond);
        if (dest_reg == REG_ZERO)
            free_temp_reg(r_dest);
        return;
    }
    case ND_NEG: {
        VecLaneFamily fam = vector_lane_family(node->ty->base);
        int r_src = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_src);
        emit_rrs(vm, vector_negop_for(fam), dest_reg, r_src, node->ty->vec_len);
        free_temp_reg(r_src);
        return;
    }
    case ND_CAST: {
        if (is_vector(node->lhs->ty)) {
            // Identity cast (usual_arith_conv casts a same-type vector
            // operand to the common type, which is itself). No conversion
            // needed -- just materialize the source value into dest_reg.
            gen_expr(vm, node->lhs, dest_reg);
            return;
        }
        // Scalar -> vector: broadcast to every lane (the `vec + scalar`
        // broadcast semantics; also reachable via a bare scalar assigned to
        // a vector variable).
        VecLaneFamily fam = vector_lane_family(node->ty->base);
        int r_scalar = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_scalar);
        emit_rrs(vm, vector_splatop_for(fam), dest_reg, r_scalar, node->ty->vec_len);
        free_temp_reg(r_scalar);
        return;
    }
    case ND_CONVERTVECTOR: {
        // __builtin_convertvector (tracker #715): cross-lane-family element
        // conversion, restricted at parse time to int32<->float32 and
        // int64<->float64 pairs (see the builtin's parse.c handler).
        VecLaneFamily src_fam = vector_lane_family(node->lhs->ty->base);
        VecLaneFamily dst_fam = vector_lane_family(node->ty->base);
        int op;
        if (src_fam == VLANE_F32 && dst_fam == VLANE_I32) op = VCVT_I32_F32;
        else if (src_fam == VLANE_I32 && dst_fam == VLANE_F32) op = VCVT_F32_I32;
        else if (src_fam == VLANE_F64 && dst_fam == VLANE_I64) op = VCVT_I64_F64;
        else if (src_fam == VLANE_I64 && dst_fam == VLANE_F64) op = VCVT_F64_I64;
        else {
            error_tok(vm, node->tok,
                      "codegen: unsupported vector conversion (should have "
                      "been rejected by __builtin_convertvector's parser)");
            op = -1;
        }
        int r_src = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_src);
        emit_rrs(vm, op, dest_reg, r_src, node->ty->vec_len);
        free_temp_reg(r_src);
        return;
    }
    case ND_COMMA:
        // A vector-typed comma reaches here from lvar_initializer's
        // ND_MEMZERO pre-zero (`ND_COMMA(memzero, rhs)`, tracker #713's
        // brace-init/copy-init path) as well as ordinary comma expressions
        // whose result is a vector.
        gen_expr(vm, node->lhs, REG_ZERO); // Discard result
        gen_expr(vm, node->rhs, dest_reg);
        return;
    default:
        error_tok(vm, node->tok, "unsupported vector expression");
    }
}

// ========== Expression Generation ==========

// Generate code for expression, result in dest_reg (integer) or dest_freg
// (float)
static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    if (!node) {
        error("codegen: null expression node");
    }

    // A vector-returning ND_FUNCALL falls through to the main switch below
    // instead of gen_vector_expr (#714): the value comes back via the
    // RETBUF+VSTR return-buffer convention (see ND_RETURN), handled in
    // ND_FUNCALL's result tail, not as a plain vector expression.
    // ND_BLOCK_CALL (Apple Blocks) is deliberately excluded from this bypass
    // -- its ABI (src/codegen.c, `case ND_BLOCK_CALL`) has no RETBUF/pointer-
    // arg machinery for aggregates at all, so a vector-typed block call still
    // falls to gen_vector_expr's default case below and reports a clean
    // "unsupported vector expression" error rather than silently
    // mis-marshalling through REG_A0.
    if (is_vector(node->ty) && node->kind != ND_FUNCALL) {
        gen_vector_expr(vm, node, dest_reg);
        return;
    }

    if (is_complex(node->ty)) {
        int imag_reg = (dest_reg == FREG_A5) ? FREG_A4 : FREG_A5;
        gen_complex_expr(vm, node, dest_reg == REG_ZERO ? FREG_A0 : dest_reg,
                         imag_reg);
        return;
    }

    switch (node->kind) {
    case ND_NULL_EXPR:
        return;

    case ND_UNREACHABLE:
        emit(vm, BTRAP);
        return;

    case ND_DECIMAL_TO_CHARS: {
        // __builtin_decimal_to_chars(buf, n, decimal_val) (#402): lowers
        // directly to DFMT. buf(A0), n(A1), val(A2)=address, width(A3).
        //
        // node->cond (the decimal value) is evaluated FIRST, before buf/n:
        // any decimal subexpression (e.g. `x - y`) emits an opaque DADD/
        // DSUB/... op, and emit_wide_op's reset_temp_regs() call marks
        // every temp register "free" again for the *next* allocation --
        // exactly like is_wide_bitint_helper_op's callers already have to
        // account for. Evaluating it last, after buf/n were already parked
        // in T-registers, let a later gen_expr()-internal alloc_temp_reg()
        // legally reclaim one of those "freed" T-registers mid-evaluation
        // and silently clobber buf/n's value (caught empirically: buf's
        // address arrived 48 bytes off at runtime). Evaluating the risky
        // operand first sidesteps the whole hazard rather than requiring
        // a push/pop spill for buf/n.
        int r_val = alloc_temp_reg();
        gen_expr(vm, node->cond, r_val); // decimal operand -> its address
        mark_temp_reg_used(r_val);
        int r_buf = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_buf);
        mark_temp_reg_used(r_buf);
        int r_n = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_n);
        mark_temp_reg_used(r_n);

        emit_mov3(vm, REG_A0, r_buf);
        emit_mov3(vm, REG_A1, r_n);
        emit_mov3(vm, REG_A2, r_val);
        emit_li3(vm, REG_A3, dec_width_code(node->cond->ty));
        if (vm->flags & CCCC_POINTER_CHECKS) {
            emit_rr(vm, CHKP3, REG_A0, 0);
            emit_rr(vm, CHKP3, REG_A2, 0);
        }
        emit_wide_op(vm, DFMT);
        if (dest_reg != REG_ZERO)
            emit_mov3(vm, dest_reg, REG_A0);
        free_temp_reg(r_n);
        free_temp_reg(r_buf);
        free_temp_reg(r_val);
        return;
    }

    case ND_NUM:
        if (is_flonum(node->ty)) {
            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + 7) & ~7; // Align
            vm->data_ptr = vm->data_seg + offset;
            long long lit_size = (node->ty->kind == TY_FLOAT) ? sizeof(float) : sizeof(double);
            check_data_capacity(vm, offset + lit_size);
            if (node->ty->kind == TY_FLOAT) {
                *(float *)vm->data_ptr = (float)node->fval;
                vm->data_ptr += sizeof(float);
            } else {
                *(double *)vm->data_ptr = node->fval;
                vm->data_ptr += sizeof(double);
            }

            int temp = alloc_temp_reg();
            emit_lda3(vm, temp, offset);
            emit_rr(vm, node->ty->kind == TY_FLOAT ? FLDR_F32 : FLDR, dest_reg, temp);
            free_temp_reg(temp);
        } else if (is_decimal(node->ty)) {
            // _Decimal32/64/128 literal (#402): encoded to BID bits at
            // COMPILE time (unlike the wide-_BitInt literal below, which
            // resolves at runtime) -- there is no runtime global-init path
            // for a static/global decimal initializer to hook into, so the
            // bytes must already be correct in the data segment. A decimal
            // value is address-based (never loaded into an FReg/int reg),
            // so dest_reg receives the literal's address, not a loaded value.
            if (!node->dec_digits)
                error_tok(vm, node->tok,
                          "internal error: _Decimal literal missing digit text");

            unsigned char bits[16];
            if (!cccc_dec_encode_literal(node->dec_digits, dec_width_code(node->ty), bits))
                error_tok(vm, node->tok,
                          "_Decimal literals require a build with CCCC_HAS_DECIMAL=1 "
                          "(run tools/fetch_intel_bid.sh, then `make CCCC_HAS_DECIMAL=1`)");

            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + (node->ty->align - 1)) & ~(long long)(node->ty->align - 1);
            vm->data_ptr = vm->data_seg + offset;
            check_data_capacity(vm, offset + node->ty->size);
            memcpy(vm->data_ptr, bits, (size_t)node->ty->size);
            vm->data_ptr += node->ty->size;

            emit_lda3(vm, dest_reg, offset);
        } else if (is_wide_bitint(node->ty)) {
            // wb/uwb literal wider than 64 bits: materialize at runtime via
            // __cccc_bitint_from_str, reading the full-precision digit text
            // (captured by the tokenizer) from the data segment.
            if (!node->wide_digits)
                error_tok(vm, node->tok,
                          "internal error: wide _BitInt literal missing digit text");

            size_t digit_len = strlen(node->wide_digits);
            long long offset = vm->data_ptr - vm->data_seg;
            offset = (offset + 7) & ~7; // Align
            vm->data_ptr = vm->data_seg + offset;
            check_data_capacity(vm, offset + (long long)digit_len + 1);
            memcpy(vm->data_ptr, node->wide_digits, digit_len + 1);
            vm->data_ptr += digit_len + 1;

            int str_addr = alloc_temp_reg();
            emit_lda3(vm, str_addr, offset);

            int words = node->ty->size / 8;
            long long dst_offset = alloc_wide_bitint_temp(vm, words);

            emit_lea3(vm, REG_A0, dst_offset);
            emit_mov3(vm, REG_A1, str_addr);
            emit_li3(vm, REG_A2, node->wide_base);
            emit_li3(vm, REG_A3, words);
            emit_li3(vm, REG_A4, node->ty->bit_width);
            emit_wide_helper(vm, "__cccc_bitint_from_str", 5);
            free_temp_reg(str_addr);

            emit_lea3(vm, dest_reg, dst_offset);
        } else {
            emit_li3(vm, dest_reg, node->val);
        }
        return;

    case ND_COMPLEX:
        if (node->val == 1 || node->val == 2) {
            int imag_reg = (dest_reg == FREG_A7) ? FREG_A6 : FREG_A7;
            gen_complex_expr(vm, node->lhs, dest_reg, imag_reg);
            if (node->val == 2)
                emit_fmov3(vm, dest_reg, imag_reg);
            if (node->ty->kind == TY_FLOAT)
                emit_fround_f32(vm, dest_reg, dest_reg);
            return;
        }
        error_tok(vm, node->tok, "unsupported non-complex projection");

    case ND_VAR:
        if (node->var->is_function) {
            // Function name used as value - function-to-pointer decay
            // Emit LTA3 with placeholder, patch later
            Pc addr_loc = emit_lta3(vm, dest_reg, 0); // Placeholder

            // Record patch location for later resolution
            PATCH_GROW(vm, func_addr_patches, num_func_addr_patches, func_addr_patches_cap);
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .location = addr_loc;
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .function = node->var;
            vm->compiler.num_func_addr_patches++;
        } else {
            // Stack instrumentation for scalar locals (not arrays/structs/
            // wide _BitInt). Wide _BitInt is address-based like structs, so
            // writes go through a raw pointer (WIDE_* opcode or CALLF helper),
            // never STR_LOCAL — meaning MARKI/MARKW are never emitted for those
            // slots. Excluding wide _BitInt from the read-side checks too keeps
            // the two sides symmetric and avoids the false "uninitialized
            // variable read" trap under CCCC_UNINIT_DETECTION (-2/-3) that
            // ticket #457 reported. (Consistent with structs/unions/arrays,
            // which are already exempt here.)
            if (node->var->is_local && !node->var->is_param &&
                node->var->ty && node->var->ty->kind != TY_ARRAY &&
                node->var->ty->kind != TY_STRUCT &&
                node->var->ty->kind != TY_UNION &&
                !is_wide_bitint(node->var->ty) &&
                !is_decimal(node->var->ty)) { // #402: address-based, same exemption
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_chkl(vm, node->var->offset);
                if (vm->flags & CCCC_UNINIT_DETECTION)
                    emit_chki(vm, node->var->offset);
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_markr(vm, node->var->offset);
            }
            if (is_promoted_local(vm, node->var)) {
                emit_promoted_read(vm, node->var, dest_reg);
                return;
            }
            if (is_fp_promoted_local(vm, node->var)) {
                emit_fp_promoted_read(vm, node->var, dest_reg);
                return;
            }
            // Fused local load: skip the LEA3+LDR two-step for simple locals
            if (is_simple_local_scalar(vm, node)) {
                emit_local_load(vm, node->ty, dest_reg, node->var->offset);
            } else if (is_flonum(node->ty)) {
                // For float types, FREG_A0-A7 have the same raw numbers as
                // REG_A0-A7. Use a temp register to avoid clobbering int regs.
                int r_addr = alloc_temp_reg();
                gen_addr(vm, node, r_addr);
                emit_load_ex(vm, node->ty, dest_reg, r_addr, !addr_is_local_frame(vm, node));
                free_temp_reg(r_addr);
            } else {
                gen_addr(vm, node, dest_reg);
                // For scalars, load the value (wide _BitInt/_Decimal stay as address)
                if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
                    node->ty->kind != TY_UNION && !is_wide_bitint(node->ty) &&
                    !is_decimal(node->ty)) {
                    emit_load_ex(vm, node->ty, dest_reg, dest_reg, !addr_is_local_frame(vm, node));
                }
            }
        }
        return;

    case ND_DEREF:
        if (restrict_cache_handle_deref(vm, node, dest_reg))
            return;
        if (promoted_deref_target(vm, node)) {
            emit_promoted_read(vm, promoted_deref_target(vm, node), dest_reg);
            return;
        }
        if (emit_indexed_load_if_possible(vm, node, dest_reg))
            return;
        gen_expr(vm, node->lhs, dest_reg);
        // TY_FUNC: dereferencing a function pointer is a no-op in C — *f and f
        // are interchangeable when f has pointer-to-function type.  Do not emit
        // a data load; the register already holds the callable address.
        if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
            node->ty->kind != TY_UNION && node->ty->kind != TY_FUNC &&
            !is_wide_bitint(node->ty) && !is_decimal(node->ty)) {
            emit_load(vm, node->ty, dest_reg, dest_reg);
        }
        return;

    case ND_ADDR:
        gen_addr(vm, node->lhs, dest_reg);
        // Track explicit address-of a local var for provenance. Dangling-pointer
        // detection no longer needs address-taken tracking here -- it's now a
        // precise dereference-time range check in op_CHKP3_fn (#670).
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_local &&
            !node->lhs->var->is_block_var) {
            if (vm->flags & CCCC_PROVENANCE_TRACK) {
                size_t var_size = node->lhs->var->ty ? node->lhs->var->ty->size : 8;
                emit_markp(vm, dest_reg, dest_reg, 1 /* STACK */, var_size);
            }
        }
        return;

    case ND_NEG:
        if (is_wide_bitint(node->ty)) {
            gen_wide_bitint_unary(vm, node, dest_reg, "__cccc_bitint_neg");
            return;
        }
        if (is_decimal(node->ty)) {
            int r_src = alloc_temp_reg();
            gen_expr(vm, node->lhs, r_src);
            mark_temp_reg_used(r_src);
            long long dst_offset = node->ret_buffer
                ? (long long)node->ret_buffer->offset
                : alloc_decimal_temp(vm, node->ty->size);
            emit_lea3(vm, REG_A0, dst_offset);
            emit_mov3(vm, REG_A1, r_src);
            emit_li3(vm, REG_A2, dec_width_code(node->ty));
            if (vm->flags & CCCC_POINTER_CHECKS) {
                emit_rr(vm, CHKP3, REG_A0, 0);
                emit_rr(vm, CHKP3, REG_A1, 0);
            }
            emit_wide_op(vm, DNEG);
            emit_lea3(vm, dest_reg, dst_offset);
            free_temp_reg(r_src);
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        if (is_flonum(node->ty)) {
            emit_frr(vm, fop_for_type(node->ty, FNEG3), dest_reg, dest_reg);
        } else {
            emit_rr(vm, NEG3, dest_reg, dest_reg);
        }
        return;

    case ND_NOT:
        if (node->lhs && (is_wide_bitint(node->lhs->ty) || is_decimal(node->lhs->ty))) {
            gen_cond_expr(vm, node->lhs, dest_reg); // 0/1
            emit_rr(vm, NOT3, dest_reg, dest_reg);  // logical negate of 0/1
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        emit_rr(vm, NOT3, dest_reg, dest_reg);
        return;

    case ND_BITNOT:
        if (is_wide_bitint(node->ty)) {
            gen_wide_bitint_unary(vm, node, dest_reg, "__cccc_bitint_not");
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        emit_rr(vm, BNOT3, dest_reg, dest_reg);
        return;

    // Binary arithmetic operations
    case ND_ADD:
    case ND_SUB:
    case ND_MUL:
    case ND_DIV:
    case ND_MOD:
    case ND_BITAND:
    case ND_BITOR:
    case ND_BITXOR:
    case ND_SHL:
    case ND_SHR:
    case ND_EQ:
    case ND_NE:
    case ND_LT:
    case ND_LE: {
        if (is_complex(node->lhs->ty) || is_complex(node->rhs->ty)) {
            if (node->kind != ND_EQ && node->kind != ND_NE)
                error_tok(vm, node->tok, "unsupported complex comparison");

            int ar = FREG_A0;
            int ai = FREG_A1;
            int br = FREG_A2;
            int bi = FREG_A3;
            gen_complex_expr(vm, node->lhs, ar, ai);
            gen_complex_expr(vm, node->rhs, br, bi);

            int r_real = alloc_temp_reg();
            int r_imag = alloc_temp_reg();
            emit_frrr(vm, fop_for_type(node->lhs->ty->base, FEQ3), r_real, ar,
                      br);
            emit_frrr(vm, fop_for_type(node->lhs->ty->base, FEQ3), r_imag, ai,
                      bi);
            emit_rrr(vm, AND3, dest_reg, r_real, r_imag);
            if (node->kind == ND_NE)
                emit_rr(vm, NOT3, dest_reg, dest_reg);
            free_temp_reg(r_imag);
            free_temp_reg(r_real);
            return;
        }

        // Check if RHS contains a function call - if so, we need to save LHS
        // because function calls clobber caller-saved temp registers
        bool rhs_has_call = contains_funcall(node->rhs);

        // Evaluate LHS first, then allocate r_rhs. gen_expr frees all temps
        // before returning, so the pool is empty after LHS completes. This keeps
        // peak usage at O(1) per level instead of O(chain-depth), fixing register
        // exhaustion on long left-associative chains (ticket #295).
        if (is_flonum(node->lhs->ty)) {
            // Float operations
            gen_expr(vm, node->lhs,
                     dest_reg); // LHS goes directly to dest (float reg)

            // LHS might contain a function call which resets temp regs.
            // Re-mark dest_reg as used so r_rhs allocation doesn't clobber it.
            mark_temp_reg_used(dest_reg);

            // Operands fed to the final float op, and the temp to release after.
            // Fast path: LHS stays in dest_reg (float), RHS goes to a fresh
            // float temp. Spill path (#587): under register pressure, save the
            // LHS float bits to the stack and reuse dest_reg for the RHS so no
            // temp stays live across the RHS recursion. PSH3/POP3 operate on
            // integer regs, so the bits go through FR2R/R2FR (same idiom as the
            // rhs_has_call branch below).
            int r_lhs_op, r_rhs_op, r_free;
            if (temp_regs_free() <= TEMP_REG_SPILL_THRESHOLD) {
                int r_tmp = alloc_temp_reg();
                emit_rr(vm, fop_for_type(node->lhs->ty, FR2R), r_tmp,
                        dest_reg);     // LHS float bits -> int reg
                emit_psh3(vm, r_tmp);  // save LHS on the stack
                free_temp_reg(r_tmp);  // nothing held across the RHS recursion
                gen_expr(vm, node->rhs, dest_reg); // RHS reuses dest; pool free
                r_free = alloc_temp_reg();
                emit_pop3(vm, r_free); // reload LHS bits into int reg
                emit_rr(vm, fop_for_type(node->lhs->ty, R2FR), r_free,
                        r_free);       // int bits -> float reg (in place)
                r_lhs_op = r_free;
                r_rhs_op = dest_reg;
            } else {
                int r_rhs = alloc_temp_reg();
                if (rhs_has_call) {
                    // For floats: convert to int, push to stack, evaluate RHS,
                    // pop, convert back. dest_reg is FREG_*, so we use FR2R to
                    // move bits to an int temp.
                    int r_temp = alloc_temp_reg();
                    emit_rr(vm, fop_for_type(node->lhs->ty, FR2R), r_temp,
                            dest_reg); // Float bits -> int reg
                    emit_psh3(vm, r_temp);               // Push int reg to stack
                    gen_expr(vm, node->rhs,
                             r_rhs);       // Evaluate RHS (may clobber all)
                    emit_pop3(vm, r_temp); // Pop saved bits into int reg
                    emit_rr(vm, fop_for_type(node->lhs->ty, R2FR), dest_reg,
                            r_temp); // Int bits -> float reg
                    free_temp_reg(r_temp);
                } else {
                    gen_expr(vm, node->rhs, r_rhs);
                }
                r_lhs_op = dest_reg;
                r_rhs_op = r_rhs;
                r_free = r_rhs;
            }

            int fop;
            switch (node->kind) {
            case ND_ADD:
                fop = FADD3;
                break;
            case ND_SUB:
                fop = FSUB3;
                break;
            case ND_MUL:
                fop = FMUL3;
                break;
            case ND_DIV:
                fop = FDIV3;
                break;
            case ND_EQ:
                fop = FEQ3;
                break;
            case ND_NE:
                fop = FNE3;
                break;
            case ND_LT:
                fop = FLT3;
                break;
            case ND_LE:
                fop = FLE3;
                break;
            default:
                error("unsupported float op");
            }
            emit_frrr(vm, fop_for_type(node->lhs->ty, fop), dest_reg, r_lhs_op,
                      r_rhs_op);
            free_temp_reg(r_free);
        } else if (is_decimal(node->lhs->ty) || is_decimal(node->ty)) {
            // _Decimal32/64/128 (#402): address-based like wide _BitInt, but
            // dispatches to the dedicated DADD/DSUB/DMUL/DDIV/DCMP opcodes
            // (fixed A-register convention, fully opaque to the optimizer --
            // see the OPS_X comment in cccc.h) instead of a CALLF helper.
            // usual_arith_conv already rejected mixing decimal with a binary
            // float or _BitInt operand, so node->lhs->ty == node->rhs->ty
            // (modulo decimal-vs-decimal rank) here.
            Type *operand_ty = node->lhs->ty;
            int w = dec_width_code(operand_ty);
            bool is_cmp = (node->kind == ND_EQ || node->kind == ND_NE ||
                           node->kind == ND_LT || node->kind == ND_LE);

            // dest_reg may be REG_ZERO (a discarded-value expression
            // statement, e.g. `(void)(a+b);`): REG_ZERO is hardwired to
            // always read back 0, so staging an address through it here
            // would silently produce a null pointer. Fall back to a fresh
            // temp in that case -- everything else still writes the final
            // result to dest_reg, which is the correct discard target.
            int work_reg = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
            gen_expr(vm, node->lhs, work_reg); // decimal operand -> its address
            emit_psh3(vm, work_reg);
            gen_expr(vm, node->rhs, work_reg); // temp pool is empty here
            // The RHS recursion may contain a call whose emit_wide_helper
            // CALLF resets the whole temp-reg bitmap, marking work_reg's bit
            // free again even though it still holds the live RHS value.
            // Re-mark it used so alloc_temp_reg() below can't hand out the
            // same register for r_rhs (which would then get silently
            // clobbered once r_lhs is popped into that same slot).
            mark_temp_reg_used(work_reg);
            int r_rhs = alloc_temp_reg();
            emit_mov3(vm, r_rhs, work_reg);
            if (work_reg != dest_reg)
                free_temp_reg(work_reg);
            int r_lhs = alloc_temp_reg();
            emit_pop3(vm, r_lhs);

            if (is_cmp) {
                emit_mov3(vm, REG_A0, r_lhs);
                emit_mov3(vm, REG_A1, r_rhs);
                emit_li3(vm, REG_A2, w);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                }
                emit_wide_op(vm, DCMP); // A0 = 0=EQ/1=LT/2=GT/3=UNORDERED
                int code = alloc_temp_reg();
                emit_mov3(vm, code, REG_A0);
                int tmp = alloc_temp_reg();
                switch (node->kind) {
                case ND_EQ:
                    emit_li3(vm, tmp, 0);
                    emit_rrr(vm, SEQ3, dest_reg, code, tmp);
                    break;
                case ND_NE:
                    emit_li3(vm, tmp, 0);
                    emit_rrr(vm, SNE3, dest_reg, code, tmp);
                    break;
                case ND_LT:
                    emit_li3(vm, tmp, 1);
                    emit_rrr(vm, SEQ3, dest_reg, code, tmp);
                    break;
                case ND_LE: {
                    // LE is EQ(0) or LT(1) -- i.e. code <= 1, which also
                    // correctly excludes GT(2) and UNORDERED(3). code is
                    // always in [0,3], so plain signed SLE3 is exact here.
                    int lim = alloc_temp_reg();
                    emit_li3(vm, lim, 1);
                    emit_rrr(vm, SLE3, dest_reg, code, lim);
                    free_temp_reg(lim);
                    break;
                }
                default: break;
                }
                free_temp_reg(tmp);
                free_temp_reg(code);
            } else {
                long long dst_offset;
                if (node->ret_buffer)
                    dst_offset = (long long)node->ret_buffer->offset;
                else
                    dst_offset = alloc_decimal_temp(vm, operand_ty->size);

                int decimal_op;
                switch (node->kind) {
                case ND_ADD: decimal_op = DADD; break;
                case ND_SUB: decimal_op = DSUB; break;
                case ND_MUL: decimal_op = DMUL; break;
                case ND_DIV: decimal_op = DDIV; break;
                default:
                    error_tok(vm, node->tok, "unsupported _Decimal operator");
                    decimal_op = DADD;
                }

                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs);
                emit_li3(vm, REG_A3, w);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                    emit_rr(vm, CHKP3, REG_A2, 0);
                }
                emit_wide_op(vm, decimal_op);
                emit_lea3(vm, dest_reg, dst_offset);
            }
            free_temp_reg(r_rhs);
            free_temp_reg(r_lhs);
        } else if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->ty)) {
            // Wide _BitInt operations: delegate to runtime helpers.
            // LHS and RHS are wide → each gen_expr returns an address.
            // Comparison result is scalar; arithmetic result is wide.
            Type *operand_ty = node->lhs->ty;
            bool wide_op = is_wide_bitint(operand_ty);

            // See the matching decimal-binop REG_ZERO note above: dest_reg
            // may be a discarded-value expression statement, so stage
            // through a fresh temp in that case rather than REG_ZERO.
            int work_reg = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
            gen_expr(vm, node->lhs, work_reg);
            emit_psh3(vm, work_reg);
            gen_expr(vm, node->rhs, work_reg); // temp pool is empty here
            // See the decimal-binop comment above: a CALLF inside the RHS
            // (emit_wide_helper, e.g. a bitwise/comparison wide-_BitInt op)
            // resets the temp-reg bitmap, so re-mark work_reg used before
            // anything else can be allocated over it.
            mark_temp_reg_used(work_reg);
            int r_rhs = alloc_temp_reg();
            emit_mov3(vm, r_rhs, work_reg);
            if (work_reg != dest_reg)
                free_temp_reg(work_reg);
            int r_lhs = alloc_temp_reg();
            emit_pop3(vm, r_lhs);

            int words  = wide_op ? (operand_ty->size / 8) : 0;
            int width  = wide_op ? operand_ty->bit_width  : 0;
            bool is_signed = wide_op ? !operand_ty->is_unsigned : false;

            // For arithmetic/bitwise ops: allocate a stack temp for the result.
            long long dst_offset = 0;
            bool is_cmp = (node->kind == ND_EQ || node->kind == ND_NE ||
                           node->kind == ND_LT || node->kind == ND_LE);
            if (!is_cmp && wide_op) {
                if (node->ret_buffer) {
                    dst_offset = (long long)node->ret_buffer->offset;
                } else {
                    dst_offset = alloc_wide_bitint_temp(vm, words);
                }
            }

            // ND_ADD/SUB/MUL/DIV/MOD/SHL/SHR dispatch to dedicated WIDE_*
            // opcodes (#456) instead of a CALLF into the runtime helper —
            // AND/OR/XOR/comparisons stay on the CALLF/emit_wide_helper path.
            int wide_opcode = 0;
            switch (node->kind) {
            case ND_ADD: wide_opcode = WIDE_ADD; break;
            case ND_SUB: wide_opcode = WIDE_SUB; break;
            case ND_MUL: wide_opcode = WIDE_MUL; break;
            case ND_DIV: wide_opcode = WIDE_DIV; break;
            case ND_MOD: wide_opcode = WIDE_MOD; break;
            case ND_SHL: wide_opcode = WIDE_SHL; break;
            case ND_SHR: wide_opcode = is_signed ? WIDE_SHR : WIDE_USHR; break;
            default: break;
            }

            const char *fn = NULL;
            switch (node->kind) {
            case ND_BITAND: fn = "__cccc_bitint_and";  break;
            case ND_BITOR:  fn = "__cccc_bitint_or";   break;
            case ND_BITXOR: fn = "__cccc_bitint_xor";  break;
            case ND_EQ: case ND_NE: case ND_LT: case ND_LE:
                fn = "__cccc_bitint_cmp"; break;
            case ND_ADD: case ND_SUB: case ND_MUL: case ND_DIV: case ND_MOD:
            case ND_SHL: case ND_SHR:
                break; // handled via wide_opcode above
            default:
                error_tok(vm, node->tok, "unsupported wide _BitInt op");
            }

            if (node->kind == ND_SHL || node->kind == ND_SHR) {
                // Shift: dst(A0), src_addr(A1), shift_amount(A2), words(A3), width(A4)
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs); // shift amount is scalar (A2)
                emit_li3(vm, REG_A3, words);
                emit_li3(vm, REG_A4, width);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                }
                emit_wide_op(vm, wide_opcode);
                emit_lea3(vm, dest_reg, dst_offset);
            } else if (is_cmp) {
                // cmp(a, b, words, width, is_signed) → {-1, 0, 1} in REG_A0
                emit_mov3(vm, REG_A0, r_lhs);
                emit_mov3(vm, REG_A1, r_rhs);
                emit_li3(vm, REG_A2, words);
                emit_li3(vm, REG_A3, width);
                emit_li3(vm, REG_A4, is_signed ? 1 : 0);
                emit_wide_helper(vm, fn, 5);
                // Convert cmp result to bool per operator
                int tmp = alloc_temp_reg();
                emit_li3(vm, tmp, 0);
                switch (node->kind) {
                case ND_EQ: emit_rrr(vm, SEQ3, dest_reg, REG_A0, tmp); break;
                case ND_NE: emit_rrr(vm, SNE3, dest_reg, REG_A0, tmp); break;
                case ND_LT: emit_rrr(vm, SLT3, dest_reg, REG_A0, tmp); break;
                case ND_LE: emit_rrr(vm, SLE3, dest_reg, REG_A0, tmp); break;
                default: break;
                }
                free_temp_reg(tmp);
            } else if (node->kind == ND_BITAND || node->kind == ND_BITOR ||
                       node->kind == ND_BITXOR) {
                // Bitwise: dst(A0), a(A1), b(A2), words(A3), width(A4)
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs);
                emit_li3(vm, REG_A3, words);
                emit_li3(vm, REG_A4, width);
                emit_wide_helper(vm, fn, 5);
                emit_lea3(vm, dest_reg, dst_offset);
            } else {
                // Arithmetic: dst(A0), a(A1), b(A2), words(A3), width(A4)
                // (DIV/MOD additionally need is_signed in A5)
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_lhs);
                emit_mov3(vm, REG_A2, r_rhs);
                emit_li3(vm, REG_A3, words);
                emit_li3(vm, REG_A4, width);
                if (node->kind == ND_DIV || node->kind == ND_MOD)
                    emit_li3(vm, REG_A5, is_signed ? 1 : 0);
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                    emit_rr(vm, CHKP3, REG_A2, 0);
                }
                emit_wide_op(vm, wide_opcode);
                emit_lea3(vm, dest_reg, dst_offset);
            }
            free_temp_reg(r_rhs);
            free_temp_reg(r_lhs);
        } else {
            // Narrow integer operations (scalar _BitInt or plain int)
            gen_expr(vm, node->lhs, dest_reg); // LHS goes directly to dest

            // LHS might contain a function call which resets temp regs.
            // Re-mark dest_reg as used so r_rhs allocation doesn't clobber it.
            mark_temp_reg_used(dest_reg);

            // Operands fed to the final op, and the temp to release afterwards.
            // Fast path: LHS stays in dest_reg, RHS goes to a fresh temp.
            // Spill path (#587): under register pressure, save LHS to the stack
            // and reuse dest_reg for the RHS so no temp stays live across the
            // RHS recursion — bounds peak register use on deeply nested trees.
            // The spill path also subsumes the rhs_has_call case (PSH3 protects
            // the LHS across any call inside the RHS).
            int r_lhs_op, r_rhs_op, r_free;
            if (temp_regs_free() <= TEMP_REG_SPILL_THRESHOLD) {
                emit_psh3(vm, dest_reg);            // save LHS
                gen_expr(vm, node->rhs, dest_reg);  // RHS reuses dest; pool free
                r_free = alloc_temp_reg();
                emit_pop3(vm, r_free);              // reload LHS
                r_lhs_op = r_free;
                r_rhs_op = dest_reg;
            } else {
                int r_rhs = alloc_temp_reg();
                if (rhs_has_call) {
                    // Save LHS to stack before function call in RHS
                    emit_psh3(vm, dest_reg);
                    gen_expr(vm, node->rhs, r_rhs);
                    // Restore saved LHS from stack
                    emit_pop3(vm, dest_reg);
                } else {
                    gen_expr(vm, node->rhs, r_rhs);
                }
                r_lhs_op = dest_reg;
                r_rhs_op = r_rhs;
                r_free = r_rhs;
            }

            int op;
            bool checked_arith = (vm->flags & CCCC_OVERFLOW_CHECKS) &&
                                 !node->ty->is_unsigned &&
                                 !(node->lhs->ty && node->lhs->ty->base);
            switch (node->kind) {
            case ND_ADD:
                op = checked_arith ? ADDC : ADD3;
                break;
            case ND_SUB:
                op = checked_arith ? SUBC : SUB3;
                break;
            case ND_MUL:
                op = checked_arith ? MULC : MUL3;
                break;
            case ND_DIV:
                op = node->ty->is_unsigned ? UDIV3 : (checked_arith ? DIVC : DIV3);
                break;
            case ND_MOD:
                op = node->ty->is_unsigned ? UMOD3 : MOD3;
                break;
            case ND_BITAND:
                op = AND3;
                break;
            case ND_BITOR:
                op = OR3;
                break;
            case ND_BITXOR:
                op = XOR3;
                break;
            case ND_SHL:
                op = SHL3;
                break;
            case ND_SHR:
                op = node->ty->is_unsigned ? USHR3 : SHR3;
                break;
            case ND_EQ:
                op = SEQ3;
                break;
            case ND_NE:
                op = SNE3;
                break;
            case ND_LT:
                op = SLT3;
                break;
            case ND_LE:
                op = SLE3;
                break;
            default:
                error("unsupported int op");
            }
            // For pointer add/sub, emit bounds check (CHKB) before the add and
            // provenance check (CHKPA) after.
            bool is_ptr_arith = (node->kind == ND_ADD || node->kind == ND_SUB) &&
                                node->lhs->ty && node->lhs->ty->base;
            if (is_ptr_arith && (vm->flags & CCCC_BOUNDS_CHECKS))
                emit_rr(vm, CHKB, r_lhs_op, r_rhs_op);

            // Unsigned 64-bit comparison: use dedicated ULT3/ULE3 opcodes.
            // Shorter unsigned types (≤32-bit) are zero-extended in 64-bit
            // registers so SLT3/SLE3 already give the correct signed result.
            bool is_u64_cmp =
                (node->kind == ND_LT || node->kind == ND_LE) &&
                node->lhs->ty && node->lhs->ty->is_unsigned &&
                node->lhs->ty->size == 8;
            if (is_u64_cmp) {
                op = (node->kind == ND_LT) ? ULT3 : ULE3;
            }

            emit_rrr(vm, op, dest_reg, r_lhs_op, r_rhs_op);

            if (node->ty->kind == TY_BITINT)
                emit_bitint_trunc(vm, node->ty, dest_reg);

            if (is_ptr_arith && (vm->flags & (CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK))) {
                emit(vm, CHKPA);
                emit_word(vm, ENCODE_R(dest_reg));
            }
            free_temp_reg(r_free);
        }

        return;
    }

    case ND_ASSIGN: {
        Obj *lhs_promoted_deref = promoted_deref_target(vm, node->lhs);
        if (lhs_promoted_deref) {
            int r_val = dest_reg == REG_ZERO ? alloc_temp_reg() : dest_reg;
            bool need_free = dest_reg == REG_ZERO;
            gen_expr(vm, node->rhs, r_val);
            emit_promoted_write(vm, lhs_promoted_deref, r_val);
            if (!lhs_promoted_deref->is_param &&
                lhs_promoted_deref->ty &&
                lhs_promoted_deref->ty->kind != TY_ARRAY &&
                lhs_promoted_deref->ty->kind != TY_STRUCT &&
                lhs_promoted_deref->ty->kind != TY_UNION) {
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_markw(vm, lhs_promoted_deref->offset);
                if (vm->flags & CCCC_UNINIT_DETECTION)
                    emit_marki(vm, lhs_promoted_deref->offset);
            }
            if (need_free)
                free_temp_reg(r_val);
            return;
        }

        // IMPORTANT: Evaluate RHS *before* computing LHS address!
        // If RHS is a function call, it will clobber temp registers.
        // Computing LHS address after ensures we get a fresh temp reg.

        // For struct/union assignments, we need memcpy (both LHS and RHS are
        // addresses)
        if (node->ty &&
            (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION ||
             is_wide_bitint(node->ty) || is_decimal(node->ty))) {
            // Struct/union/wide-_BitInt/_Decimal assignment: memcpy from
            // RHS to LHS (#402: decimal is address-based, same as these)
            int r_src = alloc_temp_reg();
            gen_expr(vm, node->rhs, r_src); // RHS is address
            mark_temp_reg_used(r_src);

            // If the LHS address expression contains a call, gen_addr below will
            // clobber every caller-saved temp at runtime, destroying the RHS
            // address held in r_src (e.g. `arr[f()] = some_struct;`). Spill it
            // across the address computation and reload afterwards. (#581)
            bool src_has_call = expr_has_call(node->lhs);
            long long r_src_spill = 0;
            if (src_has_call) {
                r_src_spill = alloc_wide_bitint_temp(vm, 1);
                emit_local_store(vm, ty_long, r_src, r_src_spill);
            }

            int r_dest = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_dest); // LHS address

            if (src_has_call) {
                mark_temp_reg_used(r_dest);
                int r_reload = alloc_temp_reg();
                emit_local_load(vm, ty_long, r_reload, r_src_spill);
                free_temp_reg(r_src);
                r_src = r_reload;
            }

            // MCPY: REG_A0=dest, REG_A1=src, REG_A2=size
            emit_mov3(vm, REG_A0, r_dest);
            emit_mov3(vm, REG_A1, r_src);
            emit_li3(vm, REG_A2, node->ty->size);
            emit(vm, MCPY);

            free_temp_reg(r_src);
            free_temp_reg(r_dest);

            // Assignment expression result is the destination address
            if (dest_reg != REG_ZERO) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
            return;
        }

        // First, evaluate RHS into a temporary or dest_reg
        int r_val = dest_reg;
        bool need_free = false;
        // Use a temp reg if dest_reg is zero or if we need to preserve it
        // (though dest_reg is output) But critically, if LHS is a bitfield, we
        // definitely need temp regs for RMW
        if (dest_reg == REG_ZERO ||
            (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield)) {
            r_val = alloc_temp_reg();
            need_free = true;
        }
        gen_expr(vm, node->rhs, r_val);

        // CRITICAL: If RHS contained a function call, reset_temp_regs() was
        // called. We need to re-mark r_val as in-use before allocating r_addr!
        mark_temp_reg_used(r_val);

        bool rhs_promoted_addr = node->lhs->kind == ND_VAR &&
                                 node->lhs->var->is_local &&
                                 node->lhs->var->name &&
                                 node->lhs->var->name[0] == '\0' &&
                                 node->rhs &&
                                 node->rhs->kind == ND_ADDR &&
                                 node->rhs->lhs &&
                                 node->rhs->lhs->kind == ND_VAR &&
                                 is_promoted_local(vm, node->rhs->lhs->var);

        // Fused local store: skip LEA3+STR for simple locals
        bool lhs_fused = node->lhs->kind == ND_VAR &&
                         is_simple_local_scalar(vm, node->lhs);
        IndexedAddr lhs_idx_check = {};
        bool lhs_indexed = node->lhs->kind == ND_DEREF &&
                           match_indexed_addr(vm, node->lhs->lhs, &lhs_idx_check) &&
                           !expr_has_call(lhs_idx_check.base) &&
                           !expr_has_call(lhs_idx_check.index);
        // If the LHS address expression itself contains a function call (e.g.
        // `form[strlen(form) - 1] = 's'`), that call clobbers every
        // caller-saved temp register at runtime — including the one holding the
        // already-evaluated RHS in r_val. (reset_temp_regs() inside the call's
        // codegen mirrors this real clobber.) Spill r_val to a one-word stack
        // slot across the address computation and reload it afterwards so the
        // store sees the correct value. (#581)
        bool lhs_has_call = !lhs_fused && expr_has_call(node->lhs);
        long long r_val_spill = 0;
        if (lhs_has_call) {
            r_val_spill = alloc_wide_bitint_temp(vm, 1);
            emit_local_store(vm, node->ty, r_val, r_val_spill);
        }

        int r_addr = -1;
        if (!lhs_fused && !lhs_indexed) {
            // Now compute LHS address (after any function calls in RHS are done)
            r_addr = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_addr);
        }

        if (lhs_has_call) {
            // The address is now in r_addr; reload r_val into a fresh temp that
            // is guaranteed distinct from r_addr. (The old r_val register was
            // clobbered by the call and its allocator bit cleared by the reset.)
            if (r_addr >= 0)
                mark_temp_reg_used(r_addr);
            int r_reload = alloc_temp_reg();
            emit_local_load(vm, node->ty, r_reload, r_val_spill);
            if (need_free)
                free_temp_reg(r_val);
            r_val = r_reload;
            need_free = true;
        }

        // #653: a store through a union member must clear (not stamp) the
        // type shadow for the accessed range -- see emit_store_ex's doc
        // comment and is_union_member_access above.
        bool lhs_saved_union_flag = vm->compiler.in_union_member_access;
        vm->compiler.in_union_member_access = is_union_member_access(node->lhs);

        // Handle Bitfields specially (Read-Modify-Write)
        if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
            Member *mem = node->lhs->member;
            int r_container = alloc_temp_reg();
            bool lhs_local_frame = addr_is_local_frame(vm, node->lhs);

            // Load container value
            emit_load_ex(vm, mem->ty, r_container,
                         r_addr, !lhs_local_frame); // Use member type (container)

            // Clear the bitfield bits: container &= ~(mask << bit_offset)
            int r_mask = alloc_temp_reg();
            long long mask = ((1ULL << mem->bit_width) - 1);
            emit_li3(vm, r_mask, ~(mask << mem->bit_offset));
            emit_rrr(vm, AND3, r_container, r_container, r_mask);

            // Prepare new value: (val & mask) << bit_offset
            int r_new = alloc_temp_reg();
            emit_mov3(vm, r_new, r_val);
            emit_li3(vm, r_mask, mask); // Reuse r_mask for positive mask
            emit_rrr(vm, AND3, r_new, r_new, r_mask); // Truncate val to width
            // Shift new value into position
            if (mem->bit_offset > 0) {
                int r_shift = alloc_temp_reg();
                emit_li3(vm, r_shift, mem->bit_offset);
                emit_rrr(vm, SHL3, r_new, r_new, r_shift);
                free_temp_reg(r_shift);
            }

            // OR new value into container
            emit_rrr(vm, OR3, r_container, r_container, r_new);

            // Store back
            emit_store_ex(vm, mem->ty, r_container,
                          r_addr, !lhs_local_frame); // Use member type (container)

            free_temp_reg(r_new);
            free_temp_reg(r_mask);
            free_temp_reg(r_container);
        } else if (node->lhs->kind == ND_VAR &&
                   is_promoted_local(vm, node->lhs->var)) {
            emit_promoted_write(vm, node->lhs->var, r_val);
        } else if (node->lhs->kind == ND_VAR &&
                   is_fp_promoted_local(vm, node->lhs->var)) {
            emit_fp_promoted_write(vm, node->lhs->var, r_val);
        } else if (lhs_indexed &&
                   emit_indexed_store_if_possible(vm, node->lhs, node->ty,
                                                  r_val)) {
            // stored by fused indexed opcode
        } else if (lhs_fused) {
            emit_local_store(vm, node->ty, r_val, node->lhs->var->offset);
        } else {
            // Standard store
            emit_store_ex(vm, node->ty, r_val, r_addr, !addr_is_local_frame(vm, node->lhs));
        }
        vm->compiler.in_union_member_access = lhs_saved_union_flag;

        // Update or invalidate the restrict cache for this store.
        restrict_cache_handle_store(vm, node->lhs, r_val);

        // Stack instrumentation: record write and mark initialized (scalars only).
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_local &&
            !node->lhs->var->is_param &&
            node->lhs->var->ty && node->lhs->var->ty->kind != TY_ARRAY &&
            node->lhs->var->ty->kind != TY_STRUCT &&
            node->lhs->var->ty->kind != TY_UNION) {
            if (vm->flags & CCCC_STACK_INSTR)
                emit_markw(vm, node->lhs->var->offset);
            if (vm->flags & CCCC_UNINIT_DETECTION)
                emit_marki(vm, node->lhs->var->offset);
        }

        if (r_addr >= 0)
            free_temp_reg(r_addr);

        // Assignment result is the value
        // If bitfield, r_val holds the RHS value, which is correct
        if (dest_reg != REG_ZERO && dest_reg != r_val) {
            emit_mov3(vm, dest_reg, r_val);
        }

        if (need_free) {
            free_temp_reg(r_val);
        }
        if (rhs_promoted_addr)
            promotion_alias_add(vm, node->lhs->var, node->rhs->lhs->var);
        return;
    }

    case ND_COND: {
        // Ternary: cond ? then : else.
        // Reuse dest_reg for the condition scratch to avoid O(depth) register
        // accumulation on deeply nested && / || conditions (#587 gap).
        // Guard: dest_reg may be REG_ZERO (discarded expression statement) — the
        // zero register silently discards writes, so the condition is always read
        // back as 0, making the branch always take the else arm. Allocate a real
        // temp when dest_reg == REG_ZERO and free it before generating the branches.
        int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
        mark_temp_reg_used(r_cond);
        gen_cond_expr(vm, node->cond, r_cond);
        Pc jz_else = emit_jz3(vm, r_cond);
        if (r_cond != dest_reg) free_temp_reg(r_cond);

        gen_expr(vm, node->then, dest_reg);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        vm->text_seg[jz_else] = vm->text_ptr + 1;
        gen_expr(vm, node->els, dest_reg);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        return;
    }

    case ND_COMMA:
        gen_expr(vm, node->lhs, REG_ZERO); // Discard result
        gen_expr(vm, node->rhs, dest_reg);
        return;

    case ND_MEMBER: {
        bool local_frame = addr_is_local_frame(vm, node);
        gen_addr(vm, node, dest_reg);

        bool is_union_member = is_union_member_access(node);
        bool saved_union_flag = vm->compiler.in_union_member_access;
        vm->compiler.in_union_member_access = is_union_member;

        if (node->member->is_bitfield) {
            Member *mem = node->member;
            // Load container value
            emit_load_ex(vm, mem->ty, dest_reg, dest_reg, !local_frame);

            if (mem->ty->is_unsigned) {
                // Unsigned: (val >> bit_offset) & mask
                if (mem->bit_offset > 0) {
                    int r_shift = alloc_temp_reg();
                    emit_li3(vm, r_shift, mem->bit_offset);
                    emit_rrr(vm, SHR3, dest_reg, dest_reg,
                             r_shift); // Logical shift right
                    free_temp_reg(r_shift);
                }
                long long mask = (1ULL << mem->bit_width) - 1;
                int r_mask = alloc_temp_reg();
                emit_li3(vm, r_mask, mask);
                emit_rrr(vm, AND3, dest_reg, dest_reg, r_mask);
                free_temp_reg(r_mask);
            } else {
                // Signed: (val << (64 - width - offset)) >> (64 - width)
                int r_shift = alloc_temp_reg();
                int left_shift = 64 - mem->bit_width - mem->bit_offset;
                int right_shift = 64 - mem->bit_width;

                emit_li3(vm, r_shift, left_shift);
                emit_rrr(vm, SHL3, dest_reg, dest_reg, r_shift);

                emit_li3(vm, r_shift, right_shift);
                emit_rrr(vm, SHR3, dest_reg, dest_reg,
                         r_shift); // Arithmetic shift preserves sign
                free_temp_reg(r_shift);
            }
        } else {
            // Standard member
            if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
                node->ty->kind != TY_UNION) {
                emit_load_ex(vm, node->ty, dest_reg, dest_reg, !local_frame);
            }
        }
        vm->compiler.in_union_member_access = saved_union_flag;
        return;
    }

    case ND_CAST:
        if (is_complex(node->lhs->ty)) {
            int imag_reg = (dest_reg == FREG_A7) ? FREG_A6 : FREG_A7;
            gen_complex_expr(vm, node->lhs, dest_reg, imag_reg);
            if (!is_flonum(node->ty))
                emit_rr(vm,
                        fop_for_type(node->lhs->ty->base,
                                     is_u64_int(node->ty) ? F2U3 : F2I3),
                        dest_reg, dest_reg);
            else if (node->ty->kind == TY_FLOAT)
                emit_fround_f32(vm, dest_reg, dest_reg);
            return;
        }
        // Wide _BitInt conversion handling
        if (is_wide_bitint(node->lhs->ty) || is_wide_bitint(node->ty)) {
            Type *src = node->lhs->ty;
            Type *dst = node->ty;
            if (is_wide_bitint(dst) && !is_wide_bitint(src)) {
                // Narrow int/float → wide _BitInt
                long long dst_offset = alloc_wide_bitint_temp(vm, dst->size / 8);
                emit_lea3(vm, REG_A0, dst_offset);
                if (is_flonum(src)) {
                    // float/double → wide: pass double bits as raw int64
                    // Float regs always store as double internally, so FR2R gives
                    // us the IEEE-754 double representation regardless of f32/f64.
                    int r_tmp = alloc_temp_reg();
                    gen_expr(vm, node->lhs, r_tmp); // puts float/double in float reg
                    emit_rr(vm, FR2R, REG_A1, r_tmp); // double bits → int reg
                    free_temp_reg(r_tmp);
                    emit_lea3(vm, REG_A0, dst_offset);
                    emit_li3(vm, REG_A2, dst->size / 8);  // words
                    emit_li3(vm, REG_A3, dst->bit_width);
                    emit_li3(vm, REG_A4, !dst->is_unsigned);
                    emit_wide_helper(vm, "__cccc_bitint_from_double", 5);
                } else {
                    gen_expr(vm, node->lhs, REG_A1); // narrow int value
                    emit_li3(vm, REG_A2, dst->size / 8);  // words
                    emit_li3(vm, REG_A3, dst->bit_width);
                    const char *fn = (!dst->is_unsigned || src->is_unsigned)
                                     ? "__cccc_bitint_from_i64"
                                     : "__cccc_bitint_from_u64";
                    emit_wide_helper(vm, fn, 4);
                }
                emit_lea3(vm, dest_reg, dst_offset);
                return;
            } else if (!is_wide_bitint(dst) && is_wide_bitint(src)) {
                // Wide _BitInt → narrow int/float
                int r_src = alloc_temp_reg();
                gen_expr(vm, node->lhs, r_src); // address of wide value
                emit_mov3(vm, REG_A0, r_src);
                free_temp_reg(r_src);
                emit_li3(vm, REG_A1, src->size / 8);  // words
                emit_li3(vm, REG_A2, src->bit_width);
                emit_li3(vm, REG_A3, !src->is_unsigned);
                if (is_flonum(dst)) {
                    emit_wide_helper(vm, "__cccc_bitint_to_double", 4);
                    // result is raw double bits in REG_A0; reinterpret as float reg
                    emit_rr(vm, R2FR, dest_reg, REG_A0);
                    if (dst->kind == TY_FLOAT)
                        emit_fround_f32(vm, dest_reg, dest_reg);
                } else if (dst->kind == TY_BOOL) {
                    // (_Bool) must reflect the whole value, not just the low
                    // word: a wide value with only high bits set is still true.
                    emit_li3(vm, REG_A1, src->size / 8); // words (A0 = address)
                    emit_wide_helper(vm, "__cccc_bitint_nonzero", 2);
                    emit_mov3(vm, dest_reg, REG_A0);
                } else {
                    emit_wide_helper(vm, "__cccc_bitint_to_i64", 4);
                    emit_mov3(vm, dest_reg, REG_A0);
                    // Apply target int truncation
                    if (dst->kind == TY_BOOL)
                        emit_rrr(vm, SNE3, dest_reg, dest_reg, REG_ZERO);
                    else if (dst->kind == TY_CHAR)
                        emit_rr(vm, dst->is_unsigned ? ZX1 : SX1, dest_reg, dest_reg);
                    else if (dst->kind == TY_SHORT)
                        emit_rr(vm, dst->is_unsigned ? ZX2 : SX2, dest_reg, dest_reg);
                    else if (dst->kind == TY_INT)
                        emit_rr(vm, dst->is_unsigned ? ZX4 : SX4, dest_reg, dest_reg);
                    else if (dst->kind == TY_BITINT)
                        emit_bitint_trunc(vm, dst, dest_reg);
                }
                return;
            } else if (is_wide_bitint(src) && is_wide_bitint(dst)) {
                // Wide → wide: sign/zero-extend (per src signedness) when
                // growing, or truncate when shrinking.
                int words_src = src->size / 8;
                int words_dst = dst->size / 8;
                long long dst_offset = alloc_wide_bitint_temp(vm, words_dst);
                int r_src = alloc_temp_reg();
                gen_expr(vm, node->lhs, r_src);
                emit_lea3(vm, REG_A0, dst_offset);
                emit_mov3(vm, REG_A1, r_src);
                free_temp_reg(r_src);
                emit_li3(vm, REG_A2, words_src);
                emit_li3(vm, REG_A3, src->bit_width);
                emit_li3(vm, REG_A4, words_dst);
                emit_li3(vm, REG_A5, dst->bit_width);
                emit_li3(vm, REG_A6, !src->is_unsigned);
                emit_wide_helper(vm, "__cccc_bitint_extend", 7);
                emit_lea3(vm, dest_reg, dst_offset);
                return;
            }
        }
        // _Decimal32/64/128 conversion handling (#402): address-based, like
        // wide _BitInt above, but dispatches to DFROMI/DTOI/DFROMBITS/
        // DTOBITS/DCVT instead of a bitint runtime helper.
        if (is_decimal(node->lhs->ty) || is_decimal(node->ty)) {
            Type *src = node->lhs->ty;
            Type *dst = node->ty;
            if (is_decimal(dst) && !is_decimal(src)) {
                long long dst_offset = node->ret_buffer
                    ? (long long)node->ret_buffer->offset
                    : alloc_decimal_temp(vm, dst->size);
                emit_lea3(vm, REG_A0, dst_offset);
                if (is_flonum(src)) {
                    // binary float/double -> decimal: bit-reinterpret via
                    // FR2R/FR2R_F32 (float reg's raw bits -> int reg), then
                    // DFROMBITS. FR2R_F32 for an f32 source packs just the
                    // 32-bit float pattern (not a misread of the full 64-bit
                    // double pattern FReg would otherwise hold it as).
                    int r_tmp = alloc_temp_reg();
                    gen_expr(vm, node->lhs, r_tmp); // src in a float reg
                    emit_rr(vm, src->kind == TY_FLOAT ? FR2R_F32 : FR2R,
                            REG_A1, r_tmp);
                    free_temp_reg(r_tmp);
                    emit_lea3(vm, REG_A0, dst_offset);
                    emit_li3(vm, REG_A2, dec_width_code(dst));
                    emit_li3(vm, REG_A3, src->kind == TY_FLOAT ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DFROMBITS);
                } else {
                    // int -> decimal
                    gen_expr(vm, node->lhs, REG_A1);
                    emit_lea3(vm, REG_A0, dst_offset); // REG_A1 may share REG_A0's slot; reload
                    emit_li3(vm, REG_A2, dec_width_code(dst));
                    emit_li3(vm, REG_A3, src->is_unsigned ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DFROMI);
                }
                emit_lea3(vm, dest_reg, dst_offset);
                return;
            } else if (!is_decimal(dst) && is_decimal(src)) {
                // dest_reg holds the source address directly (#838): no
                // fresh temp is held across anything here, matching the
                // O(1)-per-level discipline the binop branches use, so a
                // decimal cast nested in a deep operand chain costs no
                // extra register. dest_reg is written last in every arm
                // below (result overwrites the address it started with).
                // Exception: dest_reg may be REG_ZERO (discarded-value
                // statement, e.g. `(void)(_Decimal64)d;`) -- REG_ZERO always
                // reads back 0, so staging the source address through it
                // would hand DTOI/DTOBITS/DCMP a null pointer. Use a fresh
                // temp in that case instead.
                int r_src = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
                gen_expr(vm, node->lhs, r_src); // address of decimal value
                // node->lhs may be a call (e.g. a decimal-returning FFI
                // function) whose CALLF resets the temp-reg bitmap. The
                // TY_BOOL arm below allocates r_zero before r_src is fully
                // consumed -- re-mark r_src used so that alloc can't hand
                // out the same register out from under it.
                mark_temp_reg_used(r_src);
                if (is_flonum(dst)) {
                    emit_mov3(vm, REG_A0, r_src);
                    emit_li3(vm, REG_A1, dec_width_code(src));
                    emit_li3(vm, REG_A2, dst->kind == TY_FLOAT ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DTOBITS); // A0 = raw f32/f64 bits
                    // R2FR reinterprets the full 64 bits of A0 as a double
                    // bit pattern -- wrong for an f32 destination, where
                    // cccc_dec_to_bin packed only a 32-bit float pattern
                    // into the low half of A0 (upper bits zero). R2FR_F32
                    // is the f32-specific counterpart that does this
                    // correctly (same convention I2F3_F32/FR2R_F32 use
                    // elsewhere for float-reg bit transfers).
                    emit_rr(vm, dst->kind == TY_FLOAT ? R2FR_F32 : R2FR,
                            dest_reg, REG_A0);
                } else if (dst->kind == TY_BOOL) {
                    // (_Bool) is a truthiness test (nonzero -> 1): DCMP
                    // against a zero literal of src's width, same test as
                    // gen_cond_expr's decimal branch, but reusing r_src
                    // (already evaluated above) instead of re-evaluating
                    // node->lhs, which could duplicate side effects.
                    int w = dec_width_code(src);
                    unsigned char zero_bits[16] = {0};
                    long long zoff = vm->data_ptr - vm->data_seg;
                    zoff = (zoff + (src->align - 1)) & ~(long long)(src->align - 1);
                    vm->data_ptr = vm->data_seg + zoff;
                    check_data_capacity(vm, zoff + src->size);
                    if (!cccc_dec_encode_literal("0", w, zero_bits))
                        error_tok(vm, node->tok,
                                  "_Decimal requires a build with CCCC_HAS_DECIMAL=1");
                    memcpy(vm->data_ptr, zero_bits, (size_t)src->size);
                    vm->data_ptr += src->size;

                    int r_zero = alloc_temp_reg();
                    emit_lda3(vm, r_zero, zoff);
                    emit_mov3(vm, REG_A0, r_src);
                    emit_mov3(vm, REG_A1, r_zero);
                    emit_li3(vm, REG_A2, w);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DCMP);
                    int tmp = alloc_temp_reg();
                    emit_li3(vm, tmp, 0);
                    emit_rrr(vm, SNE3, dest_reg, REG_A0, tmp);
                    free_temp_reg(tmp);
                    free_temp_reg(r_zero);
                } else {
                    // decimal -> int (truncating, C semantics)
                    emit_mov3(vm, REG_A0, r_src);
                    emit_li3(vm, REG_A1, dec_width_code(src));
                    emit_li3(vm, REG_A2, dst->is_unsigned ? 1 : 0);
                    if (vm->flags & CCCC_POINTER_CHECKS)
                        emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_wide_op(vm, DTOI);
                    emit_mov3(vm, dest_reg, REG_A0);
                    if (dst->kind == TY_CHAR)
                        emit_rr(vm, dst->is_unsigned ? ZX1 : SX1, dest_reg, dest_reg);
                    else if (dst->kind == TY_SHORT)
                        emit_rr(vm, dst->is_unsigned ? ZX2 : SX2, dest_reg, dest_reg);
                    else if (dst->kind == TY_INT)
                        emit_rr(vm, dst->is_unsigned ? ZX4 : SX4, dest_reg, dest_reg);
                    else if (dst->kind == TY_BITINT && !is_wide_bitint(dst))
                        emit_bitint_trunc(vm, dst, dest_reg);
                }
                if (r_src != dest_reg)
                    free_temp_reg(r_src);
                return;
            } else if (is_decimal(src) && is_decimal(dst)) {
                // Same dest_reg discipline (and REG_ZERO exception) as the
                // decimal->non-decimal arm above -- with one more wrinkle:
                // a fixed decimal FFI/native argument in register position
                // evaluates its expression with dest_reg == REG_A0+i
                // directly (see the "generic branch" callers of gen_expr
                // with REG_A0+int_arg_idx for a decimal arg). So r_src can
                // legitimately alias REG_A0 here. emit_mov3(REG_A1, r_src)
                // MUST run before emit_lea3(REG_A0, dst_offset) clobbers it
                // -- reading r_src first makes the sequence correct
                // regardless of which register it aliases.
                int r_src = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
                gen_expr(vm, node->lhs, r_src);
                // node->lhs may be a call; re-mark r_src used in case its
                // CALLF reset the temp-reg bitmap (matches the cast arm
                // above -- alloc_decimal_temp itself never allocates a
                // temp reg, but this keeps the two arms' discipline
                // identical and independent of that implementation detail).
                mark_temp_reg_used(r_src);
                long long dst_offset = node->ret_buffer
                    ? (long long)node->ret_buffer->offset
                    : alloc_decimal_temp(vm, dst->size);
                emit_mov3(vm, REG_A1, r_src);
                emit_lea3(vm, REG_A0, dst_offset);
                emit_li3(vm, REG_A2, dec_width_code(dst));
                emit_li3(vm, REG_A3, dec_width_code(src));
                if (vm->flags & CCCC_POINTER_CHECKS) {
                    emit_rr(vm, CHKP3, REG_A0, 0);
                    emit_rr(vm, CHKP3, REG_A1, 0);
                }
                emit_wide_op(vm, DCVT);
                emit_lea3(vm, dest_reg, dst_offset);
                if (r_src != dest_reg)
                    free_temp_reg(r_src);
                return;
            }
        }
        gen_expr(vm, node->lhs, dest_reg);
        // Add type conversion if needed
        if (is_flonum(node->ty) && !is_flonum(node->lhs->ty)) {
            // int -> float
            emit_rr(vm,
                    fop_for_type(node->ty,
                                 is_u64_int(node->lhs->ty) ? U2F3 : I2F3),
                    dest_reg, dest_reg);
        } else if (!is_flonum(node->ty) && is_flonum(node->lhs->ty)) {
            // float -> int
            emit_rr(vm,
                    fop_for_type(node->lhs->ty,
                                 is_u64_int(node->ty) ? F2U3 : F2I3),
                    dest_reg, dest_reg);
        } else if (node->ty->kind == TY_FLOAT &&
                   node->lhs->ty->kind != TY_FLOAT) {
            emit_fround_f32(vm, dest_reg, dest_reg);
        } else if (!is_flonum(node->ty) && !is_flonum(node->lhs->ty)) {
            // Integer conversion - handle truncation/extension
            if (node->ty->kind == TY_CHAR) {
                emit_rr(vm, node->ty->is_unsigned ? ZX1 : SX1, dest_reg,
                        dest_reg);
            } else if (node->ty->kind == TY_SHORT) {
                emit_rr(vm, node->ty->is_unsigned ? ZX2 : SX2, dest_reg,
                        dest_reg);
            } else if (node->ty->kind == TY_INT) {
                emit_rr(vm, node->ty->is_unsigned ? ZX4 : SX4, dest_reg,
                        dest_reg);
            } else if (node->ty->kind == TY_BOOL) {
                emit_rrr(vm, SNE3, dest_reg, dest_reg,
                         REG_ZERO); // dest_reg = (dest_reg != 0)
            } else if (node->ty->kind == TY_BITINT) {
                // Container sign/zero-extend then bit-precise truncate
                if (node->ty->size == 1)
                    emit_rr(vm, node->ty->is_unsigned ? ZX1 : SX1, dest_reg, dest_reg);
                else if (node->ty->size == 2)
                    emit_rr(vm, node->ty->is_unsigned ? ZX2 : SX2, dest_reg, dest_reg);
                else if (node->ty->size == 4)
                    emit_rr(vm, node->ty->is_unsigned ? ZX4 : SX4, dest_reg, dest_reg);
                emit_bitint_trunc(vm, node->ty, dest_reg);
            }
        }
        return;

    case ND_FUNCALL: {
        // Invalidate the restrict cache up front for every call, not just the
        // general CALL/CALLN/CALLF path below. Several intrinsics (malloc/
        // free/calloc/realloc/... under CCCC_VM_HEAP, setjmp/longjmp/signal/
        // raise/dlopen/dlsym/dlclose/dlerror) lower to a dedicated opcode via
        // an early `return` that never reaches the general path's invalidate,
        // leaving stale cache entries after e.g. free() (#754). The general
        // path below still invalidates again *after* the call: this call's
        // own argument expressions run after this point and may fill a cache
        // entry (e.g. f(*p) as the first access to *p), which must not
        // survive the call it was evaluated for. Over-invalidating only costs
        // cache throughput, never correctness.
        //
        // Residual gap, not fixed here: an intrinsic's *own* argument
        // expression can itself fill a cache entry (e.g. realloc(p, *p) --
        // *p is read as the size argument), and that intrinsic's early
        // `return` below skips any invalidate after it runs, same as before
        // this fix. Unlike the general-path case above, there is no
        // "after this call" invalidate to add for those branches without
        // touching every intrinsic's early return individually. Any
        // subsequent call (of any kind) still invalidates via its own
        // top-of-case entry into this invalidate, so the gap is narrow:
        // an intrinsic argument fill immediately followed by a stale access
        // with no further call in between. restrict_cache_handle_deref's
        // hit-site checks (#750) still catch anything CHKP3/CHKT3 would
        // catch on a real load; only a silent value divergence under no
        // safety flags at all would slip through. Tracked as a follow-up.
        restrict_cache_invalidate_all(vm);

        // Capture and clear the tail-call flag immediately so that argument
        // sub-calls (e.g. return f(g(x))) and inlined bodies never see it.
        // The captured value is used below when deciding CALL vs CALLT.
        bool is_tail = vm->compiler.emitting_tail_call;
        vm->compiler.emitting_tail_call = false;

        if (is_complex(node->ty))
            error_tok(vm, node->tok,
                      "complex function return ABI is not supported");
        for (Node *arg = node->args; arg; arg = arg->next)
            if (is_complex(arg->ty))
                error_tok(vm, arg->tok,
                          "complex function argument ABI is not supported");

        // Check if this is a builtin alloca call (used for VLAs)
        if (node->lhs->kind == ND_VAR &&
            node->lhs->var->is_builtin_alloca) {
            // Special handling for alloca: uses MALC opcode
            if (!node->args) {
                error_tok(vm, node->tok, "alloca requires a size argument");
            }
            // Evaluate size argument into REG_A0 (MALC reads from REG_A0)
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);
            emit(vm, MALC); // Size in REG_A0, returns pointer in REG_A0
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
            return;
        }

        // Check if this is VM-managed signal() builtin
        if (is_extern_func_name(node->lhs, "signal") ||
            (node->lhs->kind == ND_VAR &&
             node->lhs->var == vm->compiler.builtin_signal)) {
            if (!node->args || !node->args->next) {
                error_tok(vm, node->tok, "signal requires sig and handler arguments");
            }
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);             // sig
            gen_expr(vm, node->args->next, REG_A1);       // handler
            emit(vm, VSIGNAL);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        // Check if this is VM-managed raise() builtin
        if (is_extern_func_name(node->lhs, "raise") ||
            (node->lhs->kind == ND_VAR &&
             node->lhs->var == vm->compiler.builtin_raise)) {
            if (!node->args) {
                error_tok(vm, node->tok, "raise requires a sig argument");
            }
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0); // sig
            emit(vm, VRAISE);
            /* VRAISE may jump into a VM handler; when the handler returns it
               lands here. Normalise REG_A0 = 0 so raise() always returns 0. */
            emit_li3(vm, REG_A0, 0);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        // Check if this is setjmp builtin (or its POSIX _setjmp alias)
        if (node->lhs->kind == ND_VAR &&
            (node->lhs->var == vm->compiler.builtin_setjmp ||
             node->lhs->var == vm->compiler.builtin__setjmp)) {
            if (!node->args) {
                error_tok(vm, node->tok, "setjmp requires a jmp_buf argument");
            }
            // Evaluate jmp_buf address into REG_A0 (SETJMP reads from REG_A0)
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);
            emit(vm, SETJMP); // Save context, returns 0 in REG_A0
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
            return;
        }

        // Check if this is longjmp builtin (or its POSIX _longjmp alias)
        if (node->lhs->kind == ND_VAR &&
            (node->lhs->var == vm->compiler.builtin_longjmp ||
             node->lhs->var == vm->compiler.builtin__longjmp)) {
            if (!node->args || !node->args->next) {
                error_tok(vm, node->tok,
                          "longjmp requires jmp_buf and int arguments");
            }
            // LONGJMP: env in REG_A0, val in REG_A1
            reset_temp_regs();
            gen_expr(vm, node->args, REG_A0);       // env (jmp_buf address)
            gen_expr(vm, node->args->next, REG_A1); // val
            emit(vm, LONGJMP); // Restore context and jump (does not return)
            return;
        }

        if (is_extern_func_name(node->lhs, "dlopen")) {
            reset_temp_regs();
            if (!node->args || !node->args->next)
                error_tok(vm, node->tok, "dlopen requires path and mode arguments");
            gen_expr(vm, node->args, REG_A0);
            gen_expr(vm, node->args->next, REG_A1);
            emit(vm, DLOPEN);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        if (is_extern_func_name(node->lhs, "dlsym")) {
            reset_temp_regs();
            if (!node->args || !node->args->next)
                error_tok(vm, node->tok, "dlsym requires handle and symbol arguments");
            gen_expr(vm, node->args, REG_A0);
            gen_expr(vm, node->args->next, REG_A1);
            emit(vm, DLSYM);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        if (is_extern_func_name(node->lhs, "dlclose")) {
            reset_temp_regs();
            if (!node->args)
                error_tok(vm, node->tok, "dlclose requires a handle argument");
            gen_expr(vm, node->args, REG_A0);
            emit(vm, DLCLOSE);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        if (is_extern_func_name(node->lhs, "dlerror")) {
            reset_temp_regs();
            emit(vm, DLERROR);
            if (dest_reg != REG_A0)
                emit_mov3(vm, dest_reg, REG_A0);
            return;
        }

        // When CCCC_VM_HEAP is set, route malloc/free/calloc/realloc through VM
        // heap opcodes instead of system allocators via FFI.
        if (vm->flags & CCCC_VM_HEAP) {
            if (is_extern_func_name(node->lhs, "malloc")) {
                if (!node->args)
                    error_tok(vm, node->tok, "malloc requires a size argument");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                emit(vm, MALC);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            if (is_extern_func_name(node->lhs, "free")) {
                if (!node->args)
                    error_tok(vm, node->tok, "free requires a pointer argument");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                emit(vm, MFRE);
                return;
            }
            // free_sized/free_aligned_sized (C23) route through the same MFRE
            // opcode as free: MFRE derives the real size from the AllocHeader
            // and already falls back to the host free() for non-VM-heap
            // pointers (ops.c op_MFRE_fn), so the size/alignment arguments
            // are only evaluated for side effects and otherwise discarded.
            // Without this, a VM-heap malloc() paired with free_sized() would
            // hand a VM-heap pointer straight to the host's free_sized() via
            // FFI and abort (#665 fallout: VM heap is on by default now).
            if (is_extern_func_name(node->lhs, "free_sized")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "free_sized requires ptr and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                emit(vm, MFRE);
                return;
            }
            if (is_extern_func_name(node->lhs, "free_aligned_sized")) {
                if (!node->args || !node->args->next || !node->args->next->next)
                    error_tok(vm, node->tok,
                              "free_aligned_sized requires ptr, alignment, and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                gen_expr(vm, node->args->next->next, REG_A2);
                emit(vm, MFRE);
                return;
            }
            if (is_extern_func_name(node->lhs, "calloc")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "calloc requires nmemb and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                emit(vm, CALC);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            if (is_extern_func_name(node->lhs, "realloc")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "realloc requires ptr and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                emit(vm, REALC);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            // reallocarray (#699): full parity with the rest of the malloc
            // family via the VM heap's overflow-checked REALCA opcode,
            // instead of falling through to a generic FFI call (which would
            // both skip heap-safety tracking and be unavailable on hosts
            // without a native reallocarray, e.g. this macOS SDK).
            if (is_extern_func_name(node->lhs, "reallocarray")) {
                if (!node->args || !node->args->next || !node->args->next->next)
                    error_tok(vm, node->tok, "reallocarray requires ptr, nmemb, and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);
                gen_expr(vm, node->args->next, REG_A1);
                gen_expr(vm, node->args->next->next, REG_A2);
                emit(vm, REALCA);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            // aligned_alloc/posix_memalign (C11/C23) route through the VM
            // heap's alignment-aware bump allocator (MALCA/PMEMA) so their
            // allocations get an AllocHeader and full heap safety coverage
            // (canaries, bounds/UAF/type checks, leak detection, tagging),
            // mirroring malloc/calloc/realloc/free above. Before this, they
            // fell through to the host allocator via FFI: not a crash (MFRE
            // already falls back to host free() for non-VM-heap pointers),
            // but heap safety silently didn't apply to them (#668).
            if (is_extern_func_name(node->lhs, "aligned_alloc")) {
                if (!node->args || !node->args->next)
                    error_tok(vm, node->tok, "aligned_alloc requires alignment and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args->next, REG_A0); // size
                gen_expr(vm, node->args, REG_A1);       // alignment
                emit(vm, MALCA);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
            if (is_extern_func_name(node->lhs, "posix_memalign")) {
                if (!node->args || !node->args->next || !node->args->next->next)
                    error_tok(vm, node->tok, "posix_memalign requires memptr, alignment, and size arguments");
                reset_temp_regs();
                gen_expr(vm, node->args, REG_A0);             // memptr
                gen_expr(vm, node->args->next, REG_A1);       // alignment
                gen_expr(vm, node->args->next->next, REG_A2); // size
                emit(vm, PMEMA);
                if (dest_reg != REG_A0)
                    emit_mov3(vm, dest_reg, REG_A0);
                return;
            }
        }

        // Check for FFI call - foreign functions use register-based calling
        // convention with operand-based metadata (ffi_idx, nargs,
        // double_arg_mask)
        int ffi_idx = -1;
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            ffi_idx = ffi_index_for_callee(vm, node->lhs->var);
        }

        if (ffi_idx >= 0) {
            // FFI call: args are stored as source-order 64-bit slots.
            // Slots 0-7 use REG_A0-A7; slots 8+ are pushed on the VM stack.
            reset_temp_regs();

            // Vector-by-value through the native FFI marshalling path isn't
            // wired up (#714 only covers the internal CCCC call ABI) --
            // reject with a clear diagnostic rather than silently
            // mis-marshalling a vregs[] value as a 64-bit slot.
            if (is_vector(node->ty))
                error_tok(vm, node->tok,
                          "vector return values through FFI calls are not "
                          "supported");
            // #402/#830: same rationale as the vector rejection above --
            // libffi has no decimal ffi_type, so a _Decimal return through
            // FFI would be mis-marshalled as a 64-bit int/double slot rather
            // than rejected. Remains out of scope here; see #830.
            if (is_decimal(node->ty))
                error_tok(vm, node->tok,
                          "_Decimal return values through FFI calls are not "
                          "supported");

            // #829: a decimal argument in the *variadic tail* of an FFI call
            // (our own cccc_printf/cccc_fprintf/... engine, which expects a
            // pointer for %Hf/%Df/%DDf) is passed by pointer -- see
            // gen_decimal_arg_ptr. A FIXED decimal FFI parameter has no such
            // convention to lean on (no libffi decimal ffi_type exists), so
            // it stays rejected; that by-value FFI case is #830's scope.
            bool ffi_is_variadic_call = node->func_ty && node->func_ty->is_variadic;
            int ffi_fixed_param_count = 0;
            if (ffi_is_variadic_call) {
                for (Type *p = node->func_ty->params; p; p = p->next)
                    ffi_fixed_param_count++;
            }

            // Count arguments and compute double_arg_mask/float_arg_mask
            int nargs = 0;
            uint64_t double_arg_mask = 0;
            uint64_t float_arg_mask = 0;
            for (Node *arg = node->args; arg; arg = arg->next) {
                bool ffi_arg_is_vararg = ffi_is_variadic_call &&
                                         nargs >= ffi_fixed_param_count;
                if (is_flonum(arg->ty)) {
                    if (nargs >= 64)
                        error_tok(vm, arg->tok,
                                  "too many floating-point FFI arguments");
                    if (arg->ty->kind == TY_FLOAT)
                        float_arg_mask |= (1ULL << nargs);
                    else
                        double_arg_mask |= (1ULL << nargs);
                } else if (is_vector(arg->ty)) {
                    error_tok(vm, arg->tok,
                              "vector arguments through FFI calls are not "
                              "supported");
                } else if (is_decimal(arg->ty) && !ffi_arg_is_vararg) {
                    error_tok(vm, arg->tok,
                              "_Decimal arguments through FFI calls are not "
                              "supported");
                }
                nargs++;
            }

            // Collect args into array for indexed access
            Node **arg_array = NULL;
            if (nargs > 0) {
                arg_array = calloc(nargs, sizeof(Node *));
                if (!arg_array)
                    error("out of memory");
                int idx = 0;
                for (Node *a = node->args; a; a = a->next) {
                    arg_array[idx++] = a;
                }
            }

            int num_stack_args = (nargs > 8) ? (nargs - 8) : 0;

            // Push overflow args (8+) right-to-left so vm->sp[0] is arg 8.
            for (int i = nargs - 1; i >= 8; i--) {
                Node *arg = arg_array[i];
                if (is_zero_size_aggregate(arg->ty)) {
                    gen_zero_size_arg(vm, arg, REG_T0);
                } else if (is_flonum(arg->ty)) {
                    int fs = gen_flonum_arg_to_scratch(vm, arg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_T0, fs);
                    free_temp_reg(fs);
                } else if (is_decimal(arg->ty)) {
                    // #829: only reachable here for a variadic-tail decimal
                    // arg -- a fixed one already errored out above.
                    gen_decimal_arg_ptr(vm, arg, REG_T0);
                } else {
                    gen_expr(vm, arg, REG_T0);
                }
                emit_psh3(vm, REG_T0);
            }

            // Check which register arguments contain function calls (to handle
            // clobbering). Nested FFI calls will clobber REG_A0-A7.
            bool *arg_has_call = calloc(nargs > 0 ? nargs : 1, sizeof(bool));
            if (!arg_has_call)
                error("out of memory");
            for (int i = 0; i < nargs; i++) {
                arg_has_call[i] = contains_funcall(arg_array[i]);
            }

            // Evaluate source slots 0-7 into REG_A0-A7.
            // CRITICAL: If arg[i] contains a function call, it will clobber
            // REG_A0-A7. We must save any previous args before evaluating such
            // an arg.
            int saved_reg_count = 0;
            for (int i = 0; i < nargs && i < 8; i++) {
                Node *arg = arg_array[i];

                // Before evaluating this arg, check if it contains a function
                // call. If so, save all previously-evaluated arg registers.
                if (arg_has_call[i] && i > 0) {
                    for (int j = i - 1; j >= 0; j--) {
                        emit_psh3(vm, REG_A0 + j);
                    }
                    saved_reg_count = i;
                }

                if (is_zero_size_aggregate(arg->ty)) {
                    gen_zero_size_arg(vm, arg, REG_A0 + i);
                } else if (is_flonum(arg->ty)) {
                    int fs = gen_flonum_arg_to_scratch(vm, arg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_A0 + i, fs);
                    free_temp_reg(fs);
                } else if (is_decimal(arg->ty)) {
                    // #829: only reachable here for a variadic-tail decimal
                    // arg -- a fixed one already errored out above.
                    gen_decimal_arg_ptr(vm, arg, REG_A0 + i);
                } else {
                    gen_expr(vm, arg, REG_A0 + i);
                }

                // After evaluating this arg, if we saved previous regs, restore
                // them now.
                if (arg_has_call[i] && saved_reg_count > 0) {
                    for (int j = 0; j < saved_reg_count; j++) {
                        emit_pop3(vm, REG_A0 + j);
                    }
                    saved_reg_count = 0;
                }
            }

            free(arg_has_call);
            if (arg_array)
                free(arg_array);

            // Emit CALLF (or skip if pure/const and result unused).
            Obj *ffi_fn = (node->lhs->kind == ND_VAR) ? node->lhs->var : NULL;
            bool skip_dead_callf = vm->compiler.opt_level >= 1 &&
                                   dest_reg == REG_ZERO &&
                                   ffi_fn &&
                                   (ffi_fn->is_pure || ffi_fn->is_func_const);
            if (!skip_dead_callf) {
                emit(vm, CALLF);
                emit_word(vm, ffi_idx);
                emit_word(vm, nargs);
                emit_i64(vm, (long long)double_arg_mask);
                emit_i64(vm, (long long)float_arg_mask);
            }

            if (num_stack_args > 0) {
                emit_with_arg(vm, ADJ, num_stack_args);
            }

            // Noreturn functions never return — trap if execution continues
            if (node->func_ty->is_noreturn) {
                emit(vm, BTRAP);
                return;
            }

            // Reset temp regs after call; function may have modified *restrict_params.
            // Also invalidate the restrict cache again: the up-front invalidate at
            // the top of ND_FUNCALL runs before this call's own argument
            // expressions are evaluated, so an argument access that fills a cache
            // entry (e.g. f(*p) as the first access to *p) would otherwise survive
            // past the call it was evaluated for (#754).
            restrict_cache_invalidate_all(vm);
            reset_temp_regs();

            // Result in REG_A0/FREG_A0
            if (is_flonum(node->ty)) {
                if (dest_reg != FREG_A0) {
                    emit_fmov3(vm, dest_reg, FREG_A0);
                }
            } else {
                if (dest_reg != REG_A0) {
                    emit_mov3(vm, dest_reg, REG_A0);
                }
            }
            return;
        }

        // Static inline inlining opportunity
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            Obj *callee = node->lhs->var;
            if (callee->is_inline && callee->is_static &&
                callee->body && callee->body->kind == ND_BLOCK) {
                Node *body_stmt = callee->body->body;

                // Fast path: single-return inlining (no exit label)
                if (body_stmt && !body_stmt->next &&
                    body_stmt->kind == ND_RETURN && body_stmt->lhs &&
                    !contains_self_call(body_stmt->lhs, callee)) {
                    Type *ret_ty = body_stmt->lhs->ty;
                    if (!(ret_ty && (ret_ty->kind == TY_STRUCT ||
                                     ret_ty->kind == TY_UNION ||
                                     is_wide_bitint(ret_ty)))) {
                        reset_temp_regs();
                        Node *inlined = clone_subst(vm, body_stmt->lhs,
                                                    callee->params, node->args);
                        gen_expr(vm, inlined, dest_reg);
                        return;
                    }
                }

                // Multi-statement inlining (gated by opt_level >= 2)
                if (vm->compiler.opt_level >= 2 &&
                    vm->compiler.inline_node_limit > 0 &&
                    !contains_self_call(callee->body, callee) &&
                    !contains_unsupported_control_flow(callee->body)) {
                    Type *ret_ty = callee->ty->return_ty;
                    bool void_ret = !ret_ty || ret_ty->kind == TY_VOID;
                    if ((void_ret || !(ret_ty->kind == TY_STRUCT ||
                                       ret_ty->kind == TY_UNION ||
                                       is_wide_bitint(ret_ty))) &&
                        count_ast_nodes(callee->body) <=
                        vm->compiler.inline_node_limit) {
                        reset_temp_regs();

                        // Clone entire function body with parameter substitution
                        Node *inlined_body = clone_subst(vm, callee->body,
                                                         callee->params,
                                                         node->args);

                        // Remap callee locals into caller's frame
                        int nlocals = 0;
                        Obj **orig_locals = NULL;
                        Obj **new_locals = NULL;
                        for (Obj *v = callee->locals; v; v = v->next) {
                            if (v->is_param || v == callee->va_area ||
                                v == callee->alloca_bottom)
                                continue;
                            nlocals++;
                        }
                        if (nlocals > 0) {
                            orig_locals = calloc(nlocals, sizeof(Obj *));
                            new_locals = calloc(nlocals, sizeof(Obj *));
                            int idx = 0;
                            for (Obj *v = callee->locals; v; v = v->next) {
                                if (v->is_param || v == callee->va_area ||
                                    v == callee->alloca_bottom)
                                    continue;
                                orig_locals[idx] = v;
                                Obj *nv = arena_alloc(&vm->compiler.parser_arena,
                                                       sizeof(Obj));
                                memset(nv, 0, sizeof(Obj));
                                *nv = *v;
                                char *name = arena_format(vm, "%s_inline%d",
                                                          v->name,
                                                          vm->compiler.unique_name_counter++);
                                nv->name = name;
                                nv->display_name = name;
                                int slots = var_stack_slots(v);
                                vm->compiler.ent3_extra_stack += slots;
                                nv->offset = -(vm->compiler.ent3_base_stack +
                                               vm->compiler.ent3_extra_stack);
                                new_locals[idx++] = nv;
                            }
                            replace_locals_in_ast(inlined_body, orig_locals,
                                                  new_locals, nlocals);
                        }

                        // Generate unique exit label name
                        char *exit_name = arena_format(vm, ".Linline_exit_%d",
                                                       vm->compiler.unique_name_counter++);

                        // Set inlining context
                        vm->compiler.inline_exit_name = exit_name;
                        vm->compiler.inline_result_reg = void_ret ? REG_ZERO : dest_reg;

                        // Generate inlined body
                        gen_stmt(vm, inlined_body);

                        // Define exit label (JMP targets from inlined returns)
                        define_label(vm, exit_name);

                        // Clear inlining context
                        vm->compiler.inline_exit_name = NULL;

                        // Free temporary arrays
                        if (orig_locals) free(orig_locals);
                        if (new_locals) free(new_locals);

                        return;
                    }
                }
            }
        }

        // Internal function call: evaluate arguments
        // For variadic functions, varargs (including doubles) go to integer
        // registers so ENT3 can spill them to stack for va_arg to read

        // Check if we're calling a nested function - need to pass static link
        bool calling_nested =
            (node->lhs->kind == ND_VAR && node->lhs->var->is_function &&
             node->lhs->var->is_nested);
        int static_link_offset =
            calling_nested ? 1 : 0; // Reserve A0 for static link

        bool is_variadic_call = node->func_ty && node->func_ty->is_variadic;
        int fixed_param_count = 0;
        if (is_variadic_call) {
            for (Type *p = node->func_ty->params; p; p = p->next) {
                fixed_param_count++;
            }
        }

        // Count total args and collect into array for indexed access
        int nargs = 0;
        uint64_t call_double_arg_mask = 0;
        uint64_t call_float_arg_mask = 0;
        for (Node *a = node->args; a; a = a->next) {
            if (is_flonum(a->ty)) {
                if (nargs >= 64)
                    error_tok(vm, a->tok,
                              "too many floating-point native-call arguments");
                // Variadic tail args are promoted float->double by the parser,
                // so a TY_FLOAT arg here is always a fixed parameter.
                if (a->ty->kind == TY_FLOAT)
                    call_float_arg_mask |= (1ULL << nargs);
                else
                    call_double_arg_mask |= (1ULL << nargs);
            }
            nargs++;
        }

        Node **arg_array = NULL;
        if (nargs > 0) {
            arg_array = calloc(nargs, sizeof(Node *));
            if (!arg_array)
                error("out of memory");
            int idx = 0;
            for (Node *a = node->args; a; a = a->next) {
                arg_array[idx++] = a;
            }
        }

        // Calculate how many args go on stack (args 8+)
        int num_stack_args = (nargs > 8) ? (nargs - 8) : 0;

        // Push overflow args (8+) right-to-left BEFORE register args
        // Stack grows downward, so push last arg first
        // After all pushes and CALL, these will be at bp[+2], bp[+3], etc.
        if (num_stack_args > 0) {
            for (int j = nargs - 1; j >= 8; j--) {
                Node *arg = arg_array[j];
                if (is_zero_size_aggregate(arg->ty)) {
                    gen_zero_size_arg(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                } else if (is_flonum(arg->ty)) {
                    // Float arg: evaluate to a temp-numbered float scratch (not
                    // FREG_A0 -- see gen_flonum_arg_to_scratch, #712), move bits
                    // to int reg, push
                    int freg = gen_flonum_arg_to_scratch(vm, arg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_T0,
                            freg); // Move bits to REG_T0
                    free_temp_reg(freg);
                    emit_psh3(vm, REG_T0);
                } else if (is_vector(arg->ty)) {
                    // Vector arg: copy value to scratch slot, push its address
                    // like a struct-by-value arg (#714). A variadic tail arg
                    // (#721) works the same way -- it lands in exactly one
                    // 8-byte stack slot holding the scratch pointer, which
                    // <stdarg.h>'s va_arg dereferences via
                    // __builtin_classify_type.
                    gen_vector_arg_ptr(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                } else if (is_decimal(arg->ty) &&
                          is_variadic_call && j >= fixed_param_count) {
                    // Decimal variadic tail arg (#829), stack-passed: same
                    // padded-scratch-copy rationale as gen_decimal_arg_ptr's
                    // comment below. A fixed decimal param at a stack
                    // position instead falls to the generic branch, matching
                    // #402's existing address-passthrough ABI.
                    gen_decimal_arg_ptr(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                } else {
                    // Integer/pointer arg: evaluate to temp reg, push
                    gen_expr(vm, arg, REG_T0);
                    emit_psh3(vm, REG_T0);
                }
            }
        }

        // Check which arguments contain function calls (to handle register
        // clobbering)
        bool *arg_has_call = calloc(nargs, sizeof(bool));
        if (!arg_has_call && nargs > 0)
            error("out of memory");
        for (int i = 0; i < nargs; i++) {
            arg_has_call[i] = contains_funcall(arg_array[i]);
        }

        // Now evaluate first 8 args into registers
        // CRITICAL: If arg[i] contains a function call, it will clobber
        // REG_A0-A7. We must save any previous args before evaluating such an
        // arg. For nested function calls, reserve A0 for static link
        int int_arg_idx = static_link_offset; // Start at 1 if calling nested
                                              // (A0 = static_link)
        int float_arg_idx = 0;
        int saved_int_count = 0;   // How many int regs we saved
        int saved_float_count = 0; // How many float regs we saved
        bool float_arg_is_f32[8] = {0};

        for (int i = 0; i < nargs && i < 8; i++) {
            Node *arg = arg_array[i];
            bool is_vararg = is_variadic_call && (i >= fixed_param_count);

            // Before evaluating this arg, check if it contains a function call.
            // If so, save all previously-evaluated arg registers to the stack.
            if (arg_has_call[i] && (int_arg_idx > 0 || float_arg_idx > 0)) {
                // Push int regs in reverse order (so we pop in correct order
                // later)
                for (int j = int_arg_idx - 1; j >= 0; j--) {
                    emit_psh3(vm, REG_A0 + j);
                }
                saved_int_count = int_arg_idx;

                // Push float regs: convert to int bits, push
                for (int j = float_arg_idx - 1; j >= 0; j--) {
                    emit_rr(vm, float_arg_is_f32[j] ? FR2R_F32 : FR2R,
                            REG_T0, FREG_A0 + j);
                    emit_psh3(vm, REG_T0);
                }
                saved_float_count = float_arg_idx;
            }

            if (is_zero_size_aggregate(arg->ty)) {
                if (int_arg_idx < 8) {
                    gen_zero_size_arg(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            } else if (is_flonum(arg->ty)) {
                if (is_vararg) {
                    // Variadic double: put in integer register (as bit pattern)
                    // ENT3 will spill REG_A* to stack; va_arg reads from stack
                    if (int_arg_idx < 8) {
                        // Generate double value into a temp-numbered float
                        // scratch (not FREG_A0 -- see gen_flonum_arg_to_scratch,
                        // #712), then move bits to int reg
                        int freg = gen_flonum_arg_to_scratch(vm, arg);
                        // Move double bits from freg to int reg (bit-pattern,
                        // not conversion)
                        emit_rr(vm, FR2R, REG_A0 + int_arg_idx, freg);
                        free_temp_reg(freg);
                        int_arg_idx++;
                    }
                } else {
                    // Fixed param double: evaluate into a temp-numbered float
                    // scratch (#712), then move into place with FMOV3 (writes
                    // only fregs[], can never clobber a live int arg register).
                    if (float_arg_idx < 8) {
                        int freg = gen_flonum_arg_to_scratch(vm, arg);
                        emit_fmov3(vm, FREG_A0 + float_arg_idx, freg);
                        free_temp_reg(freg);
                        float_arg_is_f32[float_arg_idx] =
                            arg->ty->kind == TY_FLOAT;
                        float_arg_idx++;
                    }
                }
            } else if (is_decimal(arg->ty) && is_vararg) {
                // Decimal variadic tail arg (#829, follow-up to #402's
                // "deferred" rejection): pass by pointer to a fresh,
                // width-independent scratch copy -- see gen_decimal_arg_ptr's
                // comment. A FIXED decimal param falls through to the
                // generic branch below instead, unchanged from #402:
                // gen_expr's address-based decimal representation already
                // gives the right by-address ABI with no extra copy needed.
                if (int_arg_idx < 8) {
                    gen_decimal_arg_ptr(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            } else if (is_vector(arg->ty)) {
                // Vector arg: pass by memory like a struct-by-value arg
                // (#714) -- copy the value to a scratch slot, pass its
                // address in the integer arg register. Works identically for
                // a variadic tail arg (#721): the pointer occupies exactly
                // one int arg slot, which ENT3 spills like any other, and
                // <stdarg.h>'s va_arg dereferences it via
                // __builtin_classify_type.
                if (int_arg_idx < 8) {
                    gen_vector_arg_ptr(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            } else {
                // Integer/pointer argument - always goes in integer register
                if (int_arg_idx < 8) {
                    gen_expr(vm, arg, REG_A0 + int_arg_idx);
                    int_arg_idx++;
                }
            }

            // After evaluating this arg, if we saved previous regs, restore
            // them now. This ensures all arg regs have correct values after
            // each step.
            if (arg_has_call[i] &&
                (saved_int_count > 0 || saved_float_count > 0)) {
                // Restore float regs (were pushed last, pop first)
                for (int j = 0; j < saved_float_count; j++) {
                    emit_pop3(vm, REG_T0);
                    emit_rr(vm, float_arg_is_f32[j] ? R2FR_F32 : R2FR,
                            FREG_A0 + j, REG_T0);
                }
                // Restore int regs
                for (int j = 0; j < saved_int_count; j++) {
                    emit_pop3(vm, REG_A0 + j);
                }
                saved_int_count = 0;
                saved_float_count = 0;
            }
        }

        free(arg_has_call);

        if (arg_array)
            free(arg_array);

        // For nested function calls, set up REG_A0 with static link
        // The static link is the callee's parent's frame pointer
        if (calling_nested) {
            Obj *callee = node->lhs->var;
            Obj *callee_parent = callee->parent_fn;
            Obj *current_fn = vm->compiler.current_fn;

            // Determine the static link value based on relationship
            // Static-link/bp passing for nested calls is compiler-internal
            // ABI plumbing, not a user-visible &local (#676): skip recording.
            if (callee_parent == current_fn) {
                // Calling our own nested function - pass our bp
                emit_lea3_internal(vm, REG_A0, 0); // LEA3 with offset 0 = current bp
            } else if (current_fn && current_fn->is_nested) {
                // We're nested and calling a sibling or parent's nested
                // function Walk our static chain to find callee's parent's bp
                Obj *static_link = find_static_link_var(current_fn);
                if (static_link) {
                    emit_lea3_internal(vm, REG_A0, static_link->offset);
                    emit_rr(vm, LDR_D, REG_A0, REG_A0);
                    // Walk chain if needed
                    for (Obj *fn = current_fn->parent_fn;
                         fn && fn != callee_parent; fn = fn->parent_fn) {
                        emit_addi3(vm, REG_A0, REG_A0,
                                   -8); // static_link offset
                        emit_rr(vm, LDR_D, REG_A0, REG_A0);
                    }
                } else {
                    // Fallback: use current bp
                    emit_lea3_internal(vm, REG_A0, 0);
                }
            } else {
                // Fallback: use current bp (shouldn't happen if parser is
                // correct)
                emit_lea3_internal(vm, REG_A0, 0);
            }
        }

        // Call function
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            Obj *fn = node->lhs->var;
            if (is_tail) {
                // Record callee; ND_RETURN emits CALLT + patch after cleanup.
                // If inlining/builtins fired earlier and we never reach here,
                // pending_tail_callee stays NULL and ND_RETURN falls back to LEV3.
                vm->compiler.pending_tail_callee = fn;
            } else {
                // Dead-call elimination: skip pure/const calls whose result is unused.
                // Arguments were already evaluated above, so their side effects still run.
                bool skip_dead_call = vm->compiler.opt_level >= 1 &&
                                      dest_reg == REG_ZERO &&
                                      (fn->is_pure || fn->is_func_const);
                if (!skip_dead_call) {
                    emit(vm, CALL);
                    Pc patch = emit_word_ptr(vm);
                    vm->text_seg[patch] = 0; // Will be patched later

                    // Record call patch location for later resolution
                    PATCH_GROW(vm, call_patches, num_call_patches, call_patches_cap);
                    vm->compiler.call_patches[vm->compiler.num_call_patches].location =
                        patch;
                    vm->compiler.call_patches[vm->compiler.num_call_patches].function =
                        fn;
                    vm->compiler.num_call_patches++;
                }
            }
        } else {
            // Indirect call - function pointer in register.
            // Evaluate the fn-ptr expression unconditionally (it may have
            // side effects, e.g. table[i++]).  Only skip the dispatch when
            // the callee's function type is annotated pure/const and the
            // result is unused.
            int r_fn = alloc_temp_reg();
            gen_expr(vm, node->lhs, r_fn);
            bool skip_dead_calln = vm->compiler.opt_level >= 1 &&
                                   dest_reg == REG_ZERO &&
                                   node->func_ty &&
                                   (node->func_ty->is_pure ||
                                    node->func_ty->is_func_const);
            if (!skip_dead_calln) {
                // Meta word (InstrWord = uint32_t) layout: bits 0-15 nargs,
                // bit 16 returns_double, bit 17 returns_float, bit 18
                // is_variadic, bits 19-31 fixed_param_count (13 bits, up to
                // 8191 fixed params) (#874/#875 -- lets op_CALLN_fn tell
                // fixed flonum params (FREG_A0+) from variadic-tail doubles
                // (bit-pattern in REG_A0+, matching the internal-call ABI
                // above) and select ffi_prep_cif_var for the callee).
                emit(vm, CALLN);
                emit_word(vm, ENCODE_R(r_fn));
                emit_word(vm, ((InstrWord)(node->ty->kind == TY_FLOAT ? 0
                                                : is_flonum(node->ty) ? 1 : 0) << 16) |
                                  ((InstrWord)(node->ty->kind == TY_FLOAT ? 1 : 0) << 17) |
                                  ((InstrWord)(is_variadic_call ? 1 : 0) << 18) |
                                  ((InstrWord)(fixed_param_count & 0x1FFF) << 19) |
                                  (InstrWord)(nargs & 0xFFFF));
                emit_i64(vm, (long long)call_double_arg_mask);
                emit_i64(vm, (long long)call_float_arg_mask);
            }
            free_temp_reg(r_fn);
        }

        // Clean up stack args pushed before the call
        if (num_stack_args > 0) {
            emit_with_arg(vm, ADJ, num_stack_args);
        }

        // Noreturn functions never return — trap if execution continues
        if (node->func_ty->is_noreturn) {
            emit(vm, BTRAP);
            return;
        }

        // Function calls clobber all temp registers (caller-saved)
        // Reset allocator so caller will recompute any addresses it needs.
        // Also invalidate the restrict cache again: see the comment on the
        // matching invalidate in the tail-call branch above (#754) -- this
        // call's own argument expressions may have filled a cache entry
        // after the up-front invalidate at the top of ND_FUNCALL ran.
        restrict_cache_invalidate_all(vm);
        reset_temp_regs();

        // Note: With runtime return buffer rotation (RETBUF opcode), chained
        // calls like f(g(), h()) automatically get different buffers for g()
        // and h()'s results. No caller-side copy is needed.

        // Result in REG_A0/FREG_A0
        if (is_flonum(node->ty)) {
            if (dest_reg != FREG_A0) {
                emit_fmov3(vm, dest_reg, FREG_A0);
            }
        } else if (is_vector(node->ty)) {
            // Vector return (#714): REG_A0 holds the RETBUF buffer address
            // (see ND_RETURN's vector branch) -- load the value out of it
            // into the destination vreg (width from the return type, #722).
            // Skip when the result is discarded (dest_reg == REG_ZERO):
            // unlike int/float write opcodes, VLDR has no built-in "writes to
            // REG_ZERO are discarded" guard (op_VLDR_fn, ops.c), so an
            // unconditional load here would write into vregs[0] instead of
            // being a no-op -- harmless in practice (index 0 is never handed
            // out by alloc_temp_reg) but skip it anyway rather than relying
            // on that.
            if (dest_reg != REG_ZERO) {
                if (vm->flags & CCCC_POINTER_CHECKS)
                    emit_rr(vm, CHKP3, REG_A0, 0);
                emit_rrs(vm, VLDR, dest_reg, REG_A0, node->ty->size);
            }
        } else {
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
        }
        return;
    }

    // ND_MEMZERO: zero-clear a local variable's storage region.
    // Emitted as the first operand of the ND_COMMA produced by lvar_initializer
    // for partial aggregate initialisers (e.g. `T arr[N] = {0}`).  The MSET
    // opcode mirrors MCPY: dest=REG_A0, count=REG_A2.
    //
    // __block variables: their stack slot (bp+offset) holds an 8-byte heap
    // pointer written by the function prologue; a blind MSET of var->ty->size
    // bytes at the slot would corrupt the pointer.  Instead, dereference the
    // slot to obtain the heap cell address and zero through that — mirroring
    // gen_addr's normal-local block path.  This ensures that `__block T arr[N]
    // = {partial}` correctly zeroes the unspecified elements in the heap cell.
    case ND_MEMZERO:
        // Compiler-internal zero-init of the var's own storage: the address
        // is consumed synchronously by the MSET below and never survives
        // beyond it (#676).
        if (node->var->is_block_var) {
            emit_lea3_internal(vm, REG_A0, node->var->offset); // &stack slot
            emit_rr(vm, LDR_D, REG_A0, REG_A0);       // heap ptr -> A0
        } else {
            emit_lea3_internal(vm, REG_A0, node->var->offset); // bp + offset -> A0
        }
        emit_li3(vm, REG_A2, node->var->ty->size); // byte count -> A2
        emit(vm, MSET);
        return;

    case ND_LOGAND: {
        // Logical AND with short-circuit evaluation.
        // Reuse dest_reg as the condition scratch to avoid O(depth) register
        // accumulation for deeply nested && chains (#587 gap).
        // Guard: dest_reg may be REG_ZERO (discarded expression statement) — the
        // zero register silently discards writes, so both conditions read back as 0
        // and jz is always taken, preventing the rhs and any side-effects from
        // running. Allocate a real temp when dest_reg == REG_ZERO and free it after
        // the last jz (before generating the final 0/1 into dest_reg).
        int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
        mark_temp_reg_used(r_cond); // protect from inner allocs
        gen_cond_expr(vm, node->lhs, r_cond);
        Pc jz_false = emit_jz3(vm, r_cond);

        gen_cond_expr(vm, node->rhs, r_cond);
        Pc jz_false2 = emit_jz3(vm, r_cond);
        if (r_cond != dest_reg) free_temp_reg(r_cond);

        // Both true
        emit_li3(vm, dest_reg, 1);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        // At least one false
        vm->text_seg[jz_false] = vm->text_ptr + 1;
        vm->text_seg[jz_false2] = vm->text_ptr + 1;
        emit_li3(vm, dest_reg, 0);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        return;
    }

    case ND_LOGOR: {
        // Logical OR with short-circuit evaluation.
        // Same dest_reg reuse + REG_ZERO guard as ND_LOGAND above (see comment there).
        int r_cond = (dest_reg == REG_ZERO) ? alloc_temp_reg() : dest_reg;
        mark_temp_reg_used(r_cond);
        gen_cond_expr(vm, node->lhs, r_cond);
        Pc jnz_true = emit_jnz3(vm, r_cond);

        gen_cond_expr(vm, node->rhs, r_cond);
        Pc jnz_true2 = emit_jnz3(vm, r_cond);
        if (r_cond != dest_reg) free_temp_reg(r_cond);

        // Both false
        emit_li3(vm, dest_reg, 0);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        // At least one true
        vm->text_seg[jnz_true] = vm->text_ptr + 1;
        vm->text_seg[jnz_true2] = vm->text_ptr + 1;
        emit_li3(vm, dest_reg, 1);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        return;
    }

    case ND_STMT_EXPR: {
        // Statement expression ({ stmt; expr })
        // Generate all statements, result of last expression goes to dest_reg
        for (Node *n = node->body; n; n = n->next) {
            if (!n->next && n->kind == ND_EXPR_STMT && n->lhs) {
                // Last statement - evaluate and keep result
                gen_expr(vm, n->lhs, dest_reg);
            } else {
                gen_stmt(vm, n);
            }
        }
        return;
    }

    case ND_FRAME_ADDR:
        // __builtin_frame_address(0) - returns current base pointer
        // LEA3 with offset 0 loads bp + 0 = bp into dest_reg
        emit_lea3(vm, dest_reg, 0);
        return;

    case ND_RETURN_ADDR:
        // __builtin_return_address(n) - returns return address n frames up as void*
        // Emits RETADDR opcode which walks the saved-bp chain at runtime and
        // bounds-checks each step.  Returns NULL past the outermost frame.
        emit_ri(vm, RETADDR, dest_reg, node->val);
        return;

    case ND_DYNOBJ_SIZE: {
        // __builtin_dynamic_object_size(ptr, type) — runtime heap size lookup.
        // Evaluate the pointer into a temp register, then emit DYNOBJSZ which
        // reads AllocHeader.requested_size for VM heap base pointers.
        int ptr_reg = alloc_temp_reg();
        gen_expr(vm, node->lhs, ptr_reg);
        emit_rri(vm, DYNOBJSZ, dest_reg, ptr_reg, node->val);
        free_temp_reg(ptr_reg);
        return;
    }

    case ND_BITOP: {
        // Bit-manipulation builtins: val = (op_selector<<8) | bit_width
        int bitop_op = (int)(node->val >> 8);
        int bitop_width = (int)(node->val & 0xFF);
        int bitop_tmp = alloc_temp_reg();
        gen_expr(vm, node->lhs, bitop_tmp);
        switch (bitop_op) {
        case 0: // CLZ
            emit_rri(vm, CLZ, dest_reg, bitop_tmp, bitop_width);
            break;
        case 1: // CTZ
            emit_rri(vm, CTZ, dest_reg, bitop_tmp, bitop_width);
            break;
        case 2: // POPCOUNT
            emit_rr(vm, POPCOUNT, dest_reg, bitop_tmp);
            break;
        case 3: { // PARITY = popcount & 1
            int parity_tmp = alloc_temp_reg();
            emit_rr(vm, POPCOUNT, dest_reg, bitop_tmp);
            emit_li3(vm, parity_tmp, 1);
            emit_rrr(vm, AND3, dest_reg, dest_reg, parity_tmp);
            free_temp_reg(parity_tmp);
            break;
        }
        case 4: // FFS
            emit_rri(vm, FFS, dest_reg, bitop_tmp, bitop_width);
            break;
        case 5: // BSWAP (bitop_width is byte-width)
            emit_rri(vm, BSWAP, dest_reg, bitop_tmp, bitop_width);
            break;
        default:
            error_tok(vm, node->tok, "codegen: unknown ND_BITOP selector %d", bitop_op);
        }
        free_temp_reg(bitop_tmp);
        return;
    }

    case ND_OVERFLOW_ARITH: {
        // Checked arithmetic: val=0/1/2 (add/sub/mul)
        // a→REG_A0, b→REG_A1, ptr→REG_A2; result bool→dest_reg
        // Encode op_type and result type kind in a single immediate
        Type *result_ty = node->cas_addr->ty->base;
        // type_kind encoding: (size_bytes << 1) | is_unsigned
        int kind_enc = (result_ty->size << 1) | (result_ty->is_unsigned ? 1 : 0);
        long long packed = ((long long)node->val << 8) | kind_enc;
        reset_temp_regs();
        gen_expr(vm, node->lhs, REG_A0);
        gen_expr(vm, node->rhs, REG_A1);
        gen_expr(vm, node->cas_addr, REG_A2);
        emit_with_arg(vm, IOVFL, packed);
        if (dest_reg != REG_A0)
            emit_mov3(vm, dest_reg, REG_A0);
        return;
    }

    case ND_ALOAD: {
        // Atomic-tagged load via ALDR: rd = *(T*)addr, tags atomic_shadow.
        // Falls back to plain emit_load for floats or exotic sizes.
        int r_addr = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_addr);
        Type *base_ty = node->lhs->ty->base;
        int sz = base_ty->size;
        if ((sz == 1 || sz == 2 || sz == 4 || sz == 8) && !is_flonum(base_ty)) {
            long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
            emit_rri(vm, ALDR, dest_reg, r_addr, width_enc);
        } else {
            emit_load(vm, base_ty, dest_reg, r_addr);
        }
        free_temp_reg(r_addr);
        return;
    }

    case ND_ASTORE: {
        // Atomic-tagged store via ASTR: *(T*)addr = val, tags atomic_shadow.
        // Falls back to plain emit_store for floats or exotic sizes.
        // Result is the stored value (C assignment semantics).
        int r_val  = alloc_temp_reg();
        int r_addr = alloc_temp_reg();
        gen_expr(vm, node->rhs, r_val);
        gen_expr(vm, node->lhs, r_addr);
        Type *base_ty = node->lhs->ty->base;
        int sz = base_ty->size;
        if ((sz == 1 || sz == 2 || sz == 4 || sz == 8) && !is_flonum(base_ty)) {
            long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
            emit_rri(vm, ASTR, r_val, r_addr, width_enc);
        } else {
            emit_store(vm, base_ty, r_val, r_addr);
        }
        if (dest_reg != r_val)
            emit_mov3(vm, dest_reg, r_val);
        free_temp_reg(r_val);
        free_temp_reg(r_addr);
        return;
    }

    case ND_EXCH: {
        // atomic_exchange(obj_ptr, new_val) → old_val
        // Operands in REG_A0 (addr), REG_A1 (new value); result in REG_A0.
        Type *base_ty = node->lhs->ty->base;
        int sz = base_ty->size;
        if ((sz != 1 && sz != 2 && sz != 4 && sz != 8) || is_flonum(base_ty))
            error_tok(vm, node->tok,
                      "atomic_exchange: unsupported type (must be 1/2/4/8-byte integer or pointer)");
        long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
        reset_temp_regs();
        gen_expr(vm, node->lhs, REG_A0); // addr (obj pointer)
        gen_expr(vm, node->rhs, REG_A1); // new value
        emit_with_arg(vm, AXCHG, width_enc);
        // old value returned in REG_A0
        if (dest_reg != REG_A0)
            emit_mov3(vm, dest_reg, REG_A0);
        return;
    }

    case ND_CAS: {
        // compare_and_swap(obj_ptr, expected_ptr, desired) → bool
        // Operands: REG_A0 = obj_ptr (T*), REG_A1 = expected_ptr (T*),
        //           REG_A2 = desired (T); result bool in REG_A0.
        Type *base_ty = node->cas_addr->ty->base;
        int sz = base_ty->size;
        if ((sz != 1 && sz != 2 && sz != 4 && sz != 8) || is_flonum(base_ty))
            error_tok(vm, node->tok,
                      "compare_and_swap: unsupported type (must be 1/2/4/8-byte integer or pointer)");
        long long width_enc = ((long long)sz << 1) | (base_ty->is_unsigned ? 1 : 0);
        reset_temp_regs();
        gen_expr(vm, node->cas_addr, REG_A0); // T* — pointer to atomic variable
        gen_expr(vm, node->cas_old,  REG_A1); // T* — pointer to expected value
        gen_expr(vm, node->cas_new,  REG_A2); // T  — desired value
        emit_with_arg(vm, ACAS, width_enc);
        // bool result in REG_A0
        if (dest_reg != REG_A0)
            emit_mov3(vm, dest_reg, REG_A0);
        return;
    }

    case ND_VLA_PTR:
        // VLA pointer/designator: load the stored pointer value
        // VLAs are implemented by storing a pointer to dynamically allocated
        // memory The pointer itself is a local variable
        if (node->var->is_local) {
            // Slot address only feeds the immediate load below (#676).
            emit_lea3_internal(vm, dest_reg, node->var->offset); // Address of pointer
            emit_rr(vm, LDR_D, dest_reg, dest_reg); // Load the pointer value
        } else {
            error_tok(vm, node->tok, "VLA must be local");
        }
        return;

    case ND_LABEL_VAL: {
        // Label address: &&label (GCC extension for computed goto)
        // Emit LTA3 with placeholder offset that will be patched later
        Pc label_addr_loc = emit_lta3(vm, dest_reg, 0);
        // Record patch location so it gets resolved when label is defined
        add_label_patch(node->unique_label ? node->unique_label : node->label,
                        label_addr_loc, true);
        return;
    }

    case ND_BLOCK_LITERAL: {
        // Block descriptor layout (stack-allocated in enclosing function's frame):
        //   [0]  = invoke pointer (function address)
        //   [8]  = descriptor byte-size (for Block_copy to know how much to malloc)
        //   [16] = first captured value
        //   [24] = second captured value  ...
        //
        // Stack allocation (via block_desc_var) gives each function invocation its
        // own descriptor, so multiple calls to the same function return independent
        // block instances without aliasing.

        int num_captures = node->num_block_captures;
        int descriptor_slots = 2 + num_captures; // invoke + size + captures
        int descriptor_size = descriptor_slots * 8;

        // Load address of the pre-allocated stack descriptor slot
        int r_desc = alloc_temp_reg();
        emit_lea3(vm, r_desc, node->block_desc_var->offset);
        mark_temp_reg_used(r_desc);

        // Load function address (will be patched later)
        int r_invoke = alloc_temp_reg();
        Pc invoke_addr_loc = emit_lta3(vm, r_invoke, 0); // Placeholder

        // Record patch for block function address
        PATCH_GROW(vm, func_addr_patches, num_func_addr_patches, func_addr_patches_cap);
        vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
            .location = invoke_addr_loc;
        vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
            .function = node->block_fn;
        vm->compiler.num_func_addr_patches++;

        // Store invoke pointer at descriptor[0]
        emit_rr(vm, STR_D, r_invoke, r_desc);
        free_temp_reg(r_invoke);

        // Store descriptor size at descriptor[1] so Block_copy knows how much to copy
        int r_size = alloc_temp_reg();
        emit_li3(vm, r_size, descriptor_size);
        int r_size_slot = alloc_temp_reg();
        emit_addi3(vm, r_size_slot, r_desc, 8);
        emit_rr(vm, STR_D, r_size, r_size_slot);
        free_temp_reg(r_size_slot);
        free_temp_reg(r_size);

        // Copy captured variable values into descriptor.
        // A capture may come from the enclosing block's own stack frame (direct
        // local) or from the enclosing block's descriptor (transitive capture
        // from a grandparent scope).  Check the enclosing function's capture
        // list first so we read from the right source.
        Obj *enc_fn = vm->compiler.current_fn;
        for (int i = 0; i < num_captures; i++) {
            Obj *cap = node->block_captures[i];
            int r_val = alloc_temp_reg();

            int enc_cap_idx = (enc_fn && enc_fn->is_block)
                              ? find_capture_index(enc_fn, cap) : -1;

            if (enc_cap_idx >= 0) {
                // Variable lives in the enclosing block's descriptor: read via
                // __static_link so we don't use a stale stack offset from an
                // outer function's frame. Compiler-internal chase (#676):
                // every intermediate address here feeds an immediate load.
                Obj *static_link = find_static_link_var(enc_fn);
                emit_lea3_internal(vm, r_val, static_link->offset);
                emit_rr(vm, LDR_D, r_val, r_val);                    // descriptor ptr
                emit_addi3(vm, r_val, r_val, (enc_cap_idx + 2) * 8); // slot addr (skip invoke+size)
                if (cap->is_block_var)
                    emit_rr(vm, LDR_D, r_val, r_val); // heap ptr from descriptor slot
                else
                    emit_load(vm, cap->ty, r_val, r_val); // value from descriptor slot
            } else if (cap->is_block_var) {
                // __block var directly in enclosing stack: copy heap pointer.
                // Slot address only feeds the immediate load (#676).
                emit_lea3_internal(vm, r_val, cap->offset);
                emit_rr(vm, LDR_D, r_val, r_val);
            } else if (cap->is_local) {
                // Regular local directly in enclosing stack: copy value.
                // Slot address only feeds the immediate load (#676).
                emit_lea3_internal(vm, r_val, cap->offset);
                emit_load(vm, cap->ty, r_val, r_val);
            } else {
                // Global
                emit_lda3(vm, r_val, cap->offset);
                emit_load(vm, cap->ty, r_val, r_val);
            }

            // Store at descriptor[(i + 2) * 8] (slots 0=invoke, 1=size, 2..n=captures)
            int r_cap_addr = alloc_temp_reg();
            emit_addi3(vm, r_cap_addr, r_desc, (i + 2) * 8);
            emit_rr(vm, STR_D, r_val, r_cap_addr);
            free_temp_reg(r_cap_addr);
            free_temp_reg(r_val);
        }

        // Return descriptor address
        if (dest_reg != r_desc) {
            emit_mov3(vm, dest_reg, r_desc);
        }
        free_temp_reg(r_desc);
        return;
    }

    case ND_BLOCK_CALL: {
        // Block invocation via descriptor:
        // 1. Evaluate block expression to get descriptor address
        // 2. Load function pointer from descriptor[0]
        // 3. Pass descriptor in A0 (as __static_link for captured vars)
        // 4. Pass user arguments in A1-A7

        // First, evaluate block expression to get descriptor address
        int r_desc = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_desc);
        mark_temp_reg_used(r_desc);

        // Count arguments
        // int nargs = 0;
        // for (Node *a = node->args; a; a = a->next) nargs++;

        // Generate user arguments into A1-A7 for ints, FREG_A0-A7 for floats.
        // The descriptor occupies REG_A0 (int slot 0), so int args start at
        // int_arg_idx=1; there is no float static-link, so float args start
        // at float_arg_idx=0. This must match the independent int/float
        // register counters ENT3 uses to spill incoming params (op_ENT3_fn,
        // src/ops.c) -- a single combined "user args start at A1" counter
        // applied to both register files (the previous scheme here) puts
        // float args in the wrong FREG_A* slot as soon as a block takes both
        // an int and a float parameter, or a float parameter at all, since
        // ENT3 counts int and float params separately. Pre-existing bug,
        // found and fixed alongside #712.
        int int_arg_idx = 1;
        int float_arg_idx = 0;
        for (Node *a = node->args; a; a = a->next) {
            add_type(vm, a);
            if (is_vector(a->ty)) {
                // Block invocation has no by-memory ABI for aggregates at
                // all (no RETBUF/pointer-arg machinery here) -- reject
                // cleanly rather than mis-marshalling a vregs[] value
                // through a plain int arg register (#714).
                error_tok(vm, a->tok,
                          "vector arguments to block calls are not "
                          "supported");
            }
            if (is_flonum(a->ty)) {
                if (float_arg_idx >= 8) {
                    error_tok(vm, a->tok, "too many block arguments");
                }
                // Evaluate into a temp-numbered float scratch, not
                // FREG_A0+float_arg_idx directly -- see
                // gen_flonum_arg_to_scratch (#712): a leading integer arg
                // already placed in REG_A1+k could otherwise be clobbered by
                // an integer scratch this expression's own codegen reuses
                // (e.g. deref address, int->float cast source).
                int fs = gen_flonum_arg_to_scratch(vm, a);
                emit_fmov3(vm, FREG_A0 + float_arg_idx, fs);
                free_temp_reg(fs);
                float_arg_idx++;
            } else {
                if (int_arg_idx >= 8) {
                    error_tok(vm, a->tok, "too many block arguments");
                }
                gen_expr(vm, a, REG_A0 + int_arg_idx);
                int_arg_idx++;
            }
        }

        // Load function pointer from descriptor[0]
        int r_fn = alloc_temp_reg();
        emit_rr(vm, LDR_D, r_fn, r_desc);

        // Pass descriptor in A0 (for __static_link access to captures)
        emit_mov3(vm, REG_A0, r_desc);
        free_temp_reg(r_desc);

        // Indirect call via function pointer
        emit(vm, CALLI);
        emit_word(vm, ENCODE_R(r_fn));
        free_temp_reg(r_fn);

        reset_temp_regs();

        // Result is in REG_A0 or FREG_A0
        if (is_flonum(node->ty)) {
            if (dest_reg != FREG_A0) {
                emit_fmov3(vm, dest_reg, FREG_A0);
            }
        } else if (dest_reg != REG_A0) {
            emit_mov3(vm, dest_reg, REG_A0);
        }
        return;
    }

    default:
        error_tok(vm, node->tok, "codegen: unsupported expression node kind %d",
                  node->kind);
    }
}

// ========== Statement Generation ==========

static void emit_source_location(VirtualMachine *vm, Node *node) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !node || !node->tok)
        return;
    if (node->tok->file == vm->dbg.last_debug_file &&
        node->tok->line_no == vm->dbg.last_debug_line &&
        node->tok->col_no == vm->dbg.last_debug_col)
        return;

    if (vm->dbg.source_map_count >= vm->dbg.source_map_capacity) {
        vm->dbg.source_map_capacity =
            vm->dbg.source_map_capacity ? vm->dbg.source_map_capacity * 2 : 1024;
        vm->dbg.source_map = realloc(vm->dbg.source_map,
            vm->dbg.source_map_capacity * sizeof(SourceMap));
    }

    SourceMap *entry = &vm->dbg.source_map[vm->dbg.source_map_count++];
    entry->pc_offset = vm->text_ptr + 1;
    entry->file = node->tok->file;
    entry->line_no = node->tok->line_no;
    entry->col_no = node->tok->col_no;
    entry->end_col_no = node->tok->col_no + node->tok->len;

    vm->dbg.last_debug_file = node->tok->file;
    vm->dbg.last_debug_line = node->tok->line_no;
    vm->dbg.last_debug_col = node->tok->col_no;
}

// ========== Restrict Memcpy Loop Lowering (#268) ==========
//
// Recognises: for (T i = 0; i < n; i++) dst[i] = src[i]
// where dst and src are restrict-qualified pointers.
// Emits a single MCPY opcode instead of the loop.

// Strip casts to find the underlying node (re-uses strip_index_casts logic).
static Node *strip_casts(Node *n) {
    while (n && n->kind == ND_CAST)
        n = n->lhs;
    return n;
}

// Matches dst[i] = src[i] body: returns true and populates dst_var/src_var/ind_var/scale.
// body must be a single ND_ASSIGN of the form: *(base + i*scale) = *(base2 + i*scale)
static bool match_memcpy_body(Node *body, Obj **dst_var, Obj **src_var,
                               Obj **ind_var, int *elem_scale) {
    // Accept either bare ND_EXPR_STMT or bare ND_ASSIGN (from ND_FOR body).
    Node *assign = body;
    if (assign && assign->kind == ND_EXPR_STMT)
        assign = assign->lhs;
    // Also handle an ND_BLOCK wrapping a single ND_EXPR_STMT
    if (assign && assign->kind == ND_BLOCK) {
        Node *inner = assign->body;
        if (!inner || inner->next)
            return false;
        assign = inner;
        if (assign->kind == ND_EXPR_STMT)
            assign = assign->lhs;
    }
    if (!assign || assign->kind != ND_ASSIGN)
        return false;

    Node *lhs = assign->lhs;
    Node *rhs = assign->rhs;

    // Both sides must be ND_DEREF of pointer arithmetic.
    if (!lhs || lhs->kind != ND_DEREF || !rhs || rhs->kind != ND_DEREF)
        return false;

    Node *laddr = strip_casts(lhs->lhs);
    Node *raddr = strip_casts(rhs->lhs);
    if (!laddr || !raddr)
        return false;

    // Match: base + index*scale
    // laddr/raddr must be ND_ADD(base_var, ND_MUL(index, scale)).
    if (laddr->kind != ND_ADD || raddr->kind != ND_ADD)
        return false;

    Node *l_index = NULL, *r_index = NULL;
    int l_scale = 0, r_scale = 0;

    // Decompose lhs address
    Node *ll = strip_casts(laddr->lhs);
    Node *lr = strip_casts(laddr->rhs);
    // Try rhs side being the index*scale
    if (!is_index_scale(lr, &l_index, &l_scale)) {
        // Maybe lhs side is the index*scale (commuted)
        if (!is_index_scale(ll, &l_index, &l_scale))
            return false;
        Node *tmp = ll; ll = lr; lr = tmp; // swap so ll=base, lr=index*scale
    }
    Node *l_base = ll;

    // Decompose rhs address
    Node *rl = strip_casts(raddr->lhs);
    Node *rr = strip_casts(raddr->rhs);
    if (!is_index_scale(rr, &r_index, &r_scale)) {
        if (!is_index_scale(rl, &r_index, &r_scale))
            return false;
        Node *tmp = rl; rl = rr; rr = tmp;
    }
    Node *r_base = rl;

    // Scales must match.
    if (l_scale != r_scale)
        return false;

    // Index variables must be the same.
    l_index = strip_casts(l_index);
    r_index = strip_casts(r_index);
    if (!l_index || l_index->kind != ND_VAR || !r_index || r_index->kind != ND_VAR)
        return false;
    if (l_index->var != r_index->var)
        return false;

    // Base pointers must be restrict-qualified parameters.
    l_base = strip_casts(l_base);
    r_base = strip_casts(r_base);
    if (!l_base || l_base->kind != ND_VAR || !r_base || r_base->kind != ND_VAR)
        return false;
    Obj *dst = l_base->var;
    Obj *src = r_base->var;
    if (!dst || !src || dst == src)
        return false;
    if (!dst->ty || dst->ty->kind != TY_PTR || !dst->ty->is_restrict)
        return false;
    if (!src->ty || src->ty->kind != TY_PTR || !src->ty->is_restrict)
        return false;

    *dst_var   = dst;
    *src_var   = src;
    *ind_var   = l_index->var;
    *elem_scale = l_scale;
    return true;
}

// Try to emit a restrict memcpy loop as a single MCPY opcode.
// Returns true if the pattern matched and MCPY was emitted.
static bool try_emit_restrict_memcpy(VirtualMachine *vm, Node *node) {
    if (!node || node->kind != ND_FOR)
        return false;
    if (vm->compiler.opt_level < 2)
        return false;
    // Flags that disable indexed-load optimisation also make this unsafe.
    if (vm->flags & CCCC_FUSION_UNSAFE_FLAGS)
        return false;

    // 1. Init: must be a declaration/assignment of induction variable to 0.
    //    After parsing, for-init declarations come as ND_BLOCK{ ND_EXPR_STMT{ ND_ASSIGN } }.
    Node *init = node->init;
    if (!init)
        return false;
    Node *init_assign = init;
    if (init_assign->kind == ND_BLOCK) {
        if (!init_assign->body || init_assign->body->next)
            return false;
        init_assign = init_assign->body;
    }
    if (init_assign->kind == ND_EXPR_STMT)
        init_assign = init_assign->lhs;
    if (!init_assign || init_assign->kind != ND_ASSIGN)
        return false;
    Node *init_lhs = strip_casts(init_assign->lhs);
    Node *init_rhs = strip_casts(init_assign->rhs);
    if (!init_lhs || init_lhs->kind != ND_VAR || !init_rhs || init_rhs->kind != ND_NUM)
        return false;
    if (init_rhs->val != 0)
        return false;
    Obj *ind_init = init_lhs->var;

    // 2. Condition: must be ind_var < bound_expr.
    Node *cond = node->cond;
    if (!cond || cond->kind != ND_LT)
        return false;
    Node *cond_lhs = strip_casts(cond->lhs);
    if (!cond_lhs || cond_lhs->kind != ND_VAR || cond_lhs->var != ind_init)
        return false;
    Node *bound_expr = cond->rhs;

    // 3. Increment: must be i = i+1 (the parser desugars i++ and ++i to this).
    Node *inc = node->inc;
    if (!inc)
        return false;
    inc = strip_casts(inc);
    if (!inc || inc->kind != ND_ASSIGN)
        return false;
    {
        Node *inc_lhs = strip_casts(inc->lhs);
        if (!inc_lhs || inc_lhs->kind != ND_VAR || inc_lhs->var != ind_init)
            return false;
        Node *inc_rhs = strip_casts(inc->rhs);
        // Accept i+1 or 1+i
        if (!inc_rhs || inc_rhs->kind != ND_ADD)
            return false;
        Node *add_lhs = strip_casts(inc_rhs->lhs);
        Node *add_rhs = strip_casts(inc_rhs->rhs);
        bool lhs_is_ind = add_lhs && add_lhs->kind == ND_VAR &&
                          add_lhs->var == ind_init;
        bool rhs_is_one = add_rhs && add_rhs->kind == ND_NUM &&
                          add_rhs->val == 1;
        bool rhs_is_ind = add_rhs && add_rhs->kind == ND_VAR &&
                          add_rhs->var == ind_init;
        bool lhs_is_one = add_lhs && add_lhs->kind == ND_NUM &&
                          add_lhs->val == 1;
        if (!((lhs_is_ind && rhs_is_one) || (rhs_is_ind && lhs_is_one)))
            return false;
    }

    // 4. Body: must be dst[i] = src[i] with restrict pointers.
    Obj *dst_var = NULL, *src_var = NULL, *ind_var = NULL;
    int elem_scale = 0;
    if (!match_memcpy_body(node->then, &dst_var, &src_var, &ind_var, &elem_scale))
        return false;
    if (ind_var != ind_init)
        return false;

    // All checks passed: emit MCPY.
    reset_temp_regs();

    // Compute bound (n).
    int r_n = alloc_temp_reg();
    gen_expr(vm, bound_expr, r_n);

    // Guard: if n <= 0 the loop is a no-op; skip MCPY to avoid memcpy with
    // a huge size_t when n is a negative signed value.
    // emit_jnz3 also invalidates the restrict cache (control-flow split).
    int r_guard = alloc_temp_reg();
    emit_rrr(vm, SLE3, r_guard, r_n, REG_ZERO);
    Pc skip_patch = emit_jnz3(vm, r_guard);
    free_temp_reg(r_guard);

    int r_dst = alloc_temp_reg();
    // Load dst pointer value — the var is a function param (pointer type)
    emit_local_load(vm, dst_var->ty, r_dst, dst_var->offset);

    int r_src = alloc_temp_reg();
    emit_local_load(vm, src_var->ty, r_src, src_var->offset);

    if (elem_scale <= 1) {
        emit_mov3(vm, REG_A2, r_n);
    } else {
        int r_scale = alloc_temp_reg();
        emit_li3(vm, r_scale, elem_scale);
        emit_rrr(vm, MUL3, REG_A2, r_n, r_scale);
        free_temp_reg(r_scale);
    }

    emit_mov3(vm, REG_A0, r_dst);
    emit_mov3(vm, REG_A1, r_src);
    emit(vm, MCPY);

    free_temp_reg(r_n);
    free_temp_reg(r_dst);
    free_temp_reg(r_src);

    // Patch the skip-MCPY target. Cache was already invalidated by emit_jnz3 above.
    vm->text_seg[skip_patch] = vm->text_ptr + 1;

    // The induction variable is loop-scoped and dead after MCPY; no need to update it.
    return true;
}

static void gen_stmt(VirtualMachine *vm, Node *node) {
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

        // Push a cleanup scope entry if this block declared cleanup vars.
        CleanupScopeEntry cleanup_entry = {};
        CleanupScopeEntry *saved_cleanup = g_cleanup_scope;
        if (node->cleanup_vars) {
            cleanup_entry.vars = node->cleanup_vars;
            cleanup_entry.depth = node->cleanup_scope_depth;
            cleanup_entry.outer = g_cleanup_scope;
            g_cleanup_scope = &cleanup_entry;
        }

        for (Node *n = node->body; n; n = n->next) {
            gen_stmt(vm, n);
        }

        // Emit cleanups at natural block exit (LIFO order).
        if (node->cleanup_vars)
            emit_scope_cleanups(vm, g_cleanup_scope);

        g_cleanup_scope = saved_cleanup;

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

        // Tail-call optimisation: return f(args) → CALLT instead of CALL+LEV3.
        // Guards: opt >= 1, not inlining, predicate checks FFI/variadic/nested/etc.
        // After gen_expr, pending_tail_callee is set only if CALL was reached;
        // inlining/builtins leave it NULL and we fall through to the LEV3 path.
        // expr_already_eval prevents re-evaluating node->lhs in the LEV3 path below.
        //
        // The parser always wraps the return expression in ND_CAST, even for
        // identity conversions (e.g. int→int).  Strip through those cast
        // wrappers to expose the underlying ND_FUNCALL for can_emit_tail_call,
        // but only while each cast is a representation no-op (return_repr_key/
        // cast_is_repr_noop, ~line 1350): the callee's own ND_RETURN cast has
        // already normalised its result to the callee's return type, so a
        // no-op cast on top of that is redundant and safe to skip. A cast
        // that genuinely changes representation (#762, e.g. `return
        // (unsigned char) g(...);` truncating a wider result) must NOT be
        // stripped -- CALLT hands the callee's raw value straight to the
        // *original* caller with no opportunity to apply it, so stopping the
        // strip there leaves tco_expr as an ND_CAST, which can_emit_tail_call
        // rejects outright (only ND_FUNCALL is eligible), correctly falling
        // back to the ordinary CALL+LEV3 path below.
        bool expr_already_eval = false;
        Node *tco_expr = node->lhs;
        while (tco_expr && tco_expr->kind == ND_CAST && tco_expr->lhs &&
               cast_is_repr_noop(tco_expr->ty, tco_expr->lhs->ty))
            tco_expr = tco_expr->lhs;
        if (tco_expr && vm->compiler.opt_level >= 1 &&
            can_emit_tail_call(vm, tco_expr)) {
            int tco_dest = is_flonum(tco_expr->ty) ? FREG_A0 : REG_A0;
            vm->compiler.emitting_tail_call = true;
            vm->compiler.pending_tail_callee = NULL;
            gen_expr(vm, tco_expr, tco_dest);
            vm->compiler.emitting_tail_call = false; // belt-and-suspenders; cleared in ND_FUNCALL
            if (vm->compiler.pending_tail_callee) {
                Obj *tco_fn = vm->compiler.pending_tail_callee;
                vm->compiler.pending_tail_callee = NULL;
                emit_flush_promoted_locals(vm);
                emit_flush_fp_promoted_locals(vm);
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_scopeout(vm, vm->current_function_scope_id);
                emit_restore_restrict_cache_regs(vm);
                emit_restore_fp_promoted_registers(vm);
                emit_restore_promoted_registers(vm);
                emit(vm, CALLT);
                Pc tco_patch = emit_word_ptr(vm);
                vm->text_seg[tco_patch] = 0;
                PATCH_GROW(vm, call_patches, num_call_patches, call_patches_cap);
                vm->compiler.call_patches[vm->compiler.num_call_patches].location =
                    tco_patch;
                vm->compiler.call_patches[vm->compiler.num_call_patches].function =
                    tco_fn;
                vm->compiler.num_call_patches++;
                return;
            }
            // Inlining/builtin handled the call; result already in tco_dest.
            // Fall through to flush/restore/LEV3, but skip re-evaluating node->lhs.
            expr_already_eval = true;
        }

        if (node->lhs && !expr_already_eval) {
            // If returning struct/union/wide-_BitInt, copy to return buffer at
            // runtime. Wide _BitInt values are address-based (like structs),
            // so returning the raw address would leave a dangling pointer
            // into the callee's torn-down frame once LEV3 runs.
            if (node->lhs->ty && (node->lhs->ty->kind == TY_STRUCT ||
                                  node->lhs->ty->kind == TY_UNION ||
                                  is_wide_bitint(node->lhs->ty) ||
                                  is_decimal(node->lhs->ty))) {
                // #402: _Decimal32/64/128 is address-based, same dangling-
                // frame hazard as struct/union/wide-_BitInt above -- the
                // value's alloc_decimal_temp scratch slot lives in THIS
                // (callee) frame, so it must be copied into the RETBUF
                // pool before LEV3 tears the frame down.
                // Evaluate source (struct address) into a temp register first
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

                // MCPY uses registers: dest in REG_A0, src in REG_A1, count in
                // REG_A2 REG_A0 already has dest from RETBUF, but we saved it
                // to r_dest
                emit_mov3(vm, REG_A1, r_src); // src = struct address
                emit_li3(vm, REG_A2, node->lhs->ty->size); // count
                emit_mov3(vm, REG_A0, r_dest); // dest = buffer address
                emit(vm, MCPY);

                // Return buffer address in REG_A0 (already there from r_dest)
                emit_mov3(vm, REG_A0, r_dest);

                free_temp_reg(r_src);
                free_temp_reg(r_dest);
            } else if (is_vector(node->lhs->ty)) {
                // Vector return (#714): unlike struct/union, the value lives
                // in a vregs[] register (gen_vector_expr), not memory -- there
                // is no source address to MCPY from. Materialize the value
                // into a vreg, then VSTR it into a fresh RETBUF buffer and
                // return that buffer's address, mirroring the struct path.
                int v_src = alloc_temp_reg();
                gen_expr(vm, node->lhs, v_src); // vector value -> vregs[v_src]
                // Belt-and-suspenders as with the struct/wide-_BitInt path
                // above: node->lhs may contain a nested call that resets the
                // temp allocator's free list.
                mark_temp_reg_used(v_src);

                emit(vm, RETBUF); // buffer address -> REG_A0
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
        // Emit cleanup calls for all active scopes before returning (LIFO, innermost first).
        // If non-void, preserve the return value across cleanup calls (they clobber REG_A0).
        if (g_cleanup_scope) {
            bool is_void_ret = !node->lhs || (node->lhs->ty && node->lhs->ty->kind == TY_VOID);
            bool is_float_ret = !is_void_ret && node->lhs && is_flonum(node->lhs->ty);
            Obj *cur_fn = vm->compiler.current_fn;
            if (!is_void_ret) {
                if (!is_float_ret) {
                    // Integer/pointer/struct-addr return: save via stack push
                    emit_psh3(vm, REG_A0);
                } else {
                    // Float/double return: save to synthetic stack slot
                    emit_ri(vm, FSTR_LOCAL, FREG_A0, cur_fn->cleanup_fp_retval_offset);
                }
            }
            emit_cleanups_to_depth(vm, 0);
            if (!is_void_ret) {
                if (!is_float_ret) {
                    emit_pop3(vm, REG_A0);
                } else {
                    emit_ri(vm, FLDR_LOCAL, FREG_A0, cur_fn->cleanup_fp_retval_offset);
                }
            }
        }

        emit_flush_promoted_locals(vm);
        emit_flush_fp_promoted_locals(vm);
        // Deactivate function-level scope before returning.
        if (vm->flags & CCCC_STACK_INSTR)
            emit_scopeout(vm, vm->current_function_scope_id);
        emit_restore_restrict_cache_regs(vm);
        emit_restore_fp_promoted_registers(vm);
        emit_restore_promoted_registers(vm);
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
            Pc jmp_end = emit_word_ptr(vm);
            vm->text_seg[jz_else] = vm->text_ptr + 1;
            gen_stmt(vm, node->els);
            vm->text_seg[jmp_end] = vm->text_ptr + 1;
        } else {
            vm->text_seg[jz_else] = vm->text_ptr + 1;
        }
        return;
    }

    case ND_FOR: {
        // Try to lower restrict copy loops to MCPY before emitting loop code.
        if (try_emit_restrict_memcpy(vm, node))
            return;

        // Init
        if (node->init) {
            gen_stmt(vm, node->init);
        }

        // Loop start is a join point (init falls through; back-edge arrives here).
        restrict_cache_invalidate_all(vm);
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
        // After back-edge, any fall-through is from a join; invalidate.
        restrict_cache_invalidate_all(vm);

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
        // Loop start is a join point (back-edge arrives here).
        restrict_cache_invalidate_all(vm);
        Pc loop_start = vm->text_ptr + 1;

        gen_stmt(vm, node->then);

        // Define continue label (jumps to condition)
        if (node->cont_label) {
            define_label(vm, node->cont_label);
        }

        reset_temp_regs();
        int r_cond = alloc_temp_reg();
        gen_cond_expr(vm, node->cond, r_cond);
        Pc jnz_start = emit_jnz3(vm, r_cond);
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

        int num_cases = 0;
        long min_case = 0;
        long max_case = 0;
        long covered_values = 0;
        SwitchCasePatch *cases =
            collect_switch_cases(node, &num_cases, &min_case, &max_case,
                                 &covered_values);
        long span = num_cases ? max_case - min_case + 1 : 0;
        bool use_jump_table =
            num_cases > 0 && covered_values >= 4 && span <= covered_values * 2;

        Pc default_patch = CCCC_INVALID_PC;
        Pc end_patch = CCCC_INVALID_PC;
        Pc table_start = CCCC_INVALID_PC;
        PatchList fail_patches = {};

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
            default_patch = emit_word_ptr(vm);
            table_start = vm->text_ptr + 1;
            vm->text_seg[table_operand] = table_start;
            for (long i = 0; i < span; i++)
                emit_word(vm, 0);

            for (int i = 0; i < num_cases; i++) {
                for (long value = cases[i].begin; value <= cases[i].end; value++) {
                    Pc entry = table_start + (Pc)(value - min_case);
                    vm->text_seg[entry] = CCCC_INVALID_PC;
                    cases[i].table_entry = entry;
                }
            }
        } else {
            int r_cmp = alloc_temp_reg();
            emit_sparse_switch_tree(vm, cases, 0, num_cases - 1, r_val, r_cmp,
                                    &fail_patches);
            free_temp_reg(r_cmp);
            if (node->default_case) {
                emit(vm, JMP);
                default_patch = emit_word_ptr(vm);
            }
        }

        void *saved_cases = vm->compiler.current_switch_cases;
        int saved_num_cases = vm->compiler.current_switch_num;
        Pc saved_table_start = vm->compiler.current_switch_table_start;
        long saved_switch_min = vm->compiler.current_switch_min;
        long saved_switch_size = vm->compiler.current_switch_size;
        Node *saved_default = vm->compiler.current_switch_default;
        Pc saved_default_patch = vm->compiler.current_default_patch;

        vm->compiler.current_switch_cases = cases;
        vm->compiler.current_switch_num = num_cases;
        vm->compiler.current_switch_table_start = table_start;
        vm->compiler.current_switch_min = min_case;
        vm->compiler.current_switch_size = span;
        vm->compiler.current_switch_default = node->default_case;
        vm->compiler.current_default_patch = default_patch;

        gen_stmt(vm, node->then);

        vm->compiler.current_switch_cases = saved_cases;
        vm->compiler.current_switch_num = saved_num_cases;
        vm->compiler.current_switch_table_start = saved_table_start;
        vm->compiler.current_switch_min = saved_switch_min;
        vm->compiler.current_switch_size = saved_switch_size;
        vm->compiler.current_switch_default = saved_default;
        vm->compiler.current_default_patch = saved_default_patch;

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
            vm->text_seg[fail_patches.items[i]] = node->default_case
                ? vm->text_seg[default_patch]
                : end_target;
        }
        if (use_jump_table) {
            Pc default_target = node->default_case ? vm->text_seg[default_patch]
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
            SwitchCasePatch *entry =
                find_switch_case(cases, vm->compiler.current_switch_num, node);
            if (entry) {
                if (vm->compiler.current_switch_table_start != CCCC_INVALID_PC) {
                    for (long value = entry->begin; value <= entry->end; value++) {
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
            Pc patch = emit_word_ptr(vm);
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
            vm->compiler.asm_callback(vm, node->asm_str, vm->compiler.asm_user_data);
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
        error_tok(vm, node->tok, "codegen: unsupported statement node kind %d",
                  node->kind);
    }
}

static bool fn_has_any_cleanup_vars(Obj *fn) {
    for (Obj *v = fn->locals; v; v = v->next)
        if (v->cleanup_fn) return true;
    return false;
}

// Assign stack offsets for parameters and locals
// Returns the total stack size (aligned to 16 bytes)
static int assign_stack_offsets(VirtualMachine *vm, Obj *fn) {
    if (!fn->is_function)
        return 0;

    int param_count = 0;
    for (Obj *param = fn->params; param; param = param->next) {
        param_count++;
    }

    // For variadic functions, copy all 8 potential arg registers so va_arg can
    // consume any register-passed variadic tail. Fixed params beyond 8 need
    // their own local slots too.
    bool is_variadic = fn->ty && fn->ty->is_variadic;
    int spill_param_count = is_variadic && param_count < 8 ? 8 : param_count;

    // When stack canaries are enabled, ENT3 reserves bp[-1] for the canary and
    // spills params/locals one slot lower (bp[-2] downward). Bake that one-slot
    // shift into the assigned offsets so codegen reads where ENT3 wrote (#445).
    int canary_bias = (vm->flags & CCCC_STACK_CANARIES) ? 1 : 0;

    // Stack size starts with space for parameters (at negative offsets), plus
    // the reserved canary slot when enabled.
    int stack_size = spill_param_count + canary_bias;

    // Assign parameter offsets (negative). Without canaries: bp[-1], bp[-2], ...
    // With canaries: bp[-2], bp[-3], ... (bp[-1] is the canary).
    int param_offset = -1 - canary_bias;
    for (Obj *param = fn->params; param; param = param->next) {
        param->offset = param_offset;
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
            stack_size += var_size;
            var->offset = -stack_size;
        }
    }

    // Allocate a float retval save slot if this function has cleanup vars and
    // a floating-point return type. Used to preserve FREG_A0 across cleanup calls.
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
    vm->compiler.frame_has_esc_agg = false;
    vm->compiler.frame_has_esc_scalar = false;

    // Reset label tracking for this function
    reset_labels();

    // Count parameters first
    // Assign stack offsets early
    int stack_size = assign_stack_offsets(vm, fn);
    int base_stack_size = stack_size;
    prepare_local_promotion(vm, fn, base_stack_size);
    prepare_fp_local_promotion(vm, fn, base_stack_size); // must follow prepare_local_promotion
    prepare_restrict_cache(vm, fn, base_stack_size);
    stack_size += vm->compiler.promoted_count + vm->compiler.fp_promoted_count
               + vm->compiler.restrict_cache_capacity;
    if (stack_size % 2 != 0)
        stack_size++;

    // Helper vars needed for ENT3 emission
    int param_count = 0;
    for (Obj *param = fn->params; param; param = param->next)
        param_count++;
    bool is_variadic = fn->ty && fn->ty->is_variadic;
    int spill_param_count = is_variadic && param_count < 8 ? 8 : param_count;

    // Record source location for function entry
    emit_source_location(vm, fn->body);

    // Record function address (offset from text_seg start)
    fn->code_addr = vm->text_ptr + 1;

    // Compute float parameter masks for ENT3
    unsigned int float_param_mask = 0;
    unsigned int f32_param_mask = 0;
    int pindex = 0;
    for (Obj *param = fn->params; param && pindex < 8;
         param = param->next, pindex++) {
        if (is_flonum(param->ty)) {
            float_param_mask |= (1u << pindex);
            if (param->ty->kind == TY_FLOAT)
                f32_param_mask |= (1u << pindex);
        }
    }

    // Assign a function-level scope ID for stack instrumentation.
    int fn_scope_id = vm->current_scope_id++;
    vm->current_function_scope_id = fn_scope_id;

    // Register all params and locals in the variable metadata map.
    for (Obj *param = fn->params; param; param = param->next) {
        add_stack_var_meta(vm, param->name, param->offset, param->ty, fn_scope_id);
        add_debug_symbol(vm, param->name, param->offset, param->ty, 1, fn);
    }
    for (Obj *var = fn->locals; var; var = var->next) {
        bool is_param = false;
        for (Obj *p = fn->params; p; p = p->next)
            if (p == var) { is_param = true; break; }
        bool is_builtin = (var == fn->va_area) || (var == fn->alloca_bottom);
        if (!is_param && !is_builtin) {
            add_stack_var_meta(vm, var->name, var->offset, var->ty, fn_scope_id);
            add_debug_symbol(vm, var->name, var->offset, var->ty, 1, fn);
        }
    }

    // Emit ENT3: [stack_size:32|param_count:32] [f32_mask:32|float_mask:32]
    long long ent3_operand =
        ((long long)stack_size) | (((long long)spill_param_count) << 32);
    long long ent3_masks =
        (long long)float_param_mask | ((long long)f32_param_mask << 32);
    emit(vm, ENT3);
    vm->compiler.ent3_stack_loc = emit_i64(vm, ent3_operand);
    vm->compiler.ent3_masks_loc = emit_i64(vm, ent3_masks);
    vm->compiler.ent3_base_stack = stack_size;
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
            // Allocate heap memory for this __block variable
            // MALC: REG_A0 = size, result in REG_A0
            emit_li3(vm, REG_A0, var->ty->size);
            emit(vm, MALC);
            // Store the heap pointer in the variable's stack slot
            int r_addr = alloc_temp_reg();
            // Slot address only feeds the immediate store below (#676).
            emit_lea3_internal(vm, r_addr, var->offset); // Address of stack slot
            emit_rr(vm, STR_D, REG_A0, r_addr); // Store heap pointer in slot
            free_temp_reg(r_addr);
        }
    }

    // Generate function body
    gen_stmt(vm, fn->body);

    // Patch ENT3 stack size if inlining added local variables
    if (vm->compiler.ent3_extra_stack > 0) {
        int new_stack = vm->compiler.ent3_base_stack +
                        vm->compiler.ent3_extra_stack;
        if (new_stack % 2 != 0)
            new_stack++;
        long long new_operand =
            ((long long)new_stack) | (((long long)spill_param_count) << 32);
        vm->text_seg[vm->compiler.ent3_stack_loc] = cc_i64_lo(new_operand);
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
        vm->text_seg[vm->compiler.ent3_masks_loc] = cc_i64_lo((long long)new_masks);
        vm->text_seg[vm->compiler.ent3_masks_loc + 1] = cc_i64_hi((long long)new_masks);
    }

    // Patch all forward jumps (break/continue/goto)
    patch_labels(vm);

    // Implicit return 0 from entry function
    const char *entry_fn = vm->compiler.entry_name ? vm->compiler.entry_name : "main";
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
        CCCCInitEntry key = list[i];
        int key_pri = init_entry_effective_priority(&key);
        int j = i - 1;
        while (j >= 0) {
            int cur_pri = init_entry_effective_priority(&list[j]);
            bool key_before_cur = ascending
                ? (key_pri < cur_pri ||
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

void gen(VirtualMachine *vm, Obj *prog) {
    // Reset patch counters
    vm->compiler.num_call_patches = 0;
    vm->compiler.num_func_addr_patches = 0;
    vm->compiler.num_data_relocs = 0;

    // Reset the persistent global label map (populated by define_label during
    // function codegen; consumed by apply_global_relocations for &&label
    // static initialisers).  We reuse any previously allocated buffer.
    num_global_labels = 0;

    // Initialize text pointer - text_seg[0] is reserved for main entry point
    vm->text_ptr = 0;

    // Initialize global variables in data segment (TLS vars go into tls_template)
    // Zero the template so uninitialised TLS vars start at 0 across recompiles.
    if (vm->tls_template_cap > 0)
        memset(vm->tls_template, 0, vm->tls_template_cap);
    vm->tls_template_size = 0;
    for (Obj *var = prog; var; var = var->next) {
        if (!var->is_function) {
            if (var->is_tls) {
                // Thread-local variable: allocate in tls_template
                size_t tls_offset = (vm->tls_template_size + 7) & ~(size_t)7;
                check_tls_capacity(vm, tls_offset + (size_t)var->ty->size);
                var->offset = (long long)tls_offset;
                if (var->init_data)
                    memcpy(vm->tls_template + tls_offset, var->init_data,
                           (size_t)var->ty->size);
                vm->tls_template_size = tls_offset + (size_t)var->ty->size;
            } else {
                // Align data pointer to 8-byte boundary
                long long offset = vm->data_ptr - vm->data_seg;
                offset = (offset + 7) & ~7;
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

    // Allocate return buffer pool for struct/union returns at end of data
    // segment
    for (int i = 0; i < RETURN_BUFFER_POOL_SIZE; i++) {
        // Align to 8-byte boundary
        long long offset = vm->data_ptr - vm->data_seg;
        offset = (offset + 7) & ~7;
        check_data_capacity(vm, offset + vm->compiler.return_buffer_size);
        vm->data_ptr = vm->data_seg + offset;
        vm->compiler.return_buffer_pool[i] = vm->data_ptr;
        vm->compiler.return_buffer_offsets[i] = offset;
        memset(vm->compiler.return_buffer_pool[i], 0,
               vm->compiler.return_buffer_size);
        vm->data_ptr += vm->compiler.return_buffer_size;
    }

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

    // Build exported symbol table [V3]: non-static function definitions (#565).
    // This is emitted into the .c4 so --link and cc_load_module() can resolve
    // cross-module CALLs.
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (!fn->is_function || !fn->body || fn->is_static) continue;
        const char *sym_name = obj_external_name(fn);
        if (!sym_name) continue;
        PATCH_GROW(vm, sym_table, num_sym_table, sym_table_cap);
        int idx = vm->compiler.num_sym_table;
        vm->compiler.sym_table[idx].pc_offset = fn->code_addr;
        vm->compiler.sym_table[idx].name      = strdup(sym_name);
        vm->compiler.sym_table[idx].name_len  = strlen(sym_name);
        vm->compiler.num_sym_table++;
    }

    // Second pass: Patch function call addresses
    for (int i = 0; i < vm->compiler.num_call_patches; i++) {
        Obj *target = vm->compiler.call_patches[i].function;
        const char *fn_name = obj_external_name(target);
        Pc loc = vm->compiler.call_patches[i].location;

        Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);

        if (!fn_def) {
            // Check for FFI function
            int ffi_idx = find_ffi_function(vm, fn_name);
            if (ffi_idx >= 0) {
                // FFI - not handled via CALL, skip
                continue;
            }
            // When building a -c bytecode target or when --link libs are
            // provided, record a text relocation instead of erroring — the
            // symbol will be resolved at link time (#565).
            if ((vm->compiler.compile_only || vm->compiler.deferred_link) && fn_name) {
                PATCH_GROW(vm, text_relocs, num_text_relocs, text_relocs_cap);
                int ridx = vm->compiler.num_text_relocs;
                vm->compiler.text_relocs[ridx].location = loc;
                vm->compiler.text_relocs[ridx].name     = strdup(fn_name);
                vm->compiler.text_relocs[ridx].name_len = strlen(fn_name);
                vm->compiler.text_relocs[ridx].resolved = 0;
                vm->compiler.num_text_relocs++;
                continue;
            }
            error("undefined function: %s", fn_name);
        }

        vm->text_seg[loc] = (Pc)fn_def->code_addr;
    }

    // Third pass: Patch function address references (for function pointers)
    for (int i = 0; i < vm->compiler.num_func_addr_patches; i++) {
        Obj *target = vm->compiler.func_addr_patches[i].function;
        Pc loc = vm->compiler.func_addr_patches[i].location;

        Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);

        if (fn_def) {
            cc_write_i64_at(vm, loc, cc_pc_to_byte_offset((Pc)fn_def->code_addr));
        } else {
            const char *fn_name = obj_external_name(target);
            int ffi_idx = find_ffi_function(vm, fn_name);
            if (ffi_idx >= 0) {
                // FFI function used as a value: store token so CALLN can
                // call it. (JMPI, the other indirect-control-flow opcode,
                // carries no callsite metadata and is only ever emitted for
                // computed goto -- landing on this token there is a runtime
                // error, not a call.)
                cc_write_i64_at(vm, loc, CCCC_FFI_TOKEN_BASE - ffi_idx);
            } else if ((vm->compiler.compile_only || vm->compiler.deferred_link) && fn_name) {
                // Cross-module function-pointer: record addr reloc for link-time patching (#566).
                PATCH_GROW(vm, addr_relocs, num_addr_relocs, addr_relocs_cap);
                int ridx = vm->compiler.num_addr_relocs;
                vm->compiler.addr_relocs[ridx].location = loc;
                vm->compiler.addr_relocs[ridx].name     = strdup(fn_name);
                vm->compiler.addr_relocs[ridx].name_len = strlen(fn_name);
                vm->compiler.addr_relocs[ridx].resolved = 0;
                vm->compiler.num_addr_relocs++;
            }
            // else: non-deferred mode; parser already rejects address-of-undeclared.
        }
    }

    hashmap_deinit(&fn_defs);

    apply_global_relocations(vm, prog);

    // Release the persistent label map; it is no longer needed after relocs are
    // applied.  Keeping this explicit rather than leaking it into the process
    // lifetime, as the repo is leak-paranoid.
    free(global_label_map);
    global_label_map = NULL;
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
                CCCCInitEntry *e = &vm->compiler.ctor_list[vm->compiler.ctor_count++];
                e->code_addr = fn->code_addr;
                e->priority = fn->init_priority;
                e->seq = seq;
            }
            if (fn->is_destructor) {
                PATCH_GROW(vm, dtor_list, dtor_count, dtor_capacity);
                CCCCInitEntry *e = &vm->compiler.dtor_list[vm->compiler.dtor_count++];
                e->code_addr = fn->code_addr;
                e->priority = fn->init_priority;
                e->seq = seq;
            }
            seq++;
        }
    }
    sort_init_entries(vm->compiler.ctor_list, vm->compiler.ctor_count, true);
    sort_init_entries(vm->compiler.dtor_list, vm->compiler.dtor_count, false);

    // Find entry function and store its address in text_seg[0]
    const char *entry = vm->compiler.entry_name ? vm->compiler.entry_name : "main";
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
            vm->dbg.source_map = malloc(vm->dbg.source_map_capacity * sizeof(SourceMap));
            if (!vm->dbg.source_map)
                error("could not malloc for source map");
            vm->dbg.source_map_count = 0;
            vm->dbg.last_debug_file = NULL;
            vm->dbg.last_debug_line = -1;
            vm->dbg.source_index = NULL;
            vm->dbg.source_index_count = 0;
            vm->dbg.num_debug_symbols = 0;
            vm->dbg.num_watchpoints = 0;
        }
    }

    // Count and collect the new non-function globals, oldest-first (the list
    // is built by prepending, so walking old_head..globals gives newest-first).
    int num_new_vars = 0;
    for (Obj *v = vm->compiler.globals; v != old_head; v = v->next)
        if (!v->is_function)
            num_new_vars++;
    if (num_new_vars > 0) {
        Obj **arr = alloca((size_t)num_new_vars * sizeof(Obj *));
        int idx = num_new_vars - 1;
        for (Obj *v = vm->compiler.globals; v != old_head; v = v->next)
            if (!v->is_function)
                arr[idx--] = v;
        for (int i = 0; i < num_new_vars; i++) {
            Obj *var = arr[i];
            if (var->is_tls) {
                size_t tls_offset = (vm->tls_template_size + 7) & ~(size_t)7;
                check_tls_capacity(vm, tls_offset + (size_t)var->ty->size);
                var->offset = (long long)tls_offset;
                if (var->init_data)
                    memcpy(vm->tls_template + tls_offset, var->init_data,
                           (size_t)var->ty->size);
                vm->tls_template_size = tls_offset + (size_t)var->ty->size;
            } else {
                long long offset = vm->data_ptr - vm->data_seg;
                offset = (offset + 7) & ~7;
                check_data_capacity(vm, offset + var->ty->size);
                vm->data_ptr = vm->data_seg + offset;
                var->offset = vm->data_ptr - vm->data_seg;
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
        int idx = num_new_fns - 1;
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
            Obj *target = vm->compiler.call_patches[i].function;
            const char *fn_name = obj_external_name(target);
            Pc loc = vm->compiler.call_patches[i].location;

            Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);
            if (!fn_def) {
                int ffi_idx = find_ffi_function(vm, fn_name);
                if (ffi_idx >= 0)
                    continue; // FFI - not handled via CALL
                error("undefined function: %s", fn_name);
            }
            vm->text_seg[loc] = (Pc)fn_def->code_addr;
        }

        for (int i = addr_patch_start; i < vm->compiler.num_func_addr_patches; i++) {
            Obj *target = vm->compiler.func_addr_patches[i].function;
            Pc loc = vm->compiler.func_addr_patches[i].location;

            Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);
            if (fn_def) {
                cc_write_i64_at(vm, loc, cc_pc_to_byte_offset((Pc)fn_def->code_addr));
            } else {
                const char *fn_name = obj_external_name(target);
                int ffi_idx = find_ffi_function(vm, fn_name);
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
