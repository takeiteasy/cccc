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
#define JCC_VERSION 3 // Version 3: data/text-relative address opcodes

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
// RRR format: [OPCODE] [rd:8|rs1:8|rs2:8|unused:40]
#define ENCODE_RRR(rd, rs1, rs2)                                               \
    ((long long)(rd) | ((long long)(rs1) << 8) | ((long long)(rs2) << 16))
#define DECODE_RRR(operands, rd, rs1, rs2)                                     \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
        rs2 = ((operands) >> 16) & 0xFF;                                       \
    } while (0)

// RR format: [OPCODE] [rd:8|rs1:8|unused:48]
#define ENCODE_RR(rd, rs1) ((long long)(rd) | ((long long)(rs1) << 8))
#define DECODE_RR(operands, rd, rs1)                                           \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
    } while (0)

// RI format: [OPCODE] [rd:8|unused:56] [immediate:64]
#define ENCODE_R(rd) ((long long)(rd))
#define DECODE_R(operands, rd)                                                 \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
    } while (0)

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

#define unreachable() error("internal error at %s:%d", __FILE__, __LINE__)

//
// preprocess.c
//

char *get_std_header(char *filename);
char *search_include_paths(JCC *vm, char *filename, int filename_len,
                           bool is_system);
void init_macros(JCC *vm);
void define_macro(JCC *vm, char *name, char *buf);
void undef_macro(JCC *vm, char *name);
Token *preprocess(JCC *vm, Token *tok);

//
// parse.c
//

Node *new_cast(JCC *vm, Node *expr, Type *ty);
int64_t const_expr(JCC *vm, Token **rest, Token *tok);
Obj *parse(JCC *vm, Token *tok);

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

extern Type *ty_error;

bool is_integer(Type *ty);
bool is_flonum(Type *ty);
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
// hashmap.c
//

void *hashmap_get(HashMap *map, const char *key);
void *hashmap_get2(HashMap *map, const char *key, int keylen);
void hashmap_put(HashMap *map, const char *key, void *val);
void hashmap_put2(HashMap *map, const char *key, int keylen, void *val);
void hashmap_delete(HashMap *map, const char *key);
void hashmap_delete2(HashMap *map, const char *key, int keylen);

// Integer key HashMap functions (avoid overhead of snprintf/strdup)
void *hashmap_get_int(HashMap *map, long long key);
void hashmap_put_int(HashMap *map, long long key, void *val);
void hashmap_delete_int(HashMap *map, long long key);
void hashmap_test(void);

// HashMap iteration
// Callback function type for iteration
// Return 0 to continue iteration, non-zero to stop
typedef int (*HashMapIterator)(char *key, int keylen, void *val,
                               void *user_data);

void hashmap_foreach(HashMap *map, HashMapIterator iter, void *user_data);
int hashmap_count_if(HashMap *map, HashMapIterator predicate, void *user_data);
void hashmap_test_iteration(void);

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
void init_url_cache(JCC *vm);
void clear_url_cache(JCC *vm);
char *fetch_url_to_cache(JCC *vm, const char *url);

//
// stdlib
//

void register_ctype_functions(JCC *vm);
void register_math_functions(JCC *vm);
void register_stdio_functions(JCC *vm);
void register_stdlib_functions(JCC *vm);
void register_string_functions(JCC *vm);
void register_time_functions(JCC *vm);
