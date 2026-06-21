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

 This file was original part of chibicc by Rui Ueyama (MIT)
 https://github.com/rui314/chibicc
*/

#pragma once

#include "cccc.h"

#ifndef __has_include
#define __has_include(x) 0
#endif

#if __has_include(<stdnoreturn.h>)
#include <stdnoreturn.h>
#else
#define noreturn
#endif

#ifndef __attribute__
#define __attribute__(x)
#endif

#define CCCC_MAGIC "CCCC\0"
#define CCCC_VERSION 2 // Bytecode format version (pre-1.0; bump only at release)
                       // V2: TLS template + reloc section added (#493)

// Stack canary constant for detecting stack overflows (used when random
// canaries disabled)
#define STACK_CANARY 0xDEADBEEFCAFEBABELL

// ========== Multi-Register VM Infrastructure ==========
// Register file indices (RISC-V style naming)
// Layout: 32 registers total
//   0     - Zero register (writes discarded)
//   1     - Return address
//   2     - Stack pointer (unused - we have vm->sp)
//   3-4   - Reserved
//   5-9   - Temporaries T0-T4 (caller-saved)
//   10-17 - Arguments/Return A0-A7 (caller-saved)
//   18-25 - Saved S0-S7 (callee-saved, preserved across calls)
//   26-31 - Temporaries T5-T10 (caller-saved)

// Token range for built-in FFI function pointers (goto */call via function pointer).
// Distinct from CCCC_DYN_TOKEN_BASE (-0x4a434300) used for dlopen symbols.
// Token for ffi_table[i] = CCCC_FFI_TOKEN_BASE - i  (all values <= CCCC_FFI_TOKEN_BASE).
#define CCCC_FFI_TOKEN_BASE (-0x4a434380LL)

#define REG_ZERO 0 // Always zero (writes discarded)
#define REG_RA 1   // Return address
#define REG_SP 2   // Stack pointer (unused for now - we have vm->sp)
#define REG_T0 5   // Temporary (caller-saved)
#define REG_T1 6   // Temporary
#define REG_T2 7   // Temporary
#define REG_T3 8   // Temporary
#define REG_T4 9   // Temporary
#define REG_A0 10  // Argument/return value
#define REG_A1 11  // Argument
#define REG_A2 12  // Argument
#define REG_A3 13  // Argument
#define REG_A4 14  // Argument
#define REG_A5 15  // Argument
#define REG_A6 16  // Argument
#define REG_A7 17  // Argument
#define REG_S0 18  // Saved (callee-saved)
#define REG_S1 19  // Saved
#define REG_S2 20  // Saved
#define REG_S3 21  // Saved
#define REG_S4 22  // Saved
#define REG_S5 23  // Saved
#define REG_S6 24  // Saved
#define REG_S7 25  // Saved
#define REG_T5 26  // Temporary (caller-saved)
#define REG_T6 27  // Temporary
#define REG_T7 28  // Temporary
#define REG_T8 29  // Temporary
#define REG_T9 30  // Temporary
#define REG_T10 31 // Temporary
#define NUM_REGS 32

// Floating-point register file indices (for float/double arguments)
// These mirror REG_A* but in the fregs[] array
#define FREG_A0 10 // Float argument/return (maps to fax)
#define FREG_A1 11 // Float argument
#define FREG_A2 12 // Float argument
#define FREG_A3 13 // Float argument
#define FREG_A4 14 // Float argument
#define FREG_A5 15 // Float argument
#define FREG_A6 16 // Float argument
#define FREG_A7 17 // Float argument
#define FREG_S0 18 // Float saved (callee-saved, mirrors REG_S0)
#define FREG_S1 19 // Float saved
#define FREG_S2 20 // Float saved
#define FREG_S3 21 // Float saved

// Instruction encoding macros for new opcodes
// Text segment words are 32-bit. Wide immediates are stored little-endian in
// two consecutive instruction words.
// RRR format: [rd:8|rs1:8|rs2:8|unused:8]
#define ENCODE_RRR(rd, rs1, rs2)                                               \
    ((InstrWord)(rd) | ((InstrWord)(rs1) << 8) |                         \
     ((InstrWord)(rs2) << 16))
#define ENCODE_RRRS(rd, rs1, rs2, scale)                                      \
    (ENCODE_RRR((rd), (rs1), (rs2)) | ((InstrWord)(scale) << 24))
#define DECODE_RRR(operands, rd, rs1, rs2)                                     \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
        rs2 = ((operands) >> 16) & 0xFF;                                       \
    } while (0)
#define DECODE_RRRS(operands, rd, rs1, rs2, scale)                            \
    do {                                                                       \
        DECODE_RRR((operands), rd, rs1, rs2);                                  \
        scale = ((operands) >> 24) & 0xFF;                                     \
    } while (0)
#define ENCODE_RRRR(rd, rs1, rs2, rs3)                                        \
    (ENCODE_RRR((rd), (rs1), (rs2)) | ((InstrWord)(rs3) << 24))
#define DECODE_RRRR(operands, rd, rs1, rs2, rs3)                              \
    do {                                                                       \
        DECODE_RRR((operands), rd, rs1, rs2);                                  \
        rs3 = ((operands) >> 24) & 0xFF;                                       \
    } while (0)

// RR format: [rd:8|rs1:8|unused:16]
#define ENCODE_RR(rd, rs1)                                                     \
    ((InstrWord)(rd) | ((InstrWord)(rs1) << 8))
#define DECODE_RR(operands, rd, rs1)                                           \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
    } while (0)

// RI format: [OPCODE] [rd:8|unused:24] [immediate:64 split low, high]
#define ENCODE_R(rd) ((InstrWord)(rd))
#define DECODE_R(operands, rd)                                                 \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
    } while (0)

static inline uint64_t cc_make_u64(InstrWord lo, InstrWord hi) {
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline long long cc_make_i64(InstrWord lo, InstrWord hi) {
    return (long long)cc_make_u64(lo, hi);
}

static inline InstrWord cc_i64_lo(long long val) {
    return (InstrWord)((uint64_t)val & 0xFFFFFFFFu);
}

static inline InstrWord cc_i64_hi(long long val) {
    return (InstrWord)(((uint64_t)val >> 32) & 0xFFFFFFFFu);
}

static inline InstrWord cc_read_word(VirtualMachine *vm) {
    return vm->text_seg[vm->pc++];
}

static inline long long cc_read_i64(VirtualMachine *vm) {
    InstrWord lo = cc_read_word(vm);
    InstrWord hi = cc_read_word(vm);
    return cc_make_i64(lo, hi);
}

static inline void cc_write_i64_at(VirtualMachine *vm, Pc pc, long long val) {
    vm->text_seg[pc] = cc_i64_lo(val);
    vm->text_seg[pc + 1] = cc_i64_hi(val);
}

static inline long long cc_read_i64_at(VirtualMachine *vm, Pc pc) {
    return cc_make_i64(vm->text_seg[pc], vm->text_seg[pc + 1]);
}

static inline Pc cc_byte_offset_to_pc(long long offset) {
    if (offset < 0 || offset % (long long)sizeof(InstrWord) != 0)
        return CCCC_INVALID_PC;
    uint64_t pc = (uint64_t)offset / sizeof(InstrWord);
    return pc > UINT32_MAX ? CCCC_INVALID_PC : (Pc)pc;
}

static inline long long cc_pc_to_byte_offset(Pc pc) {
    return (long long)pc * (long long)sizeof(InstrWord);
}

/* The register file holds a flat double. A `float` value is stored as its
 * exact double widening, so reads need no precision branch. F32 opcodes round
 * their result through `(float)` before calling cccc_freg_set_f32. */
static inline void cccc_freg_set_f64(VirtualMachine *vm, int reg, double value) {
    vm->fregs[reg].f64 = value;
}

static inline void cccc_freg_set_f32(VirtualMachine *vm, int reg, float value) {
    vm->fregs[reg].f64 = (double)value; /* exact widening */
}

static inline double cccc_freg_get_f64(VirtualMachine *vm, int reg) {
    return vm->fregs[reg].f64;
}

static inline float cccc_freg_get_f32(VirtualMachine *vm, int reg) {
    return (float)vm->fregs[reg].f64; /* narrow on read */
}

static inline long long cccc_freg_raw_f64(VirtualMachine *vm, int reg) {
    union {
        double d;
        long long ll;
    } conv;
    conv.d = cccc_freg_get_f64(vm, reg);
    return conv.ll;
}

static inline int cccc_freg_raw_f32(VirtualMachine *vm, int reg) {
    union {
        float f;
        int i;
    } conv;
    conv.f = cccc_freg_get_f32(vm, reg);
    return conv.i;
}

static inline void cccc_freg_set_raw_f64(VirtualMachine *vm, int reg, long long bits) {
    union {
        double d;
        long long ll;
    } conv;
    conv.ll = bits;
    cccc_freg_set_f64(vm, reg, conv.d);
}

static inline void cccc_freg_set_raw_f32(VirtualMachine *vm, int reg, int bits) {
    union {
        float f;
        int i;
    } conv;
    conv.i = bits;
    cccc_freg_set_f32(vm, reg, conv.f);
}

static inline int cc_opcode_operand_words(int op) {
    static const int operand_words[] = {
#define X(NAME, OPERANDS) [NAME] = OPERANDS,
        OPS_X
#undef X
    };
    if (op < 0 || op >= (int)(sizeof(operand_words) / sizeof(operand_words[0])))
        return -1;
    return operand_words[op];
}

static inline int cc_instr_words(int op) {
    int operands = cc_opcode_operand_words(op);
    return operands < 0 ? -1 : operands + 1;
}

static inline const char *cc_opcode_name(int op) {
    static const char *names[] = {
#define X(NAME, OPERANDS) [NAME] = #NAME,
        OPS_X
#undef X
    };
    if (op < 0 || op >= (int)(sizeof(names) / sizeof(names[0])) || !names[op])
        return NULL;
    return names[op];
}

void strarray_push(StringArray *arr, char *s);
void arena_strarray_push(VirtualMachine *vm, StringArray *arr, char *s);
char *format(char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *arena_strdup(VirtualMachine *vm, const char *str);
char *arena_strndup(VirtualMachine *vm, const char *str, int len);
char *arena_format(VirtualMachine *vm, char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
Token *preprocess(VirtualMachine *vm, Token *tok);

//
// tokenize.c
//

noreturn void error(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void error_at(VirtualMachine *vm, char *loc, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void error_tok(VirtualMachine *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
bool error_tok_recover(VirtualMachine *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void warn_at(VirtualMachine *vm, char *loc, CCCCWarning category, char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
void warn_tok(VirtualMachine *vm, Token *tok, CCCCWarning category, char *fmt, ...)
    __attribute__((format(printf, 4, 5)));
const char *cccc_warning_name(CCCCWarning warning);
uint64_t cccc_warning_mask_for_name(const char *name);
bool cccc_warning_is_group_name(const char *name);
bool equal(Token *tok, char *op);
Token *skip(VirtualMachine *vm, Token *tok, char *op);
bool consume(VirtualMachine *vm, Token **rest, Token *tok, char *str);
void convert_pp_tokens(VirtualMachine *vm, Token *tok);
File *new_file(VirtualMachine *vm, char *name, int file_no, char *contents);
Token *tokenize_string_literal(VirtualMachine *vm, Token *tok, Type *basety);
Token *tokenize(VirtualMachine *vm, File *file);
Token *tokenize_file(VirtualMachine *vm, char *filename);
Token *tokenize_string(VirtualMachine *vm, char *name, char *contents);
unsigned char *read_binary_file(VirtualMachine *vm, char *path, size_t *out_size);
void cc_output_preprocessed(FILE *f, VirtualMachine *vm, Token *tok);

#undef unreachable
#define unreachable() error("internal error at %s:%d", __FILE__, __LINE__)

//
// preprocess.c
//

char *cccc_path_find_executable(const char *name);
char *cccc_find_native_cc(void);
int cc_rehydrate_asm_passthru(VirtualMachine *vm);
char *get_std_header(char *filename);
const char *get_stdlib_reg_fn_name(const char *header);
const char *get_std_header_name(int i);
char *search_include_paths(VirtualMachine *vm, char *filename, int filename_len,
                           bool is_system);
void init_macros(VirtualMachine *vm);
void define_std_macros(VirtualMachine *vm);
void define_macro(VirtualMachine *vm, char *name, char *buf);
void undef_macro(VirtualMachine *vm, char *name);
Token *preprocess(VirtualMachine *vm, Token *tok);
void gate_runtime_only_macros(VirtualMachine *vm, const char *main_filename);

//
// parse.c
//

Node *new_cast(VirtualMachine *vm, Node *expr, Type *ty);
int64_t const_expr(VirtualMachine *vm, Token **rest, Token *tok);
bool node_int_const_fits(VirtualMachine *vm, Node *expr, Type *to);
Obj *parse(VirtualMachine *vm, Token *tok);
void cc_execute_top_level_macro(VirtualMachine *vm, char *name, Token *tok,
                                Node *args, int arg_count);
void cc_execute_attribute_macro(VirtualMachine *vm, MacroFn *pm, Token *tok,
                                AttrTarget *target, Node *args,
                                int arg_count);
void ensure_reflection_attrs_registered(VirtualMachine *vm);
void __builtin_ensure_string_h_decls(VirtualMachine *vm);
// Expand a deferred ND_INIT_SPLICE node into positional ND_ASSIGN chains.
// Called by quote_substitute in relfection.c after the splice chain is resolved.
Node *node_expand_init_splice(VirtualMachine *vm, Node *splice, Node *chain);
// Compile-time constant evaluators (wrappers around the static eval/eval_double).
int64_t cc_eval(VirtualMachine *vm, Node *node);
double  cc_eval_double(VirtualMachine *vm, Node *node);

//
// type.c
//

extern Type *ty_void;
extern Type *ty_bool;
extern Type *ty_nullptr_t;

extern Type *ty_char;
extern Type *ty_short;
extern Type *ty_int;
extern Type *ty_long;

extern Type *ty_uchar;
extern Type *ty_ushort;
extern Type *ty_uint;
extern Type *ty_ulong;

extern Type *ty_float;
extern Type *ty_double;
extern Type *ty_ldouble;
extern Type *ty_fcomplex;
extern Type *ty_dcomplex;
extern Type *ty_ldcomplex;

extern Type *ty_decimal32;
extern Type *ty_decimal64;
extern Type *ty_decimal128;

extern Type *ty_auto;  // C23 type-inference sentinel

extern Type *ty_error;

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_complex(Type *ty);
bool is_numeric(Type *ty);
bool is_error_type(Type *ty);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(VirtualMachine *vm, Type *ty);
Type *pointer_to(VirtualMachine *vm, Type *base);
Type *func_type(VirtualMachine *vm, Type *return_ty);
Type *array_of(VirtualMachine *vm, Type *base, int size);
Type *vla_of(VirtualMachine *vm, Type *base, Node *expr);
Type *enum_type(VirtualMachine *vm);
Type *struct_type(VirtualMachine *vm);
Type *union_type(VirtualMachine *vm);
Type *block_type(VirtualMachine *vm, Type *return_ty, Type *params);
Type *complex_type_for(VirtualMachine *vm, Type *base);
Type *bitint_type(VirtualMachine *vm, Token *tok, int width, bool is_unsigned);
void add_type(VirtualMachine *vm, Node *node);
void warn_implicit_conversion(VirtualMachine *vm, Node *expr, Type *to, Token *tok);

//
// unicode.c
//

int encode_utf8(char *buf, uint32_t c);
uint32_t decode_utf8(VirtualMachine *vm, char **new_pos, char *p);
bool is_ident1(uint32_t c);
bool is_ident2(uint32_t c);
int display_width(VirtualMachine *vm, char *p, int len);

//
// arena.c
//

void arena_init(Arena *arena, size_t default_block_size);
void *arena_alloc(Arena *arena, size_t size);
void arena_reset(Arena *arena);
void arena_destroy(Arena *arena);

//
// vm_mem.c - Virtual memory reserve/commit for VM segments
//

// Reserve a virtual range of 'bytes' bytes (no physical pages committed).
// Returns NULL on failure.
void *cccc_vm_reserve(size_t bytes);

// Commit 'len' bytes at offset 'off' from 'base'.  Pages are zero-filled by
// the OS.  'off' and 'len' are rounded to the system page size internally.
// Returns 0 on success, -1 on failure.
int cccc_vm_commit(void *base, size_t off, size_t len);

// Release a virtual range previously returned by cccc_vm_reserve.
void cccc_vm_release(void *base, size_t bytes);

//
// vm.c - Segment allocation / growth helpers (used by codegen, ops, reflect)
//

// Allocate (reserve + initial commit) all four VM segments using poolsize /
// poolsize_max from the CCCC struct.  Initialises all derived segment pointers.
void vm_alloc_segments(VirtualMachine *vm);

// Ensure the text segment has at least 'num_words' words committed.
// Returns 0 on success, -1 when the reservation cap is reached.
int vm_text_ensure_count(VirtualMachine *vm, Pc num_words);

// Ensure 'needed' more bytes are available in the data segment starting from
// the current data_ptr.  Returns 0 on success, -1 at cap.
int vm_data_ensure(VirtualMachine *vm, long long needed);

// Grow the heap segment by at least 'need' bytes.
// Advances heap_end on success.  Returns 0 on success, -1 at cap.
int vm_heap_grow(VirtualMachine *vm, size_t need);

// Grow the stack segment downward to accommodate 'slots_needed' more slots
// below the current stack_base.  Updates stack_base on success.
// Returns 0 on success, -1 when the reservation floor is reached.
int vm_stack_grow(VirtualMachine *vm, int slots_needed);

#define STACK_GUARD_SIZE 16

#define WATCHPOINT_CHECK(vm, addr, size, kind)                          \
    do {                                                                \
        if ((vm)->flags & CCCC_ENABLE_DEBUGGER)                          \
            debugger_check_watchpoint((vm), (addr), (size), (kind));    \
    } while (0)

static inline int check_stack_overflow(VirtualMachine *vm, int slots_needed) {
    if (vm->sp - slots_needed - STACK_GUARD_SIZE < vm->stack_base) {
        if (vm_stack_grow(vm, slots_needed + STACK_GUARD_SIZE) == 0)
            return 0;
        printf("\n========== STACK OVERFLOW ==========\n");
        printf("Stack space exhausted\n");
        printf("Requested:  %d slots (%d bytes)\n", slots_needed,
               slots_needed * (int)sizeof(long long));
        printf("Available:  %ld slots (%ld bytes)\n",
               (long)(vm->sp - vm->stack_base),
               (long)(vm->sp - vm->stack_base) * (long)sizeof(long long));
        printf("PC:         %u\n", vm->pc);
        printf("====================================\n");
        return -1;
    }
    return 0;
}

long long cccc_rt_dlopen(VirtualMachine *vm, const char *path, int mode);
long long cccc_rt_dlsym(VirtualMachine *vm, long long handle_token, const char *symbol);
long long cccc_rt_dlclose(VirtualMachine *vm, long long handle_token);
long long cccc_rt_dlerror(VirtualMachine *vm);
int cccc_ffi_name_in_list(char **list, int count, const char *name);
DynamicSymbol *cccc_find_dynamic_symbol(VirtualMachine *vm, long long token);
int cccc_call_native_function(VirtualMachine *vm, void *func_ptr, const char *name,
                             long long *args, int actual_nargs,
                             uint64_t double_arg_mask, uint64_t float_arg_mask,
                             int returns_double, int returns_float,
                             int is_variadic, int num_fixed_args);
VirtualMachine *cccc_current_ffi_vm(void);
// Returns the number of mutexes currently held by the active VM thread.
// Used by race detection in ops.c; implemented in stdlib/pthread.c.
int cccc_thread_held_lock_count(VirtualMachine *vm);

//
// hashmap.c
//

void *hashmap_get(HashMap *map, const char *key);
void *hashmap_get2(HashMap *map, const char *key, int keylen);
void hashmap_put(HashMap *map, const char *key, void *val);
void hashmap_put2(HashMap *map, const char *key, int keylen, void *val);
void hashmap_delete(HashMap *map, const char *key);
void hashmap_delete2(HashMap *map, const char *key, int keylen);

// Borrowed key HashMap functions (keys are NOT copied; caller must ensure lifetime)
void hashmap_put2_borrowed(HashMap *map, const char *key, int keylen, void *val);
void hashmap_put_borrowed(HashMap *map, const char *key, void *val);

// Deinitialize a HashMap: free all owned string keys and the bucket array.
// The HashMap struct itself is NOT freed.
void hashmap_deinit(HashMap *map);
void hashmap_deinit_borrowed(HashMap *map);

// Snapshot/restore: deep-clone a HashMap's bucket array and owned string
// keys so it can be rolled back after a sub-pass that must not leak #define
// changes. Values are shared with the original (arena-allocated). Restore
// (and discard the pass's additions) with hashmap_restore().
HashMap hashmap_snapshot(const HashMap *map);
void hashmap_restore(HashMap *map, HashMap snapshot);

// Integer key HashMap functions (avoid overhead of snprintf/strdup)
void *hashmap_get_int(HashMap *map, long long key);
void hashmap_put_int(HashMap *map, long long key, void *val);
void hashmap_delete_int(HashMap *map, long long key);
// HashMap iteration
// Callback function type for iteration
// Return 0 to continue iteration, non-zero to stop
typedef int (*HashMapIterator)(char *key, int keylen, void *val,
                               void *user_data);

void hashmap_foreach(HashMap *map, HashMapIterator iter, void *user_data);
int hashmap_count_if(HashMap *map, HashMapIterator predicate, void *user_data);

//
// generate_stdlib.c
//

long long wrap_strlen(long long s);
long long wrap_strcmp(long long s1, long long s2);
long long wrap_strncmp(long long s1, long long s2, long long n);
long long wrap_memcmp(long long s1, long long s2, long long n);
long long wrap_fread(long long ptr, long long size, long long nmemb,
                     long long stream);
long long wrap_fwrite(long long ptr, long long size, long long nmemb,
                      long long stream);
//
// main.c
//

//
// codegen.c
//

void gen_function(VirtualMachine *vm, Obj *fn);
void gen(VirtualMachine *vm, Obj *prog);
// Note: gen_expr is now static in codegen.c with signature:
// static void gen_expr(VirtualMachine *vm, Node *node, int dest_reg);

//
// vm.c
//

int vm_eval(VirtualMachine *vm);
int cccc_vm_eval_dispatch(VirtualMachine *vm, volatile Pc *current_pc);
#define CCCC_HOST_SIGNAL_RC (-4096)
int cccc_set_guest_signal_action(VirtualMachine *vm, int sig, int action);
void cc_vm_profile_reset(VirtualMachine *vm);
void cc_vm_profile_print(VirtualMachine *vm, FILE *f);
int cc_vm_profile_write_json(VirtualMachine *vm, FILE *f, const char *mode,
                             const char *input_name);

typedef struct ExecState {
    long long regs[32];
    FReg fregs[32];
    Pc pc;
    long long *bp;
    long long *sp;
    long long cycle;
    long long *initial_sp;
    long long *initial_bp;
    long long *stack_seg;
    long long *stack_base;
    size_t stack_committed;
    long long *shadow_stack;
    long long *shadow_sp;
    HashMap init_state;
} ExecState;

void cccc_exec_state_save(VirtualMachine *vm, ExecState *state);
void cccc_exec_state_restore(VirtualMachine *vm, const ExecState *state);
int cccc_exec_state_alloc_stack(VirtualMachine *vm, ExecState *state);
void cccc_exec_state_release_stack(VirtualMachine *vm, ExecState *state);
void cccc_exec_state_prepare_call(VirtualMachine *vm, ExecState *state, Pc entry,
                                  long long arg);
void cccc_gil_init(VirtualMachine *vm);
void cccc_gil_destroy(VirtualMachine *vm);
void cccc_gil_acquire(VirtualMachine *vm);
void cccc_gil_release(VirtualMachine *vm);

//
// optimize.c
//

void cc_optimize(VirtualMachine *vm, int level, bool fuse_ops);

//
// analyze.c
//

typedef struct {
    int n;        // 2 or 3
    int top_n;
    bool per_file;
} CcAnalyzeNgramOptions;

typedef struct {
    int top_n;
    bool json;    // emit JSON instead of text
} CcAnalyzeFusionOptions;

typedef struct {
    int def_op;
    int def_pc;
    int def_size;
    int use_op;
    int use_pc;
    int reg;
    int def_rd;
    int use_byte;
} CcFusionCandidate;

typedef struct CcNgramState CcNgramState;
typedef struct CcFusionState CcFusionState;

CcNgramState *cc_analyze_ngram_begin(const CcAnalyzeNgramOptions *opts);
void cc_analyze_ngram_feed(CcNgramState *st, const InstrWord *text,
                           long long num_words, const char *label, FILE *out);
void cc_analyze_ngram_finish(CcNgramState *st, FILE *out);

CcFusionState *cc_analyze_fusion_begin(const CcAnalyzeFusionOptions *opts);
void cc_analyze_fusion_feed(CcFusionState *st, const InstrWord *text,
                            long long num_words, const char *label,
                            FILE *out);
CcFusionCandidate *cc_analyze_fusion_collect(CcFusionState *st, int *out_count);
void cc_analyze_fusion_finish(CcFusionState *st, FILE *out);

//
// debugger.c
//

void debugger_init(VirtualMachine *vm);
void cc_debug_repl(VirtualMachine *vm);
void cc_debug_repl_host_fault(VirtualMachine *vm, int sig, void *fault_addr);
void debugger_disassemble_current(VirtualMachine *vm);
void debugger_print_stack(VirtualMachine *vm, int count);
int debugger_check_breakpoint(VirtualMachine *vm);
int debugger_check_watchpoint(VirtualMachine *vm, void *addr, int size, int access_type);
void debugger_print_registers(VirtualMachine *vm);
void debugger_print_stack(VirtualMachine *vm, int count);
void debugger_disassemble_current(VirtualMachine *vm);
int debugger_run(VirtualMachine *vm, int argc, char **argv);

//
// host_backtrace.c
//

/* Initialise libbacktrace state and warm up DWARF/Mach-O caches.
 * Must be called once at process startup (not in a signal handler) before
 * cc_host_backtrace_install_fatal().  argv0 is used to locate the binary. */
void cc_host_backtrace_init(const char *argv0);

/* Install top-level crash handlers (SIGSEGV/SIGBUS/SIGFPE/SIGILL) that print
 * a host C backtrace to stderr then re-raise the signal so the process dies
 * with the original signal/exit code.  No-op when CCCC_HAS_BACKTRACE is off
 * or on Windows. */
void cc_host_backtrace_install_fatal(void);

/* Print a host C backtrace to stderr.  Safe to call from a signal handler
 * after cc_host_backtrace_init() has completed. */
void cc_host_backtrace_print(void);

//
// serialize.c
//

char *serialize_node_to_source(VirtualMachine *vm, Node *node);

//
// dump_ast.c
//

void cc_dump_ast(FILE *f, Obj *prog, int verbose);
void cc_dump_ast_json(FILE *f, Obj *prog, int verbose);
void cc_dump_node(FILE *f, Node *node, int verbose);      // single-node dump (used by relfection.c)
const char *cc_node_kind_name(NodeKind kind);             // kind→string (used by relfection.c)

//
// json.c
//

void serialize_type_json(FILE *f, Type *ty, int indent);
void print_indent(FILE *f, int indent);
void print_escaped_string(FILE *f, const char *str);

//
// vm.c
//

long long generate_random_canary(void);

//
// url_fetch.c
//

bool is_url(const char *filename);
void init_url_cache(VirtualMachine *vm);
void clear_url_cache(VirtualMachine *vm);
char *fetch_url_to_cache(VirtualMachine *vm, const char *url);

//
// stdlib
//

// testing.c
void   cc_load_test_runtime(VirtualMachine *vm);
Token *cc_inject_test_header(VirtualMachine *vm);
int    cc_run_tests(VirtualMachine *vm, Obj *prog, const CcTestOptions *opts);

// preprocess.c — parse a whitespace-separated CLI-flag string (as allowed in
// [[cccc::test(flags = "...")]]) into a flag delta that can later be applied
// as a per-test override when recompiling.  Sets *or_bits to the bits that
// should be forced on, *set_mask to all bits explicitly named (cleared or
// set), and *opt_level/*opt_set if an -O/-Ox/--optimize=N was present.
// Unknown or malformed flags are reported via error_tok() at src_tok's
// source location and terminate compilation.
void cc_parse_test_flags(VirtualMachine *vm, Token *src_tok,
                         const char *flags_str, const char *test_name,
                         uint32_t *or_bits, uint32_t *set_mask,
                         int *opt_level, bool *opt_set);

// native.c / main.c shared infrastructure
typedef struct {
    const char **data;
    int          len;
    int          cap;
} ArgVec;

void   argv_push(ArgVec *args, const char *arg);
char  *make_tmp_path(const char *suffix);
int    run_argv(char *const argv[]);

// Native compile flags extracted from a CCCC vm instance.
typedef struct {
    const char **inc_paths;       int inc_paths_count;
    const char **sys_inc_paths;   int sys_inc_paths_count;
    const char **lib_paths;       int lib_paths_count;
    const char **libs;            int libs_count;
    const char **defines;         int defines_count;
    const char **undefs;          int undefs_count;
    const char  *std_arg;
} CcNativeCompileArgs;

//
// build.c (--build mode)
//
typedef struct {
    const char *entry_name;             // --build-entry override, or NULL
    const char *target_name;            // --build-target=NAME, or NULL (build all)
    const char *out_dir;                // -O/--build-out-dir, or NULL (default "build")
    int         verbose;                // -v (also enables host-runner verbose output)
    int         build_verbose;          // --build-verbose: per-target headers + command lines
    int         quiet;                  // --build-quiet: suppress per-step command lines
    int         keep_going;             // --build-keep-going: continue past target failures
    int         dry_run;                // --build-dry-run: print commands, run nothing
    int         jobs;                   // --build-jobs=N: parallel source compile slots (0/1 = serial)
    const CcNativeCompileArgs *defaults; // CLI -I/-D/-U/--std forwarded to each target
    const char **tool_allow;            // --build-tool-allow names (NULL = allow-all)
    int          tool_allow_count;
    int          list_targets;          // --build-list-targets: print factory names and exit
    const char  *profile;               // --build-profile=NAME: debug|release|relwithdebinfo|minsizerel
    const char  *cross_triple;          // --build-triple=TRIPLE: clang-style cross target triple (#547)
    const char  *cross_cc;              // --build-cc=COMPILER: override CC binary globally (#547)
    const char  *build_cache;           // --build-cache[=PATH]: NULL=off, ""=default path, else given path (#546)
} CcBuildOptions;

void   cc_load_build_runtime(VirtualMachine *vm);
Token *cc_inject_build_header(VirtualMachine *vm);
int    cc_run_build(VirtualMachine *vm, Obj *prog, const CcBuildOptions *opts);
char  *cccc_find_native_tool(const char *tool);
char  *cccc_path_find_executable(const char *name); // silent PATH probe (no error print)

// serialize.c
void cc_serialize_program(FILE *f, VirtualMachine *vm, Obj *prog, bool generated_only);

void register_ctype_functions(VirtualMachine *vm);
void register_fenv_functions(VirtualMachine *vm);
void register_locale_functions(VirtualMachine *vm);
void register_math_functions(VirtualMachine *vm);
void register_posix_functions(VirtualMachine *vm);
void register_pthread_functions(VirtualMachine *vm);
void register_threads_functions(VirtualMachine *vm);
void cccc_pthread_cleanup(VirtualMachine *vm);
void register_signal_functions(VirtualMachine *vm);

/* VM-managed signal pending flags (set only by async-safe native shims) */
extern volatile sig_atomic_t _cccc_pending[CCCC_NSIG];
extern volatile sig_atomic_t _cccc_any_pending;
/* Comparison helpers for test attribute assertions (CmpOp). */
static inline const char *cmp_op_str(CmpOp op) {
    switch (op) {
    case CMP_EQ: return "=";
    case CMP_NE: return "!=";
    case CMP_LT: return "<";
    case CMP_LE: return "<=";
    case CMP_GT: return ">";
    case CMP_GE: return ">=";
    default:     return "?";
    }
}

static inline bool apply_cmp_op_i64(CmpOp op, int64_t a, int64_t b) {
    switch (op) {
    case CMP_EQ: return a == b;
    case CMP_NE: return a != b;
    case CMP_LT: return a <  b;
    case CMP_LE: return a <= b;
    case CMP_GT: return a >  b;
    case CMP_GE: return a >= b;
    default:     return false;
    }
}

static inline bool apply_cmp_op_f64(CmpOp op, double a, double b, double eps) {
    switch (op) {
    case CMP_EQ: { double d = a - b; return d < eps && d > -eps; }
    case CMP_NE: { double d = a - b; return !(d < eps && d > -eps); }
    case CMP_LT: return a <  b;
    case CMP_LE: return a <= b;
    case CMP_GT: return a >  b;
    case CMP_GE: return a >= b;
    default:     return false;
    }
}

/* Async-safe native shim installed for user-registered VM signal handlers */
void _cccc_sig_shim(int sig);
void register_stdio_functions(VirtualMachine *vm);
void register_stdlib_functions(VirtualMachine *vm);
void register_string_functions(VirtualMachine *vm);
void register_time_functions(VirtualMachine *vm);
void register_wide_functions(VirtualMachine *vm);
void register_wide_bitint_functions(VirtualMachine *vm);
