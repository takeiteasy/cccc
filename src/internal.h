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

 This file was original part of chibicc by Rui Ueyama (MIT)
 https://github.com/rui314/chibicc
*/

#pragma once

#include "jcc.h"

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

#define JCC_MAGIC "JCC\0"
#define JCC_VERSION 4 // Version 4: 32-bit text words with PC-index branches

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

// Instruction encoding macros for new opcodes
// Text segment words are 32-bit. Wide immediates are stored little-endian in
// two consecutive instruction words.
// RRR format: [rd:8|rs1:8|rs2:8|unused:8]
#define ENCODE_RRR(rd, rs1, rs2)                                               \
    ((JCCInstrWord)(rd) | ((JCCInstrWord)(rs1) << 8) |                         \
     ((JCCInstrWord)(rs2) << 16))
#define DECODE_RRR(operands, rd, rs1, rs2)                                     \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
        rs2 = ((operands) >> 16) & 0xFF;                                       \
    } while (0)

// RR format: [rd:8|rs1:8|unused:16]
#define ENCODE_RR(rd, rs1)                                                     \
    ((JCCInstrWord)(rd) | ((JCCInstrWord)(rs1) << 8))
#define DECODE_RR(operands, rd, rs1)                                           \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
    } while (0)

// RI format: [OPCODE] [rd:8|unused:24] [immediate:64 split low, high]
#define ENCODE_R(rd) ((JCCInstrWord)(rd))
#define DECODE_R(operands, rd)                                                 \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
    } while (0)

static inline uint64_t cc_make_u64(JCCInstrWord lo, JCCInstrWord hi) {
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline long long cc_make_i64(JCCInstrWord lo, JCCInstrWord hi) {
    return (long long)cc_make_u64(lo, hi);
}

static inline JCCInstrWord cc_i64_lo(long long val) {
    return (JCCInstrWord)((uint64_t)val & 0xFFFFFFFFu);
}

static inline JCCInstrWord cc_i64_hi(long long val) {
    return (JCCInstrWord)(((uint64_t)val >> 32) & 0xFFFFFFFFu);
}

static inline JCCInstrWord cc_read_word(JCC *vm) {
    return vm->text_seg[vm->pc++];
}

static inline long long cc_read_i64(JCC *vm) {
    JCCInstrWord lo = cc_read_word(vm);
    JCCInstrWord hi = cc_read_word(vm);
    return cc_make_i64(lo, hi);
}

static inline void cc_write_i64_at(JCC *vm, JCCPc pc, long long val) {
    vm->text_seg[pc] = cc_i64_lo(val);
    vm->text_seg[pc + 1] = cc_i64_hi(val);
}

static inline long long cc_read_i64_at(JCC *vm, JCCPc pc) {
    return cc_make_i64(vm->text_seg[pc], vm->text_seg[pc + 1]);
}

static inline JCCPc cc_byte_offset_to_pc(long long offset) {
    if (offset < 0 || offset % (long long)sizeof(JCCInstrWord) != 0)
        return JCC_INVALID_PC;
    uint64_t pc = (uint64_t)offset / sizeof(JCCInstrWord);
    return pc > UINT32_MAX ? JCC_INVALID_PC : (JCCPc)pc;
}

static inline long long cc_pc_to_byte_offset(JCCPc pc) {
    return (long long)pc * (long long)sizeof(JCCInstrWord);
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

void strarray_push(StringArray *arr, char *s);
void arena_strarray_push(JCC *vm, StringArray *arr, char *s);
char *format(char *fmt, ...) __attribute__((format(printf, 1, 2)));
char *arena_strdup(JCC *vm, const char *str);
char *arena_strndup(JCC *vm, const char *str, int len);
char *arena_format(JCC *vm, char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
Token *preprocess(JCC *vm, Token *tok);

//
// tokenize.c
//

noreturn void error(char *fmt, ...) __attribute__((format(printf, 1, 2)));
void error_at(JCC *vm, char *loc, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void error_tok(JCC *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
bool error_tok_recover(JCC *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void warn_tok(JCC *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
bool equal(Token *tok, char *op);
Token *skip(JCC *vm, Token *tok, char *op);
bool consume(JCC *vm, Token **rest, Token *tok, char *str);
void convert_pp_tokens(JCC *vm, Token *tok);
File *new_file(JCC *vm, char *name, int file_no, char *contents);
Token *tokenize_string_literal(JCC *vm, Token *tok, Type *basety);
Token *tokenize(JCC *vm, File *file);
Token *tokenize_file(JCC *vm, char *filename);
Token *tokenize_string(JCC *vm, char *name, char *contents);
unsigned char *read_binary_file(JCC *vm, char *path, size_t *out_size);
void cc_output_preprocessed(FILE *f, Token *tok);

#undef unreachable
#define unreachable() error("internal error at %s:%d", __FILE__, __LINE__)

//
// preprocess.c
//

char *get_std_header(char *filename);
char *search_include_paths(JCC *vm, char *filename, int filename_len,
                           bool is_system);
void init_macros(JCC *vm);
void define_std_macros(JCC *vm);
void define_macro(JCC *vm, char *name, char *buf);
void undef_macro(JCC *vm, char *name);
Token *preprocess(JCC *vm, Token *tok);

//
// parse.c
//

Node *new_cast(JCC *vm, Node *expr, Type *ty);
int64_t const_expr(JCC *vm, Token **rest, Token *tok);
Obj *parse(JCC *vm, Token *tok);
void cc_execute_top_level_pragma_macro(JCC *vm, char *name, Token *tok,
                                       Node *args, int arg_count);

//
// type.c
//

extern Type *ty_void;
extern Type *ty_bool;

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

extern Type *ty_error;

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
bool is_complex(Type *ty);
bool is_numeric(Type *ty);
bool is_error_type(Type *ty);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(JCC *vm, Type *ty);
Type *pointer_to(JCC *vm, Type *base);
Type *func_type(JCC *vm, Type *return_ty);
Type *array_of(JCC *vm, Type *base, int size);
Type *vla_of(JCC *vm, Type *base, Node *expr);
Type *enum_type(JCC *vm);
Type *struct_type(JCC *vm);
Type *union_type(JCC *vm);
Type *block_type(JCC *vm, Type *return_ty, Type *params);
Type *complex_type_for(JCC *vm, Type *base);
void add_type(JCC *vm, Node *node);

//
// unicode.c
//

int encode_utf8(char *buf, uint32_t c);
uint32_t decode_utf8(JCC *vm, char **new_pos, char *p);
bool is_ident1(uint32_t c);
bool is_ident2(uint32_t c);
int display_width(JCC *vm, char *p, int len);

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
void *jcc_vm_reserve(size_t bytes);

// Commit 'len' bytes at offset 'off' from 'base'.  Pages are zero-filled by
// the OS.  'off' and 'len' are rounded to the system page size internally.
// Returns 0 on success, -1 on failure.
int jcc_vm_commit(void *base, size_t off, size_t len);

// Release a virtual range previously returned by jcc_vm_reserve.
void jcc_vm_release(void *base, size_t bytes);

//
// vm.c - Segment allocation / growth helpers (used by codegen, ops, reflect)
//

// Allocate (reserve + initial commit) all four VM segments using poolsize /
// poolsize_max from the JCC struct.  Initialises all derived segment pointers.
void vm_alloc_segments(JCC *vm);

// Ensure the text segment has at least 'num_words' words committed.
// Returns 0 on success, -1 when the reservation cap is reached.
int vm_text_ensure_count(JCC *vm, JCCPc num_words);

// Ensure 'needed' more bytes are available in the data segment starting from
// the current data_ptr.  Returns 0 on success, -1 at cap.
int vm_data_ensure(JCC *vm, long long needed);

// Grow the heap segment by at least 'need' bytes.
// Advances heap_end on success.  Returns 0 on success, -1 at cap.
int vm_heap_grow(JCC *vm, size_t need);

// Grow the stack segment downward to accommodate 'slots_needed' more slots
// below the current stack_base.  Updates stack_base on success.
// Returns 0 on success, -1 when the reservation floor is reached.
int vm_stack_grow(JCC *vm, int slots_needed);

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
// stdlib.c
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

void gen_function(JCC *vm, Obj *fn);
void gen(JCC *vm, Obj *prog);
// Note: gen_expr is now static in codegen.c with signature:
// static void gen_expr(JCC *vm, Node *node, int dest_reg);

//
// vm.c
//

int vm_eval(JCC *vm);

//
// optimize.c
//

void cc_optimize(JCC *vm, int level);

//
// llvm_backend.c
//

bool cc_llvm_backend_enabled(void);
const char *cc_llvm_backend_version(void);
int cc_llvm_backend_smoke_test(void);

//
// debugger.c
//

void debugger_init(JCC *vm);
void cc_debug_repl(JCC *vm);
void debugger_disassemble_current(JCC *vm);
void debugger_print_stack(JCC *vm, int count);
int debugger_check_breakpoint(JCC *vm);
int debugger_check_watchpoint(JCC *vm, void *addr, int size, int access_type);
void debugger_print_registers(JCC *vm);
void debugger_print_stack(JCC *vm, int count);
void debugger_disassemble_current(JCC *vm);
int debugger_run(JCC *vm, int argc, char **argv);

//
// serialize.c
//

char *serialize_node_to_source(JCC *vm, Node *node);

//
// dump_ast.c
//

void cc_dump_ast(FILE *f, Obj *prog, int verbose);
void cc_dump_ast_json(FILE *f, Obj *prog, int verbose);
void cc_dump_node(FILE *f, Node *node, int verbose);      // single-node dump (used by reflect.c)
const char *cc_node_kind_name(NodeKind kind);             // kind→string (used by reflect.c)

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
// stdlib
//

void register_ctype_functions(JCC *vm);
void register_fenv_functions(JCC *vm);
void register_locale_functions(JCC *vm);
void register_math_functions(JCC *vm);
void register_posix_functions(JCC *vm);
void register_signal_functions(JCC *vm);
void register_stdio_functions(JCC *vm);
void register_stdlib_functions(JCC *vm);
void register_string_functions(JCC *vm);
void register_time_functions(JCC *vm);
void register_wide_functions(JCC *vm);
