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

/*
 * Instruction encoding:
 *   RRR format: [OPCODE] [rd:8|rs1:8|rs2:8|unused:40]
 *   RI format:  [OPCODE] [rd:8|unused:56] [immediate:64]
 */

#include "./internal.h"


// ========== Arithmetic Operations ==========

static inline int op_ADD3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    if (rd != REG_ZERO)
        vm->regs[rd] =
            (long long)((unsigned long long)a + (unsigned long long)b);
    return 0;
}

static inline int op_SUB3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    if (rd != REG_ZERO)
        vm->regs[rd] =
            (long long)((unsigned long long)a - (unsigned long long)b);
    return 0;
}

static inline int op_MUL3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    if (rd != REG_ZERO)
        vm->regs[rd] =
            (long long)((unsigned long long)a * (unsigned long long)b);
    return 0;
}

static inline int op_MULI3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);
    unsigned long long a = (unsigned long long)vm->regs[rs1];
    unsigned long long imm = (unsigned long long)cc_read_i64(vm);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(a * imm);
    return 0;
}

static inline int op_MULADD3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    unsigned long long addend = (unsigned long long)vm->regs[rs1];
    unsigned long long lhs = (unsigned long long)vm->regs[rs2];
    unsigned long long rhs = (unsigned long long)vm->regs[rs3];

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(addend + lhs * rhs);
    return 0;
}

static inline int op_MULADDI3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long base = (unsigned long long)vm->regs[rs1];
    unsigned long long index = (unsigned long long)vm->regs[rs2];
    unsigned long long imm = (unsigned long long)cc_read_i64(vm);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(base + index * imm);
    return 0;
}

static inline int op_DIV3_fn(VirtualMachine *vm) {
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

    if (rd != REG_ZERO)
        vm->regs[rd] = (a == LLONG_MIN && b == -1) ? LLONG_MIN : a / b;
    return 0;
}

static inline int op_ADDC_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    if ((b > 0 && a > LLONG_MAX - b) || (b < 0 && a < LLONG_MIN - b)) {
        printf("\n========== INTEGER OVERFLOW ==========\n");
        printf("Addition overflow detected\n");
        printf("Operands: %lld + %lld\n", a, b);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a + b;
    return 0;
}

static inline int op_SUBC_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    if ((b < 0 && a > LLONG_MAX + b) || (b > 0 && a < LLONG_MIN + b)) {
        printf("\n========== INTEGER OVERFLOW ==========\n");
        printf("Subtraction overflow detected\n");
        printf("Operands: %lld - %lld\n", a, b);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = a - b;
    return 0;
}

static inline int op_MULC_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];
    long long result;

    if (__builtin_mul_overflow(a, b, &result)) {
        printf("\n========== INTEGER OVERFLOW ==========\n");
        printf("Multiplication overflow detected\n");
        printf("Operands: %lld * %lld\n", a, b);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_DIVC_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long a = vm->regs[rs1];
    long long b = vm->regs[rs2];

    if (b == 0) {
        printf("\n========== DIVISION BY ZERO ==========\n");
        printf("Attempted division by zero\n");
        printf("Operands: %lld / 0\n", a);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

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

static inline int op_MOD3_fn(VirtualMachine *vm) {
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

static inline int op_AND3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] & vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_OR3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] | vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_XOR3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] ^ vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SHL3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    // Shift in unsigned space: left-shifting a negative value, or shifting a
    // 1-bit into/past the sign bit, is UB on signed long long. The unsigned
    // result is bit-identical on all two's-complement targets.
    long long result =
        (long long)((unsigned long long)vm->regs[rs1] << vm->regs[rs2]);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SHR3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = vm->regs[rs1] >> vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_UDIV3_fn(VirtualMachine *vm) {
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

static inline int op_UMOD3_fn(VirtualMachine *vm) {
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

static inline int op_USHR3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long result = (unsigned long long)vm->regs[rs1] >> vm->regs[rs2];
    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)result;
    return 0;
}

// ========== Comparison Operations ==========

static inline int op_SEQ3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] == vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SNE3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] != vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SLT3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] < vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SGE3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] >= vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SGT3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] > vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_SLE3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    long long result = (vm->regs[rs1] <= vm->regs[rs2]) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_ULT3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long a = (unsigned long long)vm->regs[rs1];
    unsigned long long b = (unsigned long long)vm->regs[rs2];
    long long result = (a < b) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_ULE3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    unsigned long long a = (unsigned long long)vm->regs[rs1];
    unsigned long long b = (unsigned long long)vm->regs[rs2];
    long long result = (a <= b) ? 1 : 0;
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

// ========== Data Movement ==========

static inline int op_LI3_fn(VirtualMachine *vm) {
    // Load immediate: [LI3] [rd:8] [immediate:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long imm = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = imm;
    return 0;
}

// ---------------------------------------------------------------------------
// Threading race detection (CCCC_THREAD_SAFETY)
// ---------------------------------------------------------------------------
// Thread identity: ThreadRecord* for worker threads, (void*)1 for main.
#define CCCC_MAIN_THREAD_ID ((void *)1)

static void check_race_access(VirtualMachine *vm, void *addr, int is_write) {
    if (!(vm->flags & CCCC_THREAD_SAFETY) || !vm->gil_initialized)
        return;
    void *tid = vm->active_thread ? (void *)vm->active_thread : CCCC_MAIN_THREAD_ID;
    int no_lock = cccc_thread_held_lock_count(vm) == 0;
    void *prev = hashmap_get_int(&vm->race_shadow, (long long)(uintptr_t)addr);
    if (no_lock && prev && prev != tid) {
        fprintf(stderr,
                "\n====== DATA RACE DETECTED ======\n"
                "Address %p %s by thread %p and now %s by thread %p "
                "without a mutex\n"
                "================================\n",
                addr,
                "written",
                prev,
                is_write ? "written" : "read",
                tid);
    }
    if (no_lock && is_write)
        hashmap_put_int(&vm->race_shadow, (long long)(uintptr_t)addr, tid);

    // Mixed atomic/non-atomic access detection: warn if this non-atomic access
    // targets an address that another thread previously accessed atomically.
    // Gated on different-thread + no-lock to avoid false positives from plain
    // reads of _Atomic variables on the same thread (e.g. `if (x)` compiles to
    // a bare LDR even when x is _Atomic int).
    void *atom_tid = hashmap_get_int(&vm->atomic_shadow, (long long)(uintptr_t)addr);
    if (no_lock && atom_tid && atom_tid != tid) {
        fprintf(stderr,
                "\n====== MIXED ATOMIC/NON-ATOMIC ACCESS DETECTED ======\n"
                "Address %p was accessed atomically by thread %p "
                "and is now %s non-atomically by thread %p without a mutex\n"
                "======================================================\n",
                addr, atom_tid,
                is_write ? "written" : "read",
                tid);
    }
}

// Tag an address in the atomic_shadow map as atomically accessed by this thread.
// Called by ALDR/ASTR/AXCHG/ACAS op fns. Does not raise race warnings — atomic
// ops are synchronization primitives.
static void check_atomic_access(VirtualMachine *vm, void *addr) {
    if (!(vm->flags & CCCC_THREAD_SAFETY) || !vm->gil_initialized)
        return;
    void *tid = vm->active_thread ? (void *)vm->active_thread : CCCC_MAIN_THREAD_ID;
    hashmap_put_int(&vm->atomic_shadow, (long long)(uintptr_t)addr, tid);
}

static inline int op_LDA3_fn(VirtualMachine *vm) {
    // Load data-relative address: [LDA3] [rd:8] [byte_offset:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(vm->data_seg + offset);
    return 0;
}

static inline int op_LDTLS3_fn(VirtualMachine *vm) {
    // Load TLS-relative address: [LDTLS3] [rd:8] [byte_offset:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(vm->current_tls_seg + offset);
    return 0;
}

static inline int op_LTA3_fn(VirtualMachine *vm) {
    // Load text-relative address: [LTA3] [rd:8] [byte_offset:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    if (rd != REG_ZERO)
        vm->regs[rd] = offset;
    return 0;
}

static inline int op_MOV3_fn(VirtualMachine *vm) {
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

// Whether the epoch/interval bookkeeping below (frame_epochs, live_epochs,
// stack_intervals) should be populated. True for CCCC_DANGLING_DETECT
// (original #673/#675 purpose), and also when the program contains a
// DYNOBJSZ opcode (#648) -- DYNOBJSZ stabs stack_intervals + live_epochs to
// size escaping fixed-size stack buffers reached through an opaque pointer.
// vm->dynobjsz_present is set once, by a text-segment scan before execution
// starts (see cccc_vm_scan_dynobjsz in vm.c), so this stays cheap per call.
static inline bool stack_extents_enabled(VirtualMachine *vm) {
    return (vm->flags & CCCC_DANGLING_DETECT) || vm->dynobjsz_present;
}

// Frame-epoch liveness stack for dangling-stack-pointer detection through a
// deeper call (#673). Mirrors the saved-bp chain: one entry is pushed per
// ENT3 and popped by whichever teardown path leaves that frame (normal
// return, tail call, or a longjmp that unwinds past it). Kept as a plain
// parallel array (not just the live_epochs set) so frame identity and push
// order are available for the multi-frame longjmp truncation below.

static void frame_epoch_push(VirtualMachine *vm, long long *bp,
                              unsigned long long epoch) {
    if (vm->frame_epochs.count == vm->frame_epochs.capacity) {
        int new_cap = vm->frame_epochs.capacity ? vm->frame_epochs.capacity * 2 : 64;
        long long **new_bps = realloc(vm->frame_epochs.bps, (size_t)new_cap * sizeof(long long *));
        if (new_bps)
            vm->frame_epochs.bps = new_bps;
        unsigned long long *new_epochs = realloc(vm->frame_epochs.epochs, (size_t)new_cap * sizeof(unsigned long long));
        if (new_epochs)
            vm->frame_epochs.epochs = new_epochs;
        if (!new_bps || !new_epochs) {
            // Internal bookkeeping OOM: skip tracking this frame. Its
            // pointers will simply never match in stack_ptr_epochs, so the
            // epoch check silently falls back to the (still-precise) range
            // check for them -- no false positives, only a missed catch.
            return;
        }
        vm->frame_epochs.capacity = new_cap;
    }
    vm->frame_epochs.bps[vm->frame_epochs.count] = bp;
    vm->frame_epochs.epochs[vm->frame_epochs.count] = epoch;
    vm->frame_epochs.count++;
    hashmap_put_int(&vm->live_epochs, (long long)epoch, (void *)1);
}

// Pop exactly the top frame (normal return / tail-call unwind).
static void frame_epoch_pop(VirtualMachine *vm) {
    if (vm->frame_epochs.count == 0)
        return;
    vm->frame_epochs.count--;
    unsigned long long epoch = vm->frame_epochs.epochs[vm->frame_epochs.count];
    hashmap_delete_int(&vm->live_epochs, (long long)epoch);
}

// Pop this activation's epoch iff it pushed one (#703). Since #703 makes
// ENT3's push conditional (only frames that own an escaping local/param push
// at all), a teardown path can no longer assume the top frame_epochs entry
// belongs to the frame currently unwinding -- LEV3/CALLT run on *every*
// return, pushing or not. frame_epochs.bps[] is monotonic (deeper calls
// pushed later, at higher indices, with strictly smaller bp -- stack grows
// down), so the top entry belongs to this activation iff its bp matches
// exactly; a non-pushing frame simply isn't in the array and this is a
// no-op for it. This is the self-synchronizing analog of #673's original
// "frame_epochs depth == live saved-bp chain depth" tripwire, adapted so it
// still holds when not every frame pushes.
static inline void frame_epoch_pop_if_owner(VirtualMachine *vm) {
    // Always-on O(1) guard: the top entry, if any, can never belong to a
    // shallower (larger-bp) frame than the one currently unwinding -- that
    // would mean some teardown path failed to pop an inner frame's epoch.
    assert(vm->frame_epochs.count == 0 ||
           vm->frame_epochs.bps[vm->frame_epochs.count - 1] >= vm->bp);
#ifndef NDEBUG
    // Strong check: frame_epochs must be an in-order subsequence of the live
    // saved-bp chain (same bounded walk/bounds check as op_RETADDR_fn) --
    // every pushed entry must correspond to some frame still on the real
    // call stack, in the same order. A mismatch means frame_epochs desynced
    // from the real call stack (the #669-style bug #673's design must
    // avoid). Walked here (rather than unconditionally, as #673 originally
    // did) because it costs O(chain depth) per return; the O(1) guard above
    // catches the same class of desync cheaply for release builds.
    {
        int ei = vm->frame_epochs.count - 1;
        for (long long *frame = vm->bp;
             ei >= 0 && frame >= vm->sp && frame < vm->initial_sp;) {
            if (vm->frame_epochs.bps[ei] == frame)
                ei--;
            long long *next = (long long *)frame[0];
            if (next < vm->sp || next >= vm->initial_sp)
                break;
            frame = next;
        }
        assert(ei < 0 &&
               "frame_epochs is not a subsequence of the live saved-bp "
               "chain (#703)");
    }
#endif
    if (vm->frame_epochs.count > 0 &&
        vm->frame_epochs.bps[vm->frame_epochs.count - 1] == vm->bp)
        frame_epoch_pop(vm);
}

// Pop every frame above (and not equal to) new_bp -- used by longjmp, which
// can unwind several frames at once. The target frame (bp == new_bp) stays.
static void frame_epoch_truncate_to(VirtualMachine *vm, long long *new_bp) {
    while (vm->frame_epochs.count > 0 &&
           vm->frame_epochs.bps[vm->frame_epochs.count - 1] < new_bp) {
        vm->frame_epochs.count--;
        unsigned long long epoch = vm->frame_epochs.epochs[vm->frame_epochs.count];
        hashmap_delete_int(&vm->live_epochs, (long long)epoch);
    }
}

static inline int op_ENT3_fn(VirtualMachine *vm) {
    // Enter function: [ENT3] [stack_size:32|spill_param_count:32]
    // [f32_param_mask:32|float_param_mask:32]
    // Creates new stack frame and copies register and stack-passed arguments to
    // parameter slots.
    long long operands = cc_read_i64(vm);
    int stack_size = (int)(operands & 0xFFFFFFFF);
    int spill_param_count = (int)((operands >> 32) & 0xFFFFFFFF);
    unsigned long long masks = (unsigned long long)cc_read_i64(vm);
    // Bit 31 of each half carries #703's lazy epoch-push flags, not a param
    // mask bit (register params are capped at 8) -- pull those out first,
    // then mask them off so float_param_mask/f32_param_mask below only ever
    // see real param bits.
    bool push_epoch_agg = (masks & (unsigned long long)ENT3_PUSH_EPOCH_AGG) != 0;
    bool push_epoch_scalar =
        (masks & (((unsigned long long)ENT3_PUSH_EPOCH_SCALAR) << 32)) != 0;
    masks &= ~((unsigned long long)ENT3_PUSH_EPOCH_AGG |
               (((unsigned long long)ENT3_PUSH_EPOCH_SCALAR) << 32));
    unsigned int float_param_mask = (unsigned int)(masks & 0xFFFFFFFFu);
    unsigned int f32_param_mask = (unsigned int)(masks >> 32);

    // The canary slot (when enabled) is already included in stack_size by
    // assign_stack_offsets, which reserves bp[-1] for it (#445).
    int total_slots = stack_size + 1;
    if (check_stack_overflow(vm, total_slots)) return -1;

    // Save old base pointer
    *--vm->sp = (long long)vm->bp;
    vm->bp = vm->sp;

    // Assign this activation a fresh liveness epoch (#673, and #648 for
    // DYNOBJSZ stack-buffer sizing) -- but only when this function's body
    // actually needs one (#703). Only STKTAG and a recording LEA3 ever read
    // the *current top* epoch (frame_epochs.epochs[count-1]), and both are
    // emitted (via emit_lea3_var) only for a local/param whose address
    // escapes -- so a frame with no such local contributes nothing to
    // liveness and can skip the push/pop entirely. This is a pure recording
    // optimization, not a correctness relaxation, in the same spirit as
    // #676's LEA3-recording prune: under-recording *what gets pushed* is
    // safe here because push/pop stay self-synchronized on vm->bp (see
    // op_LEV3_fn/op_CALLT_fn) -- unlike #669's mistake of under-recording
    // *liveness itself*.
    //
    // ENT3_PUSH_EPOCH_AGG covers an escaping aggregate (STKTAG) and is
    // needed in both --dangling-detection and dynobjsz-only mode.
    // ENT3_PUSH_EPOCH_SCALAR covers an escaping scalar's recorded LEA3,
    // which op_LEA3_fn itself only performs under CCCC_DANGLING_DETECT, so
    // it's only checked in that mode here.
    if (stack_extents_enabled(vm) &&
        (push_epoch_agg ||
         (push_epoch_scalar && (vm->flags & CCCC_DANGLING_DETECT))))
        frame_epoch_push(vm, vm->bp, ++vm->frame_epoch_counter);

    // Allocate space for local variables AND parameters
    // Parameters are now stored at negative offsets like locals.
    vm->sp = vm->sp - stack_size;

    // If stack canaries are enabled, write the canary into its reserved slot at
    // bp[-1]; params/locals live at bp[-2] and below.
    if (vm->flags & CCCC_STACK_CANARIES) {
        vm->bp[-1] = vm->stack_canary;
    }

    // Copy arguments to their stack slots.
    // Parameters are at bp[-1], bp[-2], ... bp[-spill_param_count]
    // (shifted one slot lower to bp[-2], bp[-3], ... when canaries reserve bp[-1]).
    // Slots 0-7 come from REG_Ai/FREG_Ai depending on float_param_mask.
    // Slots 8+ were pushed by the caller and are visible at bp[+2], bp[+3], ...
    // We need separate int and float register indices
    int int_reg_idx = 0;
    int float_reg_idx = 0;
    for (int i = 0; i < spill_param_count; i++) {
        long long *param_slot = vm->bp - 1 - i;
        if (vm->flags & CCCC_STACK_CANARIES) {
            param_slot--; // Account for canary slot
        }

        if (i >= 8) {
            *param_slot = vm->bp[2 + (i - 8)];
        } else if (float_param_mask & (1LL << i)) {
            // Float parameter - copy from fregs[]
            if (f32_param_mask & (1u << i)) {
                *(float *)param_slot =
                    cccc_freg_get_f32(vm, FREG_A0 + float_reg_idx);
            } else {
                *param_slot =
                    cccc_freg_raw_f64(vm, FREG_A0 + float_reg_idx);
            }
            float_reg_idx++;
        } else {
            // Integer parameter - copy from regs[]
            *param_slot = vm->regs[REG_A0 + int_reg_idx];
            int_reg_idx++;
        }
    }

    // Stack overflow checking (for stack instrumentation)
    if (vm->flags & CCCC_STACK_INSTR) {
        long long stack_used = (char *)vm->initial_sp - (char *)vm->sp;
        if (stack_used >
            (long long)(vm->poolsize * sizeof(long long) * 3 / 4)) {
            if (vm->flags & CCCC_STACK_INSTR_ERRORS) {
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

static inline int op_LEV3_fn(VirtualMachine *vm) {
    // Leave function: return value already in REG_A0/FREG_A0, restore frame
    // (Caller placed return value in REG_A0 before LEV3)

    // Restore stack pointer to base pointer
    vm->sp = vm->bp;

    // If stack canaries are enabled, check canary
    if (vm->flags & CCCC_STACK_CANARIES) {
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

    // Retire this activation's liveness epoch, if it pushed one (#673, #648,
    // #703), before the bp that identifies it is overwritten below.
    if (stack_extents_enabled(vm))
        frame_epoch_pop_if_owner(vm);

    // Restore old base pointer
    vm->bp = (long long *)*vm->sp++;

    // Get return address
    long long ret_addr = *vm->sp++;

    // Check if returning from main (ret_addr == 0)
    if (ret_addr == 0) {
        vm->pc = CCCC_INVALID_PC; // Signal end of execution
        return 0;
    }

    // CFI validation. main() starts from a synthetic return address instead of
    // a CALL instruction, so the ret_addr == 0 sentinel above has no shadow entry.
    if (vm->flags & CCCC_CFI) {
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

    vm->pc = (Pc)ret_addr;
    return 0;
}

// ========== 3-Register Floating-Point Arithmetic ==========

static inline int op_FADD3_fn(VirtualMachine *vm) {
    // fregs[rd] = fregs[rs1] + fregs[rs2]
    // Format: [FADD3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    cccc_freg_set_f64(vm, rd,
                     cccc_freg_get_f64(vm, rs1) + cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FSUB3_fn(VirtualMachine *vm) {
    // fregs[rd] = fregs[rs1] - fregs[rs2]
    // Format: [FSUB3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    cccc_freg_set_f64(vm, rd,
                     cccc_freg_get_f64(vm, rs1) - cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FMUL3_fn(VirtualMachine *vm) {
    // fregs[rd] = fregs[rs1] * fregs[rs2]
    // Format: [FMUL3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    cccc_freg_set_f64(vm, rd,
                     cccc_freg_get_f64(vm, rs1) * cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FDIV3_fn(VirtualMachine *vm) {
    // fregs[rd] = fregs[rs1] / fregs[rs2]
    // Format: [FDIV3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    // Division by zero check
    if (cccc_freg_get_f64(vm, rs2) == 0.0) {
        printf("\n========== DIVISION BY ZERO ==========\n");
        printf("Floating-point division by zero detected!\n");
        printf("PC offset: %lld\n", (long long)(vm->pc - 1));
        printf("======================================\n");
        return -1;
    }

    cccc_freg_set_f64(vm, rd,
                     cccc_freg_get_f64(vm, rs1) / cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FMOV3_fn(VirtualMachine *vm) {
    // fregs[rd] = fregs[rs1]
    // Format: [FMOV3] [rd:8|rs1:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    vm->fregs[rd] = vm->fregs[rs1];
    return 0;
}

static inline int op_FNEG3_fn(VirtualMachine *vm) {
    // fregs[rd] = -fregs[rs1]
    // Format: [FNEG3] [rd:8|rs1:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    cccc_freg_set_f64(vm, rd, -cccc_freg_get_f64(vm, rs1));
    return 0;
}

static inline int op_FADD3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    cccc_freg_set_f32(vm, rd,
                     cccc_freg_get_f32(vm, rs1) + cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FSUB3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    cccc_freg_set_f32(vm, rd,
                     cccc_freg_get_f32(vm, rs1) - cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FMUL3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    cccc_freg_set_f32(vm, rd,
                     cccc_freg_get_f32(vm, rs1) * cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FDIV3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    if (cccc_freg_get_f32(vm, rs2) == 0.0f) {
        printf("\n========== DIVISION BY ZERO ==========\n");
        printf("Floating-point division by zero detected!\n");
        printf("PC offset: %lld\n", (long long)(vm->pc - 1));
        printf("======================================\n");
        return -1;
    }

    cccc_freg_set_f32(vm, rd,
                     cccc_freg_get_f32(vm, rs1) / cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FNEG3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    cccc_freg_set_f32(vm, rd, -cccc_freg_get_f32(vm, rs1));
    return 0;
}

// ========== Fused Floating-Point Multiply-Add ==========
// fregs[rd] = fregs[rs1] + fregs[rs2] * fregs[rs3]
// Encoding: RRRR format (same as MULADD3)

static inline int op_FMADD3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    // Two separate C operations force two roundings, matching FMUL3+FADD3 semantics.
    double product = cccc_freg_get_f64(vm, rs2) * cccc_freg_get_f64(vm, rs3);
    cccc_freg_set_f64(vm, rd, cccc_freg_get_f64(vm, rs1) + product);
    return 0;
}

static inline int op_FMADD3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    // Product rounded to float first, then added — two roundings, matches FMUL3_F32+FADD3_F32.
    float product = cccc_freg_get_f32(vm, rs2) * cccc_freg_get_f32(vm, rs3);
    cccc_freg_set_f64(vm, rd, (double)((float)cccc_freg_get_f32(vm, rs1) + product));
    return 0;
}

static inline int op_FMADD3_FMA_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    cccc_freg_set_f64(vm, rd, fma(cccc_freg_get_f64(vm, rs2),
                                  cccc_freg_get_f64(vm, rs3),
                                  cccc_freg_get_f64(vm, rs1)));
    return 0;
}

static inline int op_FMADD3_F32_FMA_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    cccc_freg_set_f64(vm, rd, (double)fmaf(cccc_freg_get_f32(vm, rs2),
                                           cccc_freg_get_f32(vm, rs3),
                                           cccc_freg_get_f32(vm, rs1)));
    return 0;
}

// Fused floating-point multiply-subtract: fregs[rd] = fregs[rs2]*fregs[rs3] - fregs[rs1]
// Encoding: RRRR format (same as FMADD3)

static inline int op_FMSUB3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    // Two separate C operations force two roundings, matching FMUL3+FSUB3 semantics.
    double product = cccc_freg_get_f64(vm, rs2) * cccc_freg_get_f64(vm, rs3);
    cccc_freg_set_f64(vm, rd, product - cccc_freg_get_f64(vm, rs1));
    return 0;
}

static inline int op_FMSUB3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    // Product rounded to float first, then subtracted — two roundings, matches FMUL3_F32+FSUB3_F32.
    float product = cccc_freg_get_f32(vm, rs2) * cccc_freg_get_f32(vm, rs3);
    cccc_freg_set_f64(vm, rd, (double)((float)(product - cccc_freg_get_f32(vm, rs1))));
    return 0;
}

static inline int op_FMSUB3_FMA_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    cccc_freg_set_f64(vm, rd, fma(cccc_freg_get_f64(vm, rs2),
                                  cccc_freg_get_f64(vm, rs3),
                                  -cccc_freg_get_f64(vm, rs1)));
    return 0;
}

static inline int op_FMSUB3_F32_FMA_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    cccc_freg_set_f64(vm, rd, (double)fmaf(cccc_freg_get_f32(vm, rs2),
                                           cccc_freg_get_f32(vm, rs3),
                                           -cccc_freg_get_f32(vm, rs1)));
    return 0;
}

// Fused floating-point negated multiply-subtract: fregs[rd] = fregs[rs1] - fregs[rs2]*fregs[rs3]
// Encoding: RRRR format (same as FMADD3/FMSUB3)

static inline int op_FNMSUB3_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    // Two separate C operations force two roundings, matching FMUL3+FSUB3 semantics.
    double product = cccc_freg_get_f64(vm, rs2) * cccc_freg_get_f64(vm, rs3);
    cccc_freg_set_f64(vm, rd, cccc_freg_get_f64(vm, rs1) - product);
    return 0;
}

static inline int op_FNMSUB3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    // Product rounded to float first, then subtracted — two roundings, matches FMUL3_F32+FSUB3_F32.
    float product = cccc_freg_get_f32(vm, rs2) * cccc_freg_get_f32(vm, rs3);
    cccc_freg_set_f64(vm, rd, (double)((float)(cccc_freg_get_f32(vm, rs1) - product)));
    return 0;
}

static inline int op_FNMSUB3_FMA_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    cccc_freg_set_f64(vm, rd, fma(-cccc_freg_get_f64(vm, rs2),
                                  cccc_freg_get_f64(vm, rs3),
                                  cccc_freg_get_f64(vm, rs1)));
    return 0;
}

static inline int op_FNMSUB3_F32_FMA_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, rs3;
    DECODE_RRRR(operands, rd, rs1, rs2, rs3);
    cccc_freg_set_f64(vm, rd, (double)fmaf(-cccc_freg_get_f32(vm, rs2),
                                           cccc_freg_get_f32(vm, rs3),
                                           cccc_freg_get_f32(vm, rs1)));
    return 0;
}

// ========== 3-Register Floating-Point Comparisons ==========

static inline int op_FEQ3_fn(VirtualMachine *vm) {
    // regs[rd] = (fregs[rs1] == fregs[rs2])
    // Format: [FEQ3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (cccc_freg_get_f64(vm, rs1) == cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FNE3_fn(VirtualMachine *vm) {
    // regs[rd] = (fregs[rs1] != fregs[rs2])
    // Format: [FNE3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (cccc_freg_get_f64(vm, rs1) != cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FLT3_fn(VirtualMachine *vm) {
    // regs[rd] = (fregs[rs1] < fregs[rs2])
    // Format: [FLT3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (cccc_freg_get_f64(vm, rs1) < cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FLE3_fn(VirtualMachine *vm) {
    // regs[rd] = (fregs[rs1] <= fregs[rs2])
    // Format: [FLE3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (cccc_freg_get_f64(vm, rs1) <= cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FGT3_fn(VirtualMachine *vm) {
    // regs[rd] = (fregs[rs1] > fregs[rs2])
    // Format: [FGT3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (cccc_freg_get_f64(vm, rs1) > cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FGE3_fn(VirtualMachine *vm) {
    // regs[rd] = (fregs[rs1] >= fregs[rs2])
    // Format: [FGE3] [rd:8|rs1:8|rs2:8|unused:40]
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);

    vm->regs[rd] = (cccc_freg_get_f64(vm, rs1) >= cccc_freg_get_f64(vm, rs2));
    return 0;
}

static inline int op_FEQ3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    vm->regs[rd] = (cccc_freg_get_f32(vm, rs1) == cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FNE3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    vm->regs[rd] = (cccc_freg_get_f32(vm, rs1) != cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FLT3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    vm->regs[rd] = (cccc_freg_get_f32(vm, rs1) < cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FLE3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    vm->regs[rd] = (cccc_freg_get_f32(vm, rs1) <= cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FGT3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    vm->regs[rd] = (cccc_freg_get_f32(vm, rs1) > cccc_freg_get_f32(vm, rs2));
    return 0;
}

static inline int op_FGE3_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2;
    DECODE_RRR(operands, rd, rs1, rs2);
    vm->regs[rd] = (cccc_freg_get_f32(vm, rs1) >= cccc_freg_get_f32(vm, rs2));
    return 0;
}

// ========== Internal Operations ==========

static inline int op_ADDI3_fn(VirtualMachine *vm) {
    // Add immediate: regs[rd] = regs[rs1] + immediate
    // Format: [ADDI3] [rd:8|rs1:8|unused:48] [immediate:64]
    // Used for struct offset calculations: struct_addr + offset
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);
    long long imm = cc_read_i64(vm);

    if (rd != REG_ZERO)
        vm->regs[rd] =
            (long long)((unsigned long long)vm->regs[rs1] +
                        (unsigned long long)imm);
    return 0;
}

static inline int op_NEG3_fn(VirtualMachine *vm) {
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

// Guest memory access goes through these memcpy helpers rather than a direct
// `*(T*)guest_ptr` cast: the guest can hold a pointer of any alignment (packed
// structs, char-buffer aliasing, flexible array members), and dereferencing it
// as a wider type is undefined behaviour on the host — it happens to work on
// aarch64/x86_64 but UBSan flags it and it would SIGBUS on strict-alignment
// targets (#577). __builtin_memcpy of a constant size lowers to the same single
// load/store the compiler would emit for an aligned access. The signed integer
// loads return long long so the narrow->wide conversion sign-extends, matching
// the original `*(short*)`/`*(int*)` semantics.
//
// The atomic-tagged opcodes (ALDR/ASTR/AXCHG/ACAS) keep direct derefs: C
// atomics require natural alignment, so an unaligned atomic is a guest-program
// bug, not something the VM should silently paper over.
static inline long long ld_i16(const void *p) { short v; __builtin_memcpy(&v, p, 2); return v; }
static inline long long ld_i32(const void *p) { int v;   __builtin_memcpy(&v, p, 4); return v; }
static inline long long ld_i64(const void *p) { long long v; __builtin_memcpy(&v, p, 8); return v; }
static inline void st_i16(void *p, long long v) { short x = (short)v; __builtin_memcpy(p, &x, 2); }
static inline void st_i32(void *p, long long v) { int x = (int)v; __builtin_memcpy(p, &x, 4); }
static inline void st_i64(void *p, long long v) { __builtin_memcpy(p, &v, 8); }
static inline double ld_f64(const void *p) { double v; __builtin_memcpy(&v, p, 8); return v; }
static inline float  ld_f32(const void *p) { float v;  __builtin_memcpy(&v, p, 4); return v; }
static inline void st_f64(void *p, double v) { __builtin_memcpy(p, &v, 8); }
static inline void st_f32(void *p, float v)  { __builtin_memcpy(p, &v, 4); }

static inline int op_LDR_B_fn(VirtualMachine *vm) {
    // Load byte (sign-extend): regs[rd] = *(char*)regs[rs]
    // Format: [LDR_B] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 0);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 1, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(signed char *)vm->regs[rs];
    return 0;
}

static inline int op_LDR_H_fn(VirtualMachine *vm) {
    // Load halfword (sign-extend): regs[rd] = *(short*)regs[rs]
    // Format: [LDR_H] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 0);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 2, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = ld_i16((void *)vm->regs[rs]);
    return 0;
}

static inline int op_LDR_W_fn(VirtualMachine *vm) {
    // Load word (sign-extend): regs[rd] = *(int*)regs[rs]
    // Format: [LDR_W] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 0);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = ld_i32((void *)vm->regs[rs]);
    return 0;
}

static inline int op_LDR_D_fn(VirtualMachine *vm) {
    // Load dword: regs[rd] = *(long long*)regs[rs]
    // Format: [LDR_D] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 0);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = ld_i64((void *)vm->regs[rs]);
    return 0;
}

static inline int op_STR_B_fn(VirtualMachine *vm) {
    // Store byte: *(char*)regs[rs] = regs[rd]
    // Format: [STR_B] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 1);
    *(char *)vm->regs[rs] = (char)vm->regs[rd];
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 1, WATCH_WRITE);
    return 0;
}

static inline int op_STR_H_fn(VirtualMachine *vm) {
    // Store halfword: *(short*)regs[rs] = regs[rd]
    // Format: [STR_H] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 1);
    st_i16((void *)vm->regs[rs], vm->regs[rd]);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 2, WATCH_WRITE);
    return 0;
}

static inline int op_STR_W_fn(VirtualMachine *vm) {
    // Store word: *(int*)regs[rs] = regs[rd]
    // Format: [STR_W] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 1);
    st_i32((void *)vm->regs[rs], vm->regs[rd]);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_WRITE);
    return 0;
}

static inline int op_STR_D_fn(VirtualMachine *vm) {
    // Store dword: *(long long*)regs[rs] = regs[rd]
    // Format: [STR_D] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    check_race_access(vm, (void *)vm->regs[rs], 1);
    st_i64((void *)vm->regs[rs], vm->regs[rd]);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_WRITE);
    return 0;
}

// ========== Atomic-Tagged Load/Store (ALDR / ASTR) ==========
// width_enc = (size_bytes << 1) | is_unsigned

static inline int op_ALDR_fn(VirtualMachine *vm) {
    // Atomic-tagged load: rd = *(T*)regs[rs]; tags atomic_shadow[addr] = tid
    // Format: [ALDR] [rd:8|rs:8|unused:48] [width_enc:64]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long width_enc = cc_read_i64(vm);
    int sz = (int)(width_enc >> 1);
    int is_unsigned = (int)(width_enc & 1);

    void *addr = (void *)vm->regs[rs];
    check_atomic_access(vm, addr);
    WATCHPOINT_CHECK(vm, addr, sz, WATCH_READ);
    if (rd != REG_ZERO) {
        switch (sz) {
        case 1: vm->regs[rd] = is_unsigned ? (long long)*(unsigned char *)addr
                                           : (long long)*(signed char *)addr; break;
        case 2: vm->regs[rd] = is_unsigned ? (long long)*(unsigned short *)addr
                                           : (long long)*(short *)addr; break;
        case 4: vm->regs[rd] = is_unsigned ? (long long)*(unsigned int *)addr
                                           : (long long)*(int *)addr; break;
        default: /* sz == 8 */ vm->regs[rd] = *(long long *)addr; break;
        }
    }
    return 0;
}

static inline int op_ASTR_fn(VirtualMachine *vm) {
    // Atomic-tagged store: *(T*)regs[rs] = regs[rd]; tags atomic_shadow[addr]
    // Format: [ASTR] [rd:8|rs:8|unused:48] [width_enc:64]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long width_enc = cc_read_i64(vm);
    int sz = (int)(width_enc >> 1);

    void *addr = (void *)vm->regs[rs];
    check_atomic_access(vm, addr);
    switch (sz) {
    case 1: *(char *)addr     = (char)vm->regs[rd];      break;
    case 2: *(short *)addr    = (short)vm->regs[rd];     break;
    case 4: *(int *)addr      = (int)vm->regs[rd];       break;
    default: /* sz == 8 */ *(long long *)addr = vm->regs[rd]; break;
    }
    WATCHPOINT_CHECK(vm, addr, sz, WATCH_WRITE);
    return 0;
}

// ========== Atomic Exchange / CAS (AXCHG / ACAS) ==========
// Operands pre-loaded in REG_A0..A2 by codegen (same convention as IOVFL).
// The VM GIL guarantees atomicity w.r.t. other VM threads.

static inline int op_AXCHG_fn(VirtualMachine *vm) {
    // atomic_exchange: old = *(T*)A0; *(T*)A0 = (T)A1; A0 = old
    // Format: [AXCHG] [width_enc:64]
    long long width_enc = cc_read_i64(vm);
    int sz = (int)(width_enc >> 1);
    int is_unsigned = (int)(width_enc & 1);

    void *addr = (void *)vm->regs[REG_A0];
    long long new_val = vm->regs[REG_A1];
    long long old_val;

    check_atomic_access(vm, addr);
    switch (sz) {
    case 1: old_val = is_unsigned ? (long long)*(unsigned char *)addr
                                  : (long long)*(signed char *)addr;
            *(char *)addr = (char)new_val; break;
    case 2: old_val = is_unsigned ? (long long)*(unsigned short *)addr
                                  : (long long)*(short *)addr;
            *(short *)addr = (short)new_val; break;
    case 4: old_val = is_unsigned ? (long long)*(unsigned int *)addr
                                  : (long long)*(int *)addr;
            *(int *)addr = (int)new_val; break;
    default: /* sz == 8 */ old_val = *(long long *)addr;
             *(long long *)addr = new_val; break;
    }
    vm->regs[REG_A0] = old_val;
    return 0;
}

static inline int op_ACAS_fn(VirtualMachine *vm) {
    // compare_and_swap:
    //   A0 = T*  (pointer to atomic variable)
    //   A1 = T*  (pointer to expected value; updated on failure)
    //   A2 = T   (desired value)
    // If *(T*)A0 == *(T*)A1: store A2 to *A0, return 1 in A0.
    // Else:                  store current *A0 to *A1, return 0 in A0.
    // Format: [ACAS] [width_enc:64]
    long long width_enc = cc_read_i64(vm);
    int sz = (int)(width_enc >> 1);

    void *obj_ptr  = (void *)vm->regs[REG_A0]; // T* — the atomic variable
    void *exp_ptr  = (void *)vm->regs[REG_A1]; // T* — pointer to expected
    long long desired = vm->regs[REG_A2];

    check_atomic_access(vm, obj_ptr);
    int success = 0;
    switch (sz) {
    case 1: { signed char cur = *(signed char *)obj_ptr;
              if (cur == *(signed char *)exp_ptr) { *(char *)obj_ptr = (char)desired; success = 1; }
              else                          *(char *)exp_ptr = cur; break; }
    case 2: { short cur = *(short *)obj_ptr;
              if (cur == *(short *)exp_ptr) { *(short *)obj_ptr = (short)desired; success = 1; }
              else                           *(short *)exp_ptr = cur; break; }
    case 4: { int cur = *(int *)obj_ptr;
              if (cur == *(int *)exp_ptr) { *(int *)obj_ptr = (int)desired; success = 1; }
              else                         *(int *)exp_ptr = cur; break; }
    default: { /* sz == 8 */ long long cur = *(long long *)obj_ptr;
                if (cur == *(long long *)exp_ptr) { *(long long *)obj_ptr = desired; success = 1; }
                else                               *(long long *)exp_ptr = cur; break; }
    }
    vm->regs[REG_A0] = success;
    return 0;
}

static inline int op_FLDR_fn(VirtualMachine *vm) {
    // Float load: fregs[rd] = *(double*)regs[rs]
    // Format: [FLDR] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_READ);
    cccc_freg_set_f64(vm, rd, ld_f64((void *)vm->regs[rs]));
    return 0;
}

static inline int op_FSTR_fn(VirtualMachine *vm) {
    // Float store: *(double*)regs[rs] = fregs[rd]
    // Format: [FSTR] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    st_f64((void *)vm->regs[rs], cccc_freg_get_f64(vm, rd));
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 8, WATCH_WRITE);
    return 0;
}

static inline int op_FLDR_F32_fn(VirtualMachine *vm) {
    // Float32 load: fregs[rd] = *(float*)regs[rs]
    // Format: [FLDR_F32] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_READ);
    cccc_freg_set_f32(vm, rd, ld_f32((void *)vm->regs[rs]));
    return 0;
}

static inline int op_FSTR_F32_fn(VirtualMachine *vm) {
    // Float32 store: *(float*)regs[rs] = (float)fregs[rd]
    // Format: [FSTR_F32] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    st_f32((void *)vm->regs[rs], cccc_freg_get_f32(vm, rd));
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], 4, WATCH_WRITE);
    return 0;
}

static inline int op_FROUND_F32_fn(VirtualMachine *vm) {
    // Float32 round: fregs[rd] = (float)fregs[rs]
    // Format: [FROUND_F32] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_f32(vm, rd, cccc_freg_get_f32(vm, rs));
    return 0;
}

static inline int op_LEA3_fn(VirtualMachine *vm) {
    // Load effective address: regs[rd] = bp + immediate
    // Format: [LEA3] [rd:8|LEA3_NO_RECORD:1|unused:55] [immediate:64]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    bool no_record = (operands & LEA3_NO_RECORD) != 0;
    long long offset = cc_read_i64(vm);

    if (rd != REG_ZERO) {
        long long addr = (long long)(vm->bp + offset);
        vm->regs[rd] = addr;

        // Tag this &local with the current frame's liveness epoch (#673),
        // the stack analogue of ptr_tags recording a heap allocation's
        // creation generation. Recorded whenever codegen hasn't proven the
        // address stays confined to its creating frame (#676) -- escape
        // analysis at *record* time is safe here (unlike the #669 postmortem
        // in op_ENT3_fn, which was about escape analysis deciding
        // *correctness*, i.e. whether to flag a dangling deref at all; here
        // it only ever prunes a redundant hashmap write, never the epoch
        // check itself, so under-pruning costs perf, never correctness).
        if (!no_record && (vm->flags & CCCC_DANGLING_DETECT) &&
            vm->frame_epochs.count > 0) {
            unsigned long long epoch =
                vm->frame_epochs.epochs[vm->frame_epochs.count - 1];
            hashmap_put_int(&vm->stack_ptr_epochs, addr, (void *)(intptr_t)epoch);
        }
    }
    return 0;
}

// Insert (or refresh) a retained stack address interval (#675). Stack
// addresses are reused across frames -- unlike sorted_allocs' bump-allocated
// heap addresses -- so intervals are never dropped on frame death; only an
// exact [lo,hi) repeat (the common same-slot/same-size reuse case) is
// deduped by overwriting its epoch in place, keeping the array from growing
// unboundedly for a tight loop that repeatedly re-enters the same frame
// shape. Partial overlaps are retained as distinct entries -- correctness
// (not dropping a dead frame's extent just because a later frame reused
// part of it) depends on that; see stack_interval_stab below.
static void stack_interval_insert(VirtualMachine *vm, long long lo, long long hi,
                                   unsigned long long epoch) {
    for (int i = 0; i < vm->stack_intervals.count; i++) {
        if (vm->stack_intervals.iv[i].lo == lo && vm->stack_intervals.iv[i].hi == hi) {
            vm->stack_intervals.iv[i].epoch = epoch;
            return;
        }
    }
    if (vm->stack_intervals.count == vm->stack_intervals.capacity) {
        int new_cap = vm->stack_intervals.capacity ? vm->stack_intervals.capacity * 2 : 64;
        void *new_iv = realloc(vm->stack_intervals.iv,
                                (size_t)new_cap * sizeof(*vm->stack_intervals.iv));
        if (!new_iv)
            return; // OOM: skip tracking this interval -- falls back to the
                     // (still-precise) range check for it, no false positives
        vm->stack_intervals.iv = new_iv;
        vm->stack_intervals.capacity = new_cap;
    }
    vm->stack_intervals.iv[vm->stack_intervals.count].lo = lo;
    vm->stack_intervals.iv[vm->stack_intervals.count].hi = hi;
    vm->stack_intervals.iv[vm->stack_intervals.count].epoch = epoch;
    vm->stack_intervals.count++;
}

// Resolve an interior stack address to the most-recent (max-epoch) retained
// interval containing it (#675). Epoch order IS recency order -- later
// activations always get strictly higher epochs (vm->frame_epoch_counter is
// monotonic), so whichever containing interval has the highest epoch is,
// by construction, the one that currently owns this address, whether or not
// its frame is still live. Returns false if no interval contains ptr.
// *out_hi (optional, may be NULL) receives the matched interval's upper
// bound, so a caller like DYNOBJSZ (#648) can compute remaining bytes
// (hi - ptr) without a second lookup.
// PLACEHOLDER: linear scan over all retained intervals. Fine for the
// small, mostly-distinct working set typical programs produce; if a hot
// loop retains many intervals this should become an interval tree.
// Ticket: https://todo.sr.ht/~takeiteasy/cccc/677
static bool stack_interval_stab(VirtualMachine *vm, long long ptr,
                                 unsigned long long *out_epoch,
                                 long long *out_hi) {
    bool found = false;
    unsigned long long best_epoch = 0;
    long long best_hi = 0;
    for (int i = 0; i < vm->stack_intervals.count; i++) {
        if (ptr >= vm->stack_intervals.iv[i].lo && ptr < vm->stack_intervals.iv[i].hi) {
            if (!found || vm->stack_intervals.iv[i].epoch > best_epoch) {
                best_epoch = vm->stack_intervals.iv[i].epoch;
                best_hi = vm->stack_intervals.iv[i].hi;
                found = true;
            }
        }
    }
    if (found) {
        *out_epoch = best_epoch;
        if (out_hi)
            *out_hi = best_hi;
    }
    return found;
}

static inline int op_STKTAG_fn(VirtualMachine *vm) {
    // Tag an escaping aggregate local's [bp+offset, bp+offset+size) extent
    // with the current frame's epoch, for interior dangling-pointer
    // resolution (#675) and DYNOBJSZ stack-buffer sizing (#648).
    // Format: [STKTAG] [unused:32] [offset:i64] [size:i64]
    cc_read_word(vm); // unused first word (no register operand)
    long long offset = cc_read_i64(vm);
    long long size = cc_read_i64(vm);

    if (stack_extents_enabled(vm) && vm->frame_epochs.count > 0) {
        long long lo = (long long)(vm->bp + offset);
        unsigned long long epoch = vm->frame_epochs.epochs[vm->frame_epochs.count - 1];
        stack_interval_insert(vm, lo, lo + size, epoch);
    }
    return 0;
}

static inline int op_RETADDR_fn(VirtualMachine *vm) {
    // Return address of the nth caller frame.
    // Format: [RETADDR] [rd:8|unused:56] [level:i64]
    //
    // Frame layout (established by ENT3 / CALL):
    //   frame[+1]  <- return address (pushed by CALL before ENT3)
    //   frame[+0]  <- saved old_bp   (pushed by ENT3; vm->bp points here)
    //
    // For level 0:  regs[rd] = vm->bp[+1]    (current frame's ret addr)
    // For level n:  walk n saved-bp links up, then read frame[+1]
    // Returns NULL (0) if the walk exits the live stack (past outermost frame).
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long level = cc_read_i64(vm);

    long long *frame = vm->bp;

    for (long long i = 0; i < level; i++) {
        // frame[0] is the saved old_bp; load it to step up one frame
        long long *next = (long long *)frame[0];
        // Bounds-check: next must be within the live stack region
        if (next < vm->sp || next >= vm->initial_sp) {
            // Past the outermost frame — return NULL
            if (rd != REG_ZERO)
                vm->regs[rd] = 0;
            return 0;
        }
        frame = next;
    }

    // frame[+1] holds the return address for this frame
    if (rd != REG_ZERO)
        vm->regs[rd] = frame[1];
    return 0;
}

static inline int op_I2F3_fn(VirtualMachine *vm) {
    // Int to float: fregs[rd] = (double)regs[rs]
    // Format: [I2F3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_f64(vm, rd, (double)vm->regs[rs]);
    return 0;
}

static inline int op_I2F3_F32_fn(VirtualMachine *vm) {
    // Int to float: fregs[rd] = (float)regs[rs]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_f32(vm, rd, (float)vm->regs[rs]);
    return 0;
}

static inline int op_F2I3_fn(VirtualMachine *vm) {
    // Float to int: regs[rd] = (long long)fregs[rs]
    // Format: [F2I3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)cccc_freg_get_f64(vm, rs);
    return 0;
}

static inline int op_F2I3_F32_fn(VirtualMachine *vm) {
    // Float to int: regs[rd] = (long long)fregs[rs]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)cccc_freg_get_f32(vm, rs);
    return 0;
}

static inline int op_FR2R_fn(VirtualMachine *vm) {
    // Float register to integer register (bit-pattern transfer, no conversion)
    // Format: [FR2R] [rd:8|rs:8|unused:48]
    // Copies the raw IEEE 754 bits of the double to an integer register
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO) {
        vm->regs[rd] = cccc_freg_raw_f64(vm, rs);
    }
    return 0;
}

static inline int op_FR2R_F32_fn(VirtualMachine *vm) {
    // Float register to integer register (raw f32 payload bits)
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = (unsigned int)cccc_freg_raw_f32(vm, rs);
    return 0;
}

static inline int op_R2FR_fn(VirtualMachine *vm) {
    // Integer register to float register (bit-pattern transfer, no conversion)
    // Format: [R2FR] [rd:8|rs:8|unused:48]
    // Copies the raw bits from integer register to a double (reverse of FR2R)
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_raw_f64(vm, rd, vm->regs[rs]);
    return 0;
}

static inline int op_R2FR_F32_fn(VirtualMachine *vm) {
    // Integer register to float register (raw f32 payload bits)
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_raw_f32(vm, rd, (int)vm->regs[rs]);
    return 0;
}

static inline int op_JZ3_fn(VirtualMachine *vm) {
    // Branch if zero: if (regs[rs] == 0) pc = target
    // Format: [JZ3] [rs:8|unused:56] [target:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    Pc target = cc_read_word(vm);

    if (vm->regs[rs] == 0)
        vm->pc = target;
    return 0;
}

static inline int op_JNZ3_fn(VirtualMachine *vm) {
    // Branch if non-zero: if (regs[rs] != 0) pc = target
    // Format: [JNZ3] [rs:8|unused:56] [target:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    Pc target = cc_read_word(vm);

    if (vm->regs[rs] != 0)
        vm->pc = target;
    return 0;
}

static inline int op_NOT3_fn(VirtualMachine *vm) {
    // Logical not: regs[rd] = !regs[rs]
    // Format: [NOT3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = !vm->regs[rs];
    return 0;
}

static inline int op_BNOT3_fn(VirtualMachine *vm) {
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

// Binary search for the allocation with the largest base address <= ptr.
// Returns its index, or -1 if ptr is below every tracked base address.
static int sorted_allocs_find(VirtualMachine *vm, void *ptr) {
    void **addrs = vm->sorted_allocs.addresses;
    int lo = 0, hi = vm->sorted_allocs.count - 1, result = -1;
    while (lo <= hi) {
        int mid = lo + (hi - lo) / 2;
        if ((char *)addrs[mid] <= (char *)ptr) {
            result = mid;
            lo = mid + 1;
        } else {
            hi = mid - 1;
        }
    }
    return result;
}

// Resolve the tracked heap allocation containing `ptr` (base or interior),
// via sorted_allocs_find — the same range query DYNOBJSZ uses (#647).
// Returns the AllocHeader*, or NULL if ptr is outside the VM heap or not
// within any tracked allocation. On success *out_offset = ptr - base.
// Does NOT filter freed allocations — callers that need UAF detection
// (CHKP3) must inspect header->freed themselves. Containment bound is
// header->size (the aligned/usable size), consistent with CHKB's existing
// `>= size` comparison — do NOT use requested_size here, that is DYNOBJSZ's
// bound and would tighten CHKB's semantics.
static AllocHeader *heap_alloc_for_ptr(VirtualMachine *vm, long long ptr,
                                       size_t *out_offset) {
    if (ptr < (long long)vm->heap_seg || ptr >= (long long)vm->heap_end)
        return NULL;
    int idx = sorted_allocs_find(vm, (void *)ptr);
    if (idx < 0)
        return NULL;
    AllocHeader *h = vm->sorted_allocs.headers[idx];
    size_t off = (size_t)((char *)ptr - (char *)vm->sorted_allocs.addresses[idx]);
    if (h->magic != 0xDEADBEEF || off > h->size)
        return NULL;
    if (out_offset)
        *out_offset = off;
    return h;
}

static inline int op_CHKP3_fn(VirtualMachine *vm) {
    // Check pointer validity (register-based version of CHKP)
    // Format: [CHKP3] [rs:8|unused:56]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & CCCC_POINTER_CHECKS)) {
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

    // Dangling stack pointer detection: two layers.
    //
    // 1) Cheap range check (#670, use-based / dereference-time design --
    //    replaces the neutered scope-exit check removed in #669). The VM
    //    stack grows downward: vm->sp is the current top-of-stack, and every
    //    *live* local sits at an address >= vm->sp. A pointer obtained from
    //    &local (see op_LEA3_fn: regs[rd] = (long long)(vm->bp + offset), a
    //    raw host address) whose target frame has since returned therefore
    //    satisfies stack_seg <= ptr < vm->sp -- the slot has been reclaimed
    //    by sp rising back above it. Precise by construction, but has a
    //    known gap: if the pointer is passed *deeper* into another call and
    //    dereferenced there, that deeper call's own frame re-covers the dead
    //    address (ptr >= sp again), and this check alone misses it.
    //
    // 2) Frame-epoch liveness check (#673, closes the gap above). Every
    //    &local (LEA3) is tagged in stack_ptr_epochs with the epoch of the
    //    frame that created it; live_epochs holds the epochs of every frame
    //    currently on the call stack. A pointer to a local is valid iff the
    //    frame that created it is still live -- so if the tagged epoch isn't
    //    in live_epochs, the pointer is dangling regardless of where the
    //    address now sits relative to sp. This mirrors the heap temporal-
    //    safety scheme (ptr_tags vs AllocHeader.generation) one layer down.
    //    Pointers layer 1 already caught are not re-tagged here.
    //
    // 3) Interior interval-stabbing (#675, closes layer 2's own residual
    //    gap). Interior stack pointers with a runtime-computed offset (e.g.
    //    &arr[i] for non-constant i) never pass through LEA3 with a single
    //    recorded address, so they're never found in stack_ptr_epochs above
    //    -- but STKTAG (emitted right after the LEA3 base of any *escaping*
    //    array/struct local) retains [base, base+size) tagged with the same
    //    epoch in vm->stack_intervals. Consulted only when the exact lookup
    //    misses. Resolves by max-epoch containing interval (recency), so a
    //    live frame that has reused a dead frame's address range still
    //    resolves correctly -- see stack_interval_stab's own comment.
    if (vm->flags & CCCC_DANGLING_DETECT) {
        uintptr_t p   = (uintptr_t)ptr;
        uintptr_t lo  = (uintptr_t)vm->stack_seg;
        uintptr_t cur = (uintptr_t)vm->sp;
        bool range_dangling = (p >= lo && p < cur);

        bool epoch_dangling = false;
        bool interior = false;
        if (!range_dangling) {
            void *tagged = hashmap_get_int(&vm->stack_ptr_epochs, ptr);
            if (tagged) {
                if (!hashmap_get_int(&vm->live_epochs, (long long)(intptr_t)tagged))
                    epoch_dangling = true;
            } else {
                unsigned long long iv_epoch;
                if (stack_interval_stab(vm, ptr, &iv_epoch, NULL) &&
                    !hashmap_get_int(&vm->live_epochs, (long long)iv_epoch)) {
                    epoch_dangling = true;
                    interior = true;
                }
            }
        }

        if (range_dangling || epoch_dangling) {
            printf("\n========== DANGLING STACK POINTER ==========\n");
            printf("Dereferenced a pointer into a stack frame that has already returned\n");
            printf("Address:    0x%llx\n", (unsigned long long)p);
            if (range_dangling) {
                printf("Current SP: 0x%llx (address is below live stack -> frame is dead)\n",
                       (unsigned long long)cur);
            } else if (interior) {
                printf("Creating frame's epoch is no longer live (dereferenced through an "
                       "interior pointer into a deeper call, #675)\n");
            } else {
                printf("Creating frame's epoch is no longer live (dereferenced through a "
                       "deeper call, #673)\n");
            }
            printf("Current PC: 0x%llx (offset: %lld)\n", (long long)vm->pc,
                   (long long)vm->pc);
            printf("============================================\n");
            return -1;
        }
    }

    // Resolve the containing allocation — handles base pointers and
    // interior pointers (p + k) alike via sorted_allocs_find (#650).
    {
        AllocHeader *header = heap_alloc_for_ptr(vm, ptr, NULL);

        if (header) {
            // Temporal memory tagging: detect stale pointers via side table
            if (vm->flags & CCCC_MEMORY_TAGGING) {
                intptr_t stored_gen =
                    (intptr_t)hashmap_get_int(&vm->ptr_tags, ptr);
                if (stored_gen != (intptr_t)header->generation) {
                    printf("\n========== TEMPORAL SAFETY VIOLATION ==========\n");
                    printf("Stale pointer access detected\n");
                    printf("Address:           0x%llx\n", ptr);
                    printf("Pointer generation: %ld\n", (long)stored_gen);
                    printf("Current generation: %d\n", header->generation);
                    printf("Allocated at PC offset: %lld\n", header->alloc_pc);
                    printf("Current PC:        0x%llx (offset: %lld)\n",
                           (long long)vm->pc, (long long)vm->pc);
                    printf("================================================\n");
                    return -1;
                }
            }

            // Check if freed (UAF detection)
            if ((vm->flags & CCCC_UAF_DETECTION) && header->freed) {
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
    }

    return 0;
}

static inline int op_CHKA3_fn(VirtualMachine *vm) {
    // Check pointer alignment (register-based version of CHKA)
    // Format: [CHKA3] [rs:8|unused:56] [alignment:64]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    size_t alignment = (size_t)cc_read_i64(vm);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & CCCC_ALIGNMENT_CHECKS)) {
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

static inline int op_CHKT3_fn(VirtualMachine *vm) {
    // Check type on dereference (register-based version of CHKT).
    // Effective-type model (#651): a store through a base pointer stamps
    // the allocation's type_kind ("establishes" the effective type,
    // mirroring C11 6.5p6); a load checks the pointer's static type
    // against it. type_kind == TY_VOID means "no effective type
    // established yet" (the state MALC leaves it in) — never an error.
    // Format: [CHKT3] [rs:8|store_flag:8|unused:48] [expected_type:64]
    long long operands = cc_read_word(vm);
    int rs, store_flag;
    DECODE_RR(operands, rs, store_flag);
    int expected_type = (int)cc_read_i64(vm);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & CCCC_TYPE_CHECKS)) {
        return 0;
    }

    if (ptr == 0) {
        return 0; // NULL will be caught by CHKP3
    }

    // Skip check for void* (TY_VOID) and generic pointers (TY_PTR)
    if (expected_type == TY_VOID || expected_type == TY_PTR) {
        return 0;
    }

    // Resolve the containing allocation — handles base pointers and
    // interior pointers alike via sorted_allocs_find (#650's resolver).
    size_t off;
    AllocHeader *header = heap_alloc_for_ptr(vm, ptr, &off);
    if (!header) {
        return 0; // untracked (stack/global) — types not tracked there
    }
    if (header->freed) {
        return 0; // UAF is CHKP3's job; don't double-report here
    }
    // Base-pointer-only scoping (#651 point 4): an interior/member deref
    // may legitimately have a different subobject type than the whole
    // allocation, and that isn't tracked. Only check at offset 0.
    if (off != 0) {
        return 0;
    }

    if (store_flag) {
        // Establish (or update) the allocation's effective type.
        header->type_kind = expected_type;
        return 0;
    }

    int actual_type = header->type_kind;
    if (actual_type == TY_VOID) {
        return 0; // no effective type established yet — nothing to check
    }

    if (actual_type != expected_type) {
        static const char *type_names[] = {
            "void",     "bool",   "char",        "short",  "int",
            "long",     "float",  "double",      "long double", "enum",
            "pointer",  "function", "array",     "vla",    "struct",
            "union",    "error",  "block",       "complex", "nullptr_t",
            "_BitInt",  "auto"};
        const int n_type_names = (int)(sizeof(type_names) / sizeof(type_names[0]));

        const char *expected_name =
            (expected_type >= 0 && expected_type < n_type_names)
                ? type_names[expected_type]
                : "unknown";
        const char *actual_name =
            (actual_type >= 0 && actual_type < n_type_names)
                ? type_names[actual_type]
                : "unknown";

        printf("\n========== TYPE MISMATCH DETECTED ==========\n");
        printf("Pointer type mismatch on dereference\n");
        printf("Address:       0x%llx\n", ptr);
        printf("Expected type: %s\n", expected_name);
        printf("Actual type:   %s\n", actual_name);
        printf("Allocated at PC offset: %lld\n", header->alloc_pc);
        printf("Current PC:    0x%llx (offset: %lld)\n", (long long)vm->pc,
               (long long)vm->pc);
        printf("============================================\n");
        return -1;
    }

    return 0;
}

// ========== Control Flow Opcodes ==========

static inline int op_JMP_fn(VirtualMachine *vm) {
    vm->pc = cc_read_word(vm);
    return 0;
}

static inline int op_CALL_fn(VirtualMachine *vm) {
    // Call subroutine: push return address to main stack and shadow stack
    Pc target = cc_read_word(vm);
    long long ret_addr = (long long)vm->pc;

    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = ret_addr;
    if (vm->flags & CCCC_CFI) {
        *--vm->shadow_sp = ret_addr; // Also push to shadow stack for CFI
    }
    vm->pc = target;
    return 0;
}

static inline int op_CALLT_fn(VirtualMachine *vm) {
    // Tail call: unwind current frame without popping return address.
    // The return address from the original CALL remains on the stack,
    // so the callee's LEV3 returns directly to our caller.
    Pc target = cc_read_word(vm);

    vm->sp = vm->bp;

    if (vm->flags & CCCC_STACK_CANARIES) {
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

    // Retire this activation's liveness epoch, if it pushed one (#673, #648,
    // #703). The tail-callee's own ENT3 will push a fresh one if it needs
    // one; nothing here should stay live under it.
    if (stack_extents_enabled(vm))
        frame_epoch_pop_if_owner(vm);

    vm->bp = (long long *)*vm->sp++;

    // sp now points at the return address — leave it in place

    if (vm->flags & CCCC_CFI) {
        vm->shadow_sp++;
    }

    vm->pc = target;
    return 0;
}

static inline int op_CALLI_fn(VirtualMachine *vm) {
    // Call indirect: function address in register (read from operand)
    long long operands = cc_read_word(vm);
    int rs = (int)(operands & 0xFF);
    long long ret_addr = (long long)vm->pc;
    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = ret_addr;
    if (vm->flags & CCCC_CFI) {
        *--vm->shadow_sp = ret_addr;
    }
    Pc target = cc_byte_offset_to_pc(vm->regs[rs]);
    if (target == CCCC_INVALID_PC || target > vm->text_ptr) {
        printf("error: invalid indirect call target: %lld\n", vm->regs[rs]);
        return -1;
    }
    vm->pc = target;
    return 0;
}

static inline int op_CALLN_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rs = (int)(operands & 0xFF);
    InstrWord meta = cc_read_word(vm);
    int actual_nargs = (int)(meta & 0xFFFF);
    int returns_double = (int)((meta >> 16) & 1);
    int returns_float = (int)((meta >> 17) & 1);
    uint64_t double_arg_mask = (uint64_t)cc_read_i64(vm);
    uint64_t float_arg_mask = (uint64_t)cc_read_i64(vm);

    long long target_value = vm->regs[rs];
    DynamicSymbol *sym = cccc_find_dynamic_symbol(vm, target_value);
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
            } else if (i < 64 && (float_arg_mask & (1ULL << i))) {
                args[i] = (long long)(unsigned int)cccc_freg_raw_f32(
                    vm, FREG_A0 + fp_reg_idx++);
            } else if (i < 64 && (double_arg_mask & (1ULL << i))) {
                args[i] = cccc_freg_raw_f64(vm, FREG_A0 + fp_reg_idx++);
            } else {
                args[i] = vm->regs[REG_A0 + int_reg_idx++];
            }
        }

        int rc = cccc_call_native_function(vm, sym->func_ptr, sym->name, args,
                                          actual_nargs, double_arg_mask,
                                          float_arg_mask, returns_double,
                                          returns_float, 0, actual_nargs);
        free(heap_args);
        return rc;
    }

    // Built-in FFI function pointer (registered via cc_register_cfunc etc.)
    if (target_value <= CCCC_FFI_TOKEN_BASE) {
        int ffi_idx = (int)(CCCC_FFI_TOKEN_BASE - target_value);
        if (ffi_idx >= 0 && ffi_idx < vm->compiler.ffi_count) {
            ForeignFunc *ff = &vm->compiler.ffi_table[ffi_idx];
            long long args[8];
            int nargs = ff->num_args < 8 ? ff->num_args : 8;
            for (int i = 0; i < nargs; i++)
                args[i] = vm->regs[REG_A0 + i];
            return cccc_call_native_function(vm, ff->func_ptr, ff->name,
                                             args, nargs,
                                             ff->double_arg_mask, 0,
                                             ff->returns_double, ff->returns_float,
                                             ff->is_variadic, ff->num_fixed_args);
        }
    }

    long long ret_addr = (long long)vm->pc;
    if (check_stack_overflow(vm, 1)) return -1;
    *--vm->sp = ret_addr;
    if (vm->flags & CCCC_CFI) {
        *--vm->shadow_sp = ret_addr;
    }
    Pc target = cc_byte_offset_to_pc(target_value);
    if (target == CCCC_INVALID_PC || target > vm->text_ptr) {
        printf("error: invalid indirect call target: %lld\n", target_value);
        return -1;
    }
    vm->pc = target;
    return 0;
}

static inline int op_JMPT_fn(VirtualMachine *vm) {
    Pc table_pc = cc_read_word(vm);
    InstrWord count = cc_read_word(vm);
    Pc default_pc = cc_read_word(vm);
    long long index = vm->regs[REG_A0];
    if (index < 0 || index >= (long long)count) {
        vm->pc = default_pc;
        return 0;
    }
    vm->pc = vm->text_seg[table_pc + (Pc)index];
    return 0;
}

static inline int op_JMPI_fn(VirtualMachine *vm) {
    // Jump indirect - address in register specified by operand
    // Format: [JMPI] [rs:8|unused:56]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);
    long long target_value = vm->regs[rs];

    // dlopen-loaded function pointer
    DynamicSymbol *sym = cccc_find_dynamic_symbol(vm, target_value);
    if (sym)
        return cccc_call_native_function(vm, sym->func_ptr, sym->name,
                                         NULL, 0, 0, 0, 0, 0, 0, 0);

    // Built-in FFI function pointer (registered via cc_register_cfunc etc.)
    if (target_value <= CCCC_FFI_TOKEN_BASE) {
        int ffi_idx = (int)(CCCC_FFI_TOKEN_BASE - target_value);
        if (ffi_idx >= 0 && ffi_idx < vm->compiler.ffi_count) {
            ForeignFunc *ff = &vm->compiler.ffi_table[ffi_idx];
            long long args[8];
            int nargs = ff->num_args < 8 ? ff->num_args : 8;
            for (int i = 0; i < nargs; i++)
                args[i] = vm->regs[REG_A0 + i];
            return cccc_call_native_function(vm, ff->func_ptr, ff->name,
                                             args, nargs,
                                             ff->double_arg_mask, 0,
                                             ff->returns_double, ff->returns_float,
                                             ff->is_variadic, ff->num_fixed_args);
        }
    }

    Pc target = cc_byte_offset_to_pc(target_value);
    if (target == CCCC_INVALID_PC || target > vm->text_ptr) {
        printf("error: invalid indirect jump target: %lld\n", target_value);
        return -1;
    }
    vm->pc = target;
    return 0;
}

static inline int op_ADJ_fn(VirtualMachine *vm) {
    vm->sp = vm->sp + cc_read_i64(vm);
    return 0;
}

static inline int op_PSH3_fn(VirtualMachine *vm) {
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

static inline int op_POP3_fn(VirtualMachine *vm) {
    // Pop from stack into register: regs[rd] = *sp++
    // Format: [POP3] [rd:8|unused:56]
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    vm->regs[rd] = *vm->sp++;
    return 0;
}

// ========== Type Conversion Opcodes ==========

static inline int op_SX1_fn(VirtualMachine *vm) {
    // Sign extend 1 byte to 8 bytes without depending on host char signedness.
    // Format: [SX1] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(signed char)vm->regs[rs];
    return 0;
}

static inline int op_SX2_fn(VirtualMachine *vm) {
    // Sign extend 2 bytes to 8 bytes: regs[rd] = (long long)(short)regs[rs]
    // Format: [SX2] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(short)vm->regs[rs];
    return 0;
}

static inline int op_SX4_fn(VirtualMachine *vm) {
    // Sign extend 4 bytes to 8 bytes: regs[rd] = (long long)(int)regs[rs]
    // Format: [SX4] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(int)vm->regs[rs];
    return 0;
}

static inline int op_ZX1_fn(VirtualMachine *vm) {
    // Zero extend 1 byte to 8 bytes: regs[rd] = (long long)(unsigned
    // char)regs[rs] Format: [ZX1] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(unsigned char)vm->regs[rs];
    return 0;
}

static inline int op_ZX2_fn(VirtualMachine *vm) {
    // Zero extend 2 bytes to 8 bytes: regs[rd] = (long long)(unsigned
    // short)regs[rs] Format: [ZX2] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(unsigned short)vm->regs[rs];
    return 0;
}

static inline int op_ZX4_fn(VirtualMachine *vm) {
    // Zero extend 4 bytes to 8 bytes: regs[rd] = (long long)(unsigned
    // int)regs[rs] Format: [ZX4] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    vm->regs[rd] = (long long)(unsigned int)vm->regs[rs];
    return 0;
}

// ========== Memory Allocation Opcodes ==========

// Records a new heap allocation in vm->sorted_allocs for O(log n) interior-
// pointer lookups (DYNOBJSZ, and future CHKB/CHKP3 provenance checks).
// The VM heap is a pure bump allocator — heap_ptr only ever grows, so every
// new allocation's base address is higher than all previous ones, and a
// plain append preserves sorted order without a search.
static void sorted_allocs_insert(VirtualMachine *vm, void *user_ptr, AllocHeader *header) {
    if (vm->sorted_allocs.count == vm->sorted_allocs.capacity) {
        int new_cap = vm->sorted_allocs.capacity ? vm->sorted_allocs.capacity * 2 : 64;
        void **new_addrs = realloc(vm->sorted_allocs.addresses, (size_t)new_cap * sizeof(void *));
        if (new_addrs)
            vm->sorted_allocs.addresses = new_addrs;
        AllocHeader **new_headers = realloc(vm->sorted_allocs.headers, (size_t)new_cap * sizeof(AllocHeader *));
        if (new_headers)
            vm->sorted_allocs.headers = new_headers;
        if (!new_addrs || !new_headers) {
            // Internal bookkeeping OOM: skip tracking this allocation. The
            // allocation itself already succeeded; only interior-pointer
            // DYNOBJSZ queries on it will fall back to the conservative
            // value. Capacity is left unchanged, and whichever of the two
            // arrays *did* reallocate successfully above has already been
            // written back, so both pointers remain valid to free later.
            return;
        }
        vm->sorted_allocs.capacity = new_cap;
    }
    vm->sorted_allocs.addresses[vm->sorted_allocs.count] = user_ptr;
    vm->sorted_allocs.headers[vm->sorted_allocs.count] = header;
    vm->sorted_allocs.count++;
}

// Bump-allocate `requested_size` bytes from the VM heap with an AllocHeader
// immediately preceding the returned pointer, aligned to `alignment` bytes
// (must be a power of two; the caller is responsible for validating that).
// alignment=8 is the default (malloc/calloc/realloc) path and — because
// sizeof(AllocHeader) is a multiple of 8 (see the _Static_assert next to its
// definition) and the bump pointer only ever advances by multiples of 8 —
// never introduces padding, so it is exactly the original MALC behaviour.
//
// For larger alignments, padding is inserted *before* the header (never
// between the header and the user pointer), so every consumer that recovers
// the header via `((AllocHeader*)ptr) - 1` — MFRE, REALC, DYNOBJSZ, CHKB,
// heap_alloc_for_ptr — keeps working unmodified. The pad bytes are simply
// wasted (never reclaimed), consistent with this being a bump/mark-freed
// allocator with no free-list reuse.
//
// Returns NULL (without touching any VM register) on invalid size or OOM;
// callers translate that into their own error-reporting convention.
static inline void *vm_heap_bump_alloc(VirtualMachine *vm, long long requested_size, size_t alignment) {
    if (requested_size <= 0)
        return NULL;

    // Align the requested size to 8 bytes as before.
    size_t size = (requested_size + 7) & ~7;

    // Compute padding so that (base + pad + sizeof(AllocHeader)) — i.e. the
    // user pointer — lands on an `alignment` boundary.
    size_t base = (size_t)vm->heap_ptr;
    size_t header_off = base + sizeof(AllocHeader);
    size_t pad = (alignment - (header_off % alignment)) % alignment;

    size_t total_size = pad + sizeof(AllocHeader) + size;

    // Reserve space for rear heap canary when enabled
    if (vm->flags & CCCC_HEAP_CANARIES)
        total_size += sizeof(long long);

    // Check for OOM — try to grow the heap before giving up
    size_t available = (size_t)(vm->heap_end - vm->heap_ptr);
    if (total_size > available) {
        if (vm_heap_grow(vm, total_size) != 0)
            return NULL; // Out of memory (reservation exhausted)
        // vm_heap_grow only extends heap_end (commits more of the reserved
        // pool); heap_ptr itself is unchanged, so pad/total_size still hold.
        available = (size_t)(vm->heap_end - vm->heap_ptr);
        if (total_size > available)
            return NULL;
    }

    // Allocate from bump pointer, skipping the alignment pad
    AllocHeader *header = (AllocHeader *)(vm->heap_ptr + pad);
    header->size = size;
    header->requested_size = requested_size;
    header->magic = 0xDEADBEEF;
    header->freed = 0;
    header->generation = 0;
    header->creation_generation = 0;
    header->canary = 0;
    header->alloc_pc = vm->text_seg ? (long long)vm->pc : 0;
    header->type_kind = TY_VOID;

    vm->heap_ptr = vm->heap_ptr + total_size;
    void *user_ptr = (void *)(header + 1);

    // Track base address -> header for O(log n) interior-pointer lookups.
    sorted_allocs_insert(vm, user_ptr, header);

    // Heap canaries: write front + rear guard values
    if (vm->flags & CCCC_HEAP_CANARIES) {
        header->canary = vm->stack_canary;
        *(long long *)((char *)user_ptr + size) = vm->stack_canary;
    }

    // Memory poisoning: fill with 0xCD ("clean memory") pattern
    if (vm->flags & CCCC_MEMORY_POISONING)
        memset(user_ptr, 0xCD, size);

    // Leak detection: track active allocation
    if (vm->flags & CCCC_MEMORY_LEAK_DETECT) {
        AllocRecord *rec = (AllocRecord *)malloc(sizeof(AllocRecord));
        if (rec) {
            rec->address = user_ptr;
            rec->size = requested_size;
            rec->alloc_pc = header->alloc_pc;
            rec->next = vm->alloc_list;
            vm->alloc_list = rec;
        }
    }

    // Memory tagging: register pointer→generation in side table
    if (vm->flags & CCCC_MEMORY_TAGGING)
        hashmap_put_int(&vm->ptr_tags, (long long)user_ptr, (void *)(intptr_t)0);

    return user_ptr;
}

static inline int op_MALC_fn(VirtualMachine *vm) {
    // malloc: size in REG_A0, return pointer in REG_A0
    long long requested_size = vm->regs[REG_A0];
    void *user_ptr = vm_heap_bump_alloc(vm, requested_size, 8);
    vm->regs[REG_A0] = (long long)user_ptr;

    if (vm->debug_vm && user_ptr) {
        printf("MALC: allocated %zu bytes at 0x%llx\n", (size_t)((requested_size + 7) & ~7), vm->regs[REG_A0]);
    }
    return 0;
}

static inline int op_MALCA_fn(VirtualMachine *vm) {
    // aligned_alloc: size in REG_A0, alignment in REG_A1, return pointer in REG_A0
    long long requested_size = vm->regs[REG_A0];
    size_t alignment = (size_t)vm->regs[REG_A1];

    // aligned_alloc requires a power-of-two alignment; reject anything else
    // (and anything smaller than the default 8-byte alignment just uses 8).
    if (alignment == 0 || (alignment & (alignment - 1)) != 0) {
        vm->regs[REG_A0] = 0;
        return 0;
    }
    if (alignment < 8)
        alignment = 8;

    void *user_ptr = vm_heap_bump_alloc(vm, requested_size, alignment);
    vm->regs[REG_A0] = (long long)user_ptr;

    if (vm->debug_vm && user_ptr) {
        printf("MALCA: allocated %zu bytes aligned to %zu at 0x%llx\n", (size_t)requested_size, alignment,
               vm->regs[REG_A0]);
    }
    return 0;
}

static inline int op_PMEMA_fn(VirtualMachine *vm) {
    // posix_memalign: memptr in REG_A0, alignment in REG_A1, size in REG_A2,
    // return status (0/EINVAL/ENOMEM) in REG_A0.
    void **memptr = (void **)vm->regs[REG_A0];
    size_t alignment = (size_t)vm->regs[REG_A1];
    long long requested_size = vm->regs[REG_A2];

    // POSIX: alignment must be a power of two and a multiple of sizeof(void*).
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || alignment % sizeof(void *) != 0) {
        vm->regs[REG_A0] = EINVAL;
        return 0;
    }

    if (requested_size == 0) {
        // Implementation-defined: either NULL or a unique freeable pointer.
        // Return a minimal unique allocation, matching glibc's behaviour.
        requested_size = 1;
    }

    void *user_ptr = vm_heap_bump_alloc(vm, requested_size, alignment);
    if (!user_ptr) {
        vm->regs[REG_A0] = ENOMEM;
        return 0;
    }

    *memptr = user_ptr;
    vm->regs[REG_A0] = 0;

    if (vm->debug_vm) {
        printf("PMEMA: allocated %zu bytes aligned to %zu at 0x%llx\n", (size_t)requested_size, alignment,
               (long long)user_ptr);
    }
    return 0;
}

// True if `ptr` has room for an AllocHeader below it inside the VM heap
// arena, i.e. it is safe to read `((AllocHeader *)ptr) - 1` without running
// off the front/back of the arena. Host-allocator pointers (e.g. from
// Block_copy, strdup, aligned_alloc) live outside [heap_seg, heap_end) and
// must never have their "header" peeked — doing so reads into whatever
// host allocation happens to precede them (caught by ASan as a
// heap-buffer-overflow; see #709).
static inline int is_vm_heap_ptr(VirtualMachine *vm, void *ptr) {
    return (char *)ptr >= vm->heap_seg + sizeof(AllocHeader) && (char *)ptr < vm->heap_end;
}

static inline int op_MFRE_fn(VirtualMachine *vm) {
    // free: pointer in REG_A0
    void *ptr = (void *)vm->regs[REG_A0];
    if (!ptr) {
        return 0; // free(NULL) is a no-op
    }

    // Pointer isn't inside the VM heap arena at all — it came from a host
    // allocator (e.g. Block_copy's malloc). There is no AllocHeader to read;
    // peeking at ptr-sizeof(AllocHeader) would read out of bounds. Fall
    // back to system free directly.
    if (!is_vm_heap_ptr(vm, ptr)) {
        free(ptr);
        return 0;
    }

    AllocHeader *header = ((AllocHeader *)ptr) - 1;

    // Validate header — if magic is wrong the pointer came from a non-VM
    // allocator (e.g. strdup, aligned_alloc). Fall back to system free so
    // mixed-allocator programs work correctly under -V.
    if (header->magic != 0xDEADBEEF) {
        free(ptr);
        return 0;
    }

    if (header->freed) {
        printf("\n========== DOUBLE FREE ==========\n");
        printf("Pointer already freed: 0x%llx\n", (long long)ptr);
        printf("===================================\n");
        return -1;
    }

    // Heap canary check: verify front and rear guard values
    if (vm->flags & CCCC_HEAP_CANARIES) {
        if (header->canary != vm->stack_canary) {
            printf("\n========== HEAP CANARY CORRUPTED ==========\n");
            printf("Front canary overwritten at 0x%llx\n", (long long)ptr);
            printf("Expected: 0x%llx\n", vm->stack_canary);
            printf("Found:    0x%llx\n", header->canary);
            printf("===========================================\n");
            return -1;
        }
        long long rear = *(long long *)((char *)ptr + header->size);
        if (rear != vm->stack_canary) {
            printf("\n========== HEAP CANARY CORRUPTED ==========\n");
            printf("Rear canary overwritten: heap overflow past allocation at 0x%llx\n", (long long)ptr);
            printf("Allocation size: %zu bytes\n", header->size);
            printf("Expected: 0x%llx\n", vm->stack_canary);
            printf("Found:    0x%llx\n", rear);
            printf("===========================================\n");
            return -1;
        }
    }

    header->freed = 1;
    header->generation++;

    // Memory poisoning: fill with 0xDD ("dead memory") pattern
    if (vm->flags & CCCC_MEMORY_POISONING)
        memset(ptr, 0xDD, header->size);

    // Memory tagging: side table is NOT updated on free — the stored generation
    // (set at malloc time) stays so stale pointers still fail the CHKP3 check.

    // Leak detection: remove from active allocation list
    if (vm->flags & CCCC_MEMORY_LEAK_DETECT) {
        AllocRecord *prev = NULL;
        AllocRecord *cur = vm->alloc_list;
        while (cur) {
            if (cur->address == ptr) {
                if (prev)
                    prev->next = cur->next;
                else
                    vm->alloc_list = cur->next;
                free(cur);
                break;
            }
            prev = cur;
            cur = cur->next;
        }
    }

    if (vm->debug_vm) {
        printf("MFRE: freed pointer 0x%llx\n", (long long)ptr);
    }
    return 0;
}

static inline int op_MCPY_fn(VirtualMachine *vm) {
    // memcpy: dest in REG_A0, src in REG_A1, count in REG_A2
    void *dest = (void *)vm->regs[REG_A0];
    void *src = (void *)vm->regs[REG_A1];
    size_t count = (size_t)vm->regs[REG_A2];
    memcpy(dest, src, count);
    return 0;
}

static inline int op_MSET_fn(VirtualMachine *vm) {
    // Zero-fill: dest in REG_A0, count in REG_A2 (REG_A1 unused, value is always 0)
    // Backs ND_MEMZERO (the pre-zero step emitted for partial aggregate initialisers).
    void *dest = (void *)vm->regs[REG_A0];
    size_t count = (size_t)vm->regs[REG_A2];
    memset(dest, 0, count);
    return 0;
}

// ========== Wide _BitInt(N>64) multi-word arithmetic/shifts ==========
// Thin dispatch into the existing runtime helpers in src/stdlib/wide_bitint.c
// (still used directly by the FFI/CALLF path for AND/OR/XOR/CMP/casts) —
// bypasses CALLF argument marshalling for the 8 hot ops in #456.
extern void __cccc_bitint_add(uint64_t *dst, const uint64_t *a,
                               const uint64_t *b, int words, int width);
extern void __cccc_bitint_sub(uint64_t *dst, const uint64_t *a,
                               const uint64_t *b, int words, int width);
extern void __cccc_bitint_mul(uint64_t *dst, const uint64_t *a,
                               const uint64_t *b, int words, int width);
extern void __cccc_bitint_sdiv(uint64_t *dst, const uint64_t *a,
                                const uint64_t *b, int words, int width);
extern void __cccc_bitint_udiv(uint64_t *dst, const uint64_t *a,
                                const uint64_t *b, int words, int width);
extern void __cccc_bitint_smod(uint64_t *dst, const uint64_t *a,
                                const uint64_t *b, int words, int width);
extern void __cccc_bitint_umod(uint64_t *dst, const uint64_t *a,
                                const uint64_t *b, int words, int width);
extern void __cccc_bitint_shl(uint64_t *dst, const uint64_t *a,
                               long long shift, int words, int width);
extern void __cccc_bitint_sshr(uint64_t *dst, const uint64_t *a,
                                long long shift, int words, int width);
extern void __cccc_bitint_ushr(uint64_t *dst, const uint64_t *a,
                                long long shift, int words, int width);

static inline int op_WIDE_ADD_fn(VirtualMachine *vm) {
    __cccc_bitint_add((uint64_t *)vm->regs[REG_A0], (uint64_t *)vm->regs[REG_A1],
                       (uint64_t *)vm->regs[REG_A2], (int)vm->regs[REG_A3],
                       (int)vm->regs[REG_A4]);
    return 0;
}

static inline int op_WIDE_SUB_fn(VirtualMachine *vm) {
    __cccc_bitint_sub((uint64_t *)vm->regs[REG_A0], (uint64_t *)vm->regs[REG_A1],
                       (uint64_t *)vm->regs[REG_A2], (int)vm->regs[REG_A3],
                       (int)vm->regs[REG_A4]);
    return 0;
}

static inline int op_WIDE_MUL_fn(VirtualMachine *vm) {
    __cccc_bitint_mul((uint64_t *)vm->regs[REG_A0], (uint64_t *)vm->regs[REG_A1],
                       (uint64_t *)vm->regs[REG_A2], (int)vm->regs[REG_A3],
                       (int)vm->regs[REG_A4]);
    return 0;
}

static inline int op_WIDE_DIV_fn(VirtualMachine *vm) {
    uint64_t *dst = (uint64_t *)vm->regs[REG_A0];
    const uint64_t *a = (const uint64_t *)vm->regs[REG_A1];
    const uint64_t *b = (const uint64_t *)vm->regs[REG_A2];
    int words = (int)vm->regs[REG_A3];
    int width = (int)vm->regs[REG_A4];
    if (vm->regs[REG_A5])
        __cccc_bitint_sdiv(dst, a, b, words, width);
    else
        __cccc_bitint_udiv(dst, a, b, words, width);
    return 0;
}

static inline int op_WIDE_MOD_fn(VirtualMachine *vm) {
    uint64_t *dst = (uint64_t *)vm->regs[REG_A0];
    const uint64_t *a = (const uint64_t *)vm->regs[REG_A1];
    const uint64_t *b = (const uint64_t *)vm->regs[REG_A2];
    int words = (int)vm->regs[REG_A3];
    int width = (int)vm->regs[REG_A4];
    if (vm->regs[REG_A5])
        __cccc_bitint_smod(dst, a, b, words, width);
    else
        __cccc_bitint_umod(dst, a, b, words, width);
    return 0;
}

static inline int op_WIDE_SHL_fn(VirtualMachine *vm) {
    __cccc_bitint_shl((uint64_t *)vm->regs[REG_A0], (uint64_t *)vm->regs[REG_A1],
                       vm->regs[REG_A2], (int)vm->regs[REG_A3],
                       (int)vm->regs[REG_A4]);
    return 0;
}

static inline int op_WIDE_SHR_fn(VirtualMachine *vm) {
    __cccc_bitint_sshr((uint64_t *)vm->regs[REG_A0], (uint64_t *)vm->regs[REG_A1],
                        vm->regs[REG_A2], (int)vm->regs[REG_A3],
                        (int)vm->regs[REG_A4]);
    return 0;
}

static inline int op_WIDE_USHR_fn(VirtualMachine *vm) {
    __cccc_bitint_ushr((uint64_t *)vm->regs[REG_A0], (uint64_t *)vm->regs[REG_A1],
                        vm->regs[REG_A2], (int)vm->regs[REG_A3],
                        (int)vm->regs[REG_A4]);
    return 0;
}

static inline int op_REALC_fn(VirtualMachine *vm) {
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

    // Pointer isn't inside the VM heap arena at all (e.g. Block_copy's
    // malloc) — there is no AllocHeader to read, so don't peek at
    // ptr-sizeof(AllocHeader); fall back to system realloc directly.
    if (!is_vm_heap_ptr(vm, ptr)) {
        void *new_ptr = realloc(ptr, (size_t)new_size);
        vm->regs[REG_A0] = (long long)new_ptr;
        return 0;
    }

    AllocHeader *old_header = ((AllocHeader *)ptr) - 1;

    // If the pointer didn't come from the VM heap, fall back to system realloc.
    if (old_header->magic != 0xDEADBEEF) {
        void *new_ptr = realloc(ptr, (size_t)new_size);
        vm->regs[REG_A0] = (long long)new_ptr;
        return 0;
    }

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

    // Free old block through op_MFRE_fn so all free-path checks run
    vm->regs[REG_A0] = (long long)ptr;
    int free_result = op_MFRE_fn(vm);
    vm->regs[REG_A0] = (long long)new_ptr;
    if (free_result != 0)
        return free_result;

    return 0;
}

static inline int op_CALC_fn(VirtualMachine *vm) {
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

// reallocarray(ptr, nmemb, size) (#699): routes through the VM heap the same
// way malloc/calloc/realloc/aligned_alloc do, giving it full heap-safety
// coverage (AllocHeader tracking, sorted_allocs, bounds/UAF checks). Unlike
// CALC's plain nmemb*size, this checks for multiplication overflow before
// delegating to op_REALC_fn -- overflow detection is the entire reason to
// call reallocarray instead of realloc(ptr, nmemb*size), so silently
// truncating it here would defeat the function's purpose.
static inline int op_REALCA_fn(VirtualMachine *vm) {
    // ptr in REG_A0 (passed through to REALC), nmemb in REG_A1, size in
    // REG_A2, return in REG_A0.
    long long nmemb = vm->regs[REG_A1];
    long long size  = vm->regs[REG_A2];

    if (nmemb < 0 || size < 0 || (size != 0 && nmemb > (INT64_MAX / size))) {
        // Overflow (or a negative argument): the original allocation is left
        // untouched and NULL is returned, matching real reallocarray/ENOMEM
        // semantics -- unlike realloc(ptr, 0), this must NOT free ptr.
        vm->regs[REG_A0] = 0;
        return 0;
    }

    vm->regs[REG_A1] = nmemb * size;
    return op_REALC_fn(vm);
}

// ========== Dynamic Object Size Opcode ==========

static inline int op_DYNOBJSZ_fn(VirtualMachine *vm) {
    // Runtime object byte-size: rd = remaining bytes in the heap allocation
    // containing regs[rs] (base or interior pointer).
    // Format: [DYNOBJSZ] [rd:8|rs:8|unused:48] [type:i64]
    //
    // Mirrors __builtin_object_size semantics at runtime:
    //   type bit 1 == 0 → max fallback (size_t)-1 on unknown (type 0 or 1)
    //   type bit 1 == 1 → min fallback 0 on unknown (type 2 or 3)
    //
    // Looks up the containing allocation via vm->sorted_allocs (binary search
    // for the largest tracked base address <= ptr), then computes
    // requested_size - (ptr - base). This handles both base pointers
    // (offset 0) and interior pointers (p + k) uniformly. Non-heap, freed,
    // and out-of-range pointers (offset > requested_size, e.g. into the
    // alignment padding past the requested bytes) fall through to the stack
    // path below (or the conservative fallback if that also misses).
    //
    // alloca()/VLA buffers need no separate handling here: both lower to
    // MALC (a VM heap bump allocation with an AllocHeader), so they already
    // resolve through the heap path above (#648).
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long type = cc_read_i64(vm);

    long long ptr = vm->regs[rs];

    // Conservative fallback depends on the type argument (bit 1).
    size_t result = (type & 2) ? 0 : (size_t)-1;
    bool resolved = false;

    // Only VM heap pointers have a tracked allocation.
    if (ptr != 0 &&
        ptr >= (long long)vm->heap_seg &&
        ptr < (long long)vm->heap_end) {
        int idx = sorted_allocs_find(vm, (void *)ptr);
        if (idx >= 0) {
            AllocHeader *h = vm->sorted_allocs.headers[idx];
            void *base = vm->sorted_allocs.addresses[idx];
            size_t offset = (size_t)((char *)ptr - (char *)base);
            if (h->magic == 0xDEADBEEF && !h->freed &&
                offset <= h->requested_size) {
                result = h->requested_size - offset;
                resolved = true;
            }
        }
    }

    // Fixed-size stack arrays/structs/unions whose address escapes (so their
    // provenance is opaque by the time DYNOBJSZ sees the pointer -- e.g.
    // `char buf[16]` passed through a function parameter) are recorded by
    // STKTAG into vm->stack_intervals as [base, base+size) tagged with their
    // creating frame's epoch (#675's mechanism, extended by #648). Resolve
    // ptr via the same max-epoch stab CHKP3 uses for interior dangling-
    // pointer resolution, but only trust the match while its frame's epoch
    // is still live: a match whose epoch has retired means the owning frame
    // already returned (the pointer is dangling), and the conservative
    // fallback -- not a stale size -- is the correct answer there.
    if (!resolved && ptr != 0) {
        unsigned long long iv_epoch;
        long long hi;
        if (stack_interval_stab(vm, ptr, &iv_epoch, &hi) &&
            hashmap_get_int(&vm->live_epochs, (long long)iv_epoch)) {
            result = (size_t)(hi - ptr);
        }
    }

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)(size_t)result;
    return 0;
}

// ========== Safety Opcodes ==========

static inline int op_CHKB_fn(VirtualMachine *vm) {
    // Check array bounds.
    // Format: [CHKB] [rs1:base, rs2:scaled_offset] (RR operand word)
    // rs1 = base pointer, rs2 = scaled byte offset (index * element_size)
    long long operands = cc_read_word(vm);
    int rs1, rs2;
    DECODE_RR(operands, rs1, rs2);

    if (!(vm->flags & CCCC_BOUNDS_CHECKS))
        return 0;

    long long base         = vm->regs[rs1];
    long long scaled_offset = vm->regs[rs2];

    // Resolve the containing allocation — handles base pointers and
    // interior pointers (base = q + k) alike via sorted_allocs_find (#650).
    // The bound is checked against the *effective* offset from the
    // allocation's own base, not against `base` itself, so a negative
    // scaled_offset is only an error if it steps before the allocation
    // start (e.g. p = q+2; p[-1] is valid; p[-3] is not).
    size_t base_off;
    AllocHeader *header = heap_alloc_for_ptr(vm, base, &base_off);
    if (header) {
        long long eff = (long long)base_off + scaled_offset;
        if (eff < 0 || eff >= (long long)header->size) {
            printf("\n========== ARRAY BOUNDS ERROR ==========\n");
            printf("Array index out of bounds\n");
            printf("Scaled offset: %lld bytes (base offset %zu)\n",
                   scaled_offset, base_off);
            printf("Array size:    %zu bytes\n", header->size);
            printf("Base address:  0x%llx\n", base);
            printf("Allocated at PC offset: %lld\n", header->alloc_pc);
            printf("PC: 0x%llx (offset: %lld)\n",
                   (long long)vm->pc, (long long)vm->pc);
            printf("=========================================\n");
            return -1;
        }
        return 0;
    }

    // Non-heap or untracked base (stack/global arrays): no upper bound is
    // known here, but a negative index is unconditionally invalid.
    if (scaled_offset < 0) {
        printf("\n========== ARRAY BOUNDS ERROR ==========\n");
        printf("Negative array index (scaled offset: %lld)\n", scaled_offset);
        printf("Base address: 0x%llx\n", base);
        printf("PC: 0x%llx (offset: %lld)\n",
               (long long)vm->pc, (long long)vm->pc);
        printf("=========================================\n");
        return -1;
    }

    return 0;
}

static inline int op_CHKI_fn(VirtualMachine *vm) {
    // Check initialization: fail if variable at bp+offset has not been written.
    // Format: [CHKI] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & CCCC_UNINIT_DETECTION))
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

static inline int op_MARKI_fn(VirtualMachine *vm) {
    // Mark variable at bp+offset as initialized.
    // Format: [MARKI] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & CCCC_UNINIT_DETECTION))
        return 0;

    long long addr = (long long)(vm->bp + offset);
    hashmap_put_int(&vm->init_state, addr, (void *)1);
    return 0;
}

static inline int op_CHKPA_fn(VirtualMachine *vm) {
    // Check pointer arithmetic result against its recorded provenance.
    // Format: [CHKPA] [rs:ptr_result]
    long long operands = cc_read_word(vm);
    int rs;
    DECODE_R(operands, rs);

    if (!(vm->flags & CCCC_INVALID_ARITH) || !(vm->flags & CCCC_PROVENANCE_TRACK))
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

static inline int op_MARKP_fn(VirtualMachine *vm) {
    // Record provenance for a pointer (origin, base, size).
    // Format: [MARKP] [rs_ptr:8 | rs_base:8] [origin_type:i64] [size:i64]
    long long operands = cc_read_word(vm);
    int rs_ptr, rs_base;
    DECODE_RR(operands, rs_ptr, rs_base);
    int    origin_type = (int)cc_read_word(vm);
    size_t size        = (size_t)cc_read_i64(vm);

    if (!(vm->flags & CCCC_PROVENANCE_TRACK))
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

static inline int op_SCOPEIN_fn(VirtualMachine *vm) {
    // Activate all stack variables belonging to scope_id.
    // Format: [SCOPEIN] [scope_id:i64]
    int scope_id = (int)cc_read_word(vm);

    if (!(vm->flags & CCCC_STACK_INSTR))
        return 0;

    if (vm->debug_vm)
        printf("SCOPEIN: entering scope %d (bp=0x%llx)\n", scope_id, (long long)vm->bp);

    // PLACEHOLDER: O(capacity) full hashmap scan on every scope entry. The
    // VM already maintains vm->scope_vars per-scope linked lists during
    // codegen; iterate that list (scope_vars[scope_id].head) for O(vars in
    // scope) instead of O(total tracked vars).
    // Ticket: https://todo.sr.ht/~takeiteasy/cccc/159
    for (int i = 0; i < vm->stack_var_meta.capacity; i++) {
        HashEntry *ent = &vm->stack_var_meta.buckets[i];
        if (!ent->key || ent->key == (char *)-1)
            continue;
        StackVarMeta *meta = (StackVarMeta *)ent->val;
        if (meta && meta->scope_id == scope_id) {
            meta->is_alive = 1;
            meta->bp       = (long long)vm->bp;
            // Record this specific activation's liveness by its actual
            // runtime address (bp+offset), not just the declaration-level
            // scope_id/bp pair -- recursive calls of the same function
            // enter this scope_id multiple times with different bp values,
            // and meta->bp above can only remember the most recent one
            // (#671).
            hashmap_put_int(&vm->stack_var_active,
                             (long long)vm->bp + meta->offset, meta);
            if (vm->debug_vm)
                printf("  Activated '%s' at bp%+lld\n", meta->name, meta->offset);
        }
    }
    return 0;
}

static inline int op_SCOPEOUT_fn(VirtualMachine *vm) {
    // Deactivate variables in scope_id and detect dangling pointers.
    // Format: [SCOPEOUT] [scope_id:i64]
    int scope_id = (int)cc_read_word(vm);

    if (!(vm->flags & CCCC_STACK_INSTR))
        return 0;

    if (vm->debug_vm)
        printf("SCOPEOUT: exiting scope %d (bp=0x%llx)\n", scope_id, (long long)vm->bp);

    // PLACEHOLDER: O(capacity) full hashmap scan on every scope exit; same
    // fix as SCOPEIN — walk vm->scope_vars[scope_id] directly.
    // Ticket: https://todo.sr.ht/~takeiteasy/cccc/159
    for (int i = 0; i < vm->stack_var_meta.capacity; i++) {
        HashEntry *ent = &vm->stack_var_meta.buckets[i];
        if (!ent->key || ent->key == (char *)-1)
            continue;
        StackVarMeta *meta = (StackVarMeta *)ent->val;
        if (meta && meta->scope_id == scope_id) {
            // Clear this exact activation's liveness entry. This uses the
            // current (exiting) frame's own bp, not meta->bp -- correct
            // regardless of whether a recursive call already overwrote
            // meta->bp with a deeper activation's value (#671).
            hashmap_delete_int(&vm->stack_var_active,
                                (long long)vm->bp + meta->offset);
            if (meta->bp == (long long)vm->bp) {
                meta->is_alive = 0;
                if (vm->debug_vm)
                    printf("  Deactivated '%s' at bp%+lld (reads=%lld writes=%lld)\n",
                           meta->name, meta->offset, meta->read_count, meta->write_count);
            }
        }
    }

    // NOTE (#669/#670): this used to hard-error here whenever a still-tracked
    // MARKA-recorded pointer matched the exiting scope/bp. That conflated
    // "address-taken" with "escaped" -- MARKA tracked *every* `&local`,
    // always tagged with the function's own top-level scope_id, so the check
    // only ever fired at *function* exit, where the whole frame dies
    // together and nothing can actually dangle. It had zero true-positive
    // capability and aborted on fully benign code. Real enforcement is now a
    // precise dereference-time range check in op_CHKP3_fn (#670); the
    // MARKA opcode and vm->stack_ptrs tracking it depended on have been
    // removed as dead placeholder infrastructure.
    return 0;
}

static inline int op_CHKL_fn(VirtualMachine *vm) {
    // Check variable liveness before access (use-after-scope / use-after-return).
    // Format: [CHKL] [offset:i64] [scope_id]
    long long offset   = cc_read_i64(vm);
    int       scope_id = (int)cc_read_word(vm);

    if (!(vm->flags & CCCC_STACK_INSTR))
        return 0;

    // Liveness is keyed by actual runtime address (bp+offset), not by a
    // single scope_id/bp pair on the declaration record -- recursive calls
    // of the same function have multiple simultaneous activations sharing
    // one scope_id, and a single "current bp" field on the declaration
    // can't represent "is *this* activation still live" (#671). Each
    // activation gets its own entry in stack_var_active via SCOPEIN/SCOPEOUT.
    long long addr = (long long)vm->bp + offset;
    if (hashmap_get_int(&vm->stack_var_active, addr))
        return 0;

    // Not live. scope_id (unused for the liveness decision above) resolves
    // the declaration record purely for the error message.
    StackVarMeta *meta = (StackVarMeta *)hashmap_get_int(
        &vm->stack_var_meta, stack_var_meta_key(scope_id, offset));
    if (!meta)
        return 0;

    if (vm->flags & CCCC_STACK_INSTR_ERRORS) {
        printf("\n========== USE AFTER SCOPE/RETURN DETECTED ==========\n");
        printf("Variable '%s' at bp%+lld accessed while not live (its scope "
               "has exited or its function has returned)\n",
               meta->name, meta->offset);
        printf("PC: 0x%llx (offset: %lld)\n", (long long)vm->pc, (long long)vm->pc);
        printf("=======================================================\n");
        return -1;
    } else if (vm->debug_vm) {
        printf("WARNING: Variable '%s' accessed while not live\n", meta->name);
    }
    return 0;
}

static inline int op_MARKR_fn(VirtualMachine *vm) {
    // Record a read access to the variable at bp+offset.
    // Format: [MARKR] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & CCCC_STACK_INSTR))
        return 0;

    // Looked up by actual runtime address, which yields the correct
    // declaration record directly regardless of recursion (#671) -- see
    // op_CHKL_fn.
    StackVarMeta *meta = (StackVarMeta *)hashmap_get_int(
        &vm->stack_var_active, (long long)vm->bp + offset);
    if (meta) {
        meta->read_count++;
        if (vm->debug_vm)
            printf("MARKR: '%s' read (count=%lld)\n", meta->name, meta->read_count);
    }
    return 0;
}

static inline int op_MARKW_fn(VirtualMachine *vm) {
    // Record a write access to the variable at bp+offset; marks it initialized.
    // Format: [MARKW] [offset:i64]
    long long offset = cc_read_i64(vm);

    if (!(vm->flags & CCCC_STACK_INSTR))
        return 0;

    StackVarMeta *meta = (StackVarMeta *)hashmap_get_int(
        &vm->stack_var_active, (long long)vm->bp + offset);
    if (meta) {
        meta->write_count++;
        if (!meta->initialized)
            meta->initialized = 1;
        if (vm->debug_vm)
            printf("MARKW: '%s' write (count=%lld)\n", meta->name, meta->write_count);
    }
    return 0;
}

// ========== Setjmp/Longjmp ==========

static inline int op_SETJMP_fn(VirtualMachine *vm) {
    // setjmp: jmp_buf address in REG_A0, return 0 in REG_A0
    long long *jmp_buf = (long long *)vm->regs[REG_A0];
    jmp_buf[0] = (long long)vm->pc;
    jmp_buf[1] = (long long)vm->sp;
    jmp_buf[2] = (long long)vm->bp;
    if (vm->flags & CCCC_CFI)
        jmp_buf[3] = (long long)((char *)vm->shadow_sp - (char *)vm->shadow_stack);
    else
        jmp_buf[3] = -1;
    vm->regs[REG_A0] = 0; // setjmp returns 0 on direct call
    return 0;
}

static inline int op_LONGJMP_fn(VirtualMachine *vm) {
    // longjmp: jmp_buf address in REG_A0, value in REG_A1
    long long *jmp_buf = (long long *)vm->regs[REG_A0];
    long long val = vm->regs[REG_A1];
    vm->pc = (Pc)jmp_buf[0];
    vm->sp = (long long *)jmp_buf[1];
    vm->bp = (long long *)jmp_buf[2];

    // longjmp can unwind several frames at once (#673, #648): retire every
    // activation's liveness epoch above the target frame. The target frame
    // itself (bp == vm->bp) keeps its epoch -- it's still live.
    if (stack_extents_enabled(vm))
        frame_epoch_truncate_to(vm, vm->bp);

    if (vm->flags & CCCC_CFI) {
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

static inline int op_DLOPEN_fn(VirtualMachine *vm) {
    const char *path = (const char *)vm->regs[REG_A0];
    int mode = (int)vm->regs[REG_A1];
    vm->regs[REG_A0] = cccc_rt_dlopen(vm, path, mode);
    return 0;
}

static inline int op_DLSYM_fn(VirtualMachine *vm) {
    long long handle = vm->regs[REG_A0];
    const char *symbol = (const char *)vm->regs[REG_A1];
    vm->regs[REG_A0] = cccc_rt_dlsym(vm, handle, symbol);
    return 0;
}

static inline int op_DLCLOSE_fn(VirtualMachine *vm) {
    vm->regs[REG_A0] = cccc_rt_dlclose(vm, vm->regs[REG_A0]);
    return 0;
}

static inline int op_DLERROR_fn(VirtualMachine *vm) {
    vm->regs[REG_A0] = cccc_rt_dlerror(vm);
    return 0;
}

// ========== FFI ==========

static _Thread_local VirtualMachine *cccc_tls_ffi_vm;

VirtualMachine *cccc_current_ffi_vm(void) {
    return cccc_tls_ffi_vm;
}

int cccc_ffi_name_in_list(char **list, int count, const char *name) {
    if (!name)
        name = "<anonymous>";
    size_t len = strlen(name);
    for (int i = 0; i < count; i++) {
        if (strlen(list[i]) == len && memcmp(list[i], name, len) == 0)
            return 1;
    }
    return 0;
}

static int cccc_handle_ffi_policy_error(VirtualMachine *vm, const char *kind,
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
    cccc_freg_set_f64(vm, FREG_A0, 0.0);
    return vm->ffi_errors_fatal ? -1 : 0;
}

static int cccc_check_ffi_policy(VirtualMachine *vm, const char *name, int actual_nargs,
                                int is_variadic, int num_fixed_args) {
    // In --build mode the builder API (the __builtin_build_* runtime injected by
    // building.h) is the build runtime itself, not user FFI. It is always
    // callable regardless of --ffi-allow/--ffi-deny/--disable-ffi, exactly as
    // the host-spawned cc/ar/ld are. Only the tool calls a script makes are
    // gated by the FFI policy.
    if (vm->compiler.build_mode && name &&
        strlen(name) >= 16 && memcmp(name, "__builtin_build_", 16) == 0)
        return 1;

    if (vm->disable_all_ffi)
        return cccc_handle_ffi_policy_error(
            vm, "FFI Disabled", name,
            "All FFI calls are disabled via --disable-ffi");

    if (vm->ffi_allow_count > 0 &&
        !cccc_ffi_name_in_list(vm->ffi_allow_list, vm->ffi_allow_count, name))
        return cccc_handle_ffi_policy_error(vm, "FFI Access Denied", name,
                                           "Function not in allow list");

    if (vm->ffi_allow_count == 0 &&
        cccc_ffi_name_in_list(vm->ffi_deny_list, vm->ffi_deny_count, name))
        return cccc_handle_ffi_policy_error(vm, "FFI Access Denied", name,
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

int cccc_call_native_function(VirtualMachine *vm, void *func_ptr, const char *name,
                             long long *args, int actual_nargs,
                             uint64_t double_arg_mask, uint64_t float_arg_mask,
                             int returns_double, int returns_float,
                             int is_variadic, int num_fixed_args) {
    if (!func_ptr) {
        printf("error: native function '%s' not resolved\n",
               name ? name : "<anonymous>");
        return -1;
    }

    int policy = cccc_check_ffi_policy(vm, name, actual_nargs, is_variadic,
                                      num_fixed_args);
    if (policy <= 0)
        return policy;

    ffi_cif cif;
    ffi_type **arg_types = NULL;
    ffi_type *return_type = returns_float    ? &ffi_type_float
                             : returns_double ? &ffi_type_double
                                              : &ffi_type_sint64;

    if (actual_nargs > 0) {
        arg_types = alloca((size_t)actual_nargs * sizeof(ffi_type *));
        for (int i = 0; i < actual_nargs; i++) {
            if (i < 64 && (float_arg_mask & (1ULL << i)))
                arg_types[i] = &ffi_type_float;
            else if (i < 64 && (double_arg_mask & (1ULL << i)))
                arg_types[i] = &ffi_type_double;
            else
                arg_types[i] = &ffi_type_sint64;
        }
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
        return -1;
    }

    // Float args are stored in the low 32 bits of each 64-bit slot (host is
    // little-endian), so &args[i] is also a valid ffi_type_float source.
    void **arg_ptrs =
        alloca((size_t)(actual_nargs > 0 ? actual_nargs : 1) * sizeof(void *));
    for (int i = 0; i < actual_nargs; i++)
        arg_ptrs[i] = &args[i];

    VirtualMachine *saved_ffi_vm = cccc_tls_ffi_vm;
    cccc_tls_ffi_vm = vm;

    if (returns_float) {
        float result;
        ffi_call(&cif, FFI_FN(func_ptr), &result, arg_ptrs);
        cccc_freg_set_f32(vm, FREG_A0, result);
    } else if (returns_double) {
        double result;
        ffi_call(&cif, FFI_FN(func_ptr), &result, arg_ptrs);
        cccc_freg_set_f64(vm, FREG_A0, result);
    } else {
        long long result;
        ffi_call(&cif, FFI_FN(func_ptr), &result, arg_ptrs);
        vm->regs[REG_A0] = result;
    }

    cccc_tls_ffi_vm = saved_ffi_vm;

    return 0;
}

static inline int op_CALLF_fn(VirtualMachine *vm) {
    // Foreign function call using register-based calling convention
    // Operands: [ffi_idx, nargs, double_arg_mask, float_arg_mask]
    int func_idx = (int)cc_read_word(vm);
    int actual_nargs = (int)cc_read_word(vm);
    uint64_t double_arg_mask = (uint64_t)cc_read_i64(vm);
    uint64_t float_arg_mask = (uint64_t)cc_read_i64(vm);

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
               "double_mask: 0x%llx, float_mask: 0x%llx)\n",
               ff->name, actual_nargs, ff->num_fixed_args, ff->is_variadic,
               (unsigned long long)double_arg_mask,
               (unsigned long long)float_arg_mask);

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
                   (i < 64 && (float_arg_mask & (1ULL << i)))  ? "float"
                   : (i < 64 && (double_arg_mask & (1ULL << i))) ? "double"
                                                                : "int");
    }

    int rc = cccc_call_native_function(vm, ff->func_ptr, ff->name, args,
                                      actual_nargs, double_arg_mask,
                                      float_arg_mask, ff->returns_double,
                                      ff->returns_float, ff->is_variadic,
                                      ff->num_fixed_args);
    free(heap_args);
    return rc;
}

// ========== Struct Return Buffer Support ==========

static inline int op_RETBUF_fn(VirtualMachine *vm) {
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

static inline int op_BTRAP_fn(VirtualMachine *vm) {
    if (vm->flags & CCCC_ENABLE_DEBUGGER) {
        printf("\nBTRAP: debugger break-in at PC %u\n", vm->pc);
        cc_debug_repl(vm);
        return 0;
    }
    fprintf(stderr, "BTRAP: trap executed at PC %u\n", vm->pc);
    return -1;
}

// ========== VM-Managed Signal Handling ==========

/* Returns true if sig cannot be caught or ignored (SIGKILL, SIGSTOP) */
static inline bool sig_is_uncatchable(int sig) {
#if defined(SIGKILL) && defined(SIGSTOP)
    return sig == SIGKILL || sig == SIGSTOP;
#elif defined(SIGKILL)
    return sig == SIGKILL;
#else
    return false;
#endif
}

static inline int op_VSIGNAL_fn(VirtualMachine *vm) {
    int  sig  = (int)vm->regs[REG_A0];
    long long func = vm->regs[REG_A1];

    if (sig <= 0 || sig >= CCCC_NSIG || sig_is_uncatchable(sig)) {
        vm->regs[REG_A0] = -1; /* SIG_ERR */
        return 0;
    }

    SigSlot *slot = &vm->vm_sigslots[sig];

    /* Return old handler representation */
    long long old;
    if (slot->action == 1)      old = 1; /* SIG_IGN */
    else if (slot->action == 2) old = slot->handler_fn;
    else                        old = 0; /* SIG_DFL */
    vm->regs[REG_A0] = old;

    if (func == 0) {
        /* SIG_DFL: restore host default */
        slot->action     = 0;
        slot->handler_fn = 0;
        if (cccc_set_guest_signal_action(vm, sig, 0) != 0)
            vm->regs[REG_A0] = -1;
    } else if (func == 1) {
        /* SIG_IGN: ignore on both VM and host */
        slot->action     = 1;
        slot->handler_fn = 0;
        if (cccc_set_guest_signal_action(vm, sig, 1) != 0)
            vm->regs[REG_A0] = -1;
    } else {
        /* VM function pointer: install async-safe shim as native handler */
        slot->action     = 2;
        slot->handler_fn = func;
        if (cccc_set_guest_signal_action(vm, sig, 2) != 0)
            vm->regs[REG_A0] = -1;
    }
    return 0;
}

static inline int op_VRAISE_fn(VirtualMachine *vm) {
    int sig = (int)vm->regs[REG_A0];

    if (sig <= 0 || sig >= CCCC_NSIG) {
        vm->regs[REG_A0] = -1;
        return 0;
    }

    /* SIGTRAP with debugger active: break into REPL */
#ifdef SIGTRAP
    if (sig == SIGTRAP && (vm->flags & CCCC_ENABLE_DEBUGGER)) {
        printf("\nSIGTRAP: debugger break-in at PC %u\n", vm->pc);
        cc_debug_repl(vm);
        vm->regs[REG_A0] = 0;
        return 0;
    }
#endif

    SigSlot *slot = &vm->vm_sigslots[sig];
    switch (slot->action) {
    case 1: /* IGN */
        vm->regs[REG_A0] = 0;
        return 0;
    case 2: { /* VM handler: push return address and jump to handler */
        Pc target = cc_byte_offset_to_pc(slot->handler_fn);
        if (target == CCCC_INVALID_PC || target > vm->text_ptr) {
            fprintf(stderr, "error: invalid signal handler address for sig %d\n", sig);
            return -1;
        }
        if (check_stack_overflow(vm, 1)) return -1;
        *--vm->sp = (long long)vm->pc;
        if (vm->flags & CCCC_CFI) *--vm->shadow_sp = (long long)vm->pc;
        vm->regs[REG_A0] = (long long)sig;
        vm->pc = target;
        return 0; /* dispatch loop will goto dispatch → execute handler */
    }
    default: /* DFL: delegate to host */
        vm->regs[REG_A0] = (long long)raise(sig);
        return 0;
    }
}

// ========== Bit-Manipulation Builtins ==========

static inline int op_CLZ_fn(VirtualMachine *vm) {
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

static inline int op_CTZ_fn(VirtualMachine *vm) {
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

static inline int op_POPCOUNT_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);
    long long result = __builtin_popcountll((unsigned long long)vm->regs[rs]);
    if (rd != REG_ZERO)
        vm->regs[rd] = result;
    return 0;
}

static inline int op_FFS_fn(VirtualMachine *vm) {
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

static inline int op_BSWAP_fn(VirtualMachine *vm) {
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

static inline int op_IOVFL_fn(VirtualMachine *vm) {
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
        // For widths <= 4 bytes the operands fit in 32 bits, so computing in
        // signed 64-bit cannot overflow and the range check below is valid.
        // The 8-byte case must NOT pre-compute a * b in long long here — that
        // overflows (host UB); it is handled separately via the host builtins.
        if (nbytes <= 4) {
            switch (op_type) {
            case 0: result = a + b; break;
            case 1: result = a - b; break;
            case 2: result = a * b; break;
            }
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

// ========== Fused bp-relative (local) load/store ==========
// These replace the common LEA3+LDR/STR two-opcode sequence for local vars.

static inline int op_LDR_LOCAL_B_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 0);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(signed char *)(vm->bp + offset);
    return 0;
}

static inline int op_LDR_LOCAL_H_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 0);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(short *)(vm->bp + offset);
    return 0;
}

static inline int op_LDR_LOCAL_W_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 0);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(int *)(vm->bp + offset);
    return 0;
}

static inline int op_LDR_LOCAL_D_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 0);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(long long *)(vm->bp + offset);
    return 0;
}

static inline int op_STR_LOCAL_B_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 1);
    *(char *)(vm->bp + offset) = (char)vm->regs[rd];
    return 0;
}

static inline int op_STR_LOCAL_H_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 1);
    *(short *)(vm->bp + offset) = (short)vm->regs[rd];
    return 0;
}

static inline int op_STR_LOCAL_W_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 1);
    *(int *)(vm->bp + offset) = (int)vm->regs[rd];
    return 0;
}

static inline int op_STR_LOCAL_D_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    check_race_access(vm, (void *)(vm->bp + offset), 1);
    *(long long *)(vm->bp + offset) = vm->regs[rd];
    return 0;
}

static inline int op_FLDR_LOCAL_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    cccc_freg_set_f64(vm, rd, *(double *)(vm->bp + offset));
    return 0;
}

static inline int op_FSTR_LOCAL_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    *(double *)(vm->bp + offset) = cccc_freg_get_f64(vm, rd);
    return 0;
}

static inline int op_FLDR_LOCAL_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    cccc_freg_set_f32(vm, rd, *(float *)(vm->bp + offset));
    return 0;
}

static inline int op_FSTR_LOCAL_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd;
    DECODE_R(operands, rd);
    long long offset = cc_read_i64(vm);
    *(float *)(vm->bp + offset) = cccc_freg_get_f32(vm, rd);
    return 0;
}

// ========== Fused indexed load/store ==========
// Effective address is (char *)regs[base] + regs[index] * scale + byte offset.

static inline char *op_index_addr(VirtualMachine *vm, int base, int index, int scale,
                                  long long offset) {
    return (char *)vm->regs[base] + vm->regs[index] * (long long)scale + offset;
}

static inline int op_LDR_INDEX_B_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 0);
    WATCHPOINT_CHECK(vm, addr, 1, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(signed char *)addr;
    return 0;
}

static inline int op_LDR_INDEX_H_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 0);
    WATCHPOINT_CHECK(vm, addr, 2, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(short *)addr;
    return 0;
}

static inline int op_LDR_INDEX_W_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 0);
    WATCHPOINT_CHECK(vm, addr, 4, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(int *)addr;
    return 0;
}

static inline int op_LDR_INDEX_D_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 0);
    WATCHPOINT_CHECK(vm, addr, 8, WATCH_READ);
    if (rd != REG_ZERO)
        vm->regs[rd] = *(long long *)addr;
    return 0;
}

static inline int op_STR_INDEX_B_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 1);
    *(char *)addr = (char)vm->regs[rd];
    WATCHPOINT_CHECK(vm, addr, 1, WATCH_WRITE);
    return 0;
}

static inline int op_STR_INDEX_H_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 1);
    *(short *)addr = (short)vm->regs[rd];
    WATCHPOINT_CHECK(vm, addr, 2, WATCH_WRITE);
    return 0;
}

static inline int op_STR_INDEX_W_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 1);
    *(int *)addr = (int)vm->regs[rd];
    WATCHPOINT_CHECK(vm, addr, 4, WATCH_WRITE);
    return 0;
}

static inline int op_STR_INDEX_D_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    check_race_access(vm, addr, 1);
    *(long long *)addr = vm->regs[rd];
    WATCHPOINT_CHECK(vm, addr, 8, WATCH_WRITE);
    return 0;
}

static inline int op_FLDR_INDEX_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    WATCHPOINT_CHECK(vm, addr, 8, WATCH_READ);
    cccc_freg_set_f64(vm, rd, *(double *)addr);
    return 0;
}

static inline int op_FSTR_INDEX_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    *(double *)addr = cccc_freg_get_f64(vm, rd);
    WATCHPOINT_CHECK(vm, addr, 8, WATCH_WRITE);
    return 0;
}

static inline int op_FLDR_INDEX_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    WATCHPOINT_CHECK(vm, addr, 4, WATCH_READ);
    cccc_freg_set_f32(vm, rd, *(float *)addr);
    return 0;
}

static inline int op_FSTR_INDEX_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, base, index, scale;
    DECODE_RRRS(operands, rd, base, index, scale);
    long long offset = cc_read_i64(vm);
    char *addr = op_index_addr(vm, base, index, scale, offset);
    *(float *)addr = cccc_freg_get_f32(vm, rd);
    WATCHPOINT_CHECK(vm, addr, 4, WATCH_WRITE);
    return 0;
}
