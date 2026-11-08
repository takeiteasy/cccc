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

// reflection.h's public NK_* values must equal the internal ND_* ones: comptime
// code receives raw `Node::kind` numbers. AST_JUMP_* is JumpKind + 1.

#define __CCCC_TAG(n)        n
#define NodeKind             PublicNodeKind
#define TypeKind             PublicTypeKind
#define AttrTargetKind       PublicAttrTargetKind
#define ATTR_TARGET_TYPEDEF  PUBLIC_ATTR_TARGET_TYPEDEF
#define ATTR_TARGET_TYPE     PUBLIC_ATTR_TARGET_TYPE
#define ATTR_TARGET_FUNCTION PUBLIC_ATTR_TARGET_FUNCTION
#define ATTR_TARGET_GLOBAL   PUBLIC_ATTR_TARGET_GLOBAL
#define ATTR_TARGET_MEMBER   PUBLIC_ATTR_TARGET_MEMBER
#define ATTR_TARGET_PARAM    PUBLIC_ATTR_TARGET_PARAM
#include "../../include/cccc/reflection.h"
#undef ATTR_TARGET_TYPEDEF
#undef ATTR_TARGET_TYPE
#undef ATTR_TARGET_FUNCTION
#undef ATTR_TARGET_GLOBAL
#undef ATTR_TARGET_MEMBER
#undef ATTR_TARGET_PARAM
#undef NodeKind
#undef TypeKind
#undef AttrTargetKind

#include "internal.h"

#include <stdio.h>

#define NODE_KINDS(X)                                                          \
    X(NULL_EXPR)                                                               \
    X(ADD)                                                                     \
    X(SUB)                                                                     \
    X(MUL)                                                                     \
    X(DIV)                                                                     \
    X(NEG)                                                                     \
    X(MOD)                                                                     \
    X(BITAND)                                                                  \
    X(BITOR)                                                                   \
    X(BITXOR)                                                                  \
    X(SHL)                                                                     \
    X(SHR)                                                                     \
    X(EQ)                                                                      \
    X(NE)                                                                      \
    X(LT)                                                                      \
    X(LE)                                                                      \
    X(ASSIGN)                                                                  \
    X(COND)                                                                    \
    X(COMMA)                                                                   \
    X(MEMBER)                                                                  \
    X(ADDR)                                                                    \
    X(DEREF)                                                                   \
    X(NOT)                                                                     \
    X(BITNOT)                                                                  \
    X(LOGAND)                                                                  \
    X(LOGOR)                                                                   \
    X(RETURN)                                                                  \
    X(IF)                                                                      \
    X(FOR)                                                                     \
    X(DO)                                                                      \
    X(SWITCH)                                                                  \
    X(CASE)                                                                    \
    X(BLOCK)                                                                   \
    X(GOTO)                                                                    \
    X(GOTO_EXPR)                                                               \
    X(LABEL)                                                                   \
    X(LABEL_VAL)                                                               \
    X(FUNCALL)                                                                 \
    X(EXPR_STMT)                                                               \
    X(STMT_EXPR)                                                               \
    X(VAR)                                                                     \
    X(VLA_PTR)                                                                 \
    X(NUM)                                                                     \
    X(CAST)                                                                    \
    X(MEMZERO)                                                                 \
    X(ASM)                                                                     \
    X(CAS)                                                                     \
    X(EXCH)                                                                    \
    X(FRAME_ADDR)                                                              \
    X(BLOCK_LITERAL)                                                           \
    X(BLOCK_CALL)                                                              \
    X(MACRO_CALL)                                                              \
    X(COMPLEX)                                                                 \
    X(UNREACHABLE)                                                             \
    X(BITOP)                                                                   \
    X(OVERFLOW_ARITH)                                                          \
    X(INIT_SPLICE)                                                             \
    X(ALOAD)                                                                   \
    X(ASTORE)                                                                  \
    X(RETURN_ADDR)                                                             \
    X(DYNOBJ_SIZE)                                                             \
    X(CONVERTVECTOR)                                                           \
    X(DECIMAL_TO_CHARS)                                                        \
    X(FENCE)                                                                   \
    X(QUOTE_LAZY)                                                              \
    X(DYNAMIC_CHECK)                                                           \
    X(VA_NAMED_COUNT)

int main(void) {
    int failed = 0;
#define CHECK_KIND(name)                                                       \
    if ((int)NK_##name != (int)ND_##name) {                                    \
        fprintf(stderr, "FAIL: NK_" #name " = %d, ND_" #name " = %d\n",        \
                (int)NK_##name, (int)ND_##name);                               \
        failed = 1;                                                            \
    }
    NODE_KINDS(CHECK_KIND)
#undef CHECK_KIND
#define CHECK_JUMP(name)                                                       \
    if ((int)AST_JUMP_##name != (int)JUMP_##name + 1) {                        \
        fprintf(stderr,                                                        \
                "FAIL: AST_JUMP_" #name " = %d, JUMP_" #name " = %d\n",        \
                (int)AST_JUMP_##name, (int)JUMP_##name);                       \
        failed = 1;                                                            \
    }
    CHECK_JUMP(GOTO)
    CHECK_JUMP(BREAK)
    CHECK_JUMP(CONTINUE)
#undef CHECK_JUMP
    if (failed)
        return 1;
    printf("PASS: reflection node kinds\n");
    return 0;
}
