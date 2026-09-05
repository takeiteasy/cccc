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

// Shared declarations for the src/codegen_*.c translation units (ticket #717
// split of the former monolithic src/codegen.c). Nothing in here is public
// API; it exists only to let the split files call into each other.
#ifndef CCCC_CODEGEN_INTERNAL_H
#define CCCC_CODEGEN_INTERNAL_H

#include "./internal.h"

// #1132 self-hosting spike: declare_builtin_functions() (parse_decl.c) runs
// once per TU and reassigns vm->compiler.builtin_setjmp/longjmp/etc. every
// time -- in a multi-TU program only the LAST-parsed TU's Obj survives in
// that singleton field, so an identity comparison against it silently fails
// for every earlier TU's own setjmp/longjmp call site (codegen_expr.c falls
// through to an ordinary indirect call to an unresolved symbol -- "invalid
// indirect call target" at runtime; serialize_expr.c falls through to an
// ordinary call serialized with the VM-side `long *` parameter type instead
// of the `(void *)`/real-host-name spelling these builtins need). Match by
// name instead: every TU's own registration creates a bodiless, non-local
// Obj with the exact reserved name, so name identity is exactly as precise
// as the old pointer identity but survives which TU's copy the singleton
// field happens to still point to.
static inline bool obj_is_reserved_builtin(Obj *var, const char *name) {
    return var && !var->is_local && !var->is_definition && var->name &&
           !strcmp(var->name, name);
}

// Grow a dynamic patch table by 2x when full (initial capacity 256).
// 'field' is the array pointer member, 'nf' is the count, 'cf' is the cap.
#define PATCH_GROW(vm, field, nf, cf)                                          \
    do {                                                                       \
        if ((vm)->compiler.nf >= (vm)->compiler.cf) {                          \
            int   _nc = (vm)->compiler.cf ? (vm)->compiler.cf * 2 : 256;       \
            void *_p  = realloc((vm)->compiler.field,                          \
                                (size_t)_nc * sizeof(*(vm)->compiler.field));  \
            if (!_p)                                                           \
                error("out of memory growing patch table");                    \
            (vm)->compiler.field = _p;                                         \
            (vm)->compiler.cf    = _nc;                                        \
        }                                                                      \
    } while (0)

// Persistent label map: records every label defined across all functions during
// a single gen() call.  Unlike label_defs[] (per-function, reset each time)
// this map survives until apply_global_relocations() has run, so &&label
// stored in static/global initialisers can be resolved to their text offsets
// (#573).  All .L..N names are globally unique (unique_name_counter), so there
// are no cross-function clashes.  Defined in codegen_regalloc.c, used from
// codegen_call.c and codegen_func.c.
typedef struct {
    char *name;
    Pc    offset;
} GlobalLabelEntry;
extern GlobalLabelEntry *global_label_map;
extern int               num_global_labels;
extern int               global_labels_cap;

// Per-block stack tracking vars with __attribute__((cleanup(fn))).
// Parallels the parse-time cleanup_scope_depth. Shared file-scope state
// (see ticket #139 note about thread safety). Defined in codegen_addr.c,
// used from codegen_stmt.c and codegen_func.c.
typedef struct CleanupScopeEntry {
    CleanupVar *vars;  // LIFO-ordered cleanup vars for this scope
    int         depth; // parse-time cleanup_scope_depth of this scope
    struct CleanupScopeEntry *outer;
} CleanupScopeEntry;
extern CleanupScopeEntry *g_cleanup_scope;

// Register-allocator spill threshold for the reg-heavy binary-op path.
// Defined in codegen_regalloc.c, used from codegen_expr.c.
#define TEMP_REG_SPILL_THRESHOLD 2

// One `switch` case's jump-table bookkeeping. Defined/used in
// codegen_emit.c and codegen_stmt.c.
typedef struct {
    Node *node;
    long  begin;
    long  end;
    Pc    table_entry;
    Pc   *patches;
    int   num_patches;
    int   cap_patches;
} SwitchCasePatch;

// Growable list of patch-location program counters. Defined/used in
// codegen_emit.c and codegen_stmt.c.
typedef struct {
    Pc *items;
    int len;
    int cap;
} PatchList;

// ========== Cross-file forward declarations ==========
//
// Every function below is used from at least one codegen_*.c file other
// than the one that defines it, so each lost its `static` and gained a
// prototype here. Defining file noted only where not obvious from the name.

bool addr_is_local_frame(VirtualMachine *vm, Node *node);
Obj *belongs_to_outer_function(Obj *current_fn, Obj *var);
void emit_static_chain_var_addr(VirtualMachine *vm, Obj *current_fn,
                                Obj *owner_fn, Obj *var, int dest_reg);
void emit_load_safety_checks(VirtualMachine *vm, Type *ty, int rs_addr,
                             bool dangling_check);
bool block_capture_needs_mcpy(Type *ty);
bool can_emit_tail_call(VirtualMachine *vm, Node *expr);
bool cast_is_repr_noop(Type *to, Type *from);
bool contains_funcall(Node *node);
bool contains_self_call(Node *node, Obj *fn);
bool emit_wide_helper(VirtualMachine *vm, const char *name, int nargs);
bool expr_has_call(Node *node);
bool is_extern_func_name(Node *node, const char *name);
bool is_simple_local_scalar(VirtualMachine *vm, Node *node);
bool is_u64_int(Type *ty);
bool is_union_member_access(Node *node);
bool is_wide_bitint(Type *ty);
bool is_zero_size_aggregate(Type *ty);
const char *obj_external_name(Obj *obj);
int alloc_temp_reg(void);
int assign_stack_offsets(VirtualMachine *vm, Obj *fn);
int ffi_index_for_callee(VirtualMachine *vm, Obj *callee);
int find_capture_index(Obj *block_fn, Obj *var);
int find_ffi_function(VirtualMachine *vm, const char *name);
int fop_for_type(Type *ty, int f64_op);
int gen_checked_nt_hi(VirtualMachine *vm, Node *deref);
int gen_flonum_arg_to_scratch(VirtualMachine *vm, Node *arg);
int temp_regs_free(void);
int var_stack_slots(Obj *var);
long long alloc_decimal_temp(VirtualMachine *vm, int bytes);
long long alloc_wide_bitint_temp(VirtualMachine *vm, int words);
Node *clone_subst(VirtualMachine *vm, Node *src, Obj *params, Node *args);
Obj *find_function_definition_for_patch(HashMap *fn_defs, Obj *target);
Obj *find_static_link_var(Obj *fn);
int static_link_hop_bytes(VirtualMachine *vm);
Pc emit_addi3(VirtualMachine *vm, int rd, int rs, long long imm);
Pc emit_i64(VirtualMachine *vm, long long val);
Pc emit_jnz3(VirtualMachine *vm, int rs);
Pc emit_jz3(VirtualMachine *vm, int rs);
Pc emit_lda3(VirtualMachine *vm, int rd, long long offset);
Pc emit_ldtls3(VirtualMachine *vm, int rd, long long offset);
Pc emit_lea3_internal(VirtualMachine *vm, int rd, long long offset);
Pc emit_lea3_var(VirtualMachine *vm, int rd, Obj *var);
Pc emit_lea3(VirtualMachine *vm, int rd, long long offset);
Pc emit_li3(VirtualMachine *vm, int rd, long long imm);
Pc emit_lta3(VirtualMachine *vm, int rd, long long offset);
Pc emit_ri(VirtualMachine *vm, int op, int rd, long long imm);
Pc emit_rri(VirtualMachine *vm, int op, int rd, int rs, long long imm);
Pc emit_rrrs_i(VirtualMachine *vm, int op, int rd, int base, int index,
               int scale, long long offset);
// STKTAG: tag [bp+offset, bp+offset+size) with the current frame's epoch
// (#675/#1078). See emit_lea3_var's own comment for the escaping-aggregate
// rationale; codegen_func.c's struct/union-by-value param copy shares it.
Pc emit_stktag(VirtualMachine *vm, long long offset, long long size);
Pc emit_word_ptr(VirtualMachine *vm);
SwitchCasePatch *collect_switch_cases(Node *node, int *num_cases,
                                      long *min_case, long *max_case,
                                      long *covered_values);
SwitchCasePatch *find_switch_case(SwitchCasePatch *cases, int num_cases,
                                  Node *node);
void add_debug_symbol(VirtualMachine *vm, char *name, long long offset,
                      Type *ty, int is_local, Obj *owner_fn);
void add_label_patch(char *name, Pc patch_location, bool text_relative);
void add_stack_var_meta(VirtualMachine *vm, const char *name, long long offset,
                        Type *ty, int scope_id);
void apply_global_relocations(VirtualMachine *vm, Obj *prog);
void cccc_default_asm_passthru(VirtualMachine *vm, const char *asm_str);
void check_data_capacity(VirtualMachine *vm, long long needed);
void check_tls_capacity(VirtualMachine *vm, size_t needed);
int cc_effective_align(int obj_align, int ty_align);
void define_label(VirtualMachine *vm, char *name);
void emit_bitint_trunc(VirtualMachine *vm, Type *ty, int reg);
void emit_chkab(VirtualMachine *vm, int rs_val, int rs_slo, int rs_shi,
                bool is_hi);
void emit_chki(VirtualMachine *vm, long long offset);
void emit_chkl(VirtualMachine *vm, long long offset);
void emit_chknt(VirtualMachine *vm, int rs_addr, int rs_hi, int rs_val,
                long long elem_size);
void emit_chkntz(VirtualMachine *vm, int rs_addr, int rs_hi, int rs_src,
                 long long elem_size);
void emit_cleanups_to_depth(VirtualMachine *vm, int target_depth);
void emit_fmov3(VirtualMachine *vm, int rd, int rs);
void emit_fround_f32(VirtualMachine *vm, int rd, int rs);
void emit_frr(VirtualMachine *vm, int op, int rd, int rs1);
void emit_frrr(VirtualMachine *vm, int op, int rd, int rs1, int rs2);
void emit_hmrk(VirtualMachine *vm, int depth);
void emit_hrel(VirtualMachine *vm, int depth);
void emit_load_ex(VirtualMachine *vm, Type *ty, int rd, int rs_addr,
                  bool dangling_check);
void emit_load(VirtualMachine *vm, Type *ty, int rd, int rs_addr);
void emit_local_load(VirtualMachine *vm, Type *ty, int rd, long long offset);
void emit_local_store(VirtualMachine *vm, Type *ty, int rd_val,
                      long long offset);
void emit_marki(VirtualMachine *vm, long long offset);
void emit_markp(VirtualMachine *vm, int rs_ptr, int rs_base, int origin_type,
                size_t size);
void emit_markr(VirtualMachine *vm, long long offset);
void emit_markw(VirtualMachine *vm, long long offset);
void emit_mov3(VirtualMachine *vm, int rd, int rs);
void emit_pop3(VirtualMachine *vm, int rd);
void emit_psh3(VirtualMachine *vm, int rs);
void emit_rr(VirtualMachine *vm, int op, int rd, int rs1);
void emit_rrr(VirtualMachine *vm, int op, int rd, int rs1, int rs2);
void emit_rrrs(VirtualMachine *vm, int op, int rd, int rs1, int rs2, int scale);
void emit_rrs(VirtualMachine *vm, int op, int rd, int rs1, int scale);
void emit_scope_cleanups(VirtualMachine *vm, CleanupScopeEntry *scope);
void emit_scopein(VirtualMachine *vm, int scope_id);
void emit_scopeout(VirtualMachine *vm, int scope_id);
void emit_source_location(VirtualMachine *vm, Node *node);
void emit_sparse_switch_tree(VirtualMachine *vm, SwitchCasePatch *cases, int lo,
                             int hi, int r_val, int r_cmp,
                             PatchList *fail_patches);
void emit_store_ex(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr,
                   bool dangling_check);
void emit_store(VirtualMachine *vm, Type *ty, int rd_val, int rs_addr);
void emit_wide_op(VirtualMachine *vm, int op);
void emit_with_arg(VirtualMachine *vm, int instruction, long long arg);
void emit_word(VirtualMachine *vm, InstrWord word);
void emit(VirtualMachine *vm, int instruction);
void free_switch_cases(SwitchCasePatch *cases, int num_cases);
void free_temp_reg(int reg);
void gen_addr(VirtualMachine *vm, Node *node, int dest_reg);
void gen_complex_expr(VirtualMachine *vm, Node *node, int real_reg,
                      int imag_reg);
void gen_cond_expr(VirtualMachine *vm, Node *node, int dest_reg);
void gen_decimal_arg_ptr(VirtualMachine *vm, Node *arg, int addr_reg);
void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);
void gen_stmt(VirtualMachine *vm, Node *node);
void gen_vector_arg_ptr(VirtualMachine *vm, Node *arg, int addr_reg);
void gen_wide_bitint_unary(VirtualMachine *vm, Node *node, int dest_reg,
                           const char *helper);
void gen_zero_size_arg(VirtualMachine *vm, Node *arg, int dest_reg);
void mark_temp_reg_used(int reg);
void patch_labels(VirtualMachine *vm);
void replace_locals_in_ast(Node *node, Obj **orig, Obj **map, int count);
void reset_labels(void);
void reset_temp_regs(void);

#endif // CCCC_CODEGEN_INTERNAL_H
