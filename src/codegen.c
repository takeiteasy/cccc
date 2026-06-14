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
#include "cccc.h"
#include <ctype.h>

// ========== FFI Helper ==========

static int find_ffi_function(VirtualMachine *vm, const char *name) {
    if (!vm || !name)
        return -1;

    // Exact match: compare using the cached name_len to avoid repeated strlen. (#164)
    size_t len = strlen(name);
    for (int i = 0; i < vm->compiler.ffi_count; i++) {
        if (vm->compiler.ffi_table[i].name_len == len &&
            memcmp(vm->compiler.ffi_table[i].name, name, len) == 0) {
            return i;
        }
    }

    // If no exact match, check if this looks like a specialized variadic name
    // (e.g. "printf2" → base "printf"). Compute base_len once.
    if ((len > 1 && isdigit(name[len - 1])) ||
        (len > 2 && isdigit(name[len - 1]) && isdigit(name[len - 2]))) {
        char base_name[256];
        strncpy(base_name, name, sizeof(base_name) - 1);
        base_name[sizeof(base_name) - 1] = '\0';

        size_t base_len = len;
        while (base_len > 0 && isdigit(base_name[base_len - 1])) {
            base_len--;
        }
        base_name[base_len] = '\0';

        for (int i = 0; i < vm->compiler.ffi_count; i++) {
            if (vm->compiler.ffi_table[i].is_variadic &&
                vm->compiler.ffi_table[i].name_len == base_len &&
                memcmp(vm->compiler.ffi_table[i].name, base_name, base_len) == 0) {
                return i;
            }
        }
    }

    return -1;
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
    if (vm->compiler.num_data_relocs >= MAX_CALLS)
        error("too many data relocations");
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].data_offset =
        data_offset;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_segment =
        target_segment;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].target_offset =
        target_offset;
    vm->compiler.data_relocs[vm->compiler.num_data_relocs].addend = addend;
    vm->compiler.num_data_relocs++;
}

static void apply_global_relocations(VirtualMachine *vm, Obj *prog) {
    for (Obj *var = prog; var; var = var->next) {
        if (var->is_function)
            continue;

        for (Relocation *rel = var->rel; rel; rel = rel->next) {
            if (!rel->label || !*rel->label)
                error("invalid global relocation");

            Obj *target = find_global_obj(prog, *rel->label);
            if (!target)
                error("undefined relocation target: %s", *rel->label);

            long long target_offset;
            long long value;
            int segment;

            if (target->is_function) {
                if (!target->body)
                    error("unsupported relocation to undefined function: %s",
                          target->name);
                segment = 1;
                target_offset = cc_pc_to_byte_offset((Pc)target->code_addr);
                value = target_offset + rel->addend;
            } else {
                segment = 0;
                target_offset = target->offset;
                value = (long long)(vm->data_seg + target_offset + rel->addend);
            }

            long long data_offset = var->offset + rel->offset;
            *(long long *)(vm->data_seg + data_offset) = value;
            add_data_reloc(vm, data_offset, segment, target_offset, rel->addend);
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

// ========== Restrict-param Deref Cache (#267) and Derived-Local Analysis (#269) ==========

// Forward declarations for helpers used by the restrict cache
static void emit_mov3(VirtualMachine *vm, int rd, int rs);
static void emit_local_store(VirtualMachine *vm, Type *ty, int rd_val, long long offset);
static void emit_local_load(VirtualMachine *vm, Type *ty, int rd, long long offset);
static void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr);
static void emit_normalize_promoted_scalar(VirtualMachine *vm, Type *ty, int reg);
static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);

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
        // Save slots are placed after the promoted-local save slots.
        vm->compiler.restrict_cache_save_offsets[q] =
            -(base_stack_size + vm->compiler.promoted_count + q + 1);
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
    // Requires opt_level >= 2 and no safety flags (same guards as indexed load).
    if (addr->kind != ND_ADD || vm->compiler.opt_level < 2)
        return false;
    if (vm->flags & (CCCC_POINTER_CHECKS | CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK))
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
        // Cache hit: use the S-reg directly
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

static bool contains_funcall(Node *node) {
    if (!node)
        return false;

    if (node->kind == ND_FUNCALL || node->kind == ND_BLOCK_CALL)
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
    if (find_ffi_function(vm, callee->name) >= 0)
        return false; // FFI — goes through CALLF, not CALL
    if (callee->is_nested)
        return false; // needs static link in REG_A0
    if (expr->func_ty && expr->func_ty->is_variadic)
        return false; // va_area is part of the frame
    if (callee->is_noreturn)
        return false; // BTRAP emitted after CALL; can't compose with CALLT
    if (expr->ty && (expr->ty->kind == TY_STRUCT || expr->ty->kind == TY_UNION))
        return false; // RETBUF machinery — incompatible with frame reuse
    int nargs = 0;
    for (Node *a = expr->args; a; a = a->next)
        nargs++;
    if (nargs > 8)
        return false; // stack-spill args would be below the unwound frame
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
        var->ty->kind == TY_COMPLEX)
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
    label_defs[num_label_defs].name = name;
    label_defs[num_label_defs].offset = vm->text_ptr + 1;
    num_label_defs++;
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

static Pc emit_lta3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LTA3, rd, offset);
}

// LEA3: rd = bp + offset
static Pc emit_lea3(VirtualMachine *vm, int rd, long long offset) {
    return emit_ri(vm, LEA3, rd, offset);
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
    case FR2R: return FR2R_F32;
    case R2FR: return R2FR_F32;
    default: return f64_op;
    }
}

// Load operations based on type
static void emit_bitint_trunc(VirtualMachine *vm, Type *ty, int reg);
static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);

static void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr) {
    if (vm->flags & CCCC_POINTER_CHECKS)
        emit_rr(vm, CHKP3, rs_addr, 0);
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
        if (ty->size == 1) {
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
        emit_bitint_trunc(vm, ty, rd);
    } else if (is_flonum(ty)) {
        emit_rr(vm, ty->kind == TY_FLOAT ? FLDR_F32 : FLDR, rd, rs_addr);
    } else {
        emit_rr(vm, LDR_D, rd, rs_addr);
    }
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
    if (vm->flags & (CCCC_POINTER_CHECKS | CCCC_INVALID_ARITH |
                     CCCC_PROVENANCE_TRACK))
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
    if (ty->kind == TY_BITINT) {
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
    if (ty->kind == TY_BITINT) {
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

static bool emit_indexed_load_if_possible(VirtualMachine *vm, Node *node, int dest_reg) {
    if (!node || node->kind != ND_DEREF || !node->lhs ||
        node->ty->kind == TY_ARRAY || node->ty->kind == TY_STRUCT ||
        node->ty->kind == TY_UNION || node->ty->kind == TY_COMPLEX)
        return false;
    IndexedAddr idx = {};
    if (!match_indexed_addr(vm, node->lhs, &idx))
        return false;
    int r_base = alloc_temp_reg();
    gen_expr(vm, idx.base, r_base);
    mark_temp_reg_used(r_base);
    int r_index = alloc_temp_reg();
    gen_expr(vm, idx.index, r_index);
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
        ty->kind == TY_STRUCT || ty->kind == TY_UNION || ty->kind == TY_COMPLEX)
        return false;
    IndexedAddr idx = {};
    if (!match_indexed_addr(vm, lhs->lhs, &idx))
        return false;
    int r_base = alloc_temp_reg();
    gen_expr(vm, idx.base, r_base);
    mark_temp_reg_used(r_base);
    int r_index = alloc_temp_reg();
    gen_expr(vm, idx.index, r_index);
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

// Store operations based on type
static void emit_store(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr) {
    if (vm->flags & CCCC_POINTER_CHECKS)
        emit_rr(vm, CHKP3, rs_addr, 0);
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
        if (ty->size == 1)       emit_rr(vm, STR_B, rd_val, rs_addr);
        else if (ty->size == 2)  emit_rr(vm, STR_H, rd_val, rs_addr);
        else if (ty->size == 4)  emit_rr(vm, STR_W, rd_val, rs_addr);
        else                     emit_rr(vm, STR_D, rd_val, rs_addr);
    } else if (is_flonum(ty)) {
        emit_rr(vm, ty->kind == TY_FLOAT ? FSTR_F32 : FSTR, rd_val, rs_addr);
    } else {
        emit_rr(vm, STR_D, rd_val, rs_addr);
    }
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

    // Find FFI index and annotate for JBC rehydration
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

// Recompile any asm-passthru FFI entries whose func_ptr was lost during JBC
// serialization.  Called from the JBC load path after stdlib/library resolution.
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
           node->ty->kind != TY_COMPLEX;
}

// ========== Safety Instrumentation Helpers ==========

// Register stack variable metadata for runtime instrumentation.
// Uses integer key (var->offset) so ops can look it up with hashmap_get_int.
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
    hashmap_put_int(&vm->stack_var_meta, offset, meta);
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

// Emit CHKL (check liveness) for local at bp+offset.
static void emit_chkl(VirtualMachine *vm, long long offset) {
    emit(vm, CHKL);
    emit_i64(vm, offset);
}

// Emit MARKR (mark read) for local at bp+offset.
static void emit_markr(VirtualMachine *vm, long long offset) {
    emit(vm, MARKR);
    emit_i64(vm, offset);
}

// Emit MARKW (mark write) for local at bp+offset.
static void emit_markw(VirtualMachine *vm, long long offset) {
    emit(vm, MARKW);
    emit_i64(vm, offset);
}

// Emit MARKA (mark address for dangling detection).
// rs holds the pointer; offset/size/scope_id are compile-time immediates.
static void emit_marka(VirtualMachine *vm, int rs, long long offset, size_t size,
                       int scope_id) {
    emit(vm, MARKA);
    emit_word(vm, ENCODE_R(rs));
    emit_i64(vm, offset);
    emit_i64(vm, (long long)size);
    emit_word(vm, scope_id);
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

            if (vm->compiler.num_func_addr_patches >= MAX_CALLS) {
                error("too many function address references");
            }
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
                int cap_offset = (cap_idx + 1) * 8;
                emit_lea3(vm, dest_reg, static_link->offset); // &__static_link
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
                    emit_lea3(vm, dest_reg,
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
                    // For struct/union parameters, the slot contains a pointer
                    // to the struct We need to load that pointer, not the slot
                    // address
                    if (node->var->is_param && (node->ty->kind == TY_STRUCT ||
                                                node->ty->kind == TY_UNION)) {
                        emit_lea3(vm, dest_reg,
                                  node->var->offset); // Slot address
                        emit_rr(vm, LDR_D, dest_reg,
                                dest_reg); // Load pointer from slot
                    } else if (node->var->is_block_var) {
                        // __block variable: slot contains pointer to
                        // heap-allocated wrapper
                        emit_lea3(vm, dest_reg,
                                  node->var->offset); // Slot address
                        emit_rr(vm, LDR_D, dest_reg,
                                dest_reg); // Load heap pointer from slot
                        // dest_reg now points to actual storage on heap
                    } else {
                        emit_lea3(vm, dest_reg, node->var->offset);
                    }
                }
            }
        } else {
            // Global variable
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
                emit_rr(vm, fop_for_type(node->ty->base, I2F3), real_reg,
                        real_reg);
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

// ========== Expression Generation ==========

// Generate code for expression, result in dest_reg (integer) or dest_freg
// (float)
static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg) {
    if (!node) {
        error("codegen: null expression node");
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
            if (vm->compiler.num_func_addr_patches >= MAX_CALLS) {
                error("too many function address references");
            }
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .location = addr_loc;
            vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
                .function = node->var;
            vm->compiler.num_func_addr_patches++;
        } else {
            // Stack instrumentation for scalar locals (not arrays/structs).
            if (node->var->is_local && !node->var->is_param &&
                node->var->ty && node->var->ty->kind != TY_ARRAY &&
                node->var->ty->kind != TY_STRUCT &&
                node->var->ty->kind != TY_UNION) {
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
            // Fused local load: skip the LEA3+LDR two-step for simple locals
            if (is_simple_local_scalar(vm, node)) {
                emit_local_load(vm, node->ty, dest_reg, node->var->offset);
            } else if (is_flonum(node->ty)) {
                // For float types, FREG_A0-A7 have the same raw numbers as
                // REG_A0-A7. Use a temp register to avoid clobbering int regs.
                int r_addr = alloc_temp_reg();
                gen_addr(vm, node, r_addr);
                emit_load(vm, node->ty, dest_reg, r_addr);
                free_temp_reg(r_addr);
            } else {
                gen_addr(vm, node, dest_reg);
                // For scalars, load the value
                if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
                    node->ty->kind != TY_UNION) {
                    emit_load(vm, node->ty, dest_reg, dest_reg);
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
        if (node->ty->kind != TY_ARRAY && node->ty->kind != TY_STRUCT &&
            node->ty->kind != TY_UNION) {
            emit_load(vm, node->ty, dest_reg, dest_reg);
        }
        return;

    case ND_ADDR:
        gen_addr(vm, node->lhs, dest_reg);
        // Track explicit address-of a local var for dangling detection and provenance.
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_local &&
            !node->lhs->var->is_block_var) {
            size_t var_size = node->lhs->var->ty ? node->lhs->var->ty->size : 8;
            if (vm->flags & (CCCC_DANGLING_DETECT | CCCC_STACK_INSTR))
                emit_marka(vm, dest_reg, node->lhs->var->offset, var_size,
                           vm->current_function_scope_id);
            if (vm->flags & CCCC_PROVENANCE_TRACK)
                emit_markp(vm, dest_reg, dest_reg, 1 /* STACK */, var_size);
        }
        return;

    case ND_NEG:
        gen_expr(vm, node->lhs, dest_reg);
        if (is_flonum(node->ty)) {
            emit_frr(vm, fop_for_type(node->ty, FNEG3), dest_reg, dest_reg);
        } else {
            emit_rr(vm, NEG3, dest_reg, dest_reg);
        }
        return;

    case ND_NOT:
        gen_expr(vm, node->lhs, dest_reg);
        emit_rr(vm, NOT3, dest_reg, dest_reg);
        return;

    case ND_BITNOT:
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
            int r_rhs = alloc_temp_reg();

            if (rhs_has_call) {
                // For floats: convert to int, push to stack, evaluate RHS, pop,
                // convert back dest_reg is FREG_*, so we use FR2R to move bits
                // to an int temp
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
            emit_frrr(vm, fop_for_type(node->lhs->ty, fop), dest_reg, dest_reg,
                      r_rhs);
            free_temp_reg(r_rhs);
        } else {
            // Integer operations
            gen_expr(vm, node->lhs, dest_reg); // LHS goes directly to dest

            // LHS might contain a function call which resets temp regs.
            // Re-mark dest_reg as used so r_rhs allocation doesn't clobber it.
            mark_temp_reg_used(dest_reg);
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
                emit_rr(vm, CHKB, dest_reg, r_rhs);

            emit_rrr(vm, op, dest_reg, dest_reg, r_rhs);

            if (node->ty->kind == TY_BITINT)
                emit_bitint_trunc(vm, node->ty, dest_reg);

            if (is_ptr_arith && (vm->flags & (CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK))) {
                emit(vm, CHKPA);
                emit_word(vm, ENCODE_R(dest_reg));
            }
            free_temp_reg(r_rhs);
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
            (node->ty->kind == TY_STRUCT || node->ty->kind == TY_UNION)) {
            // Struct/union assignment: memcpy from RHS address to LHS address
            int r_src = alloc_temp_reg();
            gen_expr(vm, node->rhs, r_src); // RHS is struct address
            mark_temp_reg_used(r_src);

            int r_dest = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_dest); // LHS address

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
        bool lhs_indexed = node->lhs->kind == ND_DEREF &&
                           match_indexed_addr(vm, node->lhs->lhs,
                                              &(IndexedAddr){});
        int r_addr = -1;
        if (!lhs_fused && !lhs_indexed) {
            // Now compute LHS address (after any function calls in RHS are done)
            r_addr = alloc_temp_reg();
            gen_addr(vm, node->lhs, r_addr);
        }

        // Handle Bitfields specially (Read-Modify-Write)
        if (node->lhs->kind == ND_MEMBER && node->lhs->member->is_bitfield) {
            Member *mem = node->lhs->member;
            int r_container = alloc_temp_reg();

            // Load container value
            emit_load(vm, mem->ty, r_container,
                      r_addr); // Use member type (container)

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
            emit_store(vm, mem->ty, r_container,
                       r_addr); // Use member type (container)

            free_temp_reg(r_new);
            free_temp_reg(r_mask);
            free_temp_reg(r_container);
        } else if (node->lhs->kind == ND_VAR &&
                   is_promoted_local(vm, node->lhs->var)) {
            emit_promoted_write(vm, node->lhs->var, r_val);
        } else if (lhs_indexed &&
                   emit_indexed_store_if_possible(vm, node->lhs, node->ty,
                                                  r_val)) {
            // stored by fused indexed opcode
        } else if (lhs_fused) {
            emit_local_store(vm, node->ty, r_val, node->lhs->var->offset);
        } else {
            // Standard store
            emit_store(vm, node->ty, r_val, r_addr);
        }

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
        // Ternary: cond ? then : else
        int r_cond = alloc_temp_reg();
        gen_expr(vm, node->cond, r_cond);
        Pc jz_else = emit_jz3(vm, r_cond);
        free_temp_reg(r_cond);

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

    case ND_MEMBER:
        gen_addr(vm, node, dest_reg);

        if (node->member->is_bitfield) {
            Member *mem = node->member;
            // Load container value
            emit_load(vm, mem->ty, dest_reg, dest_reg);

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
                emit_load(vm, node->ty, dest_reg, dest_reg);
            }
        }
        return;

    case ND_CAST:
        if (is_complex(node->lhs->ty)) {
            int imag_reg = (dest_reg == FREG_A7) ? FREG_A6 : FREG_A7;
            gen_complex_expr(vm, node->lhs, dest_reg, imag_reg);
            if (!is_flonum(node->ty))
                emit_rr(vm, fop_for_type(node->lhs->ty->base, F2I3), dest_reg,
                        dest_reg);
            else if (node->ty->kind == TY_FLOAT)
                emit_fround_f32(vm, dest_reg, dest_reg);
            return;
        }
        gen_expr(vm, node->lhs, dest_reg);
        // Add type conversion if needed
        if (is_flonum(node->ty) && !is_flonum(node->lhs->ty)) {
            // int -> float
            emit_rr(vm, fop_for_type(node->ty, I2F3), dest_reg, dest_reg);
        } else if (!is_flonum(node->ty) && is_flonum(node->lhs->ty)) {
            // float -> int
            emit_rr(vm, fop_for_type(node->lhs->ty, F2I3), dest_reg, dest_reg);
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
        if (is_complex(node->ty))
            error_tok(vm, node->tok,
                      "complex function return ABI is not supported");
        for (Node *arg = node->args; arg; arg = arg->next)
            if (is_complex(arg->ty))
                error_tok(vm, arg->tok,
                          "complex function argument ABI is not supported");

        // Check if this is a builtin alloca call (used for VLAs)
        if (node->lhs->kind == ND_VAR &&
            node->lhs->var == vm->compiler.builtin_alloca) {
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

        // Check if this is setjmp builtin
        if (node->lhs->kind == ND_VAR &&
            node->lhs->var == vm->compiler.builtin_setjmp) {
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

        // Check if this is longjmp builtin
        if (node->lhs->kind == ND_VAR &&
            node->lhs->var == vm->compiler.builtin_longjmp) {
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
        }

        // Check for FFI call - foreign functions use register-based calling
        // convention with operand-based metadata (ffi_idx, nargs,
        // double_arg_mask)
        int ffi_idx = -1;
        if (node->lhs->kind == ND_VAR && node->lhs->var->is_function) {
            ffi_idx = find_ffi_function(vm, node->lhs->var->name);
        }

        if (ffi_idx >= 0) {
            // FFI call: args are stored as source-order 64-bit slots.
            // Slots 0-7 use REG_A0-A7; slots 8+ are pushed on the VM stack.
            reset_temp_regs();

            // Count arguments and compute double_arg_mask/float_arg_mask
            int nargs = 0;
            uint64_t double_arg_mask = 0;
            uint64_t float_arg_mask = 0;
            for (Node *arg = node->args; arg; arg = arg->next) {
                if (is_flonum(arg->ty)) {
                    if (nargs >= 64)
                        error_tok(vm, arg->tok,
                                  "too many floating-point FFI arguments");
                    if (arg->ty->kind == TY_FLOAT)
                        float_arg_mask |= (1ULL << nargs);
                    else
                        double_arg_mask |= (1ULL << nargs);
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
                if (is_flonum(arg->ty)) {
                    gen_expr(vm, arg, FREG_A0);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_T0, FREG_A0);
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

                if (is_flonum(arg->ty)) {
                    gen_expr(vm, arg, FREG_A0);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_A0 + i, FREG_A0);
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
                                     ret_ty->kind == TY_UNION))) {
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
                                       ret_ty->kind == TY_UNION)) &&
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
                if (is_flonum(arg->ty)) {
                    // Float arg: evaluate to float reg, move bits to int reg,
                    // push
                    int freg = FREG_A0; // Use as scratch
                    gen_expr(vm, arg, freg);
                    emit_rr(vm, fop_for_type(arg->ty, FR2R), REG_T0,
                            freg); // Move bits to REG_T0
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

            if (is_flonum(arg->ty)) {
                if (is_vararg) {
                    // Variadic double: put in integer register (as bit pattern)
                    // ENT3 will spill REG_A* to stack; va_arg reads from stack
                    if (int_arg_idx < 8) {
                        // Generate double value into a float reg, then move
                        // bits to int reg
                        int freg = FREG_A0; // Use FREG_A0 as scratch
                        gen_expr(vm, arg, freg);
                        // Move double bits from freg to int reg (bit-pattern,
                        // not conversion)
                        emit_rr(vm, FR2R, REG_A0 + int_arg_idx, freg);
                        int_arg_idx++;
                    }
                } else {
                    // Fixed param double: put in float register
                    if (float_arg_idx < 8) {
                        gen_expr(vm, arg, FREG_A0 + float_arg_idx);
                        float_arg_is_f32[float_arg_idx] =
                            arg->ty->kind == TY_FLOAT;
                        float_arg_idx++;
                    }
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
            if (callee_parent == current_fn) {
                // Calling our own nested function - pass our bp
                emit_lea3(vm, REG_A0, 0); // LEA3 with offset 0 = current bp
            } else if (current_fn && current_fn->is_nested) {
                // We're nested and calling a sibling or parent's nested
                // function Walk our static chain to find callee's parent's bp
                Obj *static_link = find_static_link_var(current_fn);
                if (static_link) {
                    emit_lea3(vm, REG_A0, static_link->offset);
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
                    emit_lea3(vm, REG_A0, 0);
                }
            } else {
                // Fallback: use current bp (shouldn't happen if parser is
                // correct)
                emit_lea3(vm, REG_A0, 0);
            }
        }

        // Capture and immediately clear the tail-call flag so that argument
        // sub-calls (e.g. return f(g(x))) never see it and suppress g's CALL.
        bool is_tail = vm->compiler.emitting_tail_call;
        vm->compiler.emitting_tail_call = false;

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
                    if (vm->compiler.num_call_patches >= MAX_CALLS) {
                        error("too many function calls");
                    }
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
                emit(vm, CALLN);
                emit_word(vm, ENCODE_R(r_fn));
                emit_word(vm, ((InstrWord)(node->ty->kind == TY_FLOAT ? 0
                                                : is_flonum(node->ty) ? 1 : 0) << 16) |
                                  ((InstrWord)(node->ty->kind == TY_FLOAT ? 1 : 0) << 17) |
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
        // Also invalidate restrict cache: callee may have modified *restrict_params.
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
        } else {
            if (dest_reg != REG_A0) {
                emit_mov3(vm, dest_reg, REG_A0);
            }
        }
        return;
    }

    // ND_MEMZERO is handled in gen_stmt, but can appear in expr context
    case ND_MEMZERO:
        // Zero-initialize memory - typically done at local var declaration
        // For now, just return (handled via assignment)
        return;

    case ND_LOGAND: {
        // Logical AND with short-circuit evaluation
        int r_cond = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_cond);
        Pc jz_false = emit_jz3(vm, r_cond);

        gen_expr(vm, node->rhs, r_cond);
        Pc jz_false2 = emit_jz3(vm, r_cond);

        // Both true
        emit_li3(vm, dest_reg, 1);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        // At least one false
        vm->text_seg[jz_false] = vm->text_ptr + 1;
        vm->text_seg[jz_false2] = vm->text_ptr + 1;
        emit_li3(vm, dest_reg, 0);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        free_temp_reg(r_cond);
        return;
    }

    case ND_LOGOR: {
        // Logical OR with short-circuit evaluation
        int r_cond = alloc_temp_reg();
        gen_expr(vm, node->lhs, r_cond);
        Pc jnz_true = emit_jnz3(vm, r_cond);

        gen_expr(vm, node->rhs, r_cond);
        Pc jnz_true2 = emit_jnz3(vm, r_cond);

        // Both false
        emit_li3(vm, dest_reg, 0);
        emit(vm, JMP);
        Pc jmp_end = emit_word_ptr(vm);

        // At least one true
        vm->text_seg[jnz_true] = vm->text_ptr + 1;
        vm->text_seg[jnz_true2] = vm->text_ptr + 1;
        emit_li3(vm, dest_reg, 1);
        vm->text_seg[jmp_end] = vm->text_ptr + 1;
        free_temp_reg(r_cond);
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

    case ND_VLA_PTR:
        // VLA pointer/designator: load the stored pointer value
        // VLAs are implemented by storing a pointer to dynamically allocated
        // memory The pointer itself is a local variable
        if (node->var->is_local) {
            emit_lea3(vm, dest_reg, node->var->offset); // Address of pointer
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
        // Block literal: creates a block descriptor containing:
        // [0] = invoke pointer (function address)
        // [8...] = captured variable values (if any)
        //
        // Always creates a descriptor even for no-capture blocks
        // for uniform calling convention.

        int num_captures = node->num_block_captures;
        int descriptor_slots = 1 + num_captures; // invoke + captures
        int descriptor_size = descriptor_slots * 8;

        // Allocate descriptor in data segment
        long long desc_offset = vm->data_ptr - vm->data_seg;
        desc_offset = (desc_offset + 7) & ~7; // Align to 8 bytes
        check_data_capacity(vm, desc_offset + descriptor_size);
        vm->data_ptr = vm->data_seg + desc_offset;

        vm->data_ptr += descriptor_size;

        // Load descriptor address into temp register
        int r_desc = alloc_temp_reg();
        emit_lda3(vm, r_desc, desc_offset);
        mark_temp_reg_used(r_desc);

        // Load function address (will be patched later)
        int r_invoke = alloc_temp_reg();
        Pc invoke_addr_loc = emit_lta3(vm, r_invoke, 0); // Placeholder

        // Record patch for block function address
        if (vm->compiler.num_func_addr_patches >= MAX_CALLS)
            error("too many function address references");
        vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
            .location = invoke_addr_loc;
        vm->compiler.func_addr_patches[vm->compiler.num_func_addr_patches]
            .function = node->block_fn;
        vm->compiler.num_func_addr_patches++;

        // Store invoke pointer at descriptor[0]
        emit_rr(vm, STR_D, r_invoke, r_desc);
        free_temp_reg(r_invoke);

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
                // outer function's frame.
                Obj *static_link = find_static_link_var(enc_fn);
                emit_lea3(vm, r_val, static_link->offset);
                emit_rr(vm, LDR_D, r_val, r_val);                    // descriptor ptr
                emit_addi3(vm, r_val, r_val, (enc_cap_idx + 1) * 8); // slot addr
                if (cap->is_block_var)
                    emit_rr(vm, LDR_D, r_val, r_val); // heap ptr from descriptor slot
                else
                    emit_load(vm, cap->ty, r_val, r_val); // value from descriptor slot
            } else if (cap->is_block_var) {
                // __block var directly in enclosing stack: copy heap pointer
                emit_lea3(vm, r_val, cap->offset);
                emit_rr(vm, LDR_D, r_val, r_val);
            } else if (cap->is_local) {
                // Regular local directly in enclosing stack: copy value
                emit_lea3(vm, r_val, cap->offset);
                emit_load(vm, cap->ty, r_val, r_val);
            } else {
                // Global
                emit_lda3(vm, r_val, cap->offset);
                emit_load(vm, cap->ty, r_val, r_val);
            }

            // Store at descriptor[(i + 1) * 8]
            int r_cap_addr = alloc_temp_reg();
            emit_addi3(vm, r_cap_addr, r_desc, (i + 1) * 8);
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

        // Generate user arguments into A1-A7 (A0 is reserved for descriptor)
        int arg_idx = 0;
        for (Node *a = node->args; a; a = a->next) {
            add_type(vm, a);
            int arg_reg = REG_A1 + arg_idx; // User args start at A1
            if (arg_idx >= 7) {
                error_tok(vm, a->tok, "too many block arguments");
            }
            if (is_flonum(a->ty)) {
                gen_expr(vm, a, FREG_A1 + arg_idx);
            } else {
                gen_expr(vm, a, arg_reg);
            }
            arg_idx++;
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
        vm->dbg.source_map_capacity *= 2;
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
    if (vm->flags & (CCCC_POINTER_CHECKS | CCCC_INVALID_ARITH | CCCC_PROVENANCE_TRACK))
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
        for (Node *n = node->body; n; n = n->next) {
            gen_stmt(vm, n);
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

        // Tail-call optimisation: return f(args) → CALLT instead of CALL+LEV3.
        // Guards: opt >= 1, not inlining, predicate checks FFI/variadic/nested/etc.
        // After gen_expr, pending_tail_callee is set only if CALL was reached;
        // inlining/builtins leave it NULL and we fall through to the LEV3 path.
        // expr_already_eval prevents re-evaluating node->lhs in the LEV3 path below.
        bool expr_already_eval = false;
        if (node->lhs && vm->compiler.opt_level >= 1 &&
            can_emit_tail_call(vm, node->lhs)) {
            int tco_dest = is_flonum(node->lhs->ty) ? FREG_A0 : REG_A0;
            vm->compiler.emitting_tail_call = true;
            vm->compiler.pending_tail_callee = NULL;
            gen_expr(vm, node->lhs, tco_dest);
            vm->compiler.emitting_tail_call = false; // belt-and-suspenders; cleared in ND_FUNCALL
            if (vm->compiler.pending_tail_callee) {
                Obj *tco_fn = vm->compiler.pending_tail_callee;
                vm->compiler.pending_tail_callee = NULL;
                emit_flush_promoted_locals(vm);
                if (vm->flags & CCCC_STACK_INSTR)
                    emit_scopeout(vm, vm->current_function_scope_id);
                emit_restore_restrict_cache_regs(vm);
                emit_restore_promoted_registers(vm);
                emit(vm, CALLT);
                Pc tco_patch = emit_word_ptr(vm);
                vm->text_seg[tco_patch] = 0;
                if (vm->compiler.num_call_patches >= MAX_CALLS)
                    error("too many function calls");
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
            // If returning struct/union, copy to return buffer at runtime
            if (node->lhs->ty && (node->lhs->ty->kind == TY_STRUCT ||
                                  node->lhs->ty->kind == TY_UNION)) {
                // Evaluate source (struct address) into a temp register first
                int r_src = alloc_temp_reg();
                gen_expr(vm, node->lhs, r_src);

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
            } else if (is_flonum(node->lhs->ty)) {
                gen_expr(vm, node->lhs, FREG_A0);
            } else {
                gen_expr(vm, node->lhs, REG_A0);
            }
        }
        emit_flush_promoted_locals(vm);
        // Deactivate function-level scope before returning.
        if (vm->flags & CCCC_STACK_INSTR)
            emit_scopeout(vm, vm->current_function_scope_id);
        emit_restore_restrict_cache_regs(vm);
        emit_restore_promoted_registers(vm);
        emit(vm, LEV3);
        return;

    case ND_IF: {
        reset_temp_regs();
        int r_cond = alloc_temp_reg();
        gen_expr(vm, node->cond, r_cond);
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
            gen_expr(vm, node->cond, r_cond);
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
        gen_expr(vm, node->cond, r_cond);
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
        // break/continue/goto - emit a jump that will be patched
        if (node->unique_label) {
            // This is a break or continue statement
            emit(vm, JMP);
            Pc patch = emit_word_ptr(vm);
            vm->text_seg[patch] = 0; // Placeholder
            add_label_patch(node->unique_label, patch, false);
        } else if (node->label) {
            // Named goto - also needs patching
            emit(vm, JMP);
            Pc patch = emit_word_ptr(vm);
            vm->text_seg[patch] = 0; // Placeholder
            add_label_patch(node->label, patch, false);
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

// Assign stack offsets for parameters and locals
// Returns the total stack size (aligned to 16 bytes)
static int assign_stack_offsets(Obj *fn) {
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

    // Stack size starts with space for parameters (at negative offsets)
    int stack_size = spill_param_count;

    // Assign parameter offsets (negative, starting at -1)
    // Parameters: bp[-1], bp[-2], ...
    int param_offset = -1;
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
                       var->ty->kind == TY_COMPLEX) {
                var_size = (var->ty->size + 7) / 8;
            }
            stack_size += var_size;
            var->offset = -stack_size;
        }
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

    // Reset inlining context for this function
    vm->compiler.inline_exit_name = NULL;
    vm->compiler.ent3_extra_stack = 0;

    // Reset label tracking for this function
    reset_labels();

    // Count parameters first
    // Assign stack offsets early
    int stack_size = assign_stack_offsets(fn);
    int base_stack_size = stack_size;
    prepare_local_promotion(vm, fn, base_stack_size);
    prepare_restrict_cache(vm, fn, base_stack_size);
    stack_size += vm->compiler.promoted_count + vm->compiler.restrict_cache_capacity;
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
    emit_i64(vm, ent3_masks);
    vm->compiler.ent3_base_stack = stack_size;
    vm->compiler.ent3_extra_stack = 0;

    // Activate function-level scope (marks params/locals as alive).
    if (vm->flags & CCCC_STACK_INSTR)
        emit_scopein(vm, fn_scope_id);

    emit_save_promoted_registers(vm);
    emit_save_restrict_cache_regs(vm);
    emit_init_promoted_params(vm);

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
            emit_lea3(vm, r_addr, var->offset); // Address of stack slot
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

    // Patch all forward jumps (break/continue/goto)
    patch_labels(vm);

    // Implicit return 0 from entry function
    const char *entry_fn = vm->compiler.entry_name ? vm->compiler.entry_name : "main";
    if (strncmp(fn->name, entry_fn, strlen(entry_fn) + 1) == 0) {
        emit_li3(vm, REG_A0, 0);
    }
    emit_flush_promoted_locals(vm);
    // Deactivate function scope (for fall-through returns).
    if (vm->flags & CCCC_STACK_INSTR)
        emit_scopeout(vm, fn_scope_id);
    emit_restore_restrict_cache_regs(vm);
    emit_restore_promoted_registers(vm);
    emit(vm, LEV3);
    fn->code_end_addr = vm->text_ptr + 1;
}

// ========== Top-Level Code Generation ==========

void gen(VirtualMachine *vm, Obj *prog) {
    // Reset patch counters
    vm->compiler.num_call_patches = 0;
    vm->compiler.num_func_addr_patches = 0;
    vm->compiler.num_data_relocs = 0;

    // Initialize text pointer - text_seg[0] is reserved for main entry point
    vm->text_ptr = 0;

    // Initialize global variables in data segment
    for (Obj *var = prog; var; var = var->next) {
        if (!var->is_function) {
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
            assign_stack_offsets(fn);
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
        Obj *target = vm->compiler.call_patches[i].function;
        char *fn_name = target->name;
        Pc loc = vm->compiler.call_patches[i].location;

        Obj *fn_def = find_function_definition_for_patch(&fn_defs, target);

        if (!fn_def) {
            // Check for FFI function
            int ffi_idx = find_ffi_function(vm, fn_name);
            if (ffi_idx >= 0) {
                // FFI - not handled via CALL, skip
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
        }
    }

    hashmap_deinit(&fn_defs);

    apply_global_relocations(vm, prog);

    // Find entry function and store its address in text_seg[0]
    const char *entry = vm->compiler.entry_name ? vm->compiler.entry_name : "main";
    for (Obj *fn = prog; fn; fn = fn->next) {
        if (fn->is_function &&
            strncmp(fn->name, entry, strlen(entry) + 1) == 0) {
            vm->text_seg[0] = fn->code_addr;
            return;
        }
    }

    if (!vm->compiler.compile_only && !vm->compiler.testing_mode)
        error("%s() function not found", entry);
}
