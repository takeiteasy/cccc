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

// FFI resolution for a call/tail-call *callee*, as opposed to a bare name
// lookup (find_ffi_function above, still used directly by the reloc/patch
// passes and by runtime-helper lookups that have no guest Obj at all).
//
// #882: cross-module counterpart of the #880 shadow-fix above. A guest
// module can call a function that is itself *defined in a different
// translation unit* (declared here with no body, supplied at link time via
// `--link lib.c4a`), under a name that also happens to be a registered FFI
// symbol. find_ffi_function's/ffi_index_for_callee's exact-name match can't
// see that cross-module definition -- it isn't a body in *this* module's Obj
// list -- so such a call used to always resolve to the host FFI function.
// vm->compiler.link_syms is pre-scanned (cc_collect_link_symbols, called
// from main.c for every --link path before gen() runs) with exactly the
// symbol names that --link's own resolution pass will match, so consulting
// it here keeps this decision consistent with what --link/cc_link_bytecode
// will actually do afterwards. Only covers the compile-time-knowable case:
// a standalone `-c` build with no matching --link path, or a symbol
// supplied only later via runtime cc_load_module(), still resolves to FFI.
bool symbol_defined_by_linked_module(VirtualMachine *vm, const char *name) {
    if (!vm || !name || vm->compiler.link_syms.capacity == 0)
        return false;
    return hashmap_get(&vm->compiler.link_syms, name) != NULL;
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
    const char *name = obj_external_name(callee);
    if (symbol_defined_by_linked_module(vm, name))
        return -1; // #882: --link will supply this definition; emit CALL
    return find_ffi_function(vm, name);
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

void apply_global_relocations(VirtualMachine *vm, Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid global relocation");

            long long target_offset;
            long long value;
            int       segment;

            Obj      *target = find_global_obj(prog, *rel->label);
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
                    int ffi_idx =
                        find_ffi_function(vm, obj_external_name(target));
                    if (ffi_idx < 0)
                        error(
                            "unsupported relocation to undefined function: %s",
                            target->name);
                    long long slot  = var->offset + rel->offset;
                    long long token = CCCC_FFI_TOKEN_BASE - ffi_idx;
                    if (var->is_tls)
                        *(long long *)(vm->tls_template + slot) = token;
                    else
                        *(long long *)(vm->data_seg + slot) = token;
                    continue;
                }
                segment       = 1;
                target_offset = cc_pc_to_byte_offset((Pc)target->code_addr);
                value         = target_offset + rel->addend;
            } else {
                // #957: a static initializer taking &g needs the same
                // defined-or-deferred check as an ordinary reference (see
                // the fourth pass in gen()) -- find_global_obj resolves by
                // name regardless of whether target is ever defined, so
                // without this an extern-declared-but-never-defined global
                // silently gets an address into an inert zero slot.
                if (!target->is_definition && !target->is_tentative &&
                    !target->init_data &&
                    !(vm->compiler.compile_only || vm->compiler.deferred_link))
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

Obj *find_function_definition_for_patch(HashMap *fn_defs, Obj *target) {
    if (target->is_static && target->body)
        return target;

    return hashmap_get(fn_defs, target->name);
}

void add_debug_symbol(VirtualMachine *vm, char *name, long long offset,
                      Type *ty, int is_local, Obj *owner_fn) {
    if (!(vm->flags & CCCC_ENABLE_DEBUGGER) || !name || !*name)
        return;
    if (vm->dbg.num_debug_symbols >= MAX_DEBUG_SYMBOLS)
        return;

    DebugSymbol *sym = &vm->dbg.debug_symbols[vm->dbg.num_debug_symbols++];
    sym->name        = name;
    sym->offset      = offset;
    sym->ty          = ty;
    sym->is_local    = is_local;
    sym->scope_depth = 0;
    sym->owner_fn    = owner_fn;
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

// ========== Scalar Local Promotion (#249) ==========

typedef struct {
    Obj *var;
    int  score;
    bool address_escapes;
} PromotionCandidate;

int promoted_local_index(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.promoted_count; i++)
        if (vm->compiler.promoted_locals[i] == var)
            return i;
    return -1;
}

bool is_promoted_local(VirtualMachine *vm, Obj *var) {
    return promoted_local_index(vm, var) >= 0;
}

int promoted_local_reg(VirtualMachine *vm, Obj *var) {
    int idx = promoted_local_index(vm, var);
    return idx >= 0 ? vm->compiler.promoted_regs[idx] : -1;
}

static void promotion_alias_reset(VirtualMachine *vm) {
    vm->compiler.promotion_alias_count = 0;
}

void promotion_alias_add(VirtualMachine *vm, Obj *alias, Obj *target) {
    if (!alias || !target || vm->compiler.promotion_alias_count >= 16)
        return;
    for (int i = 0; i < vm->compiler.promotion_alias_count; i++) {
        if (vm->compiler.promotion_alias_vars[i] == alias) {
            vm->compiler.promotion_alias_targets[i] = target;
            return;
        }
    }
    int idx = vm->compiler.promotion_alias_count++;
    vm->compiler.promotion_alias_vars[idx]    = alias;
    vm->compiler.promotion_alias_targets[idx] = target;
}

static Obj *promotion_alias_target(VirtualMachine *vm, Obj *alias) {
    for (int i = 0; i < vm->compiler.promotion_alias_count; i++)
        if (vm->compiler.promotion_alias_vars[i] == alias)
            return vm->compiler.promotion_alias_targets[i];
    return NULL;
}

Obj *promoted_deref_target(VirtualMachine *vm, Node *node) {
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
    return ty->kind == TY_FLOAT || ty->kind == TY_DOUBLE ||
           ty->kind == TY_LDOUBLE;
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

static bool is_fp_promotion_candidate_ok(VirtualMachine *vm, Obj *fn,
                                         Obj *var) {
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

static void collect_promotion_candidates(VirtualMachine *vm, Obj *fn,
                                         Node *node, Node *parent,
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

void prepare_local_promotion(VirtualMachine *vm, Obj *fn, int base_stack_size) {
    vm->compiler.promoted_count = 0;
    promotion_alias_reset(vm);
    memset(vm->compiler.promoted_locals, 0,
           sizeof(vm->compiler.promoted_locals));
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

    // Only use S0-S3 for scalar promotion; S4-S7 are reserved for the restrict
    // cache.
    static const int sregs[] = {REG_S0, REG_S1, REG_S2, REG_S3};
    for (int i = 0; i < local_count && vm->compiler.promoted_count < 4; i++) {
        if (cands[i].address_escapes || cands[i].score < 3)
            continue;
        int p                                 = vm->compiler.promoted_count++;
        vm->compiler.promoted_locals[p]       = cands[i].var;
        vm->compiler.promoted_regs[p]         = sregs[p];
        vm->compiler.promoted_save_offsets[p] = -(base_stack_size + p + 1);
    }
    free(cands);
}

void prepare_fp_local_promotion(VirtualMachine *vm, Obj *fn,
                                int base_stack_size) {
    memset(vm->compiler.fp_promoted_locals, 0,
           sizeof(vm->compiler.fp_promoted_locals));
    memset(vm->compiler.fp_promoted_regs, 0,
           sizeof(vm->compiler.fp_promoted_regs));
    memset(vm->compiler.fp_promoted_save_offsets, 0,
           sizeof(vm->compiler.fp_promoted_save_offsets));
    memset(vm->compiler.fp_promoted_dirty, 0,
           sizeof(vm->compiler.fp_promoted_dirty));
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
    for (int i = 0; i < local_count && vm->compiler.fp_promoted_count < 4;
         i++) {
        if (cands[i].address_escapes || cands[i].score < 3)
            continue;
        int q                              = vm->compiler.fp_promoted_count++;
        vm->compiler.fp_promoted_locals[q] = cands[i].var;
        vm->compiler.fp_promoted_regs[q]   = fsregs[q];
        // Save slots come after the integer promoted save slots.
        vm->compiler.fp_promoted_save_offsets[q] =
            -(base_stack_size + vm->compiler.promoted_count + q + 1);
    }
    free(cands);
}

// ========== Restrict-param Deref Cache (#267) and Derived-Local Analysis
// (#269) ==========

// Evaluate a constant byte offset from a node (ND_NUM or ND_MUL(ND_NUM,
// ND_NUM)). Returns true and sets *out on success. Strips ND_CAST wrappers.
static bool eval_const_byte_offset(Node *n, long *out) {
    while (n && n->kind == ND_CAST)
        n = n->lhs;
    if (!n)
        return false;
    if (n->kind == ND_NUM) {
        *out = (long)n->val;
        return true;
    }
    if (n->kind == ND_MUL) {
        Node *ml = n->lhs, *mr = n->rhs;
        while (ml && ml->kind == ND_CAST)
            ml = ml->lhs;
        while (mr && mr->kind == ND_CAST)
            mr = mr->lhs;
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
// Only resolves direct restrict params (single-hop; derived locals not yet
// mapped).
static bool restrict_extract_base_offset(Node *expr, Obj **out_param,
                                         long *out_byte_offset,
                                         bool *out_var_offset) {
    while (expr && expr->kind == ND_CAST)
        expr = expr->lhs;
    if (!expr)
        return false;

    // Pattern: plain param
    if (expr->kind == ND_VAR) {
        Obj *var = expr->var;
        if (!var || !var->is_param || !var->ty || var->ty->kind != TY_PTR ||
            !var->ty->is_restrict || !var->ty->base ||
            !is_scalar_promotion_type(var->ty->base))
            return false;
        *out_param       = var;
        *out_byte_offset = 0;
        *out_var_offset  = false;
        return true;
    }

    // Pattern: param +/- offset
    if (expr->kind != ND_ADD && expr->kind != ND_SUB)
        return false;
    bool is_sub = (expr->kind == ND_SUB);

    // For ADD try both orderings; for SUB only (ptr - off)
    Node *sides[2][2] = {{expr->lhs, expr->rhs}, {expr->rhs, expr->lhs}};
    int   n_sides     = is_sub ? 1 : 2;
    for (int s = 0; s < n_sides; s++) {
        Node *ptr_node = sides[s][0], *off_node = sides[s][1];
        while (ptr_node && ptr_node->kind == ND_CAST)
            ptr_node = ptr_node->lhs;
        while (off_node && off_node->kind == ND_CAST)
            off_node = off_node->lhs;
        if (!ptr_node || !off_node || ptr_node->kind != ND_VAR)
            continue;
        Obj *var = ptr_node->var;
        if (!var || !var->is_param || !var->ty || var->ty->kind != TY_PTR ||
            !var->ty->is_restrict || !var->ty->base ||
            !is_scalar_promotion_type(var->ty->base))
            continue;
        long byte_off = 0;
        bool is_var   = !eval_const_byte_offset(off_node, &byte_off);
        if (is_sub && !is_var)
            byte_off = -byte_off;
        *out_param       = var;
        *out_byte_offset = is_var ? 0 : byte_off;
        *out_var_offset  = is_var;
        return true;
    }
    return false;
}

// ---- Pre-pass: collect derived locals (#269) ----

// Per-candidate state collected during the pre-pass AST walk.
#define MAX_DERIVED_CANDS 24
typedef struct {
    Obj *var;
    Obj *base_param;
    long byte_offset;
    bool var_offset;
    int  assign_count;
    bool addr_taken;
} DerivedCand;

static DerivedCand *derived_cand_find_or_create(DerivedCand *cands, int *nc,
                                                Obj *var) {
    for (int i = 0; i < *nc; i++)
        if (cands[i].var == var)
            return &cands[i];
    if (*nc >= MAX_DERIVED_CANDS)
        return NULL;
    DerivedCand *c  = &cands[(*nc)++];
    c->var          = var;
    c->base_param   = NULL;
    c->byte_offset  = 0;
    c->var_offset   = false;
    c->assign_count = 0;
    c->addr_taken   = false;
    return c;
}

static void restrict_derived_walk(Node *node, DerivedCand *cands, int *nc,
                                  bool *param_reassigned, Obj **rparams,
                                  int np) {
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
                if (c)
                    c->addr_taken = true;
            }
        }

        // Detect assignments: lhs = rhs
        if (node->kind == ND_ASSIGN && node->lhs && node->lhs->kind == ND_VAR) {
            Obj *lv = node->lhs->var;
            if (lv) {
                // Restrict param being reassigned → mark it
                for (int i = 0; i < np; i++)
                    if (rparams[i] == lv) {
                        param_reassigned[i] = true;
                        break;
                    }

                // Local pointer-to-scalar candidate
                if (lv->is_local && !lv->is_param && lv->ty &&
                    lv->ty->kind == TY_PTR && lv->ty->base &&
                    is_scalar_promotion_type(lv->ty->base)) {
                    DerivedCand *c = derived_cand_find_or_create(cands, nc, lv);
                    if (c) {
                        if (c->assign_count == 0)
                            restrict_extract_base_offset(
                                node->rhs, &c->base_param, &c->byte_offset,
                                &c->var_offset);
                        c->assign_count++;
                    }
                }
            }
        }

        restrict_derived_walk(node->lhs, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->rhs, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->cond, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->then, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->els, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->init, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->inc, cands, nc, param_reassigned, rparams,
                              np);
        restrict_derived_walk(node->body, cands, nc, param_reassigned, rparams,
                              np);
        for (Node *arg = node->args; arg; arg = arg->next)
            restrict_derived_walk(arg, cands, nc, param_reassigned, rparams,
                                  np);
    }
}

static void collect_restrict_derived_locals(VirtualMachine *vm, Obj *fn) {
    vm->compiler.restrict_derived_count = 0;
    memset(vm->compiler.restrict_derived_vars, 0,
           sizeof(vm->compiler.restrict_derived_vars));

    // Gather all restrict pointer params (at most 8 in CCCC's ABI)
    Obj *rparams[8];
    bool param_reassigned[8];
    int  np = 0;
    for (Obj *p = fn->params; p && np < 8; p = p->next)
        if (p->ty && p->ty->kind == TY_PTR && p->ty->is_restrict) {
            rparams[np]          = p;
            param_reassigned[np] = false;
            np++;
        }
    if (np == 0)
        return;

    DerivedCand cands[MAX_DERIVED_CANDS];
    int         nc = 0;
    restrict_derived_walk(fn->body, cands, &nc, param_reassigned, rparams, np);

    for (int i = 0; i < nc; i++) {
        DerivedCand *c = &cands[i];
        if (c->assign_count != 1)
            continue;
        if (c->addr_taken)
            continue;
        if (!c->base_param)
            continue;
        // Bail if the base restrict param was reassigned anywhere in the
        // function
        bool base_bad = false;
        for (int j = 0; j < np; j++)
            if (rparams[j] == c->base_param && param_reassigned[j]) {
                base_bad = true;
                break;
            }
        if (base_bad)
            continue;
        if (vm->compiler.restrict_derived_count >= MAX_RESTRICT_DERIVED)
            break;
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
// CCCC_CHECKED_BOUNDS (#770/#484) joins this set for the same reason: the
// indexed load/store fusion below elides the address computation entirely,
// so it would bypass CHKR the same way it would CHKP3/CHKT3 -- there is no
// separate re-derivation path for it the way the restrict-value cache gets
// (that cache instead declines checked-bounds derefs outright, see
// restrict_cache_handle_deref()). CCCC_FUSION_UNSAFE_FLAGS itself is
// declared in codegen_internal.h (used from codegen_emit.c/codegen_stmt.c
// too).

// Look up a variable in the restrict derivation map. Returns index or -1.
static int restrict_derived_find(VirtualMachine *vm, Obj *var) {
    for (int i = 0; i < vm->compiler.restrict_derived_count; i++)
        if (vm->compiler.restrict_derived_vars[i] == var)
            return i;
    return -1;
}

// ---- Cache setup ----

void prepare_restrict_cache(VirtualMachine *vm, Obj *fn, int base_stack_size) {
    vm->compiler.restrict_cache_count    = 0;
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

    // Pre-reserve all MAX_RESTRICT_CACHE slots (S4-S7 and their stack save
    // slots) so lazy binding can claim any slot without growing the frame
    // mid-function. Cache entries are bound on first deref access (count starts
    // at 0).
    static const int rcregs[] = {REG_S4, REG_S5, REG_S6, REG_S7};
    for (int q = 0; q < MAX_RESTRICT_CACHE; q++) {
        vm->compiler.restrict_cache_regs[q] = rcregs[q];
        // Save slots are placed after both integer and FP promoted-local save
        // slots.
        vm->compiler.restrict_cache_save_offsets[q] =
            -(base_stack_size + vm->compiler.promoted_count +
              vm->compiler.fp_promoted_count + q + 1);
    }
    vm->compiler.restrict_cache_capacity = MAX_RESTRICT_CACHE;
}

static int restrict_cache_find(VirtualMachine *vm, Obj *param,
                               long byte_offset) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        if (vm->compiler.restrict_cache_params[i] == param &&
            vm->compiler.restrict_cache_offsets[i] == byte_offset)
            return i;
    return -1;
}

// Invalidate all restrict cache entries (called at control-flow join points).
void restrict_cache_invalidate_all(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        vm->compiler.restrict_cache_valid[i] = false;
}

// Invalidate all cache entries for a restrict param (all cached offsets for
// it).
static void restrict_cache_invalidate_param(VirtualMachine *vm, Obj *param) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        if (vm->compiler.restrict_cache_params[i] == param)
            vm->compiler.restrict_cache_valid[i] = false;
}

// Bind next free cache slot to (param, byte_offset). Returns index or -1 if
// full.
static int restrict_cache_alloc(VirtualMachine *vm, Obj *param,
                                long byte_offset) {
    if (vm->compiler.restrict_cache_count >= MAX_RESTRICT_CACHE)
        return -1;
    int idx = vm->compiler.restrict_cache_count++;
    vm->compiler.restrict_cache_params[idx]  = param;
    vm->compiler.restrict_cache_offsets[idx] = byte_offset;
    vm->compiler.restrict_cache_valid[idx]   = false;
    return idx;
}

// Extract (restrict_param, byte_offset) from a ND_DEREF node.
// Handles *p (offset 0) and p[const] (constant element index).
// Also handles *q and q[const] where q is a derived local (see #269).
// Returns true and sets out_param/out_byte_offset on success.
static bool restrict_const_deref_extract(VirtualMachine *vm, Node *node,
                                         Obj **out_param,
                                         long *out_byte_offset) {
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
            *out_param       = var;
            *out_byte_offset = 0;
            return true;
        }
        // Derived local: *q where q = p + const
        int di = restrict_derived_find(vm, var);
        if (di >= 0 && !vm->compiler.restrict_derived_var_offset[di]) {
            *out_param       = vm->compiler.restrict_derived_params[di];
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
        while (ptr_node && ptr_node->kind == ND_CAST)
            ptr_node = ptr_node->lhs;
        while (off_node && off_node->kind == ND_CAST)
            off_node = off_node->lhs;
        if (!ptr_node || !off_node || ptr_node->kind != ND_VAR)
            continue;
        Obj *var = ptr_node->var;

        // Resolve: is var a restrict param or a derived local?
        Obj *base_param = NULL;
        long base_off   = 0;
        if (var && var->is_param && var->ty && var->ty->kind == TY_PTR &&
            var->ty->is_restrict && var->ty->base &&
            is_scalar_promotion_type(var->ty->base)) {
            base_param = var;
            base_off   = 0;
        } else {
            int di = restrict_derived_find(vm, var);
            if (di < 0 || vm->compiler.restrict_derived_var_offset[di])
                continue;
            base_param = vm->compiler.restrict_derived_params[di];
            base_off   = vm->compiler.restrict_derived_offsets[di];
        }

        // Evaluate the constant element offset
        long elem_off = 0;
        if (!eval_const_byte_offset(off_node, &elem_off))
            continue;
        *out_param       = base_param;
        *out_byte_offset = base_off + elem_off;
        return true;
    }
    return false;
}

// Walk a pointer expression to find its restrict-param root (for indexed
// stores). Also resolves derived locals (q = p + k maps q → p). Returns the
// restrict param Obj if found, NULL otherwise.
static Obj *restrict_root_param_of_ptr(VirtualMachine *vm, Node *ptr) {
    while (ptr) {
        if (ptr->kind == ND_CAST) {
            ptr = ptr->lhs;
            continue;
        }
        if (ptr->kind == ND_VAR) {
            Obj *var = ptr->var;
            if (var && var->is_param && var->ty && var->ty->kind == TY_PTR &&
                var->ty->is_restrict)
                return var;
            // Derived local: *q where q = p + k → root is p
            int di = restrict_derived_find(vm, var);
            if (di >= 0)
                return vm->compiler.restrict_derived_params[di];
            return NULL;
        }
        if (ptr->kind == ND_ADD || ptr->kind == ND_SUB) {
            Obj *r = restrict_root_param_of_ptr(vm, ptr->lhs);
            if (r)
                return r;
            return restrict_root_param_of_ptr(vm, ptr->rhs);
        }
        return NULL;
    }
    return NULL;
}

// Called on ND_DEREF to check/populate the restrict cache.
// Handles *restrict_param and restrict_param[const] patterns.
// Returns true and emits a register copy or a load+cache-fill on hit/miss.
bool restrict_cache_handle_deref(VirtualMachine *vm, Node *node, int dest_reg) {
    if (vm->compiler.restrict_cache_capacity == 0)
        return false;

    // A checked-pointer access (#770/#484) must run gen_addr's CHKR check on
    // every hit, not just the address re-derivation both the cache-hit and
    // cache-miss branches below already do for CHKP3/CHKT3 -- rather than
    // teach both branches a second, differently-shaped safety check, decline
    // the cache for this access and let the ordinary gen_addr(vm, node, ...)
    // path (which already emits CHKR) handle it. Narrow: only checked-bounds
    // derefs are affected, so the restrict cache still applies to every
    // other access.
    if ((vm->flags & CCCC_CHECKED_BOUNDS) && node->checked_bounds_lo &&
        node->checked_bounds_hi)
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

    // Cache miss: load the value at (param + byte_off) into cache_reg, mark
    // valid
    int r_addr = alloc_temp_reg();
    gen_expr(vm, node->lhs,
             r_addr); // evaluates the full address (p or p+offset)
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
// If the store goes through a restrict param, update or invalidate cache
// entries. If the store goes through a non-restrict pointer, do nothing
// (restrict contract).
void restrict_cache_handle_store(VirtualMachine *vm, Node *lhs, int val_reg) {
    if (vm->compiler.restrict_cache_capacity == 0)
        return;
    if (!lhs || lhs->kind != ND_DEREF || !lhs->lhs)
        return;

    // *p = val or p[const] = val: write-through the specific (param, offset)
    // entry.
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
            // Truncate to pointee width (e.g. char *restrict: cache holds
            // byte).
            emit_normalize_promoted_scalar(vm, param->ty->base, cache_reg);
            vm->compiler.restrict_cache_valid[idx] = true;
        }
        return;
    }

    // p[var] or derived pointer store: find the restrict param root and
    // invalidate all of its cached offsets (we don't know which byte_offset was
    // hit).
    Obj *base = restrict_root_param_of_ptr(vm, lhs->lhs);
    if (base) {
        restrict_cache_invalidate_param(vm, base);
        return;
    }
    // Unknown base (e.g. int *r = p; *r = x): conservatively invalidate
    // everything.
    restrict_cache_invalidate_all(vm);
}

void emit_save_restrict_cache_regs(VirtualMachine *vm) {
    for (int i = 0; i < vm->compiler.restrict_cache_count; i++)
        emit_local_store(vm, ty_long, vm->compiler.restrict_cache_regs[i],
                         vm->compiler.restrict_cache_save_offsets[i]);
}

void emit_restore_restrict_cache_regs(VirtualMachine *vm) {
    for (int i = vm->compiler.restrict_cache_count - 1; i >= 0; i--)
        emit_local_load(vm, ty_long, vm->compiler.restrict_cache_regs[i],
                        vm->compiler.restrict_cache_save_offsets[i]);
}
