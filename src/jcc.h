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
*/

#ifndef JCC_H
#define JCC_H

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <libgen.h>
#include <math.h>
#include <setjmp.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
#else
#include <dlfcn.h>
#include <unistd.h>
#endif

// libffi support for variadic foreign functions
#ifdef JCC_HAS_FFI
#include <ffi.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#define OPS_X                                                                  \
    /* Control flow */                                                         \
    X(JMP)   /* Unconditional jump */                                          \
    X(CALL)  /* Call function (direct) */                                      \
    X(CALLI) /* Call function (indirect via register) */                       \
    X(JMPT)  /* Jump table */                                                  \
    X(JMPI)  /* Indirect jump */                                               \
    /* VM memory operations (self-contained, no system calls) */               \
    X(MALC)                                                                    \
    X(MFRE)                                                                    \
    X(MCPY)                                                                    \
    X(REALC)                                                                   \
    X(CALC)                                                                    \
    /* Type conversion instructions (in-register) */                           \
    X(SX1)   /* Sign extend 1 byte to 8 bytes */                               \
    X(SX2)   /* Sign extend 2 bytes to 8 bytes */                              \
    X(SX4)   /* Sign extend 4 bytes to 8 bytes */                              \
    X(ZX1)   /* Zero extend 1 byte to 8 bytes */                               \
    X(ZX2)   /* Zero extend 2 bytes to 8 bytes */                              \
    X(ZX4)   /* Zero extend 4 bytes to 8 bytes */                              \
    X(CALLF) /* Foreign function interface */                                  \
    /* Memory safety opcodes (keep legacy for instrumentation) */              \
    X(CHKB)  /* Check array bounds */                                          \
    X(CHKI)  /* Check initialization */                                        \
    X(MARKI) /* Mark as initialized */                                         \
    X(MARKA) /* Mark address (track stack pointer for dangling detection) */   \
    X(CHKPA) /* Check pointer arithmetic (invalid arithmetic detection) */     \
    X(MARKP) /* Mark provenance (track pointer origin) */                      \
    /* Stack instrumentation opcodes */                                        \
    X(SCOPEIN)  /* Mark scope entry (allocate/activate variables) */           \
    X(SCOPEOUT) /* Mark scope exit (invalidate variables, detect dangling      \
                   pointers) */                                                \
    X(CHKL)     /* Check variable liveness before access */                    \
    X(MARKR)    /* Mark variable read access */                                \
    X(MARKW)    /* Mark variable write access */                               \
    /* Non-local jump instructions (setjmp/longjmp) */                         \
    X(SETJMP)  /* Save execution context to jmp_buf, return 0 */               \
    X(LONGJMP) /* Restore execution context from jmp_buf, return val */        \
    /* Register-based arithmetic */                                            \
    X(ADD3) /* rd = rs1 + rs2 */                                               \
    X(SUB3) /* rd = rs1 - rs2 */                                               \
    X(MUL3) /* rd = rs1 * rs2 */                                               \
    X(DIV3) /* rd = rs1 / rs2 */                                               \
    X(MOD3) /* rd = rs1 % rs2 */                                               \
    X(AND3) /* rd = rs1 & rs2 */                                               \
    X(OR3)  /* rd = rs1 | rs2 */                                               \
    X(XOR3) /* rd = rs1 ^ rs2 */                                               \
    X(SHL3) /* rd = rs1 << rs2 */                                              \
    X(SHR3) /* rd = rs1 >> rs2 */                                              \
    /* Register-based comparisons */                                           \
    X(SEQ3) /* rd = (rs1 == rs2) */                                            \
    X(SNE3) /* rd = (rs1 != rs2) */                                            \
    X(SLT3) /* rd = (rs1 < rs2) */                                             \
    X(SGE3) /* rd = (rs1 >= rs2) */                                            \
    X(SGT3) /* rd = (rs1 > rs2) */                                             \
    X(SLE3) /* rd = (rs1 <= rs2) */                                            \
    /* Register operations */                                                  \
    X(LI3)   /* rd = immediate */                                              \
    X(LDA3)  /* rd = data_seg + immediate byte offset */                       \
    X(LTA3)  /* rd = text_seg + immediate byte offset */                       \
    X(MOV3)  /* rd = rs1 */                                                    \
    X(NEG3)  /* rd = -rs1 (integer unary negation) */                          \
    X(NOT3)  /* rd = !rs (logical not) */                                      \
    X(BNOT3) /* rd = ~rs (bitwise not) */                                      \
    X(ADDI3) /* rd = rs1 + immediate */                                        \
    X(LEA3)  /* rd = bp + immediate (local variable address) */                \
    /* Register-based control flow */                                          \
    X(JZ3)  /* if (regs[rs] == 0) pc = target */                               \
    X(JNZ3) /* if (regs[rs] != 0) pc = target */                               \
    /* Register-based function frame */                                        \
    X(ENT3) /* Enter function: stack_size|param_count */                       \
    X(LEV3) /* Leave function: return value in REG_A0 */                       \
    X(ADJ)  /* Adjust stack pointer */                                         \
    X(PSH3) /* Push regs[rs] onto stack: *--sp = regs[rs] */                   \
    X(POP3) /* Pop from stack into regs[rd]: rd = *sp++ */                     \
    /* Register-based load/store */                                            \
    X(LDR_B) /* regs[rd] = *(char*)regs[rs] (load byte, sign-extend) */        \
    X(LDR_H) /* regs[rd] = *(short*)regs[rs] (load halfword, sign-extend) */   \
    X(LDR_W) /* regs[rd] = *(int*)regs[rs] (load word, sign-extend) */         \
    X(LDR_D) /* regs[rd] = *(long long*)regs[rs] (load dword) */               \
    X(STR_B) /* *(char*)regs[rs] = regs[rd] (store byte) */                    \
    X(STR_H) /* *(short*)regs[rs] = regs[rd] (store halfword) */               \
    X(STR_W) /* *(int*)regs[rs] = regs[rd] (store word) */                     \
    X(STR_D) /* *(long long*)regs[rs] = regs[rd] (store dword) */              \
    /* Floating-point register operations */                                   \
    X(FLDR)  /* fregs[rd] = *(double*)regs[rs] */                              \
    X(FSTR)  /* *(double*)regs[rs] = fregs[rd] */                              \
    X(FADD3) /* fregs[rd] = fregs[rs1] + fregs[rs2] */                         \
    X(FSUB3) /* fregs[rd] = fregs[rs1] - fregs[rs2] */                         \
    X(FMUL3) /* fregs[rd] = fregs[rs1] * fregs[rs2] */                         \
    X(FDIV3) /* fregs[rd] = fregs[rs1] / fregs[rs2] */                         \
    X(FMOV3) /* fregs[rd] = fregs[rs1] */                                      \
    X(FNEG3) /* fregs[rd] = -fregs[rs1] */                                     \
    X(FEQ3)  /* rd = (fregs[rs1] == fregs[rs2]) */                             \
    X(FNE3)  /* rd = (fregs[rs1] != fregs[rs2]) */                             \
    X(FLT3)  /* rd = (fregs[rs1] < fregs[rs2]) */                              \
    X(FLE3)  /* rd = (fregs[rs1] <= fregs[rs2]) */                             \
    X(FGT3)  /* rd = (fregs[rs1] > fregs[rs2]) */                              \
    X(FGE3)  /* rd = (fregs[rs1] >= fregs[rs2]) */                             \
    X(I2F3)  /* fregs[rd] = (double)regs[rs] */                                \
    X(F2I3)  /* regs[rd] = (long long)fregs[rs] */                             \
    X(FR2R)  /* regs[rd] = *(long long*)&fregs[rs] (bit-pattern transfer) */   \
    X(R2FR)  /* fregs[rd] = *(double*)&regs[rs] (bit-pattern transfer, reverse \
                of FR2R) */                                                    \
    /* Register-based safety opcodes */                                        \
    X(CHKP3) /* Check pointer validity: regs[rs] */                            \
    X(CHKA3) /* Check alignment: regs[rs], immediate alignment */              \
    X(CHKT3) /* Check type: regs[rs], immediate TypeKind */                    \
    /* Struct return buffer support */                                         \
    X(RETBUF) /* Get next return buffer: REG_A0 = rotating pool buffer */       \
    X(FLDR_F32)   /* fregs[rd] = (double)*(float*)regs[rs] */                  \
    X(FSTR_F32)   /* *(float*)regs[rs] = (float)fregs[rd] */                   \
    X(FROUND_F32) /* fregs[rd] = (float)fregs[rs] */

/*!
 @enum JCC_OP
 @abstract VM instruction opcodes for the JCC bytecode.
 @discussion
 The VM is stack-based with an accumulator `ax`. These opcodes are
 emitted by the code generator and interpreted by the VM executor.
*/
typedef enum {
#define X(NAME) NAME,
    OPS_X
#undef X
} JCC_OP;

/*!
 @enum JCCFlags
 @abstract Bitwise flags for JCC runtime features and safety checks.
 @discussion
 These flags control memory safety features, debugging, and runtime behavior.
 Flags can be combined with bitwise OR. Some flags are convenience constants
 that represent multiple underlying flags.
*/
typedef enum {
    // Memory safety flags (bits 0-19)
    JCC_BOUNDS_CHECKS = (1 << 0), // 0x00000001 - Array bounds checking
    JCC_UAF_DETECTION = (1 << 1), // 0x00000002 - Use-after-free detection
    JCC_TYPE_CHECKS = (1 << 2),   // 0x00000004 - Runtime type checking
    JCC_UNINIT_DETECTION =
        (1 << 3), // 0x00000008 - Uninitialized variable detection
    JCC_OVERFLOW_CHECKS = (1 << 4), // 0x00000010 - Integer overflow detection
    JCC_STACK_CANARIES = (1 << 5),  // 0x00000020 - Stack canary protection
    JCC_HEAP_CANARIES = (1 << 6),   // 0x00000040 - Heap canary protection
    JCC_MEMORY_LEAK_DETECT = (1 << 7), // 0x00000080 - Memory leak detection
    JCC_STACK_INSTR = (1 << 8), // 0x00000100 - Stack variable instrumentation
    JCC_DANGLING_DETECT = (1 << 9),   // 0x00000200 - Dangling pointer detection
    JCC_ALIGNMENT_CHECKS = (1 << 10), // 0x00000400 - Pointer alignment checking
    JCC_PROVENANCE_TRACK =
        (1 << 11), // 0x00000800 - Pointer provenance tracking
    JCC_INVALID_ARITH =
        (1 << 12), // 0x00001000 - Invalid pointer arithmetic detection
    JCC_FORMAT_STR_CHECKS = (1 << 13), // 0x00002000 - Format string validation
    JCC_RANDOM_CANARIES = (1 << 14),   // 0x00004000 - Random canary values
    JCC_MEMORY_POISONING =
        (1 << 15), // 0x00008000 - Poison allocated/freed memory
    JCC_MEMORY_TAGGING = (1 << 16), // 0x00010000 - Temporal memory tagging
    JCC_VM_HEAP = (1 << 17),        // 0x00020000 - Force VM-managed heap
    JCC_CFI = (1 << 18),            // 0x00040000 - Control flow integrity
    JCC_STACK_INSTR_ERRORS =
        (1 << 19), // 0x00080000 - Stack instrumentation errors
    JCC_ENABLE_DEBUGGER = (1 << 20), // 0x00100000 - Interactive debugger

    // Convenience flag combinations
    JCC_POINTER_SANITIZER =
        (JCC_BOUNDS_CHECKS | JCC_UAF_DETECTION | JCC_TYPE_CHECKS),
    JCC_ALL_SAFETY = 0x000FFFFF, // All safety features (bits 0-19)

    // Preset safety levels (use with -S0/-S1/-S2/-S3 or
    // --safety=none/basic/standard/max)
    JCC_SAFETY_BASIC =
        (JCC_STACK_CANARIES | JCC_HEAP_CANARIES | JCC_MEMORY_LEAK_DETECT |
         JCC_OVERFLOW_CHECKS | JCC_FORMAT_STR_CHECKS | JCC_VM_HEAP),
    JCC_SAFETY_STANDARD =
        (JCC_POINTER_SANITIZER | JCC_STACK_CANARIES | JCC_HEAP_CANARIES |
         JCC_MEMORY_LEAK_DETECT | JCC_OVERFLOW_CHECKS | JCC_UNINIT_DETECTION |
         JCC_FORMAT_STR_CHECKS | JCC_MEMORY_POISONING | JCC_VM_HEAP),
    JCC_SAFETY_MAX =
        (JCC_ALL_SAFETY | JCC_RANDOM_CANARIES | JCC_STACK_INSTR_ERRORS),

    // VM heap is auto-enabled when any of these flags are set
    JCC_VM_HEAP_TRIGGERS =
        (JCC_VM_HEAP | JCC_HEAP_CANARIES | JCC_MEMORY_LEAK_DETECT |
         JCC_UAF_DETECTION | JCC_POINTER_SANITIZER | JCC_BOUNDS_CHECKS |
         JCC_MEMORY_TAGGING),

    // Pointer validity checks
    JCC_POINTER_CHECKS = (JCC_UAF_DETECTION | JCC_BOUNDS_CHECKS |
                          JCC_DANGLING_DETECT | JCC_MEMORY_TAGGING),
} JCCFlags;

/*!
 @struct HashEntry
 @abstract Simple key/value bucket used by the project's HashMap.
 @field key Null-terminated string key.
 @field keylen Length of the key in bytes.
 @field val Pointer to the stored value.
*/
typedef struct HashEntry {
    char *key;
    int keylen;
    void *val;
} HashEntry;

/*!
 @struct HashMap
 @abstract Lightweight open-addressing hashmap used for symbol tables,
           macros, and other small maps.
 @field buckets Pointer to an array of HashEntry buckets.
 @field capacity Number of buckets allocated.
 @field used Number of occupied buckets, including tombstones.
*/
typedef struct HashMap {
    HashEntry *buckets;
    int capacity;
    int used;
} HashMap;

/*!
 @struct File
 @abstract Represents the contents and metadata of a source file.
 @field name Original filename string.
 @field file_no Unique numeric id for the file.
 @field contents File contents as a NUL-terminated buffer.
 @field display_name Optional name emitted by a `#line` directive.
 @field line_delta Line number delta applied for `#line` handling.
*/
typedef struct File {
    char *name;
    int file_no;
    char *contents;

    // For #line directive
    char *display_name;
    int line_delta;
} File;

/*!
 @struct Relocation
 @abstract A relocation record for a global variable initializer that
           references another global symbol.
 @discussion
 Each relocation describes a pointer-sized slot within a global's
 initializer data that must be patched with the address of another
 global (or label) when codegen finalizes the data segment.
*/
typedef struct Relocation {
    struct Relocation *next;
    int offset;
    char **label;
    long addend;
} Relocation;

/*!
 @struct StringArray
 @abstract Dynamic array of strings used for include paths and similar
           small lists.
 @field data Pointer to N entries of char* strings.
 @field capacity Allocated capacity of the array.
 @field len Current length (number of strings stored).
*/
typedef struct StringArray {
    char **data;
    int capacity;
    int len;
} StringArray;

/*!
 @struct EnumConstant
 @abstract Represents an enumerator constant within an enum type.
 @field name Name of the enumerator.
 @field value Integer value of the enumerator.
 @field next Pointer to the next enumerator in the linked list.
*/
typedef struct EnumConstant {
    char *name;
    int value;
    struct EnumConstant *next;
} EnumConstant;

/*!
 @struct Hideset
 @abstract Represents a set of macro names that have been hidden to
           prevent recursive macro expansion.
*/
typedef struct Hideset {
    struct Hideset *next;
    char *name;
} Hideset;

/*!
 @enum TokenKind
 @abstract Kinds of lexical tokens produced by the tokenizer and
           used by the preprocessor and parser.
*/
typedef enum {
    TK_IDENT,   // Identifiers
    TK_PUNCT,   // Punctuators
    TK_KEYWORD, // Keywords
    TK_STR,     // String literals
    TK_NUM,     // Numeric literals
    TK_PP_NUM,  // Preprocessing numbers
    TK_EOF,     // End-of-file markers
} TokenKind;

typedef struct Type Type;

/*!
 @struct Token
 @abstract Token produced by the lexer or by macro expansion.
 @field kind Token kind (see TokenKind).
 @field loc Pointer into the source buffer where the token text begins.
 @field len Number of characters in the token text.
 @field str For string literals: pointer to the unescaped contents.
*/
typedef struct Token {
    TokenKind kind;     // Token kind
    struct Token *next; // Next token
    int64_t val;        // If kind is TK_NUM, its value
    long double fval;   // If kind is TK_NUM, its value
    char *loc;          // Token location
    int len;            // Token length
    Type *ty;           // Used if TK_NUM or TK_STR
    char *str;          // String literal contents including terminating '\0'

    File *file;       // Source location
    char *filename;   // Filename
    int line_no;      // Line number
    int col_no;       // Column number (1-based)
    int line_delta;   // Line number
    bool at_bol;      // True if this token is at beginning of line
    bool has_space;   // True if this token follows a space character
    Hideset *hideset; // For macro expansion
    struct Token
        *origin; // If this is expanded from a macro, the original token
} Token;

/*!
 @enum TypeKind
 @abstract Kind tag for the `Type` structure describing C types.
*/
typedef enum {
    TY_VOID = 0,
    TY_BOOL = 1,
    TY_CHAR = 2,
    TY_SHORT = 3,
    TY_INT = 4,
    TY_LONG = 5,
    TY_FLOAT = 6,
    TY_DOUBLE = 7,
    TY_LDOUBLE = 8,
    TY_ENUM = 9,
    TY_PTR = 10,
    TY_FUNC = 11,
    TY_ARRAY = 12,
    TY_VLA = 13, // variable-length array
    TY_STRUCT = 14,
    TY_UNION = 15,
    TY_ERROR = 16, // error type for recovery
    TY_BLOCK = 17, // Apple blocks (closures)
} TypeKind;

typedef struct Node Node;
typedef struct Obj Obj;

/*!
 @struct Type
 @abstract Central representation of a C type in the compiler.
 @field kind One of TypeKind indicating the category of this type.
 @field size sizeof() value in bytes.
 @field align Alignment requirement in bytes.
 @field base For pointer/array types: the referenced element/base type.
*/
struct Type {
    TypeKind kind;
    int size;            // sizeof() value
    int align;           // alignment
    bool is_unsigned;    // unsigned or signed
    bool is_atomic;      // true if _Atomic
    bool is_const;       // true if const-qualified
    bool is_volatile;    // true if volatile-qualified
    struct Type *origin; // for type compatibility check

    // Pointer-to or array-of type. We intentionally use the same member
    // to represent pointer/array duality in C.
    //
    // In many contexts in which a pointer is expected, we examine this
    // member instead of "kind" member to determine whether a type is a
    // pointer or not. That means in many contexts "array of T" is
    // naturally handled as if it were "pointer to T", as required by
    // the C spec.
    struct Type *base;

    // Declaration
    Token *name;
    Token *name_pos;

    // Array
    int array_len;

    // Variable-length array
    Node *vla_len; // # of elements
    Obj *vla_size; // sizeof() value

    // Struct
    struct Member *members;
    bool is_flexible;
    bool is_packed;

    // Enum (tracks enum constants for code generation)
    EnumConstant *enum_constants;

    // Function type
    struct Type *return_ty;
    struct Type *params;
    bool is_variadic;
    struct Type *next;
};

/*!
 @struct Member
 @abstract Member (field) descriptor for struct and union types.
 @field ty Type of the member.
 @field name Token pointing to the member identifier.
 @field offset Byte offset of the member within its aggregate.
*/
typedef struct Member {
    struct Member *next;
    Type *ty;
    Token *tok; // for error message
    Token *name;
    int idx;
    int align;
    int offset;

    // Bitfield
    bool is_bitfield;
    int bit_offset;
    int bit_width;
} Member;

/*!
 @enum NodeKind
 @abstract Kinds of AST nodes produced by the parser.
*/
typedef enum {
    ND_NULL_EXPR = 0,      // Do nothing
    ND_ADD = 1,            // +
    ND_SUB = 2,            // -
    ND_MUL = 3,            // *
    ND_DIV = 4,            // /
    ND_NEG = 5,            // unary -
    ND_MOD = 6,            // %
    ND_BITAND = 7,         // &
    ND_BITOR = 8,          // |
    ND_BITXOR = 9,         // ^
    ND_SHL = 10,           // <<
    ND_SHR = 11,           // >>
    ND_EQ = 12,            // ==
    ND_NE = 13,            // !=
    ND_LT = 14,            // <
    ND_LE = 15,            // <=
    ND_ASSIGN = 16,        // =
    ND_COND = 17,          // ?:
    ND_COMMA = 18,         // ,
    ND_MEMBER = 19,        // . (struct member access)
    ND_ADDR = 20,          // unary &
    ND_DEREF = 21,         // unary *
    ND_NOT = 22,           // !
    ND_BITNOT = 23,        // ~
    ND_LOGAND = 24,        // &&
    ND_LOGOR = 25,         // ||
    ND_RETURN = 26,        // "return"
    ND_IF = 27,            // "if"
    ND_FOR = 28,           // "for" or "while"
    ND_DO = 29,            // "do"
    ND_SWITCH = 30,        // "switch"
    ND_CASE = 31,          // "case"
    ND_BLOCK = 32,         // { ... }
    ND_GOTO = 33,          // "goto"
    ND_GOTO_EXPR = 34,     // "goto" labels-as-values
    ND_LABEL = 35,         // Labeled statement
    ND_LABEL_VAL = 36,     // [GNU] Labels-as-values
    ND_FUNCALL = 37,       // Function call
    ND_EXPR_STMT = 38,     // Expression statement
    ND_STMT_EXPR = 39,     // Statement expression
    ND_VAR = 40,           // Variable
    ND_VLA_PTR = 41,       // VLA designator
    ND_NUM = 42,           // Integer
    ND_CAST = 43,          // Type cast
    ND_MEMZERO = 44,       // Zero-clear a stack variable
    ND_ASM = 45,           // "asm"
    ND_CAS = 46,           // Atomic compare-and-swap
    ND_EXCH = 47,          // Atomic exchange
    ND_FRAME_ADDR = 48,    // __builtin_frame_address(0) - returns base pointer
    ND_BLOCK_LITERAL = 49, // Block literal ^{ ... }
    ND_BLOCK_CALL = 50,    // Block invocation
    ND_MACRO_CALL = 51, // Pragma macro invocation (deferred until macro pass)
} NodeKind;

/*!
 @struct Node
 @abstract Represents a node in the parser's abstract syntax tree.
 @field kind Node kind (see NodeKind).
 @field ty Resolved type of this node (after semantic analysis).
 @field lhs Left-hand side child (when applicable).
 @field rhs Right-hand side child (when applicable).
*/
struct Node {
    NodeKind kind;     // Node kind
    struct Node *next; // Next node
    Type *ty;          // Type, e.g. int or pointer to int
    Token *tok;        // Representative token

    struct Node *lhs; // Left-hand side
    struct Node *rhs; // Right-hand side

    // "if" or "for" statement
    struct Node *cond;
    struct Node *then;
    struct Node *els;
    struct Node *init;
    struct Node *inc;

    // "break" and "continue" labels
    char *brk_label;
    char *cont_label;

    // Block or statement expression
    struct Node *body;

    // Struct member access
    Member *member;

    // Function call
    Type *func_ty;
    struct Node *args;
    bool pass_by_stack;
    Obj *ret_buffer;

    // Goto or labeled statement, or labels-as-values
    char *label;
    char *unique_label;
    struct Node *goto_next;

    // Switch
    struct Node *case_next;
    struct Node *default_case;

    // Case
    long begin;
    long end;

    // "asm" string literal
    char *asm_str;

    // Atomic compare-and-swap
    struct Node *cas_addr;
    struct Node *cas_old;
    struct Node *cas_new;

    // Atomic op= operators
    Obj *atomic_addr;
    struct Node *atomic_expr;

    // Variable
    Obj *var;

    // Numeric literal
    int64_t val;
    long double fval;

    // Block literal (Apple blocks extension)
    Obj *block_fn;          // Synthetic function for block's body
    Obj **block_captures;   // Array of captured variables
    int num_block_captures; // Number of captured variables

    // Pragma macro call (ND_MACRO_CALL)
    char *macro_name;    // Name of pragma macro to invoke
    int macro_arg_count; // Number of arguments
};

/*!
 @struct Obj
 @abstract Represents a C object: either a variable (global/local) or a
           function. The parser and code generator use Obj for symbol
           and storage tracking.
 @field name Identifier name of the object.
 @field ty Type of the object.
 @field is_local True for local (stack) variables; false for globals.
 @field offset For local variables: stack offset. For globals/functions
               some fields (like code_addr) are used instead.
 @field is_function True when this Obj represents a function.
 @field code_addr For functions compiled to VM bytecode: start address
                 in the text segment.
*/
struct Obj {
    struct Obj *next;
    char *name;    // Variable name
    Type *ty;      // Type
    Token *tok;    // representative token
    bool is_local; // local or global/function
    int align;     // alignment

    // Local variable
    int offset;
    bool is_param;    // true if this is a function parameter
    bool is_captured; // true if accessed by a nested function (for optimization
                      // hints)

    // Global variable or function
    bool is_function;
    bool is_definition;
    bool is_static;
    bool is_constexpr;

    // Global variable
    bool is_tentative;
    bool is_tls;
    char *init_data;
    Relocation *rel;
    Node *init_expr; // For constexpr: AST of initializer expression

    // Function
    bool is_inline;
    Obj *params;
    Node *body;
    Obj *locals;
    Obj *va_area;
    Obj *alloca_bottom;
    int stack_size;

    // Nested function support (GNU C extension)
    struct Obj *parent_fn; // Enclosing function (NULL if top-level)
    bool is_nested;        // True if defined inside another function
    int nesting_depth;     // 0 = top-level, 1 = one level deep, etc.

    // Block support (Apple blocks extension)
    bool is_block;            // True if this is a block's synthetic function
    Obj **captures;           // Array of captured outer variables
    int num_captures;         // Number of captured variables
    int block_capture_offset; // For captured vars: offset in block descriptor
    bool is_block_var;        // True if declared with __block storage qualifier

    // Static inline function
    bool is_live;
    bool is_root;
    StringArray refs;

    // Code generation (for VM)
    long long code_addr; // Address in text segment where function code starts
    long long code_end_addr; // Address after function code in text segment
};

/*!
 @struct CondIncl
 @abstract Stack entry used to track nested #if/#elif/#else processing
           during preprocessing.
*/
typedef struct CondIncl {
    struct CondIncl *next;
    enum { IN_THEN, IN_ELIF, IN_ELSE } ctx;
    Token *tok;
    bool included;
} CondIncl;

/*!
 @struct PragmaMacro
 @abstract Represents a compile-time pragma macro function.
 @discussion Pragma macros are functions marked with #pragma macro that
             execute during compilation to generate or transform AST nodes.
 @field name Function name.
 @field body_tokens Original token stream for function body (from preprocessor).
 @field compiled_fn Compiled function object (NULL until compiled).
 @field is_compiled True after successful compilation.
 @field is_macro_entry True if callable from user program macro call sites.
 @field next Pointer to next macro in linked list.
*/
typedef struct PragmaMacro {
    char *name;               // Function name
    Token *body_tokens;       // Original token stream for function body
    Obj *compiled_fn;         // Compiled function object
    bool is_compiled;         // True after successful compilation
    bool is_macro_entry;      // True for #pragma macro, false for comptime helper
    struct PragmaMacro *next; // Next macro in linked list
} PragmaMacro;

/*!
 @struct Scope
 @abstract Represents a parser block scope. Two kinds of block scopes are
           used: one for variables/typedefs and another for tags.
*/
/*!
 @struct VarScopeNode
 @abstract Linked list node for variable/typedef scope entries.
 @field var Pointer to variable object (if variable).
 @field type_def Pointer to typedef type (if typedef).
 @field enum_ty Pointer to enum type (if enum constant).
 @field enum_val Enum constant value.
 @field name Variable or typedef name.
 @field name_len Length of name.
 @field next Pointer to next node in list.

 @discussion The first 4 fields match VarScope layout for safe casting.
*/
typedef struct VarScopeNode {
    // VarScope fields (must come first for casting)
    Obj *var;
    Type *type_def;
    Type *enum_ty;
    int enum_val;
    // Additional fields for linked list
    char *name;
    int name_len;
    struct VarScopeNode *next;
} VarScopeNode;

/*!
 @struct TagScopeNode
 @abstract Linked list node for struct/union/enum tag scope entries.
 @field name Tag name.
 @field name_len Length of name.
 @field ty Pointer to tagged type.
 @field next Pointer to next node in list.
*/
typedef struct TagScopeNode {
    char *name;
    int name_len;
    Type *ty;
    struct TagScopeNode *next;
} TagScopeNode;

typedef struct Scope {
    struct Scope *next;
    // C has two block scopes; one is for variables/typedefs and
    // the other is for struct/union/enum tags.
    VarScopeNode *vars; // Linked list of variables/typedefs (not HashMap)
    TagScopeNode *tags; // Linked list of tags (not HashMap)
} Scope;

/*!
 @struct LabelEntry
 @abstract Tracks labels used for goto and labeled statements and their
           defined addresses in the generated text segment.
*/
typedef struct LabelEntry {
    char *name;         // Label name (for named labels)
    char *unique_label; // Unique label identifier (for break/continue)
    long long *address; // Address in text segment where label is defined
} LabelEntry;

/*!
 @struct GotoPatch
 @abstract Records a forward jump (JMP) that must be patched once the
           destination label is defined.
*/
typedef struct GotoPatch {
    char *name;          // Label name to jump to
    char *unique_label;  // Or unique label identifier
    long long *location; // Location of JMP instruction's address operand
} GotoPatch;

typedef struct JCC JCC;

/*!
 @typedef JCCAsmCallback
 @abstract Callback invoked when an `asm("...")` statement is encountered
           during code generation.
 @param vm The VM/compiler instance.
 @param asm_str The asm string literal content.
 @param user_data User-provided context pointer (set via cc_set_asm_callback).
 @discussion The callback may emit custom bytecode into the VM's text
             segment, perform logging, or otherwise handle the asm string.
*/
typedef void (*JCCAsmCallback)(JCC *vm, const char *asm_str, void *user_data);

/*!
 @struct ForeignFunc
 @abstract Represents a registered foreign (native C) function callable from VM
 code.
 @field name Function name (for lookup during compilation).
 @field func_ptr Pointer to the native C function.
 @field num_args Number of arguments the function expects (total for
 non-variadic, fixed args for variadic).
 @field returns_double True if function returns double, false if returns long
 long.
 @field is_variadic True if function is variadic (accepts ... arguments).
 @field num_fixed_args For variadic functions: number of fixed args before ...
 (e.g., printf has 1: format string).
 @field cif libffi call interface (only when JCC_HAS_FFI is defined).
 @field arg_types Array of argument types for libffi (NULL if not prepared, only
 when JCC_HAS_FFI is defined).
*/
typedef struct ForeignFunc {
    char *name;
    void *func_ptr;
    int num_args;
    int returns_double;
    int is_variadic;    // 1 if function is variadic (e.g., printf), 0 otherwise
    int num_fixed_args; // For variadic functions, number of fixed args (rest
                        // are variable)
    uint64_t double_arg_mask; // Bitmask indicating which args are doubles (bit
                              // N = arg N)
#ifdef JCC_HAS_FFI
    ffi_cif cif;          // libffi call interface
    ffi_type **arg_types; // Array of argument types (NULL if not prepared)
#endif
} ForeignFunc;

/*!
 @struct AllocHeader
 @abstract Metadata header stored before each heap allocation for tracking.
 @field size Size of allocation (excluding header), rounded up for alignment
 @field requested_size Original requested size (for bounds checking)
 @field magic Magic number (0xDEADBEEF) for detecting corruption.
 @field canary Front canary value for heap overflow detection (when enabled)
 @field freed Flag indicating if this block has been freed (for UAF detection)
 @field generation Generation counter incremented on each free (for UAF
 detection)
 @field alloc_pc Program counter at allocation site (for debugging)
 @field type_kind Type of allocation (for type checking on dereference)
*/
typedef struct AllocHeader {
    size_t size;             // Allocated size (rounded, excluding header)
    size_t requested_size;   // Original requested size (for bounds checking)
    int magic;               // Magic number for debugging (0xDEADBEEF)
    long long canary;        // Front canary (if heap canaries enabled)
    int freed;               // 1 if freed (for UAF detection)
    int generation;          // Generation counter (incremented on free)
    int creation_generation; // Generation when pointer was created (for
                             // temporal safety)
    long long alloc_pc;      // PC at allocation site (for leak detection)
    int type_kind; // Type of allocation (TypeKind enum, for type checking)
} AllocHeader;

/*!
 @struct FreeBlock
 @abstract Free list node for tracking freed memory blocks.
 @field next Pointer to next free block in the list.
 @field size Size of this free block (excluding header).
*/
typedef struct FreeBlock {
    struct FreeBlock *next;
    size_t size;
} FreeBlock;

/*!
 @struct AllocRecord
 @abstract Tracks an active heap allocation for leak detection.
 @field next Pointer to next record in the list.
 @field address Address of the allocated memory (user pointer).
 @field size Size of the allocation in bytes.
 @field alloc_pc Program counter at allocation site.
*/
typedef struct AllocRecord {
    struct AllocRecord *next;
    void *address;
    size_t size;
    long long alloc_pc;
} AllocRecord;

/*!
 @struct StackPtrInfo
 @abstract Tracks stack pointer information for dangling pointer detection.
 @field bp Base pointer value when pointer was created.
 @field offset Stack offset from BP.
 @field size Size of the pointed-to object.
 @field scope_id Unique identifier for the scope where variable was declared.
*/
typedef struct StackPtrInfo {
    long long bp;
    long long offset;
    size_t size;
    int scope_id;
} StackPtrInfo;

/*!
 @struct StackVarMeta
 @abstract Unified metadata for stack variable instrumentation.
 @field name Variable name (for debugging/reporting).
 @field bp Base pointer value when variable is active.
 @field offset Offset from BP (negative for locals, positive for params).
 @field ty Type information for the variable.
 @field scope_id Unique identifier for the scope where variable was declared.
 @field is_alive 1 if variable is in scope, 0 if out of scope.
 @field initialized 1 if variable has been initialized, 0 if uninitialized.
 @field read_count Number of read accesses to this variable.
 @field write_count Number of write accesses to this variable.
*/
typedef struct StackVarMeta {
    char *name;
    long long bp;
    long long offset;
    Type *ty;
    int scope_id;
    int is_alive;
    int initialized;
    long long read_count;
    long long write_count;
} StackVarMeta;

/*!
 @struct ScopeVarNode
 @abstract Linked list node for tracking variables within a scope.
 @field meta Pointer to the variable's metadata.
 @field next Pointer to the next variable in the scope.
 */
typedef struct ScopeVarNode {
    StackVarMeta *meta;
    struct ScopeVarNode *next;
} ScopeVarNode;

/*!
 @struct ScopeVarList
 @abstract Linked list of variables belonging to a specific scope.
 @field head Head of the linked list.
 @field tail Tail for efficient O(1) append operations.
 */
typedef struct ScopeVarList {
    ScopeVarNode *head;
    ScopeVarNode *tail;
} ScopeVarList;

/*!
 @struct ProvenanceInfo
 @abstract Tracks pointer provenance (origin) for validation.
 @field origin_type Type of origin: 0=HEAP, 1=STACK, 2=GLOBAL.
 @field base Base address of the original object.
 @field size Size of the original object.
*/
typedef struct ProvenanceInfo {
    int origin_type; // 0=HEAP, 1=STACK, 2=GLOBAL
    long long base;
    size_t size;
} ProvenanceInfo;

/*!
 @struct SourceMap
 @abstract Maps bytecode offsets to source file locations for debugger support.
 @field pc_offset Offset in text segment (bytecode) where this mapping applies.
 @field file Source file containing this code.
 @field line_no Line number in source file.
*/
typedef struct SourceMap {
    long long pc_offset; // Offset in text segment
    File *file;          // Source file
    int line_no;         // Line number in source
    int col_no;          // Column number (1-based)
    int end_col_no;      // End column number (1-based)
} SourceMap;

/*!
 @struct DebugSymbol
 @abstract Represents a variable's debug information for expression evaluation.
 @field name Variable name (for lookup).
 @field offset Offset from BP (negative for locals) or address in data segment
 (globals).
 @field ty Type of the variable.
 @field is_local True if local variable (BP-relative), false if global.
 @field scope_depth Scope depth (for handling shadowing).
*/
typedef struct DebugSymbol {
    char *name;       // Variable name
    long long offset; // BP offset (locals) or data segment address (globals)
    Type *ty;         // Variable type
    int is_local;     // 1=local (BP-relative), 0=global (data segment)
    int scope_depth;  // Scope depth for shadowing resolution
    Obj *owner_fn;    // Owning function for locals, NULL for globals
} DebugSymbol;

typedef struct TypeNameRecord {
    Type *ty;
    char *name;
    int name_len;
    Obj *owner_fn;
    bool is_tag;
    struct TypeNameRecord *next;
} TypeNameRecord;

/*!
 @struct Watchpoint
 @abstract Represents a data breakpoint that triggers on memory access.
 @field address Memory address being watched.
 @field size Size of watched region in bytes.
 @field type Type flags: WATCH_READ | WATCH_WRITE | WATCH_CHANGE.
 @field old_value Last known value (for change detection).
 @field expr Original expression string (for display).
 @field enabled Whether this watchpoint is currently active.
 @field hit_count Number of times this watchpoint has been triggered.
*/
#ifndef MAX_WATCHPOINTS
#define MAX_WATCHPOINTS 64
#endif

// Watchpoint type flags
#define WATCH_READ (1 << 0)   // Trigger on reads
#define WATCH_WRITE (1 << 1)  // Trigger on writes
#define WATCH_CHANGE (1 << 2) // Only trigger if value actually changes

typedef struct Watchpoint {
    void *address;       // Address being watched
    int size;            // Size in bytes
    int type;            // WATCH_READ | WATCH_WRITE | WATCH_CHANGE
    long long old_value; // Last value (for change detection)
    char *expr;          // Original expression (for display)
    int enabled;         // 1 if enabled, 0 if disabled
    int hit_count;       // Number of times triggered
} Watchpoint;

/*!
 @struct Breakpoint
 @abstract Represents a debugger breakpoint at a specific program counter
 location.
 @field pc Program counter address where the breakpoint is set.
 @field enabled Whether this breakpoint is currently active.
 @field hit_count Number of times this breakpoint has been hit.
*/
#ifndef MAX_BREAKPOINTS
#define MAX_BREAKPOINTS 256
#endif

typedef struct Breakpoint {
    long long *pc;   // PC address of breakpoint
    int enabled;     // 1 if enabled, 0 if disabled
    int hit_count;   // Number of times hit
    char *condition; // Optional condition expression (NULL if unconditional)
} Breakpoint;

/*!
 @struct CompileError
 @abstract Represents a compilation error or warning collected during
 compilation.
 @field next Pointer to the next error in the linked list.
 @field message Formatted error message string.
 @field filename Source file name where the error occurred.
 @field line_no Line number in the source file.
 @field col_no Column number in the source file.
 @field severity 0 for error, 1 for warning.
*/
typedef struct CompileError {
    struct CompileError *next; // Next error in linked list
    char *message;             // Formatted error message
    char *filename;            // Source file name
    int line_no;               // Line number
    int col_no;                // Column number
    int severity;              // 0 = error, 1 = warning
} CompileError;

/*!
 @struct ArenaBlock
 @abstract Represents a single memory block in an arena allocator.
 @field base Pointer to the start of the memory block (from mmap/VirtualAlloc).
 @field ptr Current allocation pointer (bump pointer).
 @field size Total size of this block in bytes.
 @field next Pointer to the next block in the chain.
*/
typedef struct ArenaBlock {
    char *base;              // Start of memory block
    char *ptr;               // Current allocation pointer (bump pointer)
    size_t size;             // Total block size
    struct ArenaBlock *next; // Next block in chain
} ArenaBlock;

/*!
 @struct Arena
 @abstract Arena allocator for fast, bulk memory allocation with single
 deallocation.
 @field current Currently active block for allocations.
 @field blocks Linked list of all allocated blocks (for cleanup).
 @field default_block_size Default size for new blocks (typically 1MB).
 @discussion Arena allocators use bump-pointer allocation within large memory
             blocks (allocated via mmap/VirtualAlloc). All allocations are
             freed together when the arena is destroyed. Used for parser
             frontend (tokens, AST nodes) which have fire-and-forget lifetime.
*/
typedef struct Arena {
    ArenaBlock *current;       // Current block for allocations
    ArenaBlock *blocks;        // All blocks (for cleanup)
    size_t default_block_size; // Default block size (1MB)
} Arena;

/*!
 @struct Debugger
 @abstract Encapsulates all debugger state for the JCC VM.
 @discussion Contains breakpoints, stepping control, source mapping,
             debug symbols, and watchpoints. Enabled via JCC_ENABLE_DEBUGGER
 flag.
*/
#ifndef MAX_DEBUG_SYMBOLS
#define MAX_DEBUG_SYMBOLS 4096
#endif
typedef struct Debugger {
    // Breakpoints
    Breakpoint breakpoints[MAX_BREAKPOINTS];
    int num_breakpoints;

    // Stepping control
    int single_step; // Single-step mode (stop after each instruction)
    int step_over;   // Step over mode (skip function calls)
    int step_out;    // Step out mode (run until function returns)
    long long *step_over_return_addr; // Return address for step over
    long long *step_out_bp;           // Base pointer for step out
    int debugger_attached;            // Debugger REPL is active

    // Source mapping (bytecode ↔ source lines)
    SourceMap *source_map;   // Array of PC to source location mappings
    int source_map_count;    // Number of source map entries
    int source_map_capacity; // Allocated capacity
    File *last_debug_file;   // Last file during debug info emission
    int last_debug_line;     // Last line number during debug info emission
    int last_debug_col;      // Last column number during debug info emission

    // Debug symbols for expression evaluation
    DebugSymbol debug_symbols[MAX_DEBUG_SYMBOLS];
    int num_debug_symbols;

    // Watchpoints (data breakpoints)
    Watchpoint watchpoints[MAX_WATCHPOINTS];
    int num_watchpoints;
} Debugger;

#ifndef MAX_CALLS
#define MAX_CALLS 1024
#endif

#ifndef MAX_LABELS
#define MAX_LABELS 256
#endif

#ifndef MAX_SPARSE_CASES
#define MAX_SPARSE_CASES 256
#endif

#define RETURN_BUFFER_POOL_SIZE 8

/*!
 @struct Compiler
 @abstract Encapsulates all compiler frontend state: preprocessor, parser, and
 code generator.
 @discussion Contains state for preprocessing (#include, #define, #if), parsing
 (AST construction, scope management), and code generation (labels, patches,
 FFI). Separated from VM runtime state to clarify the compilation/execution
 boundary.
*/
typedef struct Compiler {
    // Preprocessor state
    bool skip_preprocess;     // Skip preprocessing step
    HashMap macros;           // Macro definitions
    CondIncl *cond_incl;      // Conditional inclusion stack
    HashMap pragma_once;      // #pragma once tracking
    HashMap included_headers; // Track included headers for lazy stdlib loading
    HashMap include_guards;   // Header include guard cache
    int include_next_idx;     // Index for #include_next

    // Pragma macro state
    PragmaMacro *pragma_macros; // Linked list of captured pragma macros
    bool in_macro_mode;         // True when compiling/executing a pragma macro
    bool in_macro_expansion;    // True during macro AST expansion pass
    Obj *macro_globals; // Globals defined by pragma macros (separate from main
                        // program)

    // #embed directive limits
    size_t embed_limit;      // Soft limit for #embed size (default: 10MB)
    size_t embed_hard_limit; // Secondary warning threshold (default: 50MB)
    bool embed_hard_error;   // If true, exceeding limit is a hard error

    // Tokenization state
    File *current_file; // Input file
    File **input_files; // A list of all input files
    bool at_bol;        // True if at beginning of line
    bool has_space;     // True if follows a space character

    // Parser state
    Obj *locals;           // All local variable instances during parsing
    Obj *globals;          // Global variables accumulated list
    Scope *scope;          // Current scope
    Obj *initializing_var; // Variable being initialized (for const
                           // initialization)
    Obj *current_fn;       // Function being parsed
    int fn_nesting_depth;  // Current function nesting depth (0 = top-level)
    Node *gotos;           // Goto statements in current function
    Node *labels;          // Labels in current function
    char *brk_label;       // Current break jump target
    char *cont_label;      // Current continue jump target
    Node *current_switch;  // Switch statement being parsed (NULL if none)
    Obj *builtin_alloca;   // Builtin alloca function
    Obj *builtin_setjmp;   // Builtin setjmp function
    Obj *builtin_longjmp;  // Builtin longjmp function
    TypeNameRecord *type_names; // Persistent typedef/tag declarations for -M

    // Arena allocator for parser frontend (tokens, AST, preprocessor state)
    Arena parser_arena; // Fast bump-pointer allocator

    StringArray include_paths;        // Quote include search paths
    StringArray system_include_paths; // System header search paths for <...>
    HashMap include_cache;            // Cache for search_include_paths
    StringArray file_buffers; // Track allocated file buffers for cleanup

    // URL include cache (only used when JCC_HAS_CURL is enabled)
    char *url_cache_dir; // Directory for caching downloaded headers
    HashMap url_to_path; // Maps URLs to cached file paths

    // Code generation state
    int label_counter; // For generating unique labels
    int local_offset;  // Current local variable offset

    struct {
        long long *location; // Location in text segment to patch
        Obj *function;       // Function to call
    } call_patches[MAX_CALLS];
    int num_call_patches;

    // Function address patches for function pointers
    struct {
        long long *location; // Location of IMM operand to patch
        Obj *function;       // Function whose address to use
    } func_addr_patches[MAX_CALLS];
    int num_func_addr_patches;

    struct {
        long long data_offset;   // Pointer slot offset in data segment
        long long target_offset; // Target offset in text/data segment
        long long addend;        // Byte addend applied to target
        int target_segment;      // 0 = data, 1 = text
    } data_relocs[MAX_CALLS];
    int num_data_relocs;

    LabelEntry label_table[MAX_LABELS];
    int num_labels;
    GotoPatch goto_patches[MAX_LABELS];
    int num_goto_patches;

    // Switch statement code generation (for dense switches)
    long long *current_switch_table;  // Jump table being filled
    long current_switch_min;          // Minimum case value
    long current_switch_size;         // Jump table size
    Node *current_switch_default;     // Default case node
    long long *current_default_patch; // Patch location for default case jump

    // Switch statement code generation (for sparse switches)
    long long *current_sparse_case_table;           // Array of jump addresses
    int current_sparse_num;                         // Number of case entries
    Node *sparse_case_nodes[MAX_SPARSE_CASES];      // Case nodes
    long long *sparse_jump_addrs[MAX_SPARSE_CASES]; // Jump address pointers

    // Inline assembly callback
    JCCAsmCallback asm_callback; // User-provided callback for asm statements
    void *asm_user_data;         // User-provided context for callback

    // Foreign Function Interface (FFI)
    ForeignFunc *ffi_table; // Registry of foreign C functions
    int ffi_count;          // Number of registered functions
    int ffi_capacity;       // Capacity of ffi_table array

    // Current function being compiled (for VLA cleanup)
    Obj *current_codegen_fn;

    // Struct/union return buffer pool (copy-before-return approach)
    char *return_buffer_pool[RETURN_BUFFER_POOL_SIZE]; // Pool of return buffers
    int return_buffer_index; // Current buffer index (rotates 0-7)
    int return_buffer_size;  // Size of each buffer (1024 bytes)

    // Linked programs for extern offset propagation
    Obj **link_progs;    // Array of original program lists
    int link_prog_count; // Number of programs

    // Per-instance state (moved from static globals for thread-safety)
    int unique_name_counter; // Counter for new_unique_name()
    int counter_macro_value; // __COUNTER__ macro value

    // Optimization settings
    int opt_level; // Optimization level (0=none, 1=basic, 2=standard,
                   // 3=aggressive)
} Compiler;

/*!
 @struct JCC
 @abstract Encapsulates all state for the JCC compiler and virtual
           machine. Instances are independent and support embedding.
 @discussion The structure contains registers, memory segments, frontend
             state (preprocessor, tokenizer, parser) and codegen/VM
             bookkeeping. All public API functions accept an
             `JCC *` as the first parameter.
*/
struct JCC {
    // VM Registers (pure register-based architecture)
    long long regs[32]; // General-purpose register file (NUM_REGS)
    double fregs[32];   // Floating-point register file
    long long *pc;      // Program counter
    long long *bp;      // Base pointer (frame pointer)
    long long *sp;      // Stack pointer
    long long cycle;    // Instruction cycle counter

    // Exit detection (for returning from main)
    long long *initial_sp; // Initial stack pointer (for exit detection)
    long long *initial_bp; // Initial base pointer (for exit detection)

    // Stack bounds checking
    long long *stack_base; // Lower bound of stack (stack_seg start)

    // Memory Segments
    long long *text_seg;     // Text segment (bytecode instructions)
    long long *text_ptr;     // Current write position (for code generation)
    long long *stack_seg;    // Stack segment
    long long *old_text_seg; // Backup of original text segment pointer
    char *data_seg;          // Data segment (global variables/constants)
    char *data_ptr;          // Current write position in data segment
    char *heap_seg;          // Heap segment (for VM malloc/free)
    char *heap_ptr;          // Current allocation pointer (bump allocator)
    char *heap_end;          // End of heap segment
    FreeBlock *free_list;    // Head of free blocks list (for memory reuse)

    // Segregated free lists for optimized allocation
    // Size classes: 8, 16, 32, 64, 128, 256, 512, 1024, 2048, 4096, 8192, LARGE
    // (>8192)
    FreeBlock *
        size_class_lists[12]; // One free list per size class (NUM_SIZE_CLASSES)
    FreeBlock *large_list;    // For allocations > MAX_SMALL_ALLOC (8192)

    // Memory safety tracking
    AllocRecord *alloc_list; // List of active allocations (for leak detection)
    HashMap init_state; // Track initialization state of stack variables (for
                        // uninitialized detection)
    HashMap stack_ptrs; // Track stack pointers for dangling detection (ptr ->
                        // {bp, offset, size})
    HashMap provenance; // Track pointer provenance for stack/global (ptr ->
                        // {origin_type, base, size})
    HashMap stack_var_meta; // Unified stack variable metadata (bp+offset ->
                            // StackVarMeta)
    // Note: alloc_map and ptr_tags removed - now using sorted_allocs for heap
    // tracking

    // Sorted allocation array for O(log n) range queries (CHKP/CHKT performance
    // and heap provenance)
    struct {
        void **addresses;      // Sorted array of base addresses
        AllocHeader **headers; // Parallel array of headers
        int count;             // Number of active allocations
        int capacity;          // Allocated array capacity
    } sorted_allocs;

    // Configuration
    int poolsize; // Size of memory segments (bytes)
    int debug_vm; // Enable debug output during execution

    // Runtime flags (bitwise combination of JCCFlags)
    uint32_t flags; // JCCFlags bitfield for all safety and runtime features
    long long stack_canary; // Stack canary value (random if JCC_RANDOM_CANARIES
                            // set, else fixed)
    int in_vm_alloc; // Reentrancy guard: prevents HashMap from triggering VM
                     // heap recursion

    // Control Flow Integrity (shadow stack)
    long long *shadow_stack; // Shadow stack for return addresses (CFI)
    long long *shadow_sp;    // Shadow stack pointer

    // Stack instrumentation state
    int current_scope_id;          // Incremented for each scope entry
    int current_function_scope_id; // Scope ID of current function being
                                   // generated
    long long stack_high_water;    // Maximum stack usage tracking
    ScopeVarList *scope_vars;      // Array of per-scope variable lists
    int scope_vars_capacity;       // Capacity of scope_vars array

    // Struct return buffer runtime state (runtime rotation for clean chained
    // calls)
    int runtime_return_buffer_index; // Runtime rotating index for return
                                     // buffers

    // Debugger state (enable via JCC_ENABLE_DEBUGGER flag)
    Debugger dbg;

    // Compiler state (preprocessor, parser, codegen)
    Compiler compiler;

    // Error handling (setjmp/longjmp for exception-like behavior)
    jmp_buf
        *error_jmp_buf;  // Jump buffer for error handling (NULL = use exit())
    char *error_message; // Last error message (when using longjmp)

    // Error collection (for reporting multiple errors)
    CompileError *errors;      // Linked list of collected errors
    CompileError *errors_tail; // Tail pointer for O(1) append
    int error_count;           // Number of errors collected
    int warning_count;         // Number of warnings collected
    int max_errors;            // Maximum errors before stopping (default: 20)
    bool collect_errors;       // Enable error collection mode
    bool warnings_as_errors;   // Treat warnings as errors (--Werror)
};

/*!
 @function cc_init
 @abstract Initialize an JCC instance.
 @discussion The caller should allocate an `JCC` struct (usually on the
             stack) and pass its pointer to this function. This sets up
             memory segments, default include paths, and other runtime
             defaults.
 @param vm Pointer to an uninitialized JCC struct to initialize.
 @param flags Bitwise combination of JCCFlags to enable features (0 for none).
*/
void cc_init(JCC *vm, uint32_t flags);

/*!
 @function cc_destroy
 @abstract Free resources owned by an JCC instance.
 @discussion Does not free the `JCC` struct itself; the caller is
             responsible for the memory of the struct if it was
             dynamically allocated.
 @param vm The JCC instance to destroy.
*/
void cc_destroy(JCC *vm);

/*!
 @function cc_get_error_count
 @abstract Get the number of errors collected during compilation.
 @param vm The JCC instance.
 @result The number of errors collected.
*/
int cc_get_error_count(JCC *vm);

/*!
 @function cc_get_warning_count
 @abstract Get the number of warnings collected during compilation.
 @param vm The JCC instance.
 @result The number of warnings collected.
*/
int cc_get_warning_count(JCC *vm);

/*!
 @function cc_has_errors
 @abstract Check if any errors have been collected.
 @param vm The JCC instance.
 @result True if errors exist, false otherwise.
*/
bool cc_has_errors(JCC *vm);

/*!
 @function cc_clear_errors
 @abstract Clear all collected errors and warnings.
 @discussion Useful for reusing a VM instance across multiple compilations.
 @param vm The JCC instance.
*/
void cc_clear_errors(JCC *vm);

/*!
 @function cc_print_all_errors
 @abstract Print all collected errors and warnings to stderr.
 @param vm The JCC instance.
*/
void cc_print_all_errors(JCC *vm);

/*!
 @function cc_print_stack_report
 @abstract Print stack instrumentation statistics and report.
 @discussion Outputs stack usage statistics including high water mark,
             variable access counts, and scope information. Only useful
             when stack instrumentation is enabled.
 @param vm The JCC instance.
*/
void cc_print_stack_report(JCC *vm);

/*!
 @function cc_include
 @abstract Add a directory to the compiler's header search paths.
 @discussion This adds the path to the list of directories searched for
             "..." includes (quote includes).
 @param vm The JCC instance.
 @param path Filesystem path to add to include search.
*/
void cc_include(JCC *vm, const char *path);

/*!
 @function cc_system_include
 @abstract Add a directory to the compiler's system header search paths.
 @discussion This adds the path to the list of directories searched for
             <...> includes (angle bracket includes). System include paths
             are searched after regular include paths for "..." includes.
 @param vm The JCC instance.
 @param path Filesystem path to add to system include search.
*/
void cc_system_include(JCC *vm, const char *path);

/*!
 @function cc_define
 @abstract Define or override a preprocessor macro for the given VM.
 @param vm The JCC instance.
 @param name Macro identifier (NUL-terminated).
 @param buf Macro replacement text (NUL-terminated).
*/
void cc_define(JCC *vm, char *name, char *buf);

/*!
 @function cc_undef
 @abstract Remove a preprocessor macro definition from the VM.
 @param vm The JCC instance.
 @param name Macro identifier to remove.
*/
void cc_undef(JCC *vm, char *name);

/*!
 @function cc_set_asm_callback
 @abstract Register a callback invoked for `asm("...")` statements.
 @param vm The JCC instance.
 @param callback Callback function pointer, or NULL to unregister.
 @param user_data Optional user context pointer passed to the callback.
*/
void cc_set_asm_callback(JCC *vm, JCCAsmCallback callback, void *user_data);

/*!
 @function cc_register_cfunc
 @abstract Register a native C function to be callable from VM code via FFI.
 @param vm The JCC instance.
 @param name Function name (must match declarations in C source).
 @param func_ptr Pointer to the native C function.
 @param num_args Number of arguments the function expects.
 @param returns_double 1 if function returns double, 0 if returns long long.
 @discussion Registered functions can be called from C code compiled to VM
             bytecode. The CALLF instruction handles argument marshalling.
             All integer types are passed/returned as long long, floats as
 double.
*/
void cc_register_cfunc(JCC *vm, const char *name, void *func_ptr, int num_args,
                       int returns_double);

/*!
 @function cc_register_cfunc_ex
 @abstract Register a C function with detailed argument type information for
 correct FFI calling conventions.
 @param vm The JCC instance.
 @param name Function name (must match declarations in C source).
 @param func_ptr Pointer to the native C function.
 @param num_args Total number of arguments.
 @param returns_double 1 if function returns double, 0 if returns long long.
 @param double_arg_mask Bitmask indicating which arguments are doubles. Bit N
 corresponds to argument N (0-indexed). For example: 0b11 = both args 0 and 1
 are doubles (e.g., pow(double, double)). 0b01 = only arg 0 is double (e.g.,
 ldexp(double, int)).
 @discussion Use this function instead of cc_register_cfunc() for functions that
 take multiple double arguments or mix double and integer arguments. The bitmask
 ensures correct calling conventions on all platforms. For functions with only
 integer arguments or single double arguments, cc_register_cfunc() is
 sufficient.
*/
void cc_register_cfunc_ex(JCC *vm, const char *name, void *func_ptr,
                          int num_args, int returns_double,
                          uint64_t double_arg_mask);

/*!
 @function cc_register_variadic_cfunc
 @abstract Register a variadic native C function to be callable from VM code via
 FFI.
 @param vm The JCC instance.
 @param name Function name (must match declarations in C source).
 @param func_ptr Pointer to the native C variadic function.
 @param num_fixed_args Number of fixed arguments before the ... (e.g., printf
 has 1: format string).
 @param returns_double 1 if function returns double, 0 if returns long long.
 @discussion This function is only available when JCC_HAS_FFI is defined. When
 libffi is not available, use fixed-argument wrappers instead. Variadic
 functions accept a variable number of arguments after the fixed arguments.
             Example: printf has 1 fixed arg (format), fprintf has 2 (stream,
 format).
*/
void cc_register_variadic_cfunc(JCC *vm, const char *name, void *func_ptr,
                                int num_fixed_args, int returns_double);

/*!
 @function cc_load_stdlib
 @abstract Register all standard library functions available via FFI.
 @param vm The JCC instance.
 @discussion Automatically registers 50+ standard library functions including:
             - Memory: malloc, free, calloc, realloc, memcpy, memmove, memset,
 memcmp
             - String: strlen, strcpy, strncpy, strcat, strcmp, strncmp, strchr,
 strstr
             - I/O: puts, putchar, getchar, fopen, fclose, fread, fwrite, fgetc,
 fputc
             - Math: sin, cos, tan, sqrt, pow, exp, log, floor, ceil, fabs
             - Conversion: atoi, atol, atof, strtol, strtod
             - System: exit, abort, system, open, close, read, write

             This function is automatically called by cc_init(), but can be
 called manually if you want to reset the FFI registry or initialize it
 separately.
*/
void cc_load_stdlib(JCC *vm);

/*!
 @function cc_dlsym
 @abstract Update an existing registered FFI function's pointer by name.
 @param vm The JCC instance.
 @param name Function name to update.
 @param func_ptr New function pointer to assign.
 @param num_args Expected number of arguments (must match registered function).
 @param returns_double Expected return type (must match registered function).
 @return 0 on success, -1 on error (function not found or signature mismatch).
 @discussion This function is useful for updating function pointers after
 loading a dynamic library, or for redirecting calls to different
 implementations. The function must already be registered via cc_register_cfunc
 or cc_register_variadic_cfunc.
*/
int cc_dlsym(JCC *vm, const char *name, void *func_ptr, int num_args,
             int returns_double);

/*!
 @function cc_dlopen
 @abstract Load a dynamic library and resolve all registered FFI functions.
 @param vm The JCC instance.
 @param lib_path Path to the dynamic library (.so, .dylib, .dll) or NULL for
 default libraries.
 @return 0 on success, -1 on error.
 @discussion This function opens a dynamic library and attempts to resolve all
 currently registered FFI functions. Functions that cannot be resolved will
 print warnings but won't fail the entire operation. If lib_path is NULL, the
 function searches in default system libraries.

             Platform-specific behavior:
             - Unix: Uses dlopen/dlsym to load .so/.dylib files
             - Windows: Uses LoadLibrary/GetProcAddress to load .dll files

             The library handle is not closed after loading to keep function
 pointers valid.
*/
int cc_dlopen(JCC *vm, const char *lib_path);

/*!
 @function cc_load_libc
 @abstract Load the platform's standard C library and resolve FFI functions.
 @param vm The JCC instance.
 @return 0 on success, -1 on error.
 @discussion This function automatically detects and loads the correct C library
 for the current platform:
             - macOS: /usr/lib/libSystem.dylib
             - Linux: /lib64/libc.so.6 (or /lib/libc.so.6 on 32-bit)
             - FreeBSD: /lib/libc.so.7
             - Windows: msvcrt.dll
             This is useful when you want to load stdlib functions dynamically
 instead of registering them with explicit function pointers.
*/
int cc_load_libc(JCC *vm);

/*!
 @function cc_preprocess
 @abstract Run the preprocessor on a C source file and return a token stream.
 @param vm The JCC instance.
 @param path Path to the source file to preprocess.
 @return Head of the token stream (linked Token list). Caller owns tokens.
*/
Token *cc_preprocess(JCC *vm, const char *path);

/*!
 @function cc_parse
 @abstract Parse a preprocessed token stream into an AST and produce
           a linked list of top-level Obj declarations.
 @param vm The JCC instance.
 @param tok Head of the preprocessed token stream.
 @return Linked list of top-level Obj representing globals and functions.
*/
Obj *cc_parse(JCC *vm, Token *tok);

/*!
 @function cc_parse_expr
 @abstract Parse a single C expression from token stream.
 @param vm The JCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse.
 @return AST node representing the parsed expression.
*/
Node *cc_parse_expr(JCC *vm, Token **rest, Token *tok);

/*!
 @function cc_parse_assign
 @abstract Parse an assignment expression from token stream (stops at commas).
 @discussion Used for parsing function arguments and other contexts where commas
             are separators rather than operators.
 @param vm The JCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse.
 @return AST node representing the parsed assignment expression.
*/
Node *cc_parse_assign(JCC *vm, Token **rest, Token *tok);

/*!
 @function cc_parse_stmt
 @abstract Parse a single C statement from token stream.
 @param vm The JCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse.
 @return AST node representing the parsed statement.
*/
Node *cc_parse_stmt(JCC *vm, Token **rest, Token *tok);

/*!
 @function cc_parse_compound_stmt
 @abstract Parse a compound statement (block) from token stream.
 @param vm The JCC instance.
 @param rest Pointer to receive the remaining tokens after parsing.
 @param tok Head of the token stream to parse (should be opening brace).
 @return AST node representing the parsed compound statement.
*/
Node *cc_parse_compound_stmt(JCC *vm, Token **rest, Token *tok);
void cc_init_parser(JCC *vm);

/*!
 @function cc_expand_pragma_macros
 @abstract Expand all pragma macro calls in the AST.
 @discussion Walks the AST and replaces ND_MACRO_CALL nodes with the
             generated AST from executing the corresponding pragma macro.
             Must be called after cc_parse and before cc_compile.
 @param vm The JCC instance.
 @param prog Linked list of top-level Obj returned by cc_parse.
*/
void cc_expand_pragma_macros(JCC *vm, Obj *prog);

/*!
 @function cc_serialize_program
 @abstract Serialize a program AST back to C source code.
 @discussion Used with -M flag to output macro-expanded source that
             can be compiled with gcc or other C compilers.
 @param f Output file stream.
 @param vm The JCC instance.
 @param prog Program AST to serialize.
*/
void cc_serialize_program(FILE *f, JCC *vm, Obj *prog);

/*!
 @function cc_link_progs
 @abstract Link multiple parsed programs (Obj lists) into a single program.
 @discussion Takes an array of Obj* programs and combines them into one
             linked list. This allows multiple source files to be compiled
             together into a single program. The function handles duplicate
             definitions by preferring definitions over declarations.
 @param vm The JCC instance.
 @param progs Array of Obj* programs (linked lists from cc_parse).
 @param count Number of programs in the array.
 @return A single merged Obj* linked list containing all objects.
*/
Obj *cc_link_progs(JCC *vm, Obj **progs, int count);

/*!
 @function cc_compile
 @abstract Compile the parsed program (Obj list) into VM bytecode.
 @param vm The JCC instance.
 @param prog Linked list of top-level Obj returned by cc_parse.
*/
void cc_compile(JCC *vm, Obj *prog);

/*!
 @function cc_run
 @abstract Execute the compiled program within the VM.
 @param vm The JCC instance containing compiled bytecode.
 @param argc Argument count to pass to the program's main().
 @param argv Argument vector (NUL-terminated array of strings).
 @return Program exit code returned by main().
*/
int cc_run(JCC *vm, int argc, char **argv);

/*!
 @function cc_print_tokens
 @abstract Print a token stream to stdout (useful for debugging the
           preprocessor and tokenizer).
 @param tok Head of the token stream to print.
*/
void cc_print_tokens(Token *tok);

/*!
 @function cc_disassemble
 @abstract Disassemble the compiled bytecode to stdout.
 @param vm The JCC instance containing compiled bytecode.
*/
void cc_disassemble(JCC *vm);

/*!
 @function cc_output_json
 @abstract Output C header declarations as JSON for FFI wrapper generation.
 @discussion Serializes function signatures, struct/union definitions, enum
             declarations, and global variables from the parsed AST to JSON
             format. The output includes full type information with recursive
             expansion of pointers, arrays, and aggregate types. Storage class
             specifiers (static, extern) are included. Function bodies are not
             serialized - only signatures.

             The JSON output format is:
             {
               "functions": [...],
               "structs": [...],
               "unions": [...],
               "enums": [...],
               "variables": [...]
             }
 @param f Output file stream (e.g., stdout or file opened with fopen).
 @param prog Head of the parsed AST object list (from cc_parse).
*/
void cc_output_json(FILE *f, Obj *prog);

/*!
 @function cc_output_source_map_json
 @abstract Output source map as JSON to a file for debugging tools.
 @discussion Outputs source maps in JSON format, mapping bytecode PC offsets
             to source locations with file, line, and column information.
 @param vm The JCC instance with source map data.
 @param f Output file stream.
*/
void cc_output_source_map_json(JCC *vm, FILE *f);

/*!
 @function cc_save_bytecode
 @abstract Save compiled bytecode to a file for later execution.
 @discussion Serializes the text segment, data segment, and necessary
             metadata to a binary file. The file can be loaded and executed
             by cc_load_bytecode().
 @param vm The JCC instance containing compiled bytecode.
 @param path Output file path.
 @return 0 on success, -1 on error.
*/
int cc_save_bytecode(JCC *vm, const char *path);

/*!
 @function cc_load_bytecode
 @abstract Load compiled bytecode from a file.
 @discussion Deserializes bytecode previously saved with cc_save_bytecode()
             and prepares the VM for execution with cc_run().
 @param vm The JCC instance to load bytecode into.
 @param path Input file path.
 @return 0 on success, -1 on error.
*/
int cc_load_bytecode(JCC *vm, const char *path);

/*!
 @function cc_add_breakpoint
 @abstract Add a breakpoint at a specific program counter address.
 @param vm The JCC instance.
 @param pc Program counter address where breakpoint should be set.
 @return Breakpoint index, or -1 if failed (too many breakpoints).
*/
int cc_add_breakpoint(JCC *vm, long long *pc);

/*!
 @function cc_remove_breakpoint
 @abstract Remove a breakpoint by index.
 @param vm The JCC instance.
 @param index Breakpoint index to remove.
*/
void cc_remove_breakpoint(JCC *vm, int index);

/*!
 @function cc_debug_repl
 @abstract Enter interactive debugger REPL (Read-Eval-Print Loop).
 @param vm The JCC instance.
 @discussion Provides an interactive shell for debugging with commands like:
             - break/b: Set breakpoint
             - continue/c: Continue execution
             - step/s: Single step
             - next/n: Step over
             - finish/f: Step out
             - print/p: Print registers
             - stack/st: Print stack
             - help/h: Show help
*/
void cc_debug_repl(JCC *vm);

/*!
 @function cc_add_watchpoint
 @abstract Add a watchpoint at a specific memory address.
 @param vm The JCC instance.
 @param address Memory address to watch.
 @param size Size of memory region to watch (in bytes).
 @param type Watchpoint type flags (WATCH_READ | WATCH_WRITE | WATCH_CHANGE).
 @param expr Original expression string (for display purposes).
 @return Watchpoint index, or -1 if failed (too many watchpoints).
*/
int cc_add_watchpoint(JCC *vm, void *address, int size, int type,
                      const char *expr);

/*!
 @function cc_remove_watchpoint
 @abstract Remove a watchpoint by index.
 @param vm The JCC instance.
 @param index Watchpoint index to remove.
*/
void cc_remove_watchpoint(JCC *vm, int index);

/*!
 @function cc_get_source_location
 @abstract Get source file location for a given program counter.
 @param vm The JCC instance.
 @param pc Program counter address.
 @param out_file Pointer to receive the source File pointer (can be NULL).
 @param out_line Pointer to receive the line number (can be NULL).
 @return 1 if location found, 0 if not found.
*/
int cc_get_source_location(JCC *vm, long long *pc, File **out_file,
                           int *out_line, int *out_col);

/*!
 @function cc_find_pc_for_source
 @abstract Find program counter address for a given source location.
 @param vm The JCC instance.
 @param file Source file (NULL to search in any file).
 @param line Line number to find.
 @return Program counter address, or NULL if not found.
*/
long long *cc_find_pc_for_source(JCC *vm, File *file, int line);

/*!
 @function cc_find_function_entry
 @abstract Find program counter address for a function entry point by name.
 @param vm The JCC instance.
 @param name Function name to find.
 @return Program counter address, or NULL if not found.
*/
long long *cc_find_function_entry(JCC *vm, const char *name);

/*!
 @function cc_lookup_symbol
 @abstract Look up a debug symbol by name in current scope.
 @param vm The JCC instance.
 @param name Symbol name to look up.
 @return Pointer to DebugSymbol if found, NULL otherwise.
*/
DebugSymbol *cc_lookup_symbol(JCC *vm, const char *name);

/*!
 @function cc_dlopen
 @abstract Open a dynamic library.
 @param vm The JCC instance.
 @param lib_path Path to the dynamic library to open.
 @return 0 on success, -1 on failure.
*/
int cc_dlopen(JCC *vm, const char *lib_path);

/*!
 @function cc_dlsym
 @abstract Resolve a symbol in a dynamic library.
 @param vm The JCC instance.
 @param name Name of the symbol to resolve.
 @param func_ptr Pointer to the function to resolve.
 @param num_args Number of arguments the function expects.
 @param returns_double 1 if function returns double, 0 if returns long long.
 @return 0 on success, -1 on failure.
*/
int cc_dlsym(JCC *vm, const char *name, void *func_ptr, int num_args,
             int returns_double);

#ifdef __cplusplus
}
#endif
#endif
