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

#include <fenv.h> // for feraiseexcept(FE_INVALID) in cccc_f64_to_i64/cccc_f32_to_i64 (#775)

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
#define CCCC_VERSION                                                           \
    1 // Bytecode format version (pre-1.0; bump only at release)
      // V2: TLS template + reloc section added (#493)

// Product release version (#883), distinct from CCCC_VERSION above -- that
// one is the .c4 bytecode container format version and is bumped
// independently. CCCC_RELEASE_VERSION is stamped by tools/release.sh into an
// annotated git tag; it is not otherwise derived from git automatically.
#ifndef CCCC_RELEASE_VERSION
#define CCCC_RELEASE_VERSION "0.3.2"
#endif

// Git describe string, passed by build.c/Makefile via -DCCCC_GIT_DESC=...
// when building inside a git checkout. Absent from release tarballs (no
// .git), so --version falls back to CCCC_RELEASE_VERSION alone.
#ifndef CCCC_GIT_DESC
#define CCCC_GIT_DESC ""
#endif

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

// Token range for built-in FFI function pointers (goto */call via function
// pointer). Distinct from CCCC_DYN_TOKEN_BASE (-0x4a434300) used for dlopen
// symbols. Token for ffi_table[i] = CCCC_FFI_TOKEN_BASE - i  (all values <=
// CCCC_FFI_TOKEN_BASE).
#define CCCC_FFI_TOKEN_BASE (-0x4a434380LL)

#define REG_ZERO            0 // Always zero (writes discarded)
#define REG_RA              1 // Return address
#define REG_SP              2 // Stack pointer (unused for now - we have vm->sp)
#define REG_T0              5 // Temporary (caller-saved)
#define REG_T1              6 // Temporary
#define REG_T2              7 // Temporary
#define REG_T3              8 // Temporary
#define REG_T4              9 // Temporary
#define REG_A0              10 // Argument/return value
#define REG_A1              11 // Argument
#define REG_A2              12 // Argument
#define REG_A3              13 // Argument
#define REG_A4              14 // Argument
#define REG_A5              15 // Argument
#define REG_A6              16 // Argument
#define REG_A7              17 // Argument
#define REG_S0              18 // Saved (callee-saved)
#define REG_S1              19 // Saved
#define REG_S2              20 // Saved
#define REG_S3              21 // Saved
#define REG_S4              22 // Saved
#define REG_S5              23 // Saved
#define REG_S6              24 // Saved
#define REG_S7              25 // Saved
#define REG_T5              26 // Temporary (caller-saved)
#define REG_T6              27 // Temporary
#define REG_T7              28 // Temporary
#define REG_T8              29 // Temporary
#define REG_T9              30 // Temporary
#define REG_T10             31 // Temporary
#define NUM_REGS            32

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
    ((InstrWord)(rd) | ((InstrWord)(rs1) << 8) | ((InstrWord)(rs2) << 16))
#define ENCODE_RRRS(rd, rs1, rs2, scale)                                       \
    (ENCODE_RRR((rd), (rs1), (rs2)) | ((InstrWord)(scale) << 24))
#define DECODE_RRR(operands, rd, rs1, rs2)                                     \
    do {                                                                       \
        rd  = (operands) & 0xFF;                                               \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
        rs2 = ((operands) >> 16) & 0xFF;                                       \
    } while (0)
#define DECODE_RRRS(operands, rd, rs1, rs2, scale)                             \
    do {                                                                       \
        DECODE_RRR((operands), rd, rs1, rs2);                                  \
        scale = ((operands) >> 24) & 0xFF;                                     \
    } while (0)
#define ENCODE_RRRR(rd, rs1, rs2, rs3)                                         \
    (ENCODE_RRR((rd), (rs1), (rs2)) | ((InstrWord)(rs3) << 24))
#define DECODE_RRRR(operands, rd, rs1, rs2, rs3)                               \
    do {                                                                       \
        DECODE_RRR((operands), rd, rs1, rs2);                                  \
        rs3 = ((operands) >> 24) & 0xFF;                                       \
    } while (0)

// RR format: [rd:8|rs1:8|unused:16]
#define ENCODE_RR(rd, rs1) ((InstrWord)(rd) | ((InstrWord)(rs1) << 8))
#define DECODE_RR(operands, rd, rs1)                                           \
    do {                                                                       \
        rd  = (operands) & 0xFF;                                               \
        rs1 = ((operands) >> 8) & 0xFF;                                        \
    } while (0)

// RI format: [OPCODE] [rd:8|unused:24] [immediate:64 split low, high]
#define ENCODE_R(rd) ((InstrWord)(rd))
#define DECODE_R(operands, rd)                                                 \
    do {                                                                       \
        rd = (operands) & 0xFF;                                                \
    } while (0)

// LEA3-only flag (#676): set in the unused bits above rd (DECODE_R only
// reads the low 8 bits, so this doesn't disturb ordinary RI decoding) to
// suppress vm->stack_ptr_epochs recording for this LEA3's result. Set only
// when codegen has proven the address never escapes its creating frame.
#define LEA3_NO_RECORD ((InstrWord)(1 << 8))

// ENT3 masks-word flags (#703): float_param_mask/f32_param_mask only ever
// set bits 0-7 (register params are capped at 8, see gen_function), so bit
// 31 of each half is free. Set by codegen (patched post-body, mirroring the
// existing ent3_stack_loc patch) when this function's body proved it needs
// its own frame-epoch pushed -- ENT3_PUSH_EPOCH_AGG when it emits STKTAG for
// an escaping aggregate local/param (needed in both --dangling-detection and
// dynobjsz-only mode), ENT3_PUSH_EPOCH_SCALAR when it emits a recorded LEA3
// for an escaping scalar (needed only under --dangling-detection, since
// op_LEA3_fn's stack_ptr_epochs recording is itself gated on that flag).
// See op_ENT3_fn.
#define ENT3_PUSH_EPOCH_AGG    ((InstrWord)(1u << 31))
#define ENT3_PUSH_EPOCH_SCALAR ((InstrWord)(1u << 31))

static inline uint64_t cc_make_u64(InstrWord lo, InstrWord hi) {
    return (uint64_t)lo | ((uint64_t)hi << 32);
}

static inline long long cc_make_i64(InstrWord lo, InstrWord hi) {
    return (long long)cc_make_u64(lo, hi);
}

static inline InstrWord cc_i64_lo(long long val) {
    return (InstrWord)((uint64_t)val & 0xFFFFFFFFu);
}

// Composite key for vm->stack_var_meta. Two different functions whose
// locals happen to land at the same compile-time bp-relative offset (the
// common case, e.g. each function's first local) must not share a table
// entry, or SCOPEIN/CHKL/MARKR/MARKW end up reading one function's variable
// metadata while executing another's (#671). Folding the declaring
// function's unique scope_id into the key keeps lookups O(1) via the
// existing flat HashMap instead of falling back to a linear scan.
static inline long long stack_var_meta_key(int scope_id, long long offset) {
    return ((int64_t)scope_id << 32) | (uint32_t)offset;
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
    vm->text_seg[pc]     = cc_i64_lo(val);
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
static inline void cccc_freg_set_f64(VirtualMachine *vm, int reg,
                                     double value) {
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

// Defined float->integer conversion for F2I3/F2I3_F32 (#775) and the
// matching compile-time constant fold in parse.c's eval2(). A bare
// "(long long)double_value" cast is undefined behavior in the *host* C
// compiler that built cccc whenever the source is NaN, +-infinity, or
// out of range for long long -- the guest would then observe whatever
// the host CPU's convert instruction happens to do (e.g. a saturated
// value on aarch64's FCVTZS, but LLONG_MIN on x86's cvttsd2si on
// overflow), which is neither portable nor IEEE-754/C23-Annex-F
// conformant. Saturating matches what this project's primary
// development platform (aarch64) already does for free, and per C23
// Annex F.4 is a legitimate choice for "invalid" conversions alongside
// raising FE_INVALID:
//   NaN                       -> 0
//   x >= 2^63 (incl. +Inf)    -> LLONG_MAX
//   x <  -2^63 (incl. -Inf)   -> LLONG_MIN
//   otherwise                 -> the plain truncating cast
// The bounds are written as the exact powers of two (both exactly
// representable in a double/float), not as "(double)LLONG_MAX": that
// value rounds *up* to 2^63, so a "x <= (double)LLONG_MAX" guard would
// let x == 2^63 slip through and land back in UB.
static inline long long cccc_f64_to_i64(double x) {
    if (isnan(x)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (x >= 9223372036854775808.0) { // 2^63
        feraiseexcept(FE_INVALID);
        return LLONG_MAX;
    }
    if (x < -9223372036854775808.0) { // -2^63
        feraiseexcept(FE_INVALID);
        return LLONG_MIN;
    }
    return (long long)x;
}

static inline long long cccc_f32_to_i64(float x) {
    if (isnan(x)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (x >= 9223372036854775808.0f) { // 2^63
        feraiseexcept(FE_INVALID);
        return LLONG_MAX;
    }
    if (x < -9223372036854775808.0f) { // -2^63
        feraiseexcept(FE_INVALID);
        return LLONG_MIN;
    }
    return (long long)x;
}

// Defined float->unsigned-integer conversion for F2U3/F2U3_F32 (#780,
// follow-up to #775). Same rationale as cccc_f64_to_i64/cccc_f32_to_i64
// above -- a bare "(unsigned long long)double_value" cast is undefined
// behavior in the host compiler for NaN/out-of-range values -- but the
// bounds differ from the signed helpers in two ways worth calling out:
//
//   * The upper guard is 2^64 (18446744073709551616.0, exactly
//     representable in a double/float), not "(double)ULLONG_MAX": that
//     value rounds *up* to exactly 2^64, so a "x > (double)ULLONG_MAX"
//     guard would let x == 2^64 slip through, same trap as the signed
//     LLONG_MAX case documented above.
//   * The lower guard is x <= -1.0, not x < 0. "(unsigned long long)(-0.5)"
//     is well-defined C and must yield 0 with NO FE_INVALID: truncation
//     toward zero happens first, and the truncated value (-0.0) is
//     representable. Only once the truncated magnitude reaches -1 does the
//     conversion become genuinely out of range.
//
//   NaN                       -> 0
//   x >= 2^64 (incl. +Inf)    -> ULLONG_MAX
//   x <= -1.0 (incl. -Inf)    -> 0
//   otherwise                 -> the plain truncating cast
static inline unsigned long long cccc_f64_to_u64(double x) {
    if (isnan(x)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (x >= 18446744073709551616.0) { // 2^64
        feraiseexcept(FE_INVALID);
        return ULLONG_MAX;
    }
    if (x <= -1.0) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    // x is proven finite and in [0, 2^64). x86_64 has no native
    // double->uint64 instruction below AVX-512 (VCVTTSD2USI); typical
    // compiler-generated code for this cast synthesizes it from a signed
    // cvttsd2si on the full magnitude as part of a branchless fixup, which
    // spuriously raises FE_INVALID on real x86 hardware even though the
    // result is exact. aarch64's FCVTZU has no such issue. Since x is
    // already proven in range here, any FE_INVALID raised by the cast
    // itself is spurious -- clear it.
    unsigned long long r = (unsigned long long)x;
    feclearexcept(FE_INVALID);
    return r;
}

static inline unsigned long long cccc_f32_to_u64(float x) {
    if (isnan(x)) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    if (x >= 18446744073709551616.0f) { // 2^64
        feraiseexcept(FE_INVALID);
        return ULLONG_MAX;
    }
    if (x <= -1.0f) {
        feraiseexcept(FE_INVALID);
        return 0;
    }
    // See cccc_f64_to_u64 above: suppress the spurious FE_INVALID that
    // x86_64's software double->uint64 fixup can raise for in-range values.
    unsigned long long r = (unsigned long long)x;
    feclearexcept(FE_INVALID);
    return r;
}

static inline long long cccc_freg_raw_f64(VirtualMachine *vm, int reg) {
    union {
        double    d;
        long long ll;
    } conv;
    conv.d = cccc_freg_get_f64(vm, reg);
    return conv.ll;
}

static inline int cccc_freg_raw_f32(VirtualMachine *vm, int reg) {
    union {
        float f;
        int   i;
    } conv;
    conv.f = cccc_freg_get_f32(vm, reg);
    return conv.i;
}

static inline void cccc_freg_set_raw_f64(VirtualMachine *vm, int reg,
                                         long long bits) {
    union {
        double    d;
        long long ll;
    } conv;
    conv.ll = bits;
    cccc_freg_set_f64(vm, reg, conv.d);
}

static inline void cccc_freg_set_raw_f32(VirtualMachine *vm, int reg,
                                         int bits) {
    union {
        float f;
        int   i;
    } conv;
    conv.i = bits;
    cccc_freg_set_f32(vm, reg, conv.f);
}

/* Vector register accessors (up to 512-bit, see #722). The register itself
 * is a raw union (see VReg in cccc.h); the opcode carries the lane type,
 * mirroring the FReg design above, and the active WIDTH/lane count for a
 * given value rides in the instruction operand (see the SIMD opcode block
 * in cccc.h), not here. These two helpers always move the full register
 * (sizeof(VReg), all 64 bytes) -- correct for whole-register save/restore
 * use (mirrors VMOV3), NOT for a value-width VLDR/VSTR-style move (see
 * op_VLDR_fn/op_VSTR_fn in ops.c for that, which decode an explicit byte
 * width from the operand instead of assuming sizeof(VReg)). */
static inline void cccc_vreg_load(VirtualMachine *vm, int reg,
                                  const void *src) {
    memcpy(&vm->vregs[reg], src, sizeof(VReg));
}

static inline void cccc_vreg_store(VirtualMachine *vm, int reg, void *dst) {
    memcpy(dst, &vm->vregs[reg], sizeof(VReg));
}

static inline VReg *cccc_vreg(VirtualMachine *vm, int reg) {
    return &vm->vregs[reg];
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

// #964: true for the `v = alloca(tmp)` assignment declaration() (parse.c)
// builds for a VLA local -- new_vla_ptr() (parse.c) has exactly one
// construction site, at that assignment's lhs, so this check is exhaustive.
// Shared by serialize.c (deciding whether a block must stay unbraced when
// translated to native C) and codegen.c (#981: deciding whether a block
// needs an HMRK/HREL watermark pair for heap reclamation) -- both need the
// identical "does this block's own declaration statement include a VLA"
// shape check, applied directly to whichever specific ND_BLOCK node the
// caller already has in hand (never recursively hunting through a parent
// for a nested one: declaration()'s per-declarator statements are always
// flat, immediate children of the one ND_BLOCK it builds for that
// declaration, and a genuinely nested user `{ }` scope gets its own
// separate visit from whichever caller is walking the tree, so it never
// needs to be found "through" an enclosing block either).
static inline bool node_is_vla_ptr_assign(Node *n) {
    return n && n->kind == ND_ASSIGN && n->lhs && n->lhs->kind == ND_VLA_PTR;
}

// #973 follow-up: true for the initializer of a pointer-to-VLA local (see
// Obj.deferred_vla_ptr_init, cccc.h and the matching ND_EXPR_STMT case in
// serialize_stmt()) -- the same "must stay in scope past this block" shape
// as node_is_vla_ptr_assign, just for `int (*p)[n] = &v;` instead of a VLA's
// own `v = alloca(...)`.
static inline bool node_is_deferred_vla_ptr_init(Node *n) {
    return n && n->kind == ND_ASSIGN && n->lhs && n->lhs->kind == ND_VAR &&
           n->lhs->var->deferred_vla_ptr_init == n;
}

// #964: true if any of blk's *immediate* statements is a VLA_PTR assignment
// or (#973 follow-up) a deferred pointer-to-VLA initializer. declaration()
// bundles a statement's per-declarator initializers into one ND_BLOCK (e.g.
// `int n = 4, v[n];` is a single ND_BLOCK holding both), so a block
// containing such a declarator has to stay unbraced when serialized -- see
// serialize_stmt_list_item() in serialize.c. #981 (codegen.c) reuses this
// same check to decide whether a block needs an HMRK/HREL watermark pair: a
// missed detection only forfeits reclamation for that block (its VLA
// storage is still swept, just later, at frame exit), it never
// miscompiles anything, so under-detecting here is always safe.
static inline bool block_defines_vla(Node *blk) {
    if (!blk || blk->kind != ND_BLOCK)
        return false;
    for (Node *s = blk->body; s; s = s->next)
        if (s->kind == ND_EXPR_STMT && (node_is_vla_ptr_assign(s->lhs) ||
                                        node_is_deferred_vla_ptr_init(s->lhs)))
            return true;
    return false;
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
uint64_t cccc_warning_mask_for_name(const char *name);
bool cccc_warning_is_group_name(const char *name);
void error_at(VirtualMachine *vm, char *loc, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void error_tok(VirtualMachine *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
bool error_tok_recover(VirtualMachine *vm, Token *tok, char *fmt, ...)
    __attribute__((format(printf, 3, 4)));
void warn_at(VirtualMachine *vm, char *loc, CCCCWarning category, char *fmt,
             ...) __attribute__((format(printf, 4, 5)));
void warn_tok(VirtualMachine *vm, Token *tok, CCCCWarning category, char *fmt,
              ...) __attribute__((format(printf, 4, 5)));
const char *cccc_warning_name(CCCCWarning warning);
bool equal(Token *tok, char *op);
Token *skip(VirtualMachine *vm, Token *tok, char *op);
bool consume(VirtualMachine *vm, Token **rest, Token *tok, char *str);
void convert_pp_tokens(VirtualMachine *vm, Token *tok);
File *new_file(VirtualMachine *vm, char *name, int file_no, char *contents);
Token *tokenize_string_literal(VirtualMachine *vm, Token *tok, Type *basety);
Token *tokenize(VirtualMachine *vm, File *file);
Token *tokenize_file(VirtualMachine *vm, char *filename, bool allow_shebang);
Token *tokenize_string(VirtualMachine *vm, char *name, char *contents);
unsigned char *read_binary_file(VirtualMachine *vm, char *path,
                                size_t *out_size);

#undef unreachable
#define unreachable() error("internal error at %s:%d", __FILE__, __LINE__)

//
// preprocess.c
//

char *cccc_path_find_executable(const char *name);
char *cccc_find_native_cc(void);
void define_std_macros(VirtualMachine *vm);
char *get_std_header(char *filename);
const char *get_stdlib_reg_fn_name(const char *header);
const char *get_std_header_name(int i);
char *search_include_paths(VirtualMachine *vm, char *filename, int filename_len,
                           bool is_system);
Token *tokenize_private_header(VirtualMachine *vm, char *name, char *tag);
void init_macros(VirtualMachine *vm);
void define_macro(VirtualMachine *vm, char *name, char *buf);
void undef_macro(VirtualMachine *vm, char *name);
Token *preprocess(VirtualMachine *vm, Token *tok);
bool is_compiler_owned_header(const char *name);
void isolate_comptime_macros(VirtualMachine *vm);
bool try_extract_attr_macro(VirtualMachine *vm, Token **tok_ptr,
                            bool emit_scan);

//
// parse.c
//

bool is_flonum(Type *ty);
Node *new_cast(VirtualMachine *vm, Node *expr, Type *ty);
int64_t const_expr(VirtualMachine *vm, Token **rest, Token *tok);
// #1095: const_expr()'s own body, but surfacing sizeof/_Alignof-of-a-
// from_include-type provenance (Node.layout_ty/layout_is_align, #1031) to
// the caller instead of discarding it with the node -- see
// node_layout_const()/const_expr_layout()'s own comments (parse_analysis.c).
bool node_layout_const(Node *node, Type **out_ty, bool *out_align);
int64_t const_expr_layout(VirtualMachine *vm, Token **rest, Token *tok,
                          Type **out_ty, bool *out_align);
// #815/#816: shared duplicate/overlapping case-label check -- used by the
// switch-statement epilogue here and by the comptime reflection switch
// builders in reflection.c.
void check_case_conflict(VirtualMachine *vm, Node *chain, Node *c);
bool node_int_const_fits(VirtualMachine *vm, Node *expr, Type *to);
Obj *parse(VirtualMachine *vm, Token *tok);
void cc_leave_top_file_scope(
    VirtualMachine *vm); // #1001, parse.c -- see comment there
void cc_execute_top_level_macro(VirtualMachine *vm, char *name, Token *tok,
                                Node *args, int arg_count);
void cc_execute_attribute_macro(VirtualMachine *vm, MacroFn *pm, Token *tok,
                                AttrTarget *target, Node *args, int arg_count);
void ensure_reflection_attrs_registered(VirtualMachine *vm);
void __builtin_ensure_string_h_decls(void);
void cc_apply_attr_to_fn(VirtualMachine *vm, Obj *fn, const char *attr_text,
                         Token *site_tok);
// Expand a deferred ND_INIT_SPLICE node into positional ND_ASSIGN chains.
// Called by quote_substitute in relfection.c after the splice chain is
// resolved.
Node *node_expand_init_splice(VirtualMachine *vm, Node *splice, Node *chain);
// Compile-time constant evaluators (wrappers around the static
// eval/eval_double).
int64_t cc_eval(VirtualMachine *vm, Node *node);
double cc_eval_double(VirtualMachine *vm, Node *node);

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

extern Type *ty_auto; // C23 type-inference sentinel

extern Type *ty_error;

bool is_integer(Type *ty);
bool is_complex(Type *ty);
bool is_numeric(Type *ty);
bool is_vector(Type *ty);
bool is_decimal(Type *ty);
int dec_width_code(Type *ty); // 0/1/2 for _Decimal32/64/128, else -1
bool is_error_type(Type *ty);

// #832: `env` selects which BID rounding mode/exception-flag policy an
// entry point uses -- CCCC_DEC_ENV_DYNAMIC translates the host's *current*
// fesetround() mode and raises the resulting BID exception flags via
// feraiseexcept() (every runtime call site: VM opcodes, strtod, scanf);
// CCCC_DEC_ENV_STATIC always rounds to-nearest and discards flags (the
// compile-time constant folder only, src/parse.c's eval_decimal, which must
// never observe or perturb the host FP environment it runs inside). See
// src/stdlib/decimal.c's top-of-#ifdef comment for the full rationale.
#define CCCC_DEC_ENV_STATIC  0
#define CCCC_DEC_ENV_DYNAMIC 1

// _Decimal32/64/128 runtime shim (src/stdlib/decimal.c, tracker #402). `w` is
// the width code from dec_width_code(). Declared unconditionally; defined
// under CCCC_HAS_DECIMAL via the Intel BID library, else return
// false/UNORDERED/-1 as documented per-function below. Raw byte pointers
// only, so no BID type needs to appear in a VM header.
bool cccc_dec_binop(int op /* '+','-','*','/' */, int w, void *dst,
                    const void *a, const void *b, int env);
bool cccc_dec_neg(int w, void *dst, const void *a); // exact, no env
int cccc_dec_cmp(int w, const void *a,
                 const void *b); // 0=EQ,1=LT,2=GT,3=UNORDERED; quiet, no env
bool cccc_dec_from_int(int w, void *dst, long long v, bool is_unsigned,
                       int env);
bool cccc_dec_to_int(int w, const void *src, long long *out, bool is_unsigned,
                     int env);
bool cccc_dec_from_bin(int w, void *dst, uint64_t bits, bool src_is_f32,
                       int env);
bool cccc_dec_to_bin(int w, const void *src, bool dst_is_f32,
                     uint64_t *out_bits, int env);
bool cccc_dec_convert(int dst_w, int src_w, void *dst, const void *src,
                      int env);
int cccc_dec_format(char *buf, size_t n, const void *val,
                    int w);              // -1 if unsupported
bool cccc_dec_encode_literal(const char *digits, int w,
                             void *out); // compile-time only, always to-nearest

// printf/scanf %Hf/%Df/%DDf integration (tracker #829, phase 2 of #402).
// cccc_dec_format() above only ever produces BID's canonical shortest-form
// string (the __builtin_decimal_to_chars contract); cccc_dec_format_ex()
// implements the full printf float surface (f/F/e/E/g/G, flags, field
// width, precision) on top of the same decompose-and-render machinery.
// `conv` is one of 'f' 'F' 'e' 'E' 'g' 'G'; `flags` is a bitmask of
// CCCC_DECFMT_*; `field_width`/`prec` < 0 mean "not specified" (prec's
// default matches C's per-conversion default: 6 for f/e, 6-treated-as-1 for
// g). Returns the length that would have been written (snprintf contract),
// or -1 if unsupported (CCCC_HAS_DECIMAL off).
#define CCCC_DECFMT_MINUS 1u  // '-' flag: left-justify
#define CCCC_DECFMT_PLUS  2u  // '+' flag: force sign
#define CCCC_DECFMT_SPACE 4u  // ' ' flag: space for positive sign
#define CCCC_DECFMT_ALT   8u  // '#' flag: keep trailing zeros / decimal point
#define CCCC_DECFMT_ZERO  16u // '0' flag: zero-pad (ignored if '-' set)
int cccc_dec_format_ex(char *buf, size_t n, const void *val, int w, int conv,
                       unsigned flags, int field_width, int prec);
bool cccc_dec_from_string(int w, void *dst, const char *s,
                          int env); // scanf %Hf/%Df/%DDf

// strtod32/64/128 (tracker #832, phase 2 of #402): parse a decimal out of a
// NUL-terminated string, C's strtod() contract (`*endptr` set past the
// longest valid prefix, or left at `s` if there was none). Backs
// include/stdlib.h's strtod32/64/128 static inline wrappers via the
// __cccc_dec_strtod FFI trampoline (src/stdlib/stdlib.c) -- no new opcode,
// same FFI-wrapper pattern <decimal_math.h> uses. Returns false only when
// built without CCCC_HAS_DECIMAL.
bool cccc_dec_strtod(int w, void *dst, const char *s, char **endptr, int env);

// _Decimal32/64/128 <math.h> transcendentals (tracker #828, phase 2 of #402).
// Defined in src/stdlib/decimal_math.c; the typed guest-facing API lives in
// include/decimal_math.h.
void register_decimal_math_functions(VirtualMachine *vm);
bool is_compatible(Type *t1, Type *t2);
Type *copy_type(VirtualMachine *vm, Type *ty);
Type *pointer_to(VirtualMachine *vm, Type *base);
Type *func_type(VirtualMachine *vm, Type *return_ty);
Type *array_of(VirtualMachine *vm, Type *base, int size);
Type *vector_of(VirtualMachine *vm, Type *base, int bytes);
Type *vector_mask_type(VirtualMachine *vm, Type *vecty);
Type *vla_of(VirtualMachine *vm, Type *base, Node *expr);
// #973 follow-up: true if ty is TY_VLA, or a chain of TY_PTR/TY_ARRAY whose
// eventual base is TY_VLA (e.g. `int (*)[n]`) -- used to detect a local
// whose C declarator reads a runtime-only variable and so can't be hoisted
// to the top of its function. See Obj.deferred_vla_ptr_init (cccc.h).
bool type_contains_vla(Type *ty);
Type *enum_type(VirtualMachine *vm);
Type *struct_type(VirtualMachine *vm);
Type *union_type(VirtualMachine *vm);
Type *block_type(VirtualMachine *vm, Type *return_ty, Type *params);
// #994: size-aware block descriptor layout. A capture's slot is 8 bytes
// (a scalar value, a __block heap-box pointer, or a TY_VLA's placeholder
// pointer) or align_to(cap->ty->size, 8) for a wider by-value aggregate --
// no longer a flat one-slot-per-capture array. codegen.c uses these
// instead of re-deriving the layout so parse.c's descriptor-local size and
// codegen.c's slot addressing can never drift apart.
long cc_block_capture_offset(Obj *block_fn, int idx);
long cc_block_desc_size(Obj *block_fn);
Type *complex_type_for(VirtualMachine *vm, Type *base);
Type *bitint_type(VirtualMachine *vm, Token *tok, int width, bool is_unsigned);
void add_type(VirtualMachine *vm, Node *node);
void warn_implicit_conversion(VirtualMachine *vm, Node *expr, Type *to,
                              Token *tok);

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

#define WATCHPOINT_CHECK(vm, addr, size, kind)                                 \
    do {                                                                       \
        if ((vm)->flags & CCCC_ENABLE_DEBUGGER)                                \
            debugger_check_watchpoint((vm), (addr), (size), (kind));           \
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
long long cccc_rt_dlsym(VirtualMachine *vm, long long handle_token,
                        const char *symbol);
long long cccc_rt_dlclose(VirtualMachine *vm, long long handle_token);
long long cccc_rt_dlerror(VirtualMachine *vm);
int cccc_ffi_name_in_list(char **list, int count, const char *name);
DynamicSymbol *cccc_find_dynamic_symbol(VirtualMachine *vm, long long token);
int cccc_call_native_function(VirtualMachine *vm, void *func_ptr,
                              const char *name, long long *args,
                              int actual_nargs, uint64_t double_arg_mask,
                              uint64_t float_arg_mask, int returns_double,
                              int returns_float, int is_variadic,
                              int num_fixed_args);
VirtualMachine *cccc_current_ffi_vm(void);
// Returns the number of mutexes currently held by the active VM thread.
// Used by race detection in ops.c; implemented in stdlib/pthread.c.
int cccc_thread_held_lock_count(VirtualMachine *vm);

// Synchronously call a guest function-pointer VALUE (as produced by a bare
// function-name expression: either a byte offset into vm->text_seg, or an
// FFI token for an already-registered host function taken as a value, e.g.
// `qsort(a, n, sz, alphasort)`) from host C code that is already executing
// inside a native/FFI call on the current thread with the GIL held.
//
// This is for host libc functions that invoke a guest-supplied callback
// synchronously and expect an ordinary C return value back (qsort/bsearch
// comparators, glob's errfunc, atexit/at_quick_exit handlers run from
// wrap_exit/wrap_quick_exit while still inside that FFI call, ...). It must
// NOT be used from an async signal handler or any context without the GIL
// held and a live vm_eval on the C stack. A post-GIL-release context (e.g.
// cc_run's normal-return atexit drain in vm.c) must instead call cc_run_at
// directly with each handler's byte offset converted to a Pc -- the same
// top-level cc_run_at cycle already used for constructors/destructors.
//
// args/nargs: up to 8 plain integer/pointer arguments (REG_A0..REG_A7) --
// sufficient for every current use site; no float/double argument or return
// support is implemented since none of the callers need it.
// out_ival: receives the callee's integer/pointer return value (REG_A0) on
// success.
// Returns 0 on success, -1 if fn_value doesn't resolve to a callable target
// or if the nested vm_eval reports an error (e.g. the guest callback itself
// faulted).
int cccc_call_guest_callback(VirtualMachine *vm, long long fn_value,
                             const long long *args, int nargs,
                             long long *out_ival);

// Runs `entry` as a complete, non-nested top-level VM execution cycle (same
// machinery as cc_run_at) with a single pointer argument in REG_A0 --
// i.e. matching a void (*)(void *) signature. Used from
// cccc_pthread_run_main_tss_destructors (stdlib/pthread.c) to invoke TSS/
// pthread-key destructors after pthread_exit() on the main thread, a
// post-GIL-release context where cccc_call_guest_callback above cannot be
// used. See its definition in vm.c for the full rationale.
int cc_run_at1(VirtualMachine *vm, Pc entry, void *arg);

// Called once, right after cc_run_at(main) returns in cc_run (vm.c), before
// atexit handlers/destructors run. Drains TSS/pthread-key destructors for
// the main thread's ThreadRecord, but ONLY if pthread_exit() was actually
// called by main -- a plain `return` from main() must NOT run them (matches
// glibc; see man/COVERAGE.md's <threads.h> row). No-op if pthread_exit()
// was never called on the main thread. Implemented in stdlib/pthread.c
// (stubbed out under _WIN32, same as the rest of that file).
void cccc_pthread_run_main_tss_destructors(VirtualMachine *vm);

// VM-heap-aware allocator primitives (ops.c, part of vm.c's translation
// unit) -- back both the MALC/MALCA/PMEMA/MFRE opcodes (direct
// malloc/aligned_alloc/posix_memalign/free calls, routed here by codegen's
// is_extern_func_name special-casing) AND the cccc_ffi_* wrappers in
// stdlib.c that a bare malloc/free/calloc/realloc/reallocarray/aligned_alloc/
// posix_memalign value resolves to when taken as a function pointer and
// called indirectly (#865) -- that indirect path bypasses codegen's
// syntactic routing entirely, so it needs these as plain callable functions
// rather than only as register-based opcode glue. cccc_vm_heap_calloc/
// realloc/reallocarray are built from cccc_vm_heap_malloc/free rather than
// extracted from their opcode counterparts (op_CALC_fn/op_REALC_fn/
// op_REALCA_fn), which remain untouched.
void *cccc_vm_heap_malloc(VirtualMachine *vm, long long requested_size);
void *cccc_vm_heap_malloc_aligned(VirtualMachine *vm, long long requested_size,
                                  size_t alignment);
int cccc_vm_heap_posix_memalign(VirtualMachine *vm, void **memptr,
                                size_t alignment, long long requested_size);
int cccc_vm_heap_free(VirtualMachine *vm, void *ptr);
void *cccc_vm_heap_calloc(VirtualMachine *vm, long long nmemb, long long size);
void *cccc_vm_heap_realloc(VirtualMachine *vm, void *ptr, long long new_size);
void *cccc_vm_heap_reallocarray(VirtualMachine *vm, void *ptr, long long nmemb,
                                long long size);

//
// hashmap.c
//

void *hashmap_get(HashMap *map, const char *key);
void *hashmap_get2(HashMap *map, const char *key, int keylen);
void hashmap_put(HashMap *map, const char *key, void *val);
void hashmap_put2(HashMap *map, const char *key, int keylen, void *val);
void hashmap_delete(HashMap *map, const char *key);
void hashmap_delete2(HashMap *map, const char *key, int keylen);

// Borrowed key HashMap functions (keys are NOT copied; caller must ensure
// lifetime)
void hashmap_put2_borrowed(HashMap *map, const char *key, int keylen,
                           void *val);
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
// #1041: reset the host's process-global getopt() state (optind/opterr/
// optopt, and optreset where available) right before guest code starts
// running -- cccc's own CLI parsing (main.c) shares that same state via
// include/getopt.h's __cccc_optind_ptr() etc accessor shims, so a guest
// program calling getopt() as its first action would otherwise see
// whatever cccc's own argument parsing left behind.
void cccc_reset_getopt_state(void);

typedef struct ExecState {
    long long  regs[32];
    FReg       fregs[32];
    VReg       vregs[32];
    Pc         pc;
    long long *bp;
    long long *sp;
    long long  cycle;
    long long *initial_sp;
    long long *initial_bp;
    long long *stack_seg;
    long long *stack_base;
    size_t     stack_committed;
    long long *shadow_stack;
    long long *shadow_sp;
    HashMap    init_state;
    // Per-thread dangling-detector state (#866) -- see the comment above
    // VirtualMachine's own copy of these fields in cccc.h. frame_epoch_counter
    // starts at 0 for a brand-new thread (via cccc_exec_state_alloc_stack's
    // memset) and is safe to do so: it only needs to be monotonic *within*
    // one stack's address range to disambiguate a reused address's old vs.
    // new occupant, and no two threads ever share a stack allocation.
    unsigned long long frame_epoch_counter;
    FrameEpochs        frame_epochs;
    HashMap            live_epochs;
    HashMap            stack_ptr_epochs;
    StackIntervals     stack_intervals;
} ExecState;

void cccc_exec_state_save(VirtualMachine *vm, ExecState *state);
void cccc_exec_state_restore(VirtualMachine *vm, const ExecState *state);
int cccc_exec_state_alloc_stack(VirtualMachine *vm, ExecState *state);
void cccc_exec_state_release_stack(VirtualMachine *vm, ExecState *state);
void cccc_exec_state_prepare_call(VirtualMachine *vm, ExecState *state,
                                  Pc entry, long long arg);
void cccc_gil_init(VirtualMachine *vm);
void cccc_gil_destroy(VirtualMachine *vm);
void cccc_gil_acquire(VirtualMachine *vm);
void cccc_gil_release(VirtualMachine *vm);

//
// optimize.c
//

void cc_optimize(VirtualMachine *vm, int level);

//
// analyze.c
//
// CcAnalyzeNgramOptions/cc_analyze_ngram_* live in cccc.h (main.c-only);
// the fusion-candidate half lives here since optimize.c calls it too.

typedef struct {
    int  top_n;
    bool json; // emit JSON instead of text
} CcAnalyzeFusionOptions;

typedef struct CcFusionState CcFusionState;

CcFusionState *cc_analyze_fusion_begin(const CcAnalyzeFusionOptions *opts);
void cc_analyze_fusion_feed(CcFusionState *st, const InstrWord *text,
                            long long num_words, const char *label, FILE *out);
void cc_analyze_fusion_finish(CcFusionState *st, FILE *out);

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

CcFusionCandidate *cc_analyze_fusion_collect(CcFusionState *st, int *out_count);

//
// debugger.c
//

void debugger_init(VirtualMachine *vm);
void cc_debug_repl_host_fault(VirtualMachine *vm, int sig, void *fault_addr);
void debugger_disassemble_current(VirtualMachine *vm);
void debugger_print_stack(VirtualMachine *vm, int count);
int debugger_check_breakpoint(VirtualMachine *vm);
int debugger_check_watchpoint(VirtualMachine *vm, void *addr, int size,
                              int access_type);
void debugger_print_registers(VirtualMachine *vm);
int debugger_run(VirtualMachine *vm, int argc, char **argv);
int cc_is_valid_vm_address(
    VirtualMachine *vm,
    void *addr); // text/data/heap/stack segment bounds check (used by dump.c's
                 // cc_dump_value, #666)

//
// host_backtrace.c
//

/* Print a host C backtrace to stderr.  Safe to call from a signal handler
 * after cc_host_backtrace_init() has completed. */
void cc_host_backtrace_print(void);

//
// serialize.c
//

//
// dump_ast.c
//

void cc_dump_node(FILE *f, Node *node,
                  int verbose);   // single-node dump (used by relfection.c)
const char *
cc_node_kind_name(NodeKind kind); // kind→string (used by relfection.c)
const char *cc_type_kind_name(
    TypeKind kind); // kind→string (used by serialize.c's #963c hard error)
void cc_dump_type(FILE *f,
                  Type *ty); // C-ish type spelling (used by the REPL, #661)

// Recursive aggregate/scalar value formatter (#666), shared between the REPL
// and (follow-up) the debugger's inspect/print commands. Neither prints a
// "(type) " prefix or trailing newline -- callers add that themselves, since
// the debugger will want "name = value" rather than "(type) value".
//
// cc_dump_value: memory-based -- addr points directly at a live value of
// type ty (a global/RETBUF-pool/heap address the caller already validated is
// still live). This is the entry point for the debugger follow-up: it always
// has a frame/global address and a Type, never registers.
void cc_dump_value(FILE *f, VirtualMachine *vm, Type *ty, const void *addr);
// cc_dump_value_reg: REPL glue -- ty's result came back in a register pair
// (ival/fval) rather than from a known address. For TY_STRUCT/TY_UNION/
// TY_ARRAY/TY_VECTOR, ival *is* the address (RETBUF pool for a returned
// struct/union/vector, data-segment address for an array lvalue), so this
// just validates and delegates to cc_dump_value.
void cc_dump_value_reg(FILE *f, VirtualMachine *vm, Type *ty, long long ival,
                       double fval);

//
// json.c
//

void serialize_type_json(FILE *f, Type *ty, int indent);
void print_indent(FILE *f, int indent);
void print_escaped_string(FILE *f, const char *str);

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

// vm.c — global pointer to the currently executing VM (set/cleared by
// cc_run_at)
extern VirtualMachine *cc_running_vm;

// reflection.c — the VM live for the current compile. Seeded by cc_init,
// save/restored around macro execution windows in macros.c, cleared by
// cc_destroy. Every comptime __builtin_* reads this instead of taking an
// explicit VirtualMachine* parameter.
extern VirtualMachine *__builtin_current_vm;

// ops.c (included into vm.c) — propagate the #653 heap type shadow from
// src to dst, exposed for the memcpy/memmove shims in stdlib/string.c
// (host-function writes to guest heap memory have no other VM hook). A
// no-op when --type-checks is off or either range isn't in the tracked
// heap; see type_shadow_copy's doc comment in ops.c for full semantics.
void cc_type_shadow_copy(VirtualMachine *vm, void *dst, const void *src,
                         size_t len);

// ops.c (included into vm.c) -- #769: exposed for wrap_qsort's shim in
// stdlib/stdlib.c, so a host qsort() call can preserve the #653 type shadow
// across the sort instead of clearing it. cc_type_shadow_elements_uniform
// returns true iff every `size`-byte element in [base, base+nmemb*size)
// carries the same shadow byte pattern as element 0 (see its doc comment in
// ops.c) -- a permutation of such an array can't change what's caught, so
// wrap_qsort skips the clear when this holds both before and after the
// underlying host qsort() runs. cc_type_shadow_clear_range is a plain
// segment-agnostic clear (heap or globals) for when it doesn't.
bool cc_type_shadow_elements_uniform(VirtualMachine *vm, const void *base,
                                     size_t nmemb, size_t size);
void cc_type_shadow_clear_range(VirtualMachine *vm, void *p, size_t len);

// Preprocess include/cccc/testing.h and register CCCC_ASSERT* macros; see
// cc_inject_test_header's definition (src/testing.c) for the full contract.
Token *cc_inject_test_header(VirtualMachine *vm);

// preprocess.c — parse a whitespace-separated CLI-flag string (as allowed in
// Parses a whitespace-separated CLI-flag string from
// [[cccc::test(flags="...")]] into a CcTestFlagsDelta.  Supports safety
// presets, -O levels, safety check flags, -W*/-Werror* warning flags, and
// -f/-fno- optimisation-pass flags. Unknown or malformed flags are reported via
// error_tok() at src_tok's source location and terminate compilation.
void cc_parse_test_flags(VirtualMachine *vm, Token *src_tok,
                         const char *flags_str, const char *test_name,
                         CcTestFlagsDelta *out);

// exec.c: shared infrastructure for spawning host toolchain processes
// (cc/ar/ld), used by both -c=native (main.c) and --build (build.c).
typedef struct {
    const char **data;
    int          len;
    int          cap;
} ArgVec;

void argv_push(ArgVec *args, const char *arg);
int run_argv(char *const argv[]);
int run_argv_env(char *const argv[], char *const envp[]);
int run_argv_quiet(char *const argv[]);

// Preprocess include/cccc/building.h; see cc_inject_build_header's
// definition (src/build.c) for the full contract.
Token *cc_inject_build_header(VirtualMachine *vm);

//
// build.c (--build mode)
//
char *cccc_find_native_tool(const char *tool);
char *cccc_path_find_executable(
    const char *name); // silent PATH probe (no error print)

void register_ctype_functions(VirtualMachine *vm);
void register_fenv_functions(VirtualMachine *vm);
void register_locale_functions(VirtualMachine *vm);
void register_math_functions(VirtualMachine *vm);
void register_posix_functions(VirtualMachine *vm);
// Per-domain POSIX registrars (#946 split of posix.c), called directly by
// register_posix_functions() and also mapped 1:1 from tools/stdlib.tsv for
// a header whose registered functions live in exactly one domain (#1012) --
// see get_stdlib_reg_fn_name()/register_stdlib_for_header() in preprocess.c.
// A header that spans more than one domain (unistd.h, strings.h,
// sys/resource.h) stays mapped to register_posix_functions above.
void register_posix_aio_functions(VirtualMachine *vm);
void register_posix_dir_functions(VirtualMachine *vm);
void register_posix_io_functions(VirtualMachine *vm);
void register_posix_ipc_functions(VirtualMachine *vm);
void register_posix_lang_functions(VirtualMachine *vm);
void register_posix_mqueue_functions(VirtualMachine *vm);
void register_posix_ndbm_functions(VirtualMachine *vm);
void register_posix_net_functions(VirtualMachine *vm);
void register_posix_poll_functions(VirtualMachine *vm);
void register_posix_sched_functions(VirtualMachine *vm);
void register_posix_search_functions(VirtualMachine *vm);
void register_posix_spawn_functions(VirtualMachine *vm);
void register_posix_statfs_functions(VirtualMachine *vm);
void register_posix_wait_functions(VirtualMachine *vm);
void register_posix_wordexp_functions(VirtualMachine *vm);
void register_pthread_functions(VirtualMachine *vm);
void register_threads_functions(VirtualMachine *vm);
void cccc_pthread_cleanup(VirtualMachine *vm);
void register_signal_functions(VirtualMachine *vm);

/* VM-managed signal pending flags (set only by async-safe native shims) */
extern volatile sig_atomic_t _cccc_pending[CCCC_NSIG];
extern volatile sig_atomic_t _cccc_any_pending;

/* SIGEV_THREAD (#870) notification cookies -- set only by the async-safe
   host trampoline cccc_sigev_thread_trampoline() (src/stdlib/posix_aio.c),
   polled by the VM dispatch loop's safe point (src/vm.c) alongside
   _cccc_pending/_cccc_any_pending above. */
#define CCCC_SIGEV_MAX 64
extern volatile sig_atomic_t _cccc_sigev_pending[CCCC_SIGEV_MAX];
extern volatile sig_atomic_t _cccc_sigev_any_pending;
void cccc_sigev_cookie_free(int idx);
/* out_sival_addr is the address of a persistent host-side union sigval
   slot (not the raw 8 bytes) -- guest-to-guest calls pass struct/union
   arguments by reference, see src/stdlib/posix_aio.c's definition. */
int cccc_sigev_cookie_guest_fn(int idx, long long *out_fn,
                               long long *out_sival_addr);
/* Comparison helpers for test attribute assertions (CmpOp). */
static inline const char *cmp_op_str(CmpOp op) {
    switch (op) {
        case CMP_EQ:
            return "=";
        case CMP_NE:
            return "!=";
        case CMP_LT:
            return "<";
        case CMP_LE:
            return "<=";
        case CMP_GT:
            return ">";
        case CMP_GE:
            return ">=";
        default:
            return "?";
    }
}

static inline bool apply_cmp_op_i64(CmpOp op, int64_t a, int64_t b) {
    switch (op) {
        case CMP_EQ:
            return a == b;
        case CMP_NE:
            return a != b;
        case CMP_LT:
            return a < b;
        case CMP_LE:
            return a <= b;
        case CMP_GT:
            return a > b;
        case CMP_GE:
            return a >= b;
        default:
            return false;
    }
}

static inline bool apply_cmp_op_f64(CmpOp op, double a, double b, double eps) {
    switch (op) {
        case CMP_EQ: {
            double d = a - b;
            return d < eps && d > -eps;
        }
        case CMP_NE: {
            double d = a - b;
            return !(d < eps && d > -eps);
        }
        case CMP_LT:
            return a < b;
        case CMP_LE:
            return a <= b;
        case CMP_GT:
            return a > b;
        case CMP_GE:
            return a >= b;
        default:
            return false;
    }
}

/* Async-safe native shim installed for user-registered VM signal handlers */
void _cccc_sig_shim(int sig);
/* Async-safe SA_SIGINFO variant (#745) -- also captures siginfo_t fields for
   cccc_guest_siginfo_for to materialize as a guest-layout siginfo_t later,
   from the dispatch loop rather than from native signal context. */
void _cccc_sig_shim_info(int sig, siginfo_t *info, void *ucontext);
/* Returns (as a guest-visible address) a host-static, guest-layout
   siginfo_t for signal sig, populated either from real captured async data
   (synthesized == 0) or synthesized per raise()'s own POSIX semantics
   (synthesized == 1: si_code = SI_USER, si_pid/si_uid = getpid()/getuid()).
   See src/stdlib/signal.c. */
long long cccc_guest_siginfo_for(int sig, int synthesized);
/* #787: called by both signal-delivery sites (vm.c's dispatch-loop poll and
   op_VRAISE_fn in ops.c) immediately after pushing the handler's return
   address onto the VM stack and before jumping to it. Pushes a SigFrame so
   the dispatch loop can detect the handler's return and restore
   vm->sig_blocked, applies sa_mask/SA_NODEFER to vm->sig_blocked for the
   duration of the handler, and -- if the slot requests SA_RESETHAND --
   resets the disposition to SIG_DFL before returning. Returns the sa_flags
   value as it stood at the moment of delivery (the caller's SA_SIGINFO
   check must use this return value, not slot->sa_flags, since SA_RESETHAND
   may have just zeroed the live slot). `async` must be true from the
   dispatch loop's pending-signal poll (asynchronous, can land mid-
   instruction -- takes a register-file snapshot, #877) and false from
   op_VRAISE_fn (synchronous, runs at a call boundary -- no snapshot
   needed). See src/stdlib/signal.c. */
int cccc_signal_prepare_delivery(VirtualMachine *vm, int sig, SigSlot *slot,
                                 bool async);
/* #877: called by the SIGEV_THREAD poll (vm.c), which has no SigSlot of
   its own, immediately after pushing the handler's return address onto
   the VM stack and before jumping to it -- same register-snapshot
   protection as cccc_signal_prepare_delivery's async path, without the
   sa_mask/SA_RESETHAND machinery a real signal slot carries. Caller must
   check vm->sig_depth < CCCC_SIG_FRAME_MAX first and defer (leave the
   notification pending) rather than call this when full. See
   src/stdlib/signal.c. */
void cccc_signal_push_async_frame(VirtualMachine *vm);
/* #787: called from the dispatch loop, gated on vm->sig_depth != 0. Pops
   any SigFrame(s) whose handler has returned (vm->sp risen back above
   f->sp_at_entry) and restores vm->sig_blocked, so a signal deferred while
   blocked can be redelivered. #877: also restores the register-file
   snapshot taken at async delivery, but only on a genuine handler return
   (not a longjmp() out of the handler). See src/stdlib/signal.c. */
void cccc_signal_poll_handler_returns(VirtualMachine *vm);
void register_stdio_functions(VirtualMachine *vm);
void register_stdlib_functions(VirtualMachine *vm);
void register_string_functions(VirtualMachine *vm);
void register_time_functions(VirtualMachine *vm);
void register_wide_functions(VirtualMachine *vm);
void register_wide_bitint_functions(VirtualMachine *vm);
