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

/*
 * Instruction encoding:
 *   RRR format: [OPCODE] [rd:8|rs1:8|rs2:8|unused:40]
 *   RI format:  [OPCODE] [rd:8|unused:56] [immediate:64]
 */

#include "./internal.h"
#include "jcc.h"
#include <limits.h>

#define STACK_GUARD_SIZE 16

// Guard watchpoint checks so production runs (no JCC_ENABLE_DEBUGGER) skip
// the call entirely rather than paying the call/return overhead on every
// load and store. debugger_check_watchpoint already short-circuits on the
// same flag, but the call itself was still emitted unconditionally. (#157)
#define WATCHPOINT_CHECK(vm, addr, size, kind)                          \
    do {                                                                \
        if ((vm)->flags & JCC_ENABLE_DEBUGGER)                          \
            debugger_check_watchpoint((vm), (addr), (size), (kind));    \
    } while (0)

static inline int check_stack_overflow(JCC *vm, int slots_needed) {
    if (vm->sp - slots_needed - STACK_GUARD_SIZE < vm->stack_base) {
        // Try to grow the stack downward before reporting overflow.
        // With PROT_NONE pages below the committed floor, crossing without
        // growing first would produce a hard SIGSEGV, so growth must happen
        // here before any sp decrement.
        if (vm_stack_grow(vm, slots_needed + STACK_GUARD_SIZE) == 0)
            return 0; // Growth succeeded; retry the caller's operation.
        // Reservation exhausted — true overflow.
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

// ========== Arithmetic Operations ==========

int op_ADD3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    // Check for signed addition overflow (only if overflow checks enabled)
    if (vm->flags & JCC_OVERFLOW_CHECKS) {
        if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
            printf("\n========== INTEGER OVERFLOW ==========\n");
            printf("Addition overflow detected\n");
            printf("Operands: %lld + %lld\n", a, b);
            printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
                   (long long)vm->pc);
            printf("======================================\n");
            return -1;
        }
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a + b;
    return 0;
}

int op_SUB3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    // Check for signed subtraction overflow (only if overflow checks enabled)
    if (vm->flags & JCC_OVERFLOW_CHECKS) {
        if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
            printf("\n========== INTEGER OVERFLOW ==========\n");
            printf("Subtraction overflow detected\n");
            printf("Operands: %lld - %lld\n", a, b);
            printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
                   (long long)vm->pc);
            printf("======================================\n");
            return -1;
        }
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a - b;
    return 0;
}

int op_MUL3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    // Check for signed multiplication overflow (only if overflow checks
    // enabled)
    if ((vm->flags & JCC_OVERFLOW_CHECKS) && a != 0 && b != 0) {
        if (a == LLONG_MIN || b == LLONG_MIN) {
            // LLONG_MIN * anything except 0, 1, -1 overflows
            if ((a == LLONG_MIN && (b != 0 && b != 1 && b != -1)) ||
                (b == LLONG_MIN && (a != 0 && a != 1 && a != -1))) {
                printf("\n========== INTEGER OVERFLOW ==========\n");
                printf("Multiplication overflow detected\n");
                printf("Operands: %lld * %lld\n", a, b);
                printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
                       (long long)vm->pc);
                printf("======================================\n");
                return -1;
            }
        } else {
            long long result = a * b;
            if (result / a != b) {
                printf("\n========== INTEGER OVERFLOW ==========\n");
                printf("Multiplication overflow detected\n");
                printf("Operands: %lld * %lld\n", a, b);
                printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
                       (long long)vm->pc);
                printf("======================================\n");
                return -1;
            }
        }
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a * b;
    return 0;
}

int op_DIV3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    // Check for division by zero
    if (b == 0) {
        printf("\n========== DIVISION BY ZERO ==========\n");
        printf("Attempted division by zero\n");
        printf("Operands: %lld / 0\n", a);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    // Check for signed division overflow (LLONG_MIN / -1)
    if (a == LLONG_MIN && b == -1) {
        printf("\n========== INTEGER OVERFLOW ==========\n");
        printf("Division overflow detected\n");
        printf("Operands: %lld / %lld\n", a, b);
        printf("Result would overflow (LLONG_MIN / -1 = LLONG_MAX + 1)\n");
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a / b;
    return 0;
}

int op_MOD3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    // Check for modulo by zero
    if (b == 0) {
        printf("\n========== MODULO BY ZERO ==========\n");
        printf("Attempted modulo by zero\n");
        printf("Operands: %lld %% 0\n", a);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a % b;
    return 0;
}

// ========== Bitwise Operations ==========

int op_AND3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] & vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_OR3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] | vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_XOR3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] ^ vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SHL3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] << vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SHR3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] >> vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_UDIV3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long a = (unsigned long long)vm->regs[rs1];
    unsigned long long b = (unsigned long long)vm->regs[rs2];

    if (b == 0) {
        printf("\n========== DIVISION BY ZERO ==========\n");
        printf("Attempted unsigned division by zero\n");
        printf("Operands: %llu / 0\n", a);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(a / b);
    return 0;
}

int op_UMOD3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long a = (unsigned long long)vm->regs[rs1];
    unsigned long long b = (unsigned long long)vm->regs[rs2];

    if (b == 0) {
        printf("\n========== MODULO BY ZERO ==========\n");
        printf("Attempted unsigned modulo by zero\n");
        printf("Operands: %llu %% 0\n", a);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(a % b);
    return 0;
}

int op_USHR3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long result = (unsigned long long)vm->regs[rs1] >> vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)result;
    return 0;
}

// ========== Comparison Operations ==========

int op_SEQ3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] == vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SNE3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] != vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SLT3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] < vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SGE3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] >= vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SGT3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] > vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_SLE3_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] <= vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

// ========== Data Movement ==========

int op_LI3_fn(JCC *vm) {
    // Load immediate: [LI3] [rd:8] [immediate:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long imm = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = imm;
    return 0;
}

int op_LDA3_fn(JCC *vm) {
    // Load data-relative address: [LDA3] [rd:8] [byte_offset:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(vm->data_seg + offset);
    return 0;
}

int op_LTA3_fn(JCC *vm) {
    // Load text-relative address: [LTA3] [rd:8] [byte_offset:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = offset;
    return 0;
}

int op_MOV3_fn(JCC *vm) {
    // Move register: [MOV3] [rd:8|rs1:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    (void)rs2; // Unused
    if (rd != REG_ZERO)
        vm->regs[rd] = vm->regs[rs1];
    return 0;
}

// ========== Register-Based Calling Convention ==========

int op_ENT3_fn(JCC *vm) {
    // Enter function: [ENT3] [stack_size:32|param_count:32]
    // [f32_param_mask:32|float_param_mask:32]
    // Creates new stack frame and copies REG_A0-REG_An and FREG_A0-FREG_An to
    // parameter slots
    long long operands = cc_read_i64(vm);
    int stack_size = (int)(operands & 0xFFFFFFFF);
    int param_count = (int)((operands >> 32) & 0xFFFFFFFF);
    unsigned long long masks = (unsigned long long)cc_read_i64(vm);
    unsigned int float_param_mask = (unsigned int)(masks & 0xFFFFFFFFu);
    unsigned int f32_param_mask = (unsigned int)(masks >> 32);

    int total_slots = stack_size + 1 + ((vm->flags & JCC_STACK_CANARIES) ? 1 : 0);
    if (check_stack_overflow(vm, total_slots)) return -1;

    // Save old base pointer
    *--vm->sp = (long long)vm->bp;
    vm->bp = vm->sp;

    // If stack canaries are enabled, write canary after old bp
    if (vm->flags & JCC_STACK_CANARIES) {
        *--vm->sp = vm->stack_canary;
    }

    // Allocate space for local variables AND parameters
    // Parameters are now stored at negative offsets like locals
    vm->sp = vm->sp - stack_size;

    // Copy register arguments to their stack slots
    // Parameters are at bp[-1], bp[-2], ... bp[-param_count]
    // Map: REG_Ai/FREG_Ai -> bp[-i-1] depending on float_param_mask
    // We need separate int and float register indices
    int int_reg_idx = 0;
    int float_reg_idx = 0;
    for (int i = 0; i < param_count && i < 8; i++) {
        long long *param_slot = vm->bp - 1 - i;
        if (vm->flags & JCC_STACK_CANARIES) {
            param_slot--; // Account for canary slot
        }

        if (float_param_mask & (1LL << i)) {
            // Float parameter - copy from fregs[]
            if (f32_param_mask & (1u << i)) {
                *(float *)param_slot = (float)vm->fregs[FREG_A0 + float_reg_idx];
            } else {
                union {
                    double d;
                    long long ll;
                } conv;
                conv.d = vm->fregs[FREG_A0 + float_reg_idx];
                *param_slot = conv.ll;
            }
            float_reg_idx++;
        } else {
            // Integer parameter - copy from regs[]
            *param_slot = vm->regs[REG_A0 + int_reg_idx];
            int_reg_idx++;
        }
    }

    // Stack overflow checking (for stack instrumentation)
    if (vm->flags & JCC_STACK_INSTR) {
        long long stack_used = (char *)vm->initial_sp - (char *)vm->sp;
        if (stack_used >
            (long long)(vm->poolsize * sizeof(long long) * 3 / 4)) {
            if (vm->flags & JCC_STACK_INSTR_ERRORS) {
                printf("\n===========================================\n");
                printf("STACK OVERFLOW: Stack usage exceeded 75%% threshold\n");
                printf("  Stack used: %lld bytes\n", stack_used);
                printf("  Stack size: %lld bytes\n",
                       (long long)(vm->poolsize * sizeof(long long)));
                printf("===========================================\n");
                return -1;
            } else if (vm->debug_vm) {
                printf("WARNING: Stack usage %lld bytes exceeds threshold\n",
                       stack_used);
            }
        }
    }

    return 0;
}

int op_LEV3_fn(JCC *vm) {
    // Leave function: return value already in REG_A0/FREG_A0, restore frame
    // (Caller placed return value in REG_A0 before LEV3)

    // Restore stack pointer to base pointer
    vm->sp = vm->bp;

    // If stack canaries are enabled, check canary
    if (vm->flags & JCC_STACK_CANARIES) {
        long long canary = vm->sp[-1];
        if (canary != vm->stack_canary) {
            printf("\n========== STACK OVERFLOW DETECTED ==========\n");
            printf("Stack canary corrupted!\n");
            printf("Expected: 0x%llx\n", vm->stack_canary);
            printf("Found:    0x%llx\n", canary);
            printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
                   (long long)vm->pc);
            printf("This indicates a stack buffer overflow.\n");
            printf("=============================================\n");
            return -1;
        }
    }

    // Restore old base pointer
    vm->bp = (long long *)*vm->sp++;

    // Get return address
    long long ret_addr = *vm->sp++;

    // Check if returning from main (ret_addr == 0)
    if (ret_addr == 0) {
        vm->pc = JCC_INVALID_PC; // Signal end of execution
        return 0;
    }

    // CFI validation. main() starts from a synthetic return address instead of
    // a CALL instruction, so the ret_addr == 0 sentinel above has no shadow entry.
    if (vm->flags & JCC_CFI) {
        long long shadow_ret_addr = *vm->shadow_sp++;
        if (shadow_ret_addr != ret_addr) {
            printf("\n========== CFI VIOLATION ==========\n");
            printf("Control flow integrity violation detected!\n");
            printf("Expected return address: 0x%llx\n", shadow_ret_addr);
            printf("Actual return address:   0x%llx\n", ret_addr);
            printf("Current PC offset:       %lld\n",
                   (long long)vm->pc);
            printf("This indicates a ROP attack or stack corruption.\n");
            printf("====================================\n");
            return -1;
        }
    }

    vm->pc = (JCCPc)ret_addr;
    return 0;
}

// ========== 3-Register Floating-Point Arithmetic ==========

int op_FADD3_fn(JCC *vm) {
    // fregs[rd] = fregs[rs1] + fregs[rs2]
    // Format: [FADD3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->fregs[rd] = vm->fregs[rs1] + vm->fregs[rs2];
    return 0;
}

int op_FSUB3_fn(JCC *vm) {
    // fregs[rd] = fregs[rs1] - fregs[rs2]
    // Format: [FSUB3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->fregs[rd] = vm->fregs[rs1] - vm->fregs[rs2];
    return 0;
}

int op_FMUL3_fn(JCC *vm) {
    // fregs[rd] = fregs[rs1] * fregs[rs2]
    // Format: [FMUL3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->fregs[rd] = vm->fregs[rs1] * vm->fregs[rs2];
    return 0;
}

int op_FDIV3_fn(JCC *vm) {
    // fregs[rd] = fregs[rs1] / fregs[rs2]
    // Format: [FDIV3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    // Division by zero check
    if (vm->fregs[rs2] == 0.0) {
        printf("\n========== DIVISION BY ZERO ==========\n");
        printf("Floating-point division by zero detected!\n");
        printf("PC offset: %lld\n", (long long)(vm->pc - 1));
        printf("======================================\n");
        return -1;
    }

    vm->fregs[rd] = vm->fregs[rs1] / vm->fregs[rs2];
    return 0;
}

int op_FMOV3_fn(JCC *vm) {
    // fregs[rd] = fregs[rs1]
    // Format: [FMOV3] [rd:8|rs1:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    vm->fregs[rd] = vm->fregs[rs1];
    return 0;
}

int op_FNEG3_fn(JCC *vm) {
    // fregs[rd] = -fregs[rs1]
    // Format: [FNEG3] [rd:8|rs1:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    vm->fregs[rd] = -vm->fregs[rs1];
    return 0;
}

// ========== 3-Register Floating-Point Comparisons ==========

int op_FEQ3_fn(JCC *vm) {
    // regs[rd] = (fregs[rs1] == fregs[rs2])
    // Format: [FEQ3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (vm->fregs[rs1] == vm->fregs[rs2]);
    return 0;
}

int op_FNE3_fn(JCC *vm) {
    // regs[rd] = (fregs[rs1] != fregs[rs2])
    // Format: [FNE3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (vm->fregs[rs1] != vm->fregs[rs2]);
    return 0;
}

int op_FLT3_fn(JCC *vm) {
    // regs[rd] = (fregs[rs1] < fregs[rs2])
    // Format: [FLT3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (vm->fregs[rs1] < vm->fregs[rs2]);
    return 0;
}

int op_FLE3_fn(JCC *vm) {
    // regs[rd] = (fregs[rs1] <= fregs[rs2])
    // Format: [FLE3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (vm->fregs[rs1] <= vm->fregs[rs2]);
    return 0;
}

int op_FGT3_fn(JCC *vm) {
    // regs[rd] = (fregs[rs1] > fregs[rs2])
    // Format: [FGT3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (vm->fregs[rs1] > vm->fregs[rs2]);
    return 0;
}

int op_FGE3_fn(JCC *vm) {
    // regs[rd] = (fregs[rs1] >= fregs[rs2])
    // Format: [FGE3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (vm->fregs[rs1] >= vm->fregs[rs2]);
    return 0;
}

// ========== Internal Operations ==========

int op_ADDI3_fn(JCC *vm) {
    // Add immediate: regs[rd] = regs[rs1] + immediate
    // Format: [ADDI3] [rd:8|rs1:8|unused:48] [immediate:64]
    // Used for struct offset calculations: struct_addr + offset
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);
    long long imm = cc_read_i64(vm);

    if (rd != REG_ZERO)
        vm->regs[rd] = vm->regs[rs1] + imm;
    return 0;
}

int op_NEG3_fn(JCC *vm) {
    // Integer negation: regs[rd] = -regs[rs1]
    // Format: [NEG3] [rd:8|rs1:8|unused:48]
    // Replaces PUSH/-1/MUL pattern for ND_NEG
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    if (rd != REG_ZERO)
        vm->regs[rd] = -vm->regs[rs1];
    return 0;
}

// ========== Register-Based Load/Store ==========

int op_LDR_B_fn(JCC *vm) {
    // Load byte (sign-extend): regs[rd] = *(char*)regs[rs]
    // Format: [LDR_B] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 1, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(char *)vm->regs[rs];
    return 0;
}

int op_LDR_H_fn(JCC *vm) {
    // Load halfword (sign-extend): regs[rd] = *(short*)regs[rs]
    // Format: [LDR_H] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 2, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(short *)vm->regs[rs];
    return 0;
}

int op_LDR_W_fn(JCC *vm) {
    // Load word (sign-extend): regs[rd] = *(int*)regs[rs]
    // Format: [LDR_W] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(int *)vm->regs[rs];
    return 0;
}

int op_LDR_D_fn(JCC *vm) {
    // Load dword: regs[rd] = *(long long*)regs[rs]
    // Format: [LDR_D] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(long long *)vm->regs[rs];
    return 0;
}

int op_STR_B_fn(JCC *vm) {
    // Store byte: *(char*)regs[rs] = regs[rd]
    // Format: [STR_B] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    *(char *)vm->regs[rs] = (char)vm->regs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 1, WATCH_WRITE);
    return 0;
}

int op_STR_H_fn(JCC *vm) {
    // Store halfword: *(short*)regs[rs] = regs[rd]
    // Format: [STR_H] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    *(short *)vm->regs[rs] = (short)vm->regs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 2, WATCH_WRITE);
    return 0;
}

int op_STR_W_fn(JCC *vm) {
    // Store word: *(int*)regs[rs] = regs[rd]
    // Format: [STR_W] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    *(int *)vm->regs[rs] = (int)vm->regs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_WRITE);
    return 0;
}

int op_STR_D_fn(JCC *vm) {
    // Store dword: *(long long*)regs[rs] = regs[rd]
    // Format: [STR_D] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    *(long long *)vm->regs[rs] = vm->regs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_WRITE);
    return 0;
}

int op_FLDR_fn(JCC *vm) {
    // Float load: fregs[rd] = *(double*)regs[rs]
    // Format: [FLDR] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_READ);
    vm->fregs[rd] = *(double *)vm->regs[rs];
    return 0;
}

int op_FSTR_fn(JCC *vm) {
    // Float store: *(double*)regs[rs] = fregs[rd]
    // Format: [FSTR] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    *(double *)vm->regs[rs] = vm->fregs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_WRITE);
    return 0;
}

int op_FLDR_F32_fn(JCC *vm) {
    // Float32 load: fregs[rd] = (double)*(float*)regs[rs]
    // Format: [FLDR_F32] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_READ);
    vm->fregs[rd] = (double)*(float *)vm->regs[rs];
    return 0;
}

int op_FSTR_F32_fn(JCC *vm) {
    // Float32 store: *(float*)regs[rs] = (float)fregs[rd]
    // Format: [FSTR_F32] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    *(float *)vm->regs[rs] = (float)vm->fregs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_WRITE);
    return 0;
}

int op_FROUND_F32_fn(JCC *vm) {
    // Float32 round: fregs[rd] = (float)fregs[rs]
    // Format: [FROUND_F32] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    vm->fregs[rd] = (double)(float)vm->fregs[rs];
    return 0;
}

int op_LEA3_fn(JCC *vm) {
    // Load effective address: regs[rd] = bp + immediate
    // Format: [LEA3] [rd:8|unused:56] [immediate:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(vm->bp + offset);
    return 0;
}

int op_I2F3_fn(JCC *vm) {
    // Int to float: fregs[rd] = (double)regs[rs]
    // Format: [I2F3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    vm->fregs[rd] = (double)vm->regs[rs];
    return 0;
}

int op_F2I3_fn(JCC *vm) {
    // Float to int: regs[rd] = (long long)fregs[rs]
    // Format: [F2I3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)vm->fregs[rs];
    return 0;
}

int op_FR2R_fn(JCC *vm) {
    // Float register to integer register (bit-pattern transfer, no conversion)
    // Format: [FR2R] [rd:8|rs:8|unused:48]
    // Copies the raw IEEE 754 bits of the double to an integer register
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO) {
        union {
            double d;
            long long ll;
        } conv;
        conv.d = vm->fregs[rs];
        vm->regs[rd] = conv.ll;
    }
    return 0;
}

int op_R2FR_fn(JCC *vm) {
    // Integer register to float register (bit-pattern transfer, no conversion)
    // Format: [R2FR] [rd:8|rs:8|unused:48]
    // Copies the raw bits from integer register to a double (reverse of FR2R)
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    union {
        double d;
        long long ll;
    } conv;
    conv.ll = vm->regs[rs];
    vm->fregs[rd] = conv.d;
    return 0;
}

int op_JZ3_fn(JCC *vm) {
    // Branch if zero: if (regs[rs] == 0) pc = target
    // Format: [JZ3] [rs:8|unused:56] [target:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    JCCPc target = cc_read_word(vm);

    if (vm->regs[rs] == 0)
        vm->pc = target;
    return 0;
}

int op_JNZ3_fn(JCC *vm) {
    // Branch if non-zero: if (regs[rs] != 0) pc = target
    // Format: [JNZ3] [rs:8|unused:56] [target:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    JCCPc target = cc_read_word(vm);

    if (vm->regs[rs] != 0)
        vm->pc = target;
    return 0;
}

int op_NOT3_fn(JCC *vm) {
    // Logical not: regs[rd] = !regs[rs]
    // Format: [NOT3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = !vm->regs[rs];
    return 0;
}

int op_BNOT3_fn(JCC *vm) {
    // Bitwise not: regs[rd] = ~regs[rs]
    // Format: [BNOT3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = ~vm->regs[rs];
    return 0;
}

// ========== Register-Based Safety Opcodes ==========

int op_CHKP3_fn(JCC *vm) {
    // Check pointer validity (register-based version of CHKP)
    // Format: [CHKP3] [rs:8|unused:56]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & JCC_POINTER_CHECKS)) {
        return 0;
    }

    if (ptr == 0) {
        printf("\n========== NULL POINTER DEREFERENCE ==========\n");
        printf("Attempted to dereference NULL pointer\n");
        printf("PC: 0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("============================================\n");
        return -1;
    }

    // Check if pointer is in heap range
    if (ptr >= (long long)vm->heap_seg && ptr < (long long)vm->heap_end) {
        // Find allocation header - need to search backwards
        AllocHeader *header = ((AllocHeader *)ptr) - 1;

        // Check if freed (UAF detection)
        if ((vm->flags & JCC_UAF_DETECTION) && header->magic == 0xDEADBEEF &&
            header->freed) {
            printf("\n========== USE-AFTER-FREE DETECTED ==========\n");
            printf("Attempted to access freed memory\n");
            printf("Address:     0x%llx\n", ptr);
            printf("Size:        %zu bytes\n", header->size);
            printf("Allocated at PC offset: %lld\n", header->alloc_pc);
            printf("Generation:  %d (freed)\n", header->generation);
            printf("Current PC:  0x%llx (offset: %lld)\n", (long long)vm->pc,
                   (long long)vm->pc);
            printf("============================================\n");
            return -1;
        }
    }

    return 0;
}

int op_CHKA3_fn(JCC *vm) {
    // Check pointer alignment (register-based version of CHKA)
    // Format: [CHKA3] [rs:8|unused:56] [alignment:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    size_t alignment = (size_t)cc_read_i64(vm);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & JCC_ALIGNMENT_CHECKS)) {
        return 0;
    }

    if (ptr == 0) {
        return 0; // NULL will be caught by CHKP3
    }

    if (alignment > 1 && (ptr % alignment) != 0) {
        printf("\n========== ALIGNMENT ERROR ==========\n");
        printf("Pointer is misaligned for type\n");
        printf("Address:       0x%llx\n", ptr);
        printf("Required alignment: %zu bytes\n", alignment);
        printf("Current PC:    0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("=====================================\n");
        return -1;
    }

    return 0;
}

int op_CHKT3_fn(JCC *vm) {
    // Check type on dereference (register-based version of CHKT)
    // Format: [CHKT3] [rs:8|unused:56] [expected_type:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    int expected_type = (int)cc_read_i64(vm);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & JCC_TYPE_CHECKS)) {
        return 0;
    }

    if (ptr == 0) {
        return 0; // NULL will be caught by CHKP3
    }

    // Skip check for void* (TY_VOID) and generic pointers (TY_PTR)
    if (expected_type == TY_VOID || expected_type == TY_PTR) {
        return 0;
    }

    // Only check heap allocations
    if (ptr >= (long long)vm->heap_seg && ptr < (long long)vm->heap_end) {
        AllocHeader *header = ((AllocHeader *)ptr) - 1;

        if (header->magic == 0xDEADBEEF) {
            int actual_type = header->type_kind;

            if (actual_type != TY_VOID && actual_type != TY_PTR) {
                if (actual_type != expected_type) {
                    const char *type_names[] = {
                        "void",        "bool", "char",    "short",
                        "int",         "long", "float",   "double",
                        "long double", "enum", "pointer", "function",
                        "array",       "vla",  "struct",  "union"};

                    const char *expected_name =
                        (expected_type >= 0 && expected_type < 16)
                            ? type_names[expected_type]
                            : "unknown";
                    const char *actual_name =
                        (actual_type >= 0 && actual_type < 16)
                            ? type_names[actual_type]
                            : "unknown";

                    printf("\n========== TYPE MISMATCH DETECTED ==========\n");
                    printf("Pointer type mismatch on dereference\n");
                    printf("Address:       0x%llx\n", ptr);
                    printf("Expected type: %s\n", expected_name);
                    printf("Actual type:   %s\n", actual_name);
                    printf("Allocated at PC offset: %lld\n", header->alloc_pc);
                    printf("Current PC:    0x%llx (offset: %lld)\n",
                           (long long)vm->pc,
                           (long long)vm->pc);
                    printf("============================================\n");
                    return -1;
                }
            }
        }
    }

    return 0;
}

// ========== Control Flow Opcodes ==========

int op_JMP_fn(JCC *vm) {
    vm->pc = cc_read_word(vm);
    return 0;
}

int op_CALL_fn(JCC *vm) {
    // Call subroutine: push return address to main stack and shadow stack
    JCCPc target = cc_read_word(vm);
    long long ret_addr = (long long)vm->pc;

    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = ret_addr;
    if (vm->flags & JCC_CFI) {
        *--vm->shadow_sp = ret_addr; // Also push to shadow stack for CFI
    }
    vm->pc = target;
    return 0;
}

int op_CALLI_fn(JCC *vm) {
    // Call indirect: function address in register (read from operand)
    long long operands = cc_read_word(vm);
    int rs = (int)(operands & 0xFF);
    long long ret_addr = (long long)vm->pc;
    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = ret_addr;
    if (vm->flags & JCC_CFI) {
        *--vm->shadow_sp = ret_addr;
    }
    JCCPc target = cc_byte_offset_to_pc(vm->regs[rs]);
    if (target == JCC_INVALID_PC || target > vm->text_ptr) {
        printf("error: invalid indirect call target: %lld\n", vm->regs[rs]);
        return -1;
    }
    vm->pc = target;
    return 0;
}

int op_CALLN_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rs = (int)(operands & 0xFF);
    JCCInstrWord meta = cc_read_word(vm);
    int actual_nargs = (int)(meta & 0xFFFF);
    int returns_double = (int)((meta >> 16) & 1);
    uint64_t double_arg_mask = (uint64_t)cc_read_i64(vm);

    long long target_value = vm->regs[rs];
    DynamicSymbol *sym = jcc_find_dynamic_symbol(vm, target_value);
    if (sym) {
        enum { CALLN_STACK_ARG_SLOTS = 32 };
        long long stack_args_buf[CALLN_STACK_ARG_SLOTS];
        long long *heap_args = NULL;
        long long *args = stack_args_buf;
        if (actual_nargs > CALLN_STACK_ARG_SLOTS) {
            heap_args = malloc((size_t)actual_nargs * sizeof(long long));
            if (!heap_args) {
                printf("error: failed to allocate args for native call\n");
                return -1;
            }
            args = heap_args;
        }

        int int_reg_idx = 0;
        int fp_reg_idx = 0;
        for (int i = 0; i < actual_nargs; i++) {
            if (i >= 8) {
                args[i] = vm->sp[i - 8];
            } else if (i < 64 && (double_arg_mask & (1ULL << i))) {
                union {
                    double d;
                    long long ll;
                } conv;
                conv.d = vm->fregs[FREG_A0 + fp_reg_idx++];
                args[i] = conv.ll;
            } else {
                args[i] = vm->regs[REG_A0 + int_reg_idx++];
            }
        }

        int rc = jcc_call_native_function(vm, sym->func_ptr, sym->name, args,
                                          actual_nargs, double_arg_mask,
                                          returns_double, 0, actual_nargs);
        free(heap_args);
        return rc;
    }

    long long ret_addr = (long long)vm->pc;
    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = ret_addr;
    if (vm->flags & JCC_CFI) {
        *--vm->shadow_sp = ret_addr;
    }
    JCCPc target = cc_byte_offset_to_pc(target_value);
    if (target == JCC_INVALID_PC || target > vm->text_ptr) {
        printf("error: invalid indirect call target: %lld\n", target_value);
        return -1;
    }
    vm->pc = target;
    return 0;
}

int op_JMPT_fn(JCC *vm) {
    JCCPc table_pc = cc_read_word(vm);
    JCCInstrWord count = cc_read_word(vm);
    JCCPc default_pc = cc_read_word(vm);
    long long index = vm->regs[REG_A0];
    if (index < 0 || index >= (long long)count) {
        vm->pc = default_pc;
        return 0;
    }
    vm->pc = vm->text_seg[table_pc + (JCCPc)index];
    return 0;
}

int op_JMPI_fn(JCC *vm) {
    // Jump indirect - address in register specified by operand
    // Format: [JMPI] [rs:8|unused:56]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    JCCPc target = cc_byte_offset_to_pc(vm->regs[rs]);
    if (target == JCC_INVALID_PC || target > vm->text_ptr) {
        printf("error: invalid indirect jump target: %lld\n", vm->regs[rs]);
        return -1;
    }
    vm->pc = target;
    return 0;
}

int op_ADJ_fn(JCC *vm) {
    vm->sp = vm->sp + cc_read_i64(vm);
    return 0;
}

int op_PSH3_fn(JCC *vm) {
    // Push register value onto stack: *--sp = regs[rs]
    // Format: [PSH3] [rs:8|unused:56]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    // Guard BEFORE the decrement: with PROT_NONE below the committed floor,
    // crossing without growing first produces a hard SIGSEGV.
    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = vm->regs[rs];
    return 0;
}

int op_POP3_fn(JCC *vm) {
    // Pop from stack into register: regs[rd] = *sp++
    // Format: [POP3] [rd:8|unused:56]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    vm->regs[rd] = *vm->sp++;
    return 0;
}

// ========== Type Conversion Opcodes ==========

int op_SX1_fn(JCC *vm) {
    // Sign extend 1 byte to 8 bytes: regs[rd] = (long long)(char)regs[rs]
    // Format: [SX1] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(char)vm->regs[rs];
    return 0;
}

int op_SX2_fn(JCC *vm) {
    // Sign extend 2 bytes to 8 bytes: regs[rd] = (long long)(short)regs[rs]
    // Format: [SX2] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(short)vm->regs[rs];
    return 0;
}

int op_SX4_fn(JCC *vm) {
    // Sign extend 4 bytes to 8 bytes: regs[rd] = (long long)(int)regs[rs]
    // Format: [SX4] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(int)vm->regs[rs];
    return 0;
}

int op_ZX1_fn(JCC *vm) {
    // Zero extend 1 byte to 8 bytes: regs[rd] = (long long)(unsigned
    // char)regs[rs] Format: [ZX1] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(unsigned char)vm->regs[rs];
    return 0;
}

int op_ZX2_fn(JCC *vm) {
    // Zero extend 2 bytes to 8 bytes: regs[rd] = (long long)(unsigned
    // short)regs[rs] Format: [ZX2] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(unsigned short)vm->regs[rs];
    return 0;
}

int op_ZX4_fn(JCC *vm) {
    // Zero extend 4 bytes to 8 bytes: regs[rd] = (long long)(unsigned
    // int)regs[rs] Format: [ZX4] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(unsigned int)vm->regs[rs];
    return 0;
}

// ========== Memory Allocation Opcodes ==========

int op_MALC_fn(JCC *vm) {
    // malloc: size in REG_A0, return pointer in REG_A0
    long long requested_size = vm->regs[REG_A0];
    if (requested_size <= 0) {
        vm->regs[REG_A0] = 0; // Return NULL for invalid size
        return 0;
    }

    // Align to 8-byte boundary
    size_t size = (requested_size + 7) & ~7;
    size_t total_size = size + sizeof(AllocHeader);

    // Check for OOM — try to grow the heap before giving up
    size_t available = (size_t)(vm->heap_end - vm->heap_ptr);
    if (total_size > available) {
        if (vm_heap_grow(vm, total_size) != 0) {
            vm->regs[REG_A0] = 0; // Out of memory (reservation exhausted)
            return 0;
        }
        available = (size_t)(vm->heap_end - vm->heap_ptr);
        if (total_size > available) {
            vm->regs[REG_A0] = 0;
            return 0;
        }
    }

    // Allocate from bump pointer
    AllocHeader *header = (AllocHeader *)vm->heap_ptr;
    header->size = size;
    header->requested_size = requested_size;
    header->magic = 0xDEADBEEF;
    header->freed = 0;
    header->generation = 0;
    header->alloc_pc = vm->text_seg ? (long long)vm->pc : 0;
    header->type_kind = TY_VOID;

    vm->heap_ptr = vm->heap_ptr + total_size;
    vm->regs[REG_A0] = (long long)(header + 1); // Return pointer after header

    if (vm->debug_vm) {
        printf("MALC: allocated %zu bytes at 0x%llx\n", size, vm->regs[REG_A0]);
    }
    return 0;
}

int op_MFRE_fn(JCC *vm) {
    // free: pointer in REG_A0
    void *ptr = (void *)vm->regs[REG_A0];
    if (!ptr) {
        return 0; // free(NULL) is a no-op
    }

    AllocHeader *header = ((AllocHeader *)ptr) - 1;

    // Validate header
    if (header->magic != 0xDEADBEEF) {
        printf("\n========== INVALID FREE ==========\n");
        printf("Pointer does not appear to be from malloc: 0x%llx\n",
               (long long)ptr);
        printf("===================================\n");
        return -1;
    }

    if (header->freed) {
        printf("\n========== DOUBLE FREE ==========\n");
        printf("Pointer already freed: 0x%llx\n", (long long)ptr);
        printf("===================================\n");
        return -1;
    }

    header->freed = 1;
    header->generation++;

    if (vm->debug_vm) {
        printf("MFRE: freed pointer 0x%llx\n", (long long)ptr);
    }
    return 0;
}

int op_MCPY_fn(JCC *vm) {
    // memcpy: dest in REG_A0, src in REG_A1, count in REG_A2
    void *dest = (void *)vm->regs[REG_A0];
    void *src = (void *)vm->regs[REG_A1];
    size_t count = (size_t)vm->regs[REG_A2];
    memcpy(dest, src, count);
    return 0;
}

int op_REALC_fn(JCC *vm) {
    // realloc: ptr in REG_A0, new_size in REG_A1, return in REG_A0
    void *ptr = (void *)vm->regs[REG_A0];
    long long new_size = vm->regs[REG_A1];

    if (!ptr) {
        // realloc(NULL, size) == malloc(size)
        vm->regs[REG_A0] = new_size;
        return op_MALC_fn(vm);
    }

    if (new_size <= 0) {
        // realloc(ptr, 0) == free(ptr)
        op_MFRE_fn(vm);
        vm->regs[REG_A0] = 0;
        return 0;
    }

    AllocHeader *old_header = ((AllocHeader *)ptr) - 1;
    size_t old_size = old_header->size;

    // Allocate new block
    vm->regs[REG_A0] = new_size;
    int result = op_MALC_fn(vm);
    if (result != 0 || vm->regs[REG_A0] == 0) {
        return result;
    }

    // Copy data
    void *new_ptr = (void *)vm->regs[REG_A0];
    size_t copy_size =
        old_size < (size_t)new_size ? old_size : (size_t)new_size;
    memcpy(new_ptr, ptr, copy_size);

    // Free old block
    old_header->freed = 1;
    old_header->generation++;

    // Result already in REG_A0
    return 0;
}

int op_CALC_fn(JCC *vm) {
    // calloc: nmemb in REG_A0, size in REG_A1, return in REG_A0
    long long nmemb = vm->regs[REG_A0];
    long long size = vm->regs[REG_A1];
    long long total = nmemb * size;

    vm->regs[REG_A0] = total;
    int result = op_MALC_fn(vm);
    if (result != 0 || vm->regs[REG_A0] == 0) {
        return result;
    }

    // Zero the memory
    memset((void *)vm->regs[REG_A0], 0, total);
    return 0;
}

// ========== Safety Opcodes ==========

int op_CHKB_fn(JCC *vm) {
    // Check array bounds.
    // Format: [CHKB] [rs1:base, rs2:scaled_offset] (RR operand word)
    // rs1 = base pointer, rs2 = scaled byte offset (index * element_size)
    long long operands = cc_read_word(vm);
    int rs1, rs2;
    DECODE_RR(operands, rs1, rs2);

    if (!(vm->flags & JCC_BOUNDS_CHECKS))
        return 0;

    long long base         = vm->regs[rs1];
    long long scaled_offset = vm->regs[rs2];

    if (scaled_offset < 0) {
        printf("\n========== ARRAY BOUNDS ERROR ==========\n");
        printf("Negative array index (scaled offset: %lld)\n", scaled_offset);
        printf("Base address: 0x%llx\n", base);
        printf("PC: 0x%llx (offset: %lld)\n",
               (long long)vm->pc, (long long)vm->pc);
        printf("=========================================\n");
        return -1;
    }

    if (base >= (long long)vm->heap_seg && base < (long long)vm->heap_end) {
        AllocHeader *header = ((AllocHeader *)base) - 1;
        if (header->magic == 0xDEADBEEF) {
            if (scaled_offset >= (long long)header->size) {
                printf("\n========== ARRAY BOUNDS ERROR ==========\n");
                printf("Array index out of bounds\n");
                printf("Scaled offset: %lld bytes\n", scaled_offset);
                printf("Array size:    %zu bytes\n", header->size);
                printf("Base address:  0x%llx\n", base);
                printf("Allocated at PC offset: %lld\n", header->alloc_pc);
                printf("PC: 0x%llx (offset: %lld)\n",
                       (long long)vm->pc, (long long)vm->pc);
                printf("=========================================\n");
                return -1;
            }
        }
    }
    return 0;
}

int op_CHKI_fn(JCC *vm) {
    // Check initialization: fail if variable at bp+offset has not been written.
    // Format: [CHKI] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & JCC_UNINIT_DETECTION))
        return 0;

    long long addr = (long long)(vm->bp + offset);
    if (!hashmap_get_int(&vm->init_state, addr)) {
        printf("\n========== UNINITIALIZED VARIABLE READ ==========\n");
        printf("Attempted to read uninitialized variable\n");
        printf("Stack offset: %lld\n", offset);
        printf("Address:      0x%llx\n", addr);
        printf("BP:           0x%llx\n", (long long)vm->bp);
        printf("PC:           0x%llx (offset: %lld)\n",
               (long long)vm->pc, (long long)vm->pc);
        printf("================================================\n");
        return -1;
    }
    return 0;
}

int op_MARKI_fn(JCC *vm) {
    // Mark variable at bp+offset as initialized.
    // Format: [MARKI] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & JCC_UNINIT_DETECTION))
        return 0;

    long long addr = (long long)(vm->bp + offset);
    hashmap_put_int(&vm->init_state, addr, (void *)1);
    return 0;
}

int op_MARKA_fn(JCC *vm) {
    // Mark a stack address for dangling-pointer detection.
    // Format: [MARKA] [rs:ptr] [offset:i64] [size:i64] [scope_id:i64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    long long offset   = cc_read_i64(vm);
    size_t    size     = (size_t)cc_read_i64(vm);
    int       scope_id = (int)cc_read_word(vm);

    if (!(vm->flags & JCC_DANGLING_DETECT) && !(vm->flags & JCC_STACK_INSTR))
        return 0;

    long long ptr = vm->regs[rs];

    if (vm->debug_vm) {
        printf("MARKA: tracking ptr 0x%llx (bp=0x%llx offset=%lld size=%zu scope=%d)\n",
               ptr, (long long)vm->bp, offset, size, scope_id);
    }

    StackPtrInfo *info = malloc(sizeof(StackPtrInfo));
    if (!info) {
        fprintf(stderr, "MARKA: failed to allocate StackPtrInfo\n");
        return 0;
    }
    info->bp       = (long long)vm->bp;
    info->offset   = offset;
    info->size     = size;
    info->scope_id = scope_id;
    hashmap_put_int(&vm->stack_ptrs, ptr, info);
    return 0;
}

int op_CHKPA_fn(JCC *vm) {
    // Check pointer arithmetic result against its recorded provenance.
    // Format: [CHKPA] [rs:ptr_result]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);

    if (!(vm->flags & JCC_INVALID_ARITH) || !(vm->flags & JCC_PROVENANCE_TRACK))
        return 0;

    long long ptr = vm->regs[rs];
    if (ptr == 0)
        return 0;

    ProvenanceInfo *info = (ProvenanceInfo *)hashmap_get_int(&vm->provenance, ptr);
    if (info) {
        long long end = info->base + (long long)info->size;
        if (ptr < info->base || ptr > end) {
            const char *origins[] = {"HEAP", "STACK", "GLOBAL"};
            printf("\n========== INVALID POINTER ARITHMETIC ==========\n");
            printf("Pointer arithmetic result outside object bounds\n");
            printf("Origin:      %s\n", origins[info->origin_type]);
            printf("Object base: 0x%llx\n", info->base);
            printf("Object size: %zu bytes\n", info->size);
            printf("Result ptr:  0x%llx\n", ptr);
            printf("Offset:      %lld bytes from base\n", ptr - info->base);
            printf("PC:          0x%llx (offset: %lld)\n",
                   (long long)vm->pc, (long long)vm->pc);
            printf("===============================================\n");
            return -1;
        }
    }
    return 0;
}

int op_MARKP_fn(JCC *vm) {
    // Record provenance for a pointer (origin, base, size).
    // Format: [MARKP] [rs_ptr:8 | rs_base:8] [origin_type:i64] [size:i64]
    long long operands = cc_read_word(vm);
    int rs_ptr, rs_base;
    DECODE_RR(operands, rs_ptr, rs_base);
    int    origin_type = (int)cc_read_word(vm);
    size_t size        = (size_t)cc_read_i64(vm);

    if (!(vm->flags & JCC_PROVENANCE_TRACK))
        return 0;

    long long ptr  = vm->regs[rs_ptr];
    long long base = vm->regs[rs_base];

    ProvenanceInfo *info = malloc(sizeof(ProvenanceInfo));
    if (!info)
        return 0;
    info->origin_type = origin_type;
    info->base        = base;
    info->size        = size;
    hashmap_put_int(&vm->provenance, ptr, info);
    return 0;
}

int op_SCOPEIN_fn(JCC *vm) {
    // Activate all stack variables belonging to scope_id.
    // Format: [SCOPEIN] [scope_id:i64]
    int scope_id = (int)cc_read_word(vm);

    if (!(vm->flags & JCC_STACK_INSTR))
        return 0;

    if (vm->debug_vm)
        printf("SCOPEIN: entering scope %d (bp=0x%llx)\n", scope_id, (long long)vm->bp);

    // PLACEHOLDER: O(capacity) full hashmap scan on every scope entry. The
    // VM already maintains vm->scope_vars per-scope linked lists during
    // codegen; iterate that list (scope_vars[scope_id].head) for O(vars in
    // scope) instead of O(total tracked vars).
    // Ticket: https://todo.sr.ht/~takeiteasy/jcc/159
    for (int i = 0; i < vm->stack_var_meta.capacity; i++) {
        HashEntry *ent = &vm->stack_var_meta.buckets[i];
        if (!ent->key || ent->key == (char *)-1)
            continue;
        StackVarMeta *meta = (StackVarMeta *)ent->val;
        if (meta && meta->scope_id == scope_id) {
            meta->is_alive = 1;
            meta->bp       = (long long)vm->bp;
            if (vm->debug_vm)
                printf("  Activated '%s' at bp%+lld\n", meta->name, meta->offset);
        }
    }
    return 0;
}

int op_SCOPEOUT_fn(JCC *vm) {
    // Deactivate variables in scope_id and detect dangling pointers.
    // Format: [SCOPEOUT] [scope_id:i64]
    int scope_id = (int)cc_read_word(vm);

    if (!(vm->flags & JCC_STACK_INSTR))
        return 0;

    if (vm->debug_vm)
        printf("SCOPEOUT: exiting scope %d (bp=0x%llx)\n", scope_id, (long long)vm->bp);

    // PLACEHOLDER: O(capacity) full hashmap scan on every scope exit; same
    // fix as SCOPEIN — walk vm->scope_vars[scope_id] directly.
    // Ticket: https://todo.sr.ht/~takeiteasy/jcc/159
    for (int i = 0; i < vm->stack_var_meta.capacity; i++) {
        HashEntry *ent = &vm->stack_var_meta.buckets[i];
        if (!ent->key || ent->key == (char *)-1)
            continue;
        StackVarMeta *meta = (StackVarMeta *)ent->val;
        if (meta && meta->scope_id == scope_id && meta->bp == (long long)vm->bp) {
            meta->is_alive = 0;
            if (vm->debug_vm)
                printf("  Deactivated '%s' at bp%+lld (reads=%lld writes=%lld)\n",
                       meta->name, meta->offset, meta->read_count, meta->write_count);
        }
    }

    if (vm->flags & JCC_DANGLING_DETECT) {
        for (int i = 0; i < vm->stack_ptrs.capacity; i++) {
            HashEntry *ent = &vm->stack_ptrs.buckets[i];
            if (!ent->key || ent->key == (char *)-1)
                continue;
            StackPtrInfo *ptr_info = (StackPtrInfo *)ent->val;
            if (ptr_info && ptr_info->scope_id == scope_id &&
                ptr_info->bp == (long long)vm->bp) {
                if (vm->flags & JCC_STACK_INSTR_ERRORS) {
                    printf("\n========== DANGLING POINTER DETECTED ==========\n");
                    printf("Pointer to stack variable in scope %d still exists\n", scope_id);
                    printf("BP offset: %lld\n", ptr_info->offset);
                    printf("Scope is now exiting — this pointer will dangle\n");
                    printf("PC: 0x%llx (offset: %lld)\n",
                           (long long)vm->pc, (long long)vm->pc);
                    printf("==============================================\n");
                    return -1;
                } else if (vm->debug_vm) {
                    printf("WARNING: Dangling pointer detected for scope %d\n", scope_id);
                }
            }
        }
    }
    return 0;
}

int op_CHKL_fn(JCC *vm) {
    // Check variable liveness before access (use-after-scope / use-after-return).
    // Format: [CHKL] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & JCC_STACK_INSTR))
        return 0;

    StackVarMeta *meta =
        (StackVarMeta *)hashmap_get_int(&vm->stack_var_meta, offset);
    if (!meta)
        return 0;

    if (meta->bp != (long long)vm->bp && meta->bp != 0) {
        if (vm->flags & JCC_STACK_INSTR_ERRORS) {
            printf("\n========== USE AFTER RETURN DETECTED ==========\n");
            printf("Variable '%s' at bp%+lld accessed after function return\n",
                   meta->name, meta->offset);
            printf("Variable BP:  0x%llx\n", meta->bp);
            printf("Current BP:   0x%llx\n", (long long)vm->bp);
            printf("PC:           0x%llx (offset: %lld)\n",
                   (long long)vm->pc, (long long)vm->pc);
            printf("==============================================\n");
            return -1;
        }
    }

    if (!meta->is_alive) {
        if (vm->flags & JCC_STACK_INSTR_ERRORS) {
            printf("\n========== USE AFTER SCOPE DETECTED ==========\n");
            printf("Variable '%s' at bp%+lld accessed after scope exit\n",
                   meta->name, meta->offset);
            printf("Scope ID: %d\n", meta->scope_id);
            printf("PC:       0x%llx (offset: %lld)\n",
                   (long long)vm->pc, (long long)vm->pc);
            printf("=============================================\n");
            return -1;
        } else if (vm->debug_vm) {
            printf("WARNING: Variable '%s' accessed after scope exit\n", meta->name);
        }
    }
    return 0;
}

int op_MARKR_fn(JCC *vm) {
    // Record a read access to the variable at bp+offset.
    // Format: [MARKR] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & JCC_STACK_INSTR))
        return 0;

    StackVarMeta *meta =
        (StackVarMeta *)hashmap_get_int(&vm->stack_var_meta, offset);
    if (meta && meta->bp == (long long)vm->bp) {
        meta->read_count++;
        if (vm->debug_vm)
            printf("MARKR: '%s' read (count=%lld)\n", meta->name, meta->read_count);
    }
    return 0;
}

int op_MARKW_fn(JCC *vm) {
    // Record a write access to the variable at bp+offset; marks it initialized.
    // Format: [MARKW] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & JCC_STACK_INSTR))
        return 0;

    StackVarMeta *meta =
        (StackVarMeta *)hashmap_get_int(&vm->stack_var_meta, offset);
    if (meta && meta->bp == (long long)vm->bp) {
        meta->write_count++;
        if (!meta->initialized)
            meta->initialized = 1;
        if (vm->debug_vm)
            printf("MARKW: '%s' write (count=%lld)\n", meta->name, meta->write_count);
    }
    return 0;
}

// ========== Setjmp/Longjmp ==========

int op_SETJMP_fn(JCC *vm) {
    // setjmp: jmp_buf address in REG_A0, return 0 in REG_A0
    long long *jmp_buf = (long long *)vm->regs[REG_A0];
    jmp_buf[0] = (long long)vm->pc;
    jmp_buf[1] = (long long)vm->sp;
    jmp_buf[2] = (long long)vm->bp;
    if (vm->flags & JCC_CFI)
        jmp_buf[3] = (long long)((char *)vm->shadow_sp - (char *)vm->shadow_stack);
    else
        jmp_buf[3] = -1;
    vm->regs[REG_A0] = 0; // setjmp returns 0 on direct call
    return 0;
}

int op_LONGJMP_fn(JCC *vm) {
    // longjmp: jmp_buf address in REG_A0, value in REG_A1
    long long *jmp_buf = (long long *)vm->regs[REG_A0];
    long long val = vm->regs[REG_A1];
    vm->pc = (JCCPc)jmp_buf[0];
    vm->sp = (long long *)jmp_buf[1];
    vm->bp = (long long *)jmp_buf[2];
    if (vm->flags & JCC_CFI) {
        long long saved_offset = jmp_buf[3];
        size_t reserved_stack = (size_t)vm->poolsize_max * sizeof(long long);
        if (saved_offset < 0 || (size_t)saved_offset > reserved_stack ||
            saved_offset % (long long)sizeof(long long) != 0)
            return -1; // corrupted or non-CFI jmp_buf used under CFI
        vm->shadow_sp = (long long *)((char *)vm->shadow_stack + saved_offset);
    }
    vm->regs[REG_A0] = val ? val : 1; // Return value (never 0)
    return 0;
}

int op_DLOPEN_fn(JCC *vm) {
    const char *path = (const char *)vm->regs[REG_A0];
    int mode = (int)vm->regs[REG_A1];
    vm->regs[REG_A0] = jcc_rt_dlopen(vm, path, mode);
    return 0;
}

int op_DLSYM_fn(JCC *vm) {
    long long handle = vm->regs[REG_A0];
    const char *symbol = (const char *)vm->regs[REG_A1];
    vm->regs[REG_A0] = jcc_rt_dlsym(vm, handle, symbol);
    return 0;
}

int op_DLCLOSE_fn(JCC *vm) {
    vm->regs[REG_A0] = jcc_rt_dlclose(vm, vm->regs[REG_A0]);
    return 0;
}

int op_DLERROR_fn(JCC *vm) {
    vm->regs[REG_A0] = jcc_rt_dlerror(vm);
    return 0;
}

// ========== FFI ==========

int jcc_ffi_name_in_list(char **list, int count, const char *name) {
    if (!name)
        name = "<anonymous>";
    size_t len = strlen(name);
    for (int i = 0; i < count; i++) {
        if (strlen(list[i]) == len && memcmp(list[i], name, len) == 0)
            return 1;
    }
    return 0;
}

static int jcc_handle_ffi_policy_error(JCC *vm, const char *kind,
                                       const char *name,
                                       const char *details) {
    if (!name)
        name = "<anonymous>";
    printf("\n========== FFI SAFETY ERROR ==========\n");
    printf("Error type: %s\n", kind);
    printf("Function:   %s\n", name);
    printf("Details:    %s\n", details);
    printf("PC offset:  %u\n", (unsigned)vm->pc);
    printf("======================================\n");
    vm->regs[REG_A0] = 0;
    vm->fregs[FREG_A0] = 0.0;
    return vm->ffi_errors_fatal ? -1 : 0;
}

static int jcc_check_ffi_policy(JCC *vm, const char *name, int actual_nargs,
                                int is_variadic, int num_fixed_args) {
    if (vm->disable_all_ffi)
        return jcc_handle_ffi_policy_error(
            vm, "FFI Disabled", name,
            "All FFI calls are disabled via --disable-ffi");

    if (vm->ffi_allow_count > 0 &&
        !jcc_ffi_name_in_list(vm->ffi_allow_list, vm->ffi_allow_count, name))
        return jcc_handle_ffi_policy_error(vm, "FFI Access Denied", name,
                                           "Function not in allow list");

    if (vm->ffi_allow_count == 0 &&
        jcc_ffi_name_in_list(vm->ffi_deny_list, vm->ffi_deny_count, name))
        return jcc_handle_ffi_policy_error(vm, "FFI Access Denied", name,
                                           "Function in deny list");

    if (vm->enable_ffi_type_checking) {
        if (is_variadic) {
            if (actual_nargs < num_fixed_args) {
                printf("error: FFI function '%s': argument count mismatch "
                       "(requires at least %d, called with %d)\n",
                       name ? name : "<anonymous>", num_fixed_args,
                       actual_nargs);
                return -1;
            }
        } else if (actual_nargs != num_fixed_args) {
            printf("error: FFI function '%s': argument count mismatch "
                   "(requires %d, called with %d)\n",
                   name ? name : "<anonymous>", num_fixed_args, actual_nargs);
            return -1;
        }
    }

    return 1;
}

int jcc_call_native_function(JCC *vm, void *func_ptr, const char *name,
                             long long *args, int actual_nargs,
                             uint64_t double_arg_mask, int returns_double,
                             int is_variadic, int num_fixed_args) {
    if (!func_ptr) {
        printf("error: native function '%s' not resolved\n",
               name ? name : "<anonymous>");
        return -1;
    }

    int policy = jcc_check_ffi_policy(vm, name, actual_nargs, is_variadic,
                                      num_fixed_args);
    if (policy <= 0)
        return policy;

    ffi_cif cif;
    ffi_type **arg_types = NULL;
    ffi_type *return_type =
        returns_double ? &ffi_type_double : &ffi_type_sint64;

    if (actual_nargs > 0) {
        arg_types = malloc((size_t)actual_nargs * sizeof(ffi_type *));
        if (!arg_types) {
            printf("error: failed to allocate arg types for FFI\n");
            return -1;
        }
        for (int i = 0; i < actual_nargs; i++)
            arg_types[i] = (i < 64 && (double_arg_mask & (1ULL << i)))
                               ? &ffi_type_double
                               : &ffi_type_sint64;
    }

    ffi_status status;
    if (is_variadic) {
        status = ffi_prep_cif_var(&cif, FFI_DEFAULT_ABI,
                                  (unsigned int)num_fixed_args,
                                  (unsigned int)actual_nargs, return_type,
                                  arg_types);
    } else {
        status = ffi_prep_cif(&cif, FFI_DEFAULT_ABI,
                              (unsigned int)actual_nargs, return_type,
                              arg_types);
    }

    if (status != FFI_OK) {
        printf("error: failed to prepare FFI cif (status=%d)\n", status);
        free(arg_types);
        return -1;
    }

    void **arg_ptrs =
        malloc((size_t)(actual_nargs > 0 ? actual_nargs : 1) * sizeof(void *));
    if (!arg_ptrs) {
        printf("error: failed to allocate arg pointers for FFI\n");
        free(arg_types);
        return -1;
    }
    for (int i = 0; i < actual_nargs; i++)
        arg_ptrs[i] = &args[i];

    if (returns_double) {
        double result;
        ffi_call(&cif, FFI_FN(func_ptr), &result, arg_ptrs);
        vm->fregs[FREG_A0] = result;
    } else {
        long long result;
        ffi_call(&cif, FFI_FN(func_ptr), &result, arg_ptrs);
        vm->regs[REG_A0] = result;
    }

    free(arg_ptrs);
    free(arg_types);
    return 0;
}

int op_CALLF_fn(JCC *vm) {
    // Foreign function call using register-based calling convention
    // Operands: [ffi_idx, nargs, double_arg_mask]
    int func_idx = (int)cc_read_word(vm);
    int actual_nargs = (int)cc_read_word(vm);
    uint64_t double_arg_mask = (uint64_t)cc_read_i64(vm);

    if (func_idx < 0 || func_idx >= vm->compiler.ffi_count) {
        printf("error: invalid FFI function index: %d\n", func_idx);
        return -1;
    }

    ForeignFunc *ff = &vm->compiler.ffi_table[func_idx];
    if (!ff->func_ptr) {
        printf("error: FFI function '%s' not resolved\n", ff->name);
        return -1;
    }

    if (vm->debug_vm)
        printf("CALLF: calling %s with %d args (fixed: %d, variadic: %d, "
               "double_mask: 0x%llx)\n",
               ff->name, actual_nargs, ff->num_fixed_args, ff->is_variadic,
               (unsigned long long)double_arg_mask);

    enum { CALLF_STACK_ARG_SLOTS = 32 };
    long long stack_args_buf[CALLF_STACK_ARG_SLOTS];
    long long *heap_args = NULL;
    long long *args = stack_args_buf;
    if (actual_nargs > CALLF_STACK_ARG_SLOTS) {
        heap_args = malloc((size_t)actual_nargs * sizeof(long long));
        if (!heap_args) {
            printf("error: failed to allocate args for FFI\n");
            return -1;
        }
        args = heap_args;
    }

    for (int i = 0; i < actual_nargs; i++) {
        if (i < 8)
            args[i] = vm->regs[REG_A0 + i];
        else
            args[i] = vm->sp[i - 8];

        if (vm->debug_vm)
            printf("  arg[%d] = 0x%llx (%lld) [%s]\n", i, args[i], args[i],
                   (i < 64 && (double_arg_mask & (1ULL << i))) ? "double"
                                                                : "int");
    }

    int rc = jcc_call_native_function(vm, ff->func_ptr, ff->name, args,
                                      actual_nargs, double_arg_mask,
                                      ff->returns_double, ff->is_variadic,
                                      ff->num_fixed_args);
    free(heap_args);
    return rc;
}

// ========== Struct Return Buffer Support ==========

int op_RETBUF_fn(JCC *vm) {
    // Get next return buffer from rotating pool at runtime
    // This ensures chained struct-returning calls (e.g., f(g(), h()))
    // get different buffers automatically
    int count = vm->compiler.return_buffer_count;
    if (count <= 0 || count > RETURN_BUFFER_POOL_SIZE) {
        printf("error: invalid return buffer pool metadata\n");
        return -1;
    }
    int idx = vm->runtime_return_buffer_index % count;
    if (!vm->compiler.return_buffer_pool[idx]) {
        printf("error: return buffer pool was not rehydrated\n");
        return -1;
    }
    vm->runtime_return_buffer_index = (idx + 1) % count;
    vm->regs[REG_A0] = (long long)vm->compiler.return_buffer_pool[idx];
    return 0;
}

int op_BTRAP_fn(JCC *vm) {
    printf("\n========== UNREACHABLE EXECUTED ==========\n");
    printf("__builtin_unreachable() or TRAP was executed at PC %u\n", vm->pc);
    printf("This code path should never be reached.\n");
    printf("==========================================\n");
    return -1;
}

// ========== Bit-Manipulation Builtins ==========

int op_CLZ_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long width = cc_read_i64(vm);
    long long val = vm->regs[rs];
    long long result;
    if (width <= 32)
        result = (val == 0) ? 32 : __builtin_clz((unsigned int)(uint32_t)val);
    else
        result = (val == 0) ? 64 : __builtin_clzll((unsigned long long)val);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_CTZ_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long width = cc_read_i64(vm);
    long long val = vm->regs[rs];
    long long result;
    if (width <= 32)
        result = (val == 0) ? 32 : __builtin_ctz((unsigned int)(uint32_t)val);
    else
        result = (val == 0) ? 64 : __builtin_ctzll((unsigned long long)val);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_POPCOUNT_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long result = __builtin_popcountll((unsigned long long)vm->regs[rs]);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_FFS_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long width = cc_read_i64(vm);
    long long val = vm->regs[rs];
    long long result;
    if (width <= 32)
        result = __builtin_ffs((int)(int32_t)val); // returns 0 for 0, spec-defined
    else
        result = __builtin_ffsll((long long)val);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

int op_BSWAP_fn(JCC *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long byte_width = cc_read_i64(vm);
    long long val = vm->regs[rs];
    long long result;
    if (byte_width <= 2)
        result = (long long)__builtin_bswap16((uint16_t)val);
    else if (byte_width <= 4)
        result = (long long)__builtin_bswap32((uint32_t)val);
    else
        result = (long long)__builtin_bswap64((uint64_t)val);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

// ========== Checked Arithmetic Builtins ==========

int op_IOVFL_fn(JCC *vm) {
    // Operand: (op_type << 8) | (size_bytes << 1) | is_unsigned
    // op_type: 0=add, 1=sub, 2=mul
    // Inputs: a=regs[REG_A0], b=regs[REG_A1], ptr=regs[REG_A2]
    // Output: overflow bool in regs[REG_A0]; result stored via ptr
    long long packed = cc_read_i64(vm);
    int op_type  = (int)((packed >> 8) & 0xFF);
    int size_enc = (int)(packed & 0xFF);
    int nbytes   = (size_enc >> 1) & 0x7F;
    int unsign   = size_enc & 1;

    long long a   = vm->regs[REG_A0];
    long long b   = vm->regs[REG_A1];
    void     *ptr = (void *)vm->regs[REG_A2];

    int overflow = 0;
    long long result = 0;

    if (!unsign) {
        // Signed overflow: compute in 64-bit, check fit
        switch (op_type) {
        case 0: result = a + b; break;
        case 1: result = a - b; break;
        case 2: result = a * b; break;
        }
        if (nbytes <= 1) {
            overflow = (result < -128 || result > 127);
            if (ptr) *(int8_t *)ptr = (int8_t)result;
        } else if (nbytes <= 2) {
            overflow = (result < -32768 || result > 32767);
            if (ptr) *(int16_t *)ptr = (int16_t)result;
        } else if (nbytes <= 4) {
            overflow = (result < (long long)INT32_MIN || result > (long long)INT32_MAX);
            if (ptr) *(int32_t *)ptr = (int32_t)result;
        } else {
            // 64-bit signed: use host overflow detection
            long long r64;
            if (op_type == 0)
                overflow = __builtin_add_overflow(a, b, &r64);
            else if (op_type == 1)
                overflow = __builtin_sub_overflow(a, b, &r64);
            else
                overflow = __builtin_mul_overflow(a, b, &r64);
            result = r64;
            if (ptr) *(int64_t *)ptr = r64;
        }
    } else {
        // Unsigned overflow: compute in 64-bit with masking
        unsigned long long ua = (unsigned long long)a;
        unsigned long long ub = (unsigned long long)b;
        unsigned long long ur;
        switch (op_type) {
        case 0: ur = ua + ub; break;
        case 1: ur = ua - ub; break;
        default: ur = ua * ub; break;
        }
        unsigned long long mask = (nbytes >= 8) ? UINT64_MAX :
                                  ((1ULL << (nbytes * 8)) - 1ULL);
        unsigned long long truncated = ur & mask;
        if (nbytes <= 1)      { overflow = (ur != truncated); if (ptr) *(uint8_t *)ptr  = (uint8_t)truncated;  }
        else if (nbytes <= 2) { overflow = (ur != truncated); if (ptr) *(uint16_t *)ptr = (uint16_t)truncated; }
        else if (nbytes <= 4) { overflow = (ur != truncated); if (ptr) *(uint32_t *)ptr = (uint32_t)truncated; }
        else                  {
            unsigned long long r64u;
            if (op_type == 0)
                overflow = __builtin_add_overflow(ua, ub, &r64u);
            else if (op_type == 1)
                overflow = __builtin_sub_overflow(ua, ub, &r64u);
            else
                overflow = __builtin_mul_overflow(ua, ub, &r64u);
            if (ptr) *(uint64_t *)ptr = r64u;
        }
        result = (long long)truncated;
    }

    vm->regs[REG_A0] = overflow ? 1 : 0;
    return 0;
}
