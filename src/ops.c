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

    // IEEE-754 finite/0.0 is well-defined (a correctly-signed infinity,
    // raising FE_DIVBYZERO) and 0.0/0.0 is NaN (raising FE_INVALID) --
    // neither is UB, unlike integer division by zero, so this is a plain
    // divide by default (#773). --trap-fp-divzero opts back into the old
    // abort-on-any-zero-divisor behavior for debugging.
    if ((vm->flags & CCCC_TRAP_FP_DIVZERO) && cccc_freg_get_f64(vm, rs2) == 0.0) {
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

    // See op_FDIV3_fn above -- IEEE-754 finite/0.0 division is well-defined,
    // not UB, so this only traps when explicitly opted in (#773).
    if ((vm->flags & CCCC_TRAP_FP_DIVZERO) && cccc_freg_get_f32(vm, rs2) == 0.0f) {
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

// Resolve an interior stack address to the retained interval that currently
// owns it (#675, prefer-live resolution added for #727). Epoch order is
// recency order -- later activations always get strictly higher epochs
// (vm->frame_epoch_counter is monotonic) -- but recency alone is unsound
// when two *sibling* frames' tagged extents spatially overlap: a dead
// frame's STKTAG range can outlive its frame and still have a higher epoch
// than a live frame's own range that currently occupies (part of) the same
// addresses (e.g. a live frame's escaping-vector scratch sits inside a
// stale, already-returned sibling's escaping-aggregate range at the same
// stack depth -- #727). Preferring the max-epoch *live* containing interval
// when one exists fixes this: only when every containing interval is dead
// do we fall back to the plain max-epoch (dead) interval, which still
// reports a genuine dangling deref correctly. Returns false if no interval
// contains ptr. *out_hi (optional, may be NULL) receives the matched
// interval's upper bound, so a caller like DYNOBJSZ (#648) can compute
// remaining bytes (hi - ptr) without a second lookup.
// PLACEHOLDER: linear scan over all retained intervals. Fine for the
// small, mostly-distinct working set typical programs produce; if a hot
// loop retains many intervals this should become an interval tree.
// Ticket: https://todo.sr.ht/~takeiteasy/cccc/677
static bool stack_interval_stab(VirtualMachine *vm, long long ptr,
                                 unsigned long long *out_epoch,
                                 long long *out_hi) {
    bool found = false, found_live = false;
    unsigned long long best_epoch = 0, best_live_epoch = 0;
    long long best_hi = 0, best_live_hi = 0;
    for (int i = 0; i < vm->stack_intervals.count; i++) {
        if (ptr >= vm->stack_intervals.iv[i].lo && ptr < vm->stack_intervals.iv[i].hi) {
            unsigned long long epoch = vm->stack_intervals.iv[i].epoch;
            if (!found || epoch > best_epoch) {
                best_epoch = epoch;
                best_hi = vm->stack_intervals.iv[i].hi;
                found = true;
            }
            if (hashmap_get_int(&vm->live_epochs, (long long)epoch) &&
                (!found_live || epoch > best_live_epoch)) {
                best_live_epoch = epoch;
                best_live_hi = vm->stack_intervals.iv[i].hi;
                found_live = true;
            }
        }
    }
    if (found_live) {
        *out_epoch = best_live_epoch;
        if (out_hi)
            *out_hi = best_live_hi;
        return true;
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
    // Float to int: regs[rd] = (long long)fregs[rs], saturating on
    // NaN/out-of-range with FE_INVALID raised (#775) -- see
    // cccc_f64_to_i64 in internal.h for why a bare cast is UB here.
    // Format: [F2I3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = cccc_f64_to_i64(cccc_freg_get_f64(vm, rs));
    return 0;
}

static inline int op_F2I3_F32_fn(VirtualMachine *vm) {
    // Float to int: regs[rd] = (long long)fregs[rs], saturating (#775).
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = cccc_f32_to_i64(cccc_freg_get_f32(vm, rs));
    return 0;
}

static inline int op_U2F3_fn(VirtualMachine *vm) {
    // Unsigned int to float: fregs[rd] = (double)(unsigned long long)regs[rs]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_f64(vm, rd, (double)(unsigned long long)vm->regs[rs]);
    return 0;
}

static inline int op_F2U3_fn(VirtualMachine *vm) {
    // Float to unsigned int: regs[rd] = (unsigned long long)fregs[rs],
    // saturating on NaN/out-of-range with FE_INVALID raised (#780) -- see
    // cccc_f64_to_u64 in internal.h for why a bare cast is UB here.
    // Format: [F2U3] [rd:8|rs:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)cccc_f64_to_u64(cccc_freg_get_f64(vm, rs));
    return 0;
}

static inline int op_U2F3_F32_fn(VirtualMachine *vm) {
    // Unsigned int to float: fregs[rd] = (float)(unsigned long long)regs[rs]
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    cccc_freg_set_f32(vm, rd, (float)(unsigned long long)vm->regs[rs]);
    return 0;
}

static inline int op_F2U3_F32_fn(VirtualMachine *vm) {
    // Float to unsigned int: regs[rd] = (unsigned long long)fregs[rs],
    // saturating (#780).
    long long operands = cc_read_word(vm);
    int rd, rs;
    DECODE_RR(operands, rd, rs);

    if (rd != REG_ZERO)
        vm->regs[rd] = (long long)cccc_f32_to_u64(cccc_freg_get_f32(vm, rs));
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

// ---- Byte-granular subobject type shadow (#653), page-chunked (#753), ----
// ---- heap + globals (#752)                                            ----
//
// Logically one TypeKind byte per tracked segment byte (TY_VOID == 0 means
// "no effective type established"), indexed relative to the segment's own
// base pointer. Physically a sparse page table per segment (TypeShadowSeg,
// src/cccc.h) rather than one flat array: a page is allocated on first
// stamp into its range and freed back to NULL the instant a clear zeroes
// it in full, so a program with a large peak-then-shrink allocation
// pattern doesn't keep paying host memory for heap regions nothing lives
// in anymore -- cost tracks the live stamped footprint, not the segment's
// reservation. An absent page reads back as all TY_VOID, identical to an
// allocated all-zero page.
//
// Two segments are tracked: vm->heap_shadow (the original #653 scope) and
// vm->data_shadow (globals, #752). Stack subobjects remain untracked --
// unlike the heap and data segments, stack addresses are reused across
// frames/recursion, which would need its own liveness bookkeeping (mirror
// frame_epochs/stack_intervals) to avoid reopening the class of
// false-positive that #673/#675/#727/#740 fixed in the dangling-pointer
// detector; deferred as a follow-up.
#define TYPE_SHADOW_PAGE_SHIFT 16
#define TYPE_SHADOW_PAGE_SIZE  (1u << TYPE_SHADOW_PAGE_SHIFT)

// #767: a 64 KiB word-compare with early exit is ~1000 dispatch-steps' worth
// of work, so charging this many cycles of silence per page actually
// scanned bounds type_shadow_sweep's worst-case amortized cost at ~0.5
// bytes scanned per VM cycle (well under 1% overhead), independent of how
// productive any individual sweep turns out to be -- see type_shadow_sweep.
#define TYPE_SHADOW_SWEEP_CYCLES_PER_PAGE (1 << 17)

// Grows `seg`'s page *vector* (not the pages themselves) so it covers
// `committed` bytes. Lazily called here (rather than in vm_heap_grow) so
// it stays in sync even if --type-checks is toggled on mid-run by #pragma
// cccc config(safety=N); every heap-mutating opcode
// (MALC/CALC/MALCA/REALC/MFRE/MCPY/MSET), RETBUF, and CHKT3 itself call
// this (via type_shadow_locate) before touching a segment's shadow.
static bool type_shadow_ensure(VirtualMachine *vm, TypeShadowSeg *seg, size_t committed) {
    if (!(vm->flags & CCCC_TYPE_CHECKS))
        return seg->pages != NULL;
    size_t need = (committed + TYPE_SHADOW_PAGE_SIZE - 1) >> TYPE_SHADOW_PAGE_SHIFT;
    if (seg->page_count >= need)
        return seg->pages != NULL;
    unsigned char **np = realloc(seg->pages, need * sizeof(*np));
    if (!np)
        return seg->pages != NULL;
    for (size_t i = seg->page_count; i < need; i++)
        np[i] = NULL;
    seg->pages = np;
    seg->page_count = need;
    return true;
}

// Validates [p, p+len) lies within [base, base+committed) and, on success,
// writes the byte offset from base to *out_off. Does not touch the page
// table.
static bool type_shadow_bounds(const char *base, size_t committed, const void *p, size_t len,
                                size_t *out_off) {
    if (len == 0 || (const char *)p < base)
        return false;
    size_t off = (size_t)((const char *)p - base);
    if (off > committed || len > committed - off)
        return false;
    *out_off = off;
    return true;
}

// Resolves which tracked segment [p, p+len) falls wholly within -- heap or
// data (globals) -- and writes its byte offset from that segment's base to
// *out_off. Returns NULL (leaving *out_off unset) if the range doesn't fit
// wholly inside either tracked segment, e.g. a stack address, a wild
// pointer, or a range straddling a segment boundary.
static TypeShadowSeg *type_shadow_locate(VirtualMachine *vm, const void *p, size_t len, size_t *out_off) {
    if (type_shadow_bounds(vm->heap_seg, vm->heap_committed, p, len, out_off))
        return &vm->heap_shadow;
    if (type_shadow_bounds(vm->data_seg, vm->data_committed, p, len, out_off))
        return &vm->data_shadow;
    return NULL;
}

// Sets len shadow bytes of `seg` starting at absolute segment-byte offset
// `off` to the constant `val`. val == 0 (clear) frees a page the moment
// this call zeroes it in full (page_off == 0 and the whole page-sized
// chunk), rather than scanning for an all-zero page on every partial clear
// -- interior pages of a large cleared range are freed exactly, edge pages
// that only got partially zeroed stay allocated (harmless: at most two
// stray pages per clear). val != 0 (stamp) allocates a page on demand.
// #767: records that `idx` was partially zeroed (not fully reclaimed) so a
// later sweep can check whether it has since gone all-zero. O(1): a linear
// dedup scan over at most TYPE_SHADOW_CAND_MAX entries. Dropping a push
// when the list is full is sound -- the page just stays allocated, exactly
// today's behavior -- and stays rare in practice, since a free of an N-page
// block pushes at most its two edge pages.
static void type_shadow_cand_push(TypeShadowSeg *seg, size_t idx) {
    for (size_t i = 0; i < seg->cand_count; i++)
        if (seg->cand[i] == idx)
            return;
    if (seg->cand_count == TYPE_SHADOW_CAND_MAX)
        return;
    seg->cand[seg->cand_count++] = idx;
}

// #767: reclaims candidate pages (partially zeroed since the last sweep)
// that have since gone all-zero, freeing host memory type_shadow_fill's
// exact-full-page-clear reclaim rule alone would leave stranded. Freeing a
// verified all-zero page is observationally identical to leaving it
// allocated -- every reader already treats a NULL page as all TY_VOID -- so
// this cannot change what any caller sees, only how much host memory is
// held.
//
// Must only be called at a statement boundary in a wrapper (type_shadow_clear,
// type_shadow_copy), never from inside type_shadow_fill/scatter/gather/
// check_uniform: each of those holds a raw `page` pointer live across a
// memset/memcpy, and a reentrant sweep freeing that exact page would be a
// use-after-free. Must never free `seg->pages` itself or shrink
// `seg->page_count` -- op_CALLF_fn/op_CALLN_fn's ffi_shadow_backstop gates
// the entire FFI-clear backstop on `heap_shadow.pages || data_shadow.pages`
// being non-NULL; nulling the vector here would silently disable that
// backstop and let a later unshimmed host write leave a stale stamp behind
// -- a false CHKT3 positive on correct guest code, not just a missed one.
static void type_shadow_sweep(VirtualMachine *vm, TypeShadowSeg *seg) {
    if (seg->cand_count == 0 || vm->cycle < seg->next_sweep_cycle)
        return;
    size_t scanned = 0;
    for (size_t i = 0; i < seg->cand_count; i++) {
        size_t idx = seg->cand[i];
        if (idx >= seg->page_count)
            continue;
        unsigned char *page = seg->pages[idx];
        if (!page)
            continue;
        scanned++;
        const uint64_t *w = (const uint64_t *)page; // calloc'd: max-aligned
        bool zero = true;
        for (size_t j = 0; j < TYPE_SHADOW_PAGE_SIZE / sizeof(*w); j++) {
            if (w[j]) {
                zero = false;
                break;
            }
        }
        if (zero) {
            free(page);
            seg->pages[idx] = NULL;
            vm->type_shadow_pages_swept++;
        }
    }
    seg->cand_count = 0;
    vm->type_shadow_sweeps++;
    // Charged forward by pages actually scanned (not a flat threshold): a
    // sweep that only checked 2 edge pages gets a much sooner deadline than
    // one that checked all 32, so worst-case scan work stays bounded by
    // construction regardless of how many pages a given sweep reclaims.
    seg->next_sweep_cycle = vm->cycle + (long long)scanned * TYPE_SHADOW_SWEEP_CYCLES_PER_PAGE;
}

static void type_shadow_fill(TypeShadowSeg *seg, size_t off, size_t len, unsigned char val) {
    size_t end = off + len;
    while (off < end) {
        size_t page_idx = off >> TYPE_SHADOW_PAGE_SHIFT;
        size_t page_off = off & (TYPE_SHADOW_PAGE_SIZE - 1);
        size_t chunk = TYPE_SHADOW_PAGE_SIZE - page_off;
        if (chunk > end - off)
            chunk = end - off;
        if (page_idx >= seg->page_count)
            return; // shouldn't happen after type_shadow_ensure, stay safe
        unsigned char *page = seg->pages[page_idx];
        if (!page) {
            if (val == 0) {
                off += chunk;
                continue; // already no-info, nothing to clear
            }
            page = calloc(1, TYPE_SHADOW_PAGE_SIZE);
            if (!page)
                return; // OOM: leave this and remaining chunks as no-info
            seg->pages[page_idx] = page;
        }
        memset(page + page_off, val, chunk);
        if (val == 0 && page_off == 0 && chunk == TYPE_SHADOW_PAGE_SIZE) {
            free(page);
            seg->pages[page_idx] = NULL;
        } else if (val == 0) {
            // #767: zeroed part of an already-allocated page, but not the
            // whole page in one call -- queue it as a sweep candidate
            // rather than leaving it allocated indefinitely.
            type_shadow_cand_push(seg, page_idx);
        }
        off += chunk;
    }
}

// Establish `kind` as the effective type for every byte in [p, p+len).
static void type_shadow_stamp(VirtualMachine *vm, void *p, size_t len, int kind) {
    size_t off;
    TypeShadowSeg *seg = type_shadow_locate(vm, p, len, &off);
    if (!seg)
        return;
    size_t committed = (seg == &vm->heap_shadow) ? vm->heap_committed : vm->data_committed;
    if (type_shadow_ensure(vm, seg, committed))
        type_shadow_fill(seg, off, len, (unsigned char)kind);
}

// Erase effective-type info for every byte in [p, p+len) (back to "no
// effective type established"). Calls type_shadow_ensure so the page
// vector stays grown to cover the segment even when the only mutating
// traffic through a range is clears (e.g. malloc immediately followed by a
// memset(0), or every fresh MALC below).
static void type_shadow_clear(VirtualMachine *vm, void *p, size_t len) {
    size_t off;
    TypeShadowSeg *seg = type_shadow_locate(vm, p, len, &off);
    if (!seg)
        return;
    size_t committed = (seg == &vm->heap_shadow) ? vm->heap_committed : vm->data_committed;
    if (type_shadow_ensure(vm, seg, committed)) {
        type_shadow_fill(seg, off, len, 0);
        // #767: this is the single choke point every clear path runs
        // through (MFRE, MSET, the CALLF/CALLN backstop, RETBUF, realloc),
        // so hooking it here services every segment's candidate list
        // without needing a separate hook at each individual clear site.
        type_shadow_sweep(vm, seg);
    }
}

// Returns true and writes the shared TypeKind to *out_kind iff every byte
// in [p, p+len) carries the same non-TY_VOID stamp -- i.e. there is
// unambiguous effective-type info for the whole range. A missing page, a
// TY_VOID byte, or a range straddling two different stamps (e.g. the tail
// of one stamp and the head of another) all collapse to "no info" (false)
// rather than a guess. Never allocates.
static bool type_shadow_check_uniform(VirtualMachine *vm, const void *p, size_t len, unsigned char *out_kind) {
    size_t off;
    TypeShadowSeg *seg = type_shadow_locate(vm, p, len, &off);
    if (!seg || !seg->pages)
        return false;
    size_t end = off + len;
    bool have_kind = false;
    unsigned char kind = 0;
    while (off < end) {
        size_t page_idx = off >> TYPE_SHADOW_PAGE_SHIFT;
        size_t page_off = off & (TYPE_SHADOW_PAGE_SIZE - 1);
        size_t chunk = TYPE_SHADOW_PAGE_SIZE - page_off;
        if (chunk > end - off)
            chunk = end - off;
        unsigned char *page = (page_idx < seg->page_count) ? seg->pages[page_idx] : NULL;
        for (size_t i = 0; i < chunk; i++) {
            unsigned char b = page ? page[page_off + i] : 0;
            if (b == TY_VOID)
                return false;
            if (!have_kind) {
                kind = b;
                have_kind = true;
            } else if (b != kind) {
                return false;
            }
        }
        off += chunk;
    }
    if (!have_kind)
        return false;
    *out_kind = kind;
    return true;
}

// Copies len shadow bytes of `seg` into `out`, starting at absolute
// segment-byte offset `off`. A missing page reads back as TY_VOID. Never
// allocates.
static void type_shadow_gather(TypeShadowSeg *seg, size_t off, size_t len, unsigned char *out) {
    size_t end = off + len, pos = 0;
    while (off < end) {
        size_t page_idx = off >> TYPE_SHADOW_PAGE_SHIFT;
        size_t page_off = off & (TYPE_SHADOW_PAGE_SIZE - 1);
        size_t chunk = TYPE_SHADOW_PAGE_SIZE - page_off;
        if (chunk > end - off)
            chunk = end - off;
        unsigned char *page = (page_idx < seg->page_count) ? seg->pages[page_idx] : NULL;
        if (page)
            memcpy(out + pos, page + page_off, chunk);
        else
            memset(out + pos, 0, chunk);
        off += chunk;
        pos += chunk;
    }
}

// Writes len shadow bytes of `seg` from `in`, starting at absolute
// segment-byte offset `off`. Allocates pages on demand, but skips
// allocating a page purely to write all-zero bytes into it (an absent
// page already reads back as TY_VOID); frees a page it writes an all-zero
// full-page chunk into, mirroring type_shadow_fill's reclaim rule.
static void type_shadow_scatter(TypeShadowSeg *seg, size_t off, size_t len, const unsigned char *in) {
    size_t end = off + len, pos = 0;
    while (off < end) {
        size_t page_idx = off >> TYPE_SHADOW_PAGE_SHIFT;
        size_t page_off = off & (TYPE_SHADOW_PAGE_SIZE - 1);
        size_t chunk = TYPE_SHADOW_PAGE_SIZE - page_off;
        if (chunk > end - off)
            chunk = end - off;
        if (page_idx >= seg->page_count)
            return; // shouldn't happen after type_shadow_ensure, stay safe

        bool all_zero = true;
        for (size_t i = 0; i < chunk; i++) {
            if (in[pos + i]) {
                all_zero = false;
                break;
            }
        }

        unsigned char *page = seg->pages[page_idx];
        if (!page && all_zero) {
            off += chunk;
            pos += chunk;
            continue;
        }
        if (!page) {
            page = calloc(1, TYPE_SHADOW_PAGE_SIZE);
            if (!page)
                return; // OOM: leave this and remaining chunks as no-info
            seg->pages[page_idx] = page;
        }
        memcpy(page + page_off, in + pos, chunk);
        if (all_zero && page_off == 0 && chunk == TYPE_SHADOW_PAGE_SIZE) {
            free(page);
            seg->pages[page_idx] = NULL;
        } else if (all_zero) {
            // #767: this chunk was all-zero but didn't reclaim the whole
            // page (either a partial chunk, or a full-page chunk into a
            // page whose other bytes are nonzero) -- queue it, mirroring
            // type_shadow_fill's candidate push. The `!page && all_zero`
            // skip-alloc case above never reaches here (page stays NULL,
            // nothing to reclaim).
            type_shadow_cand_push(seg, page_idx);
        }
        off += chunk;
        pos += chunk;
    }
}

// Propagate effective-type info from [src, src+len) to [dst, dst+len)
// (mirrors memcpy copying the effective type along with the bytes, C11
// §6.5p6) -- either range may be in the heap or in globals; a copy between
// the two segments (e.g. a global struct assigned into a malloc'd one) is
// tracked the same as a same-segment copy. If src isn't inside a tracked
// segment (e.g. a stack source), conservatively clear dst instead of
// leaving it stale.
static void type_shadow_copy(VirtualMachine *vm, void *dst, const void *src, size_t len) {
    size_t dst_off;
    TypeShadowSeg *dst_seg = type_shadow_locate(vm, dst, len, &dst_off);
    if (!dst_seg)
        return; // dst outside tracked segments: nothing to propagate to
    size_t dst_committed = (dst_seg == &vm->heap_shadow) ? vm->heap_committed : vm->data_committed;
    if (!type_shadow_ensure(vm, dst_seg, dst_committed))
        return;

    size_t src_off;
    TypeShadowSeg *src_seg = type_shadow_locate(vm, src, len, &src_off);
    if (!src_seg || !src_seg->pages) {
        type_shadow_fill(dst_seg, dst_off, len, 0); // OOM/untracked src: don't risk a stale stamp
        return;
    }

    // Gather the whole source range into a temporary buffer before
    // scattering it to dst. src and dst can overlap (mirrors memmove) when
    // they're the same segment; gather-then-scatter of the *whole* range
    // is trivially correct for that, whereas chunk-by-chunk in place, with
    // src/dst potentially at different page offsets, is easy to get subtly
    // wrong. Correctness over micro-optimizing an opt-in safety check.
    unsigned char stack_buf[4096];
    unsigned char *buf = len <= sizeof(stack_buf) ? stack_buf : malloc(len);
    if (!buf) {
        type_shadow_fill(dst_seg, dst_off, len, 0); // OOM: don't risk a stale stamp
        return;
    }
    type_shadow_gather(src_seg, src_off, len, buf);
    type_shadow_scatter(dst_seg, dst_off, len, buf);
    if (buf != stack_buf)
        free(buf);
    // #767: service candidates type_shadow_scatter just queued. Placed
    // after the buf free (not interleaved with gather/scatter above) --
    // see type_shadow_sweep's doc comment for why it may never run
    // reentrant with those.
    type_shadow_sweep(vm, dst_seg);
}

// Non-static entry point for the memcpy/memmove shims in stdlib/string.c
// (a separate translation unit -- ops.c is #include'd into vm.c, so its
// static helpers aren't visible there). Declared in internal.h.
void cc_type_shadow_copy(VirtualMachine *vm, void *dst, const void *src, size_t len) {
    if (!vm)
        return;
    type_shadow_copy(vm, dst, src, len);
}

// #769: true iff every element in [base, base + nmemb*size) carries the
// same shadow *byte pattern* as element 0 -- i.e. the shadow is invariant
// under any permutation of whole elements. Strictly more general than
// type_shadow_check_uniform (which demands one uniform non-TY_VOID byte
// across the *whole* range): this is element-pattern uniformity, so a
// struct array (mixed member types, TY_VOID padding between members) still
// qualifies as long as every element matches element 0 byte-for-byte,
// including any TY_VOID bytes. An all-TY_VOID range is trivially uniform
// (nothing to lose by skipping the clear).
//
// This is what lets wrap_qsort (src/stdlib/stdlib.c) skip clearing the
// shadow across a host qsort() call: qsort only reorders whole elements, it
// never rewrites their bytes, so if the shadow pattern repeats identically
// per element going in, it still does coming out, regardless of which
// permutation qsort's host C implementation happened to apply.
//
// Bails to false (caller falls back to the whole-range clear) whenever the
// range can't be proven uniform cheaply and safely: size == 0 or too large
// to fit the stack scratch buffer, nmemb*size overflow, or the range isn't
// wholly inside one tracked segment (type_shadow_locate). nmemb <= 1 is
// vacuously true (qsort/bsearch are no-ops on 0 or 1 elements). Never
// allocates a shadow page -- type_shadow_gather reads absent pages back as
// all TY_VOID.
bool cc_type_shadow_elements_uniform(VirtualMachine *vm, const void *base,
                                     size_t nmemb, size_t size) {
    if (!vm || !(vm->flags & CCCC_TYPE_CHECKS))
        return false;
    if (nmemb <= 1)
        return true;
    if (size == 0 || size > 4096)
        return false;
    if (nmemb > SIZE_MAX / size)
        return false; // overflow
    size_t len = nmemb * size;

    size_t off;
    TypeShadowSeg *seg = type_shadow_locate(vm, base, len, &off);
    if (!seg)
        return false;

    unsigned char elem0[4096];
    type_shadow_gather(seg, off, size, elem0);

    unsigned char elem[4096];
    for (size_t i = 1; i < nmemb; i++) {
        type_shadow_gather(seg, off + i * size, size, elem);
        if (memcmp(elem, elem0, size) != 0)
            return false;
    }
    return true;
}

// #769: non-static wrapper over type_shadow_clear for wrap_qsort (separate
// translation unit, same rationale as cc_type_shadow_copy above).
// Segment-agnostic (goes through type_shadow_locate inside
// type_shadow_clear), so it covers a global array too, not just heap.
void cc_type_shadow_clear_range(VirtualMachine *vm, void *p, size_t len) {
    if (!vm)
        return;
    type_shadow_clear(vm, p, len);
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
                if (!hashmap_get_int(&vm->live_epochs, (long long)(intptr_t)tagged)) {
                    // Stack addresses are reused; a returned sibling's
                    // exact-recorded escaping local can leave a stale tag at
                    // an address a *live* frame now legitimately owns (#740,
                    // e.g. a stack-spilled variadic vector arg's va_arg slot
                    // colliding with a dead sibling's va_list base). Prefer a
                    // live containing interval before concluding dangling,
                    // exactly as stack_interval_stab does within layer 3
                    // (#727) -- if only dead intervals (or none) cover ptr,
                    // this is still a genuine dangling deref (#673).
                    unsigned long long iv_epoch;
                    if (!(stack_interval_stab(vm, ptr, &iv_epoch, NULL) &&
                          hashmap_get_int(&vm->live_epochs, (long long)iv_epoch)))
                        epoch_dangling = true;
                }
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
    // Effective-type model (#651), extended to per-offset byte granularity
    // (#653 -- subobject/member accesses are now tracked, not just base
    // pointers) and to globals (#752 -- vm->data_shadow): a store through
    // any address stamps the owning segment's shadow across the accessed
    // byte range ("establishes" the effective type, mirroring C11 6.5p6);
    // a load checks the pointer's static type against the shadow. TY_VOID
    // (0) means "no effective type established yet" (the state a fresh
    // allocation, a fresh global, or type_shadow_clear leaves bytes in) —
    // never an error to load through.
    // Format: [CHKT3] [rs:8|mode:8|unused:48] [(size<<8)|expected_type:64]
    long long operands = cc_read_word(vm);
    int rs, mode;
    DECODE_RR(operands, rs, mode);
    long long imm = cc_read_i64(vm);
    int expected_type = (int)(imm & 0xFF);
    size_t size = (size_t)(imm >> 8);
    long long ptr = vm->regs[rs];

    if (!(vm->flags & CCCC_TYPE_CHECKS)) {
        return 0;
    }

    if (ptr == 0) {
        return 0; // NULL will be caught by CHKP3
    }

    // Dispatch on address class before touching the shadow. An address
    // inside the heap segment keeps the exact #653 gate: it must resolve
    // to a live allocation via heap_alloc_for_ptr (base pointers and
    // interior pointers alike, #650's resolver) or this opcode says
    // nothing at all -- a heap-range address that ISN'T inside any live
    // allocation (a gap, header bytes, past sorted_allocs) is left to
    // CHKP3 to report. Widening CHKT3's reporting domain to those
    // addresses would risk a new class of false positive, so this must not
    // change just because global tracking (#752) is being added below.
    //
    // An address outside the heap segment skips allocation resolution
    // entirely: type_shadow_locate (called from the type_shadow_* helpers
    // below) is what decides whether it's an untracked address (e.g. the
    // stack -- no report, ever) or falls inside vm->data_seg (globals).
    AllocHeader *header = NULL;
    size_t off = 0;
    if ((const char *)ptr >= vm->heap_seg && (const char *)ptr < vm->heap_end) {
        header = heap_alloc_for_ptr(vm, ptr, &off);
        if (!header) {
            return 0; // heap-range but untracked — CHKP3's job, not ours
        }
        if (header->freed) {
            return 0; // UAF is CHKP3's job; don't double-report here
        }
    }

    if (mode == CHKT3_MODE_CLEAR) {
        // Union member access: erase any stamped type for this range so a
        // later non-union access through the same bytes starts fresh
        // rather than false-positiving against whichever member the union
        // was last written through. A no-op for any address outside a
        // tracked segment (e.g. the stack).
        type_shadow_clear(vm, (void *)ptr, size);
        return 0;
    }

    // Skip check for void* (TY_VOID) and generic pointers (TY_PTR)
    if (expected_type == TY_VOID || expected_type == TY_PTR) {
        return 0;
    }

    // Any object's representation is always legally accessible as
    // character type (C11 6.5p7): never flag a char load, and a char
    // store clears the range rather than stamping it "char", so a
    // hand-rolled byte-copy loop erases the destination's prior effective
    // type instead of mis-stamping it.
    if (expected_type == TY_CHAR) {
        if (mode == CHKT3_MODE_STAMP)
            type_shadow_clear(vm, (void *)ptr, size);
        return 0;
    }

    if (mode == CHKT3_MODE_STAMP) {
        // Establish (or re-establish) the effective type for this range.
        type_shadow_stamp(vm, (void *)ptr, size, expected_type);
        return 0;
    }

    unsigned char actual_type;
    if (!type_shadow_check_uniform(vm, (void *)ptr, size, &actual_type)) {
        // No unambiguous effective type for this range: either nothing has
        // been stamped yet, or the range straddles more than one stamp
        // (e.g. the tail of one stamp and the head of another) -- treat
        // both as no info rather than guessing which stamp applies.
        return 0;
    }

    if (actual_type != (unsigned char)expected_type) {
        // Positional, indexed by TypeKind -- must stay in exact sync with
        // the enum in cccc.h. TY_VECTOR (22) was missing here before #402
        // (a pre-existing overflow: this diagnostic's expected/actual name
        // silently fell to "unknown" for any vector-typed CHKT3 mismatch);
        // fixed in passing alongside appending the three decimal kinds.
        static const char *type_names[] = {
            "void",     "bool",   "char",        "short",  "int",
            "long",     "float",  "double",      "long double", "enum",
            "pointer",  "function", "array",     "vla",    "struct",
            "union",    "error",  "block",       "complex", "nullptr_t",
            "_BitInt",  "auto",   "vector",
            "_Decimal32", "_Decimal64", "_Decimal128"};
        const int n_type_names = (int)(sizeof(type_names) / sizeof(type_names[0]));

        const char *expected_name =
            (expected_type >= 0 && expected_type < n_type_names)
                ? type_names[expected_type]
                : "unknown";
        const char *actual_name =
            (actual_type < n_type_names)
                ? type_names[actual_type]
                : "unknown";

        printf("\n========== TYPE MISMATCH DETECTED ==========\n");
        printf("Pointer type mismatch on dereference\n");
        printf("Address:          0x%llx\n", ptr);
        // header is only resolved for a heap address (#752): a global has
        // no AllocHeader/alloc_pc to report.
        if (header) {
            printf("Offset in alloc:  %zu (allocation size: %zu)\n", off, header->size);
        }
        printf("Expected type:    %s\n", expected_name);
        printf("Actual type:      %s\n", actual_name);
        if (header) {
            printf("Allocated at PC offset: %lld\n", header->alloc_pc);
        }
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

    // sp now points at the return address — leave it in place.
    //
    // CFI: the original CALL's return address stays on the main stack (see
    // above), so its shadow-stack twin must stay too -- do NOT pop it here.
    // The tail-callee's own LEV3 is what eventually consumes both entries
    // together when it actually returns. Popping the shadow entry here
    // (#756) desynchronised the two stacks by one slot per tail call: with
    // one CALL feeding N chained CALLTs, the shadow stack would underflow
    // (or, for a deeper call graph, walk into unrelated memory) well before
    // the matching LEV3, corrupting subsequent CALL/LEV3 shadow bookkeeping
    // and eventually faulting with a host SIGSEGV instead of the intended
    // controlled CFI/UAF trap.

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

// Forward declaration: full definition (and the #751/#768 classification
// table it drives off) lives below, near op_CALLF_fn, its original and
// still-primary call site. op_CALLN_fn (indirect calls through a function
// pointer or dlsym'd symbol) shares it so it isn't a soundness gap relative
// to CALLF -- see the call site below for why that mattered.
static void ffi_shadow_backstop(VirtualMachine *vm, const char *name,
                                const long long *args, int actual_nargs,
                                uint64_t double_arg_mask, uint64_t float_arg_mask);

static inline int op_CALLN_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rs = (int)(operands & 0xFF);
    InstrWord meta = cc_read_word(vm);
    int actual_nargs = (int)(meta & 0xFFFF);
    int returns_double = (int)((meta >> 16) & 1);
    int returns_float = (int)((meta >> 17) & 1);
    // #874/#875: callsite-declared variadic shape, so both the
    // DynamicSymbol and FFI-token branches below can tell fixed flonum
    // params (FREG_A0+) from variadic-tail doubles (bit pattern in
    // REG_A0+) and thread is_variadic through to the FFI ABI. See
    // src/codegen.c's CALLN emitter for the bit layout.
    int callsite_is_variadic = (int)((meta >> 18) & 1);
    int callsite_fixed_param_count = (int)((meta >> 19) & 0x1FFF);
    uint64_t double_arg_mask = (uint64_t)cc_read_i64(vm);
    uint64_t float_arg_mask = (uint64_t)cc_read_i64(vm);

    long long target_value = vm->regs[rs];
    DynamicSymbol *sym = cccc_find_dynamic_symbol(vm, target_value);
    bool is_ffi_token = !sym && target_value <= CCCC_FFI_TOKEN_BASE;
    ForeignFunc *ff = NULL;
    if (is_ffi_token) {
        int ffi_idx = (int)(CCCC_FFI_TOKEN_BASE - target_value);
        if (ffi_idx >= 0 && ffi_idx < vm->compiler.ffi_count)
            ff = &vm->compiler.ffi_table[ffi_idx];
        else
            is_ffi_token = false;
    }

    if (sym || ff) {
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

        // Layout comes from the callsite (what codegen actually emitted),
        // not from the FFI registration -- the two can diverge if the
        // guest declares the pointer with a different arity than the
        // registration.
        int int_reg_idx = 0;
        int fp_reg_idx = 0;
        for (int i = 0; i < actual_nargs; i++) {
            bool is_tail_double = callsite_is_variadic &&
                                   i >= callsite_fixed_param_count;
            if (i >= 8) {
                args[i] = vm->sp[i - 8];
            } else if (i < 64 && (float_arg_mask & (1ULL << i))) {
                args[i] = (long long)(unsigned int)cccc_freg_raw_f32(
                    vm, FREG_A0 + fp_reg_idx++);
            } else if (i < 64 && (double_arg_mask & (1ULL << i)) &&
                       !is_tail_double) {
                args[i] = cccc_freg_raw_f64(vm, FREG_A0 + fp_reg_idx++);
            } else {
                args[i] = vm->regs[REG_A0 + int_reg_idx++];
            }
        }

        // ABI (variadic-ness, fixed arg count for ffi_prep_cif_var) comes
        // from the registration when we have one; a dlsym'd symbol has no
        // registration, so it falls back to what the callsite declared.
        // #768: CALLN previously had no FFI-clear backstop at all -- an
        // unclassified/unshimmed host function reached through a function
        // pointer or dlsym'd symbol could write heap bytes with no VM hook
        // and leave a stale shadow stamp behind. Same backstop as CALLF
        // (see its call site for the full rationale); classification is by
        // name, so a `ff` (registered FFI symbol) with a known name gets
        // real classification, and a bare `sym` (dlsym, name usually
        // unrecognised) falls through to the sound default clear.
        ffi_shadow_backstop(vm, ff ? ff->name : sym->name, args, actual_nargs,
                            double_arg_mask, float_arg_mask);

        int rc;
        if (ff) {
            rc = cccc_call_native_function(vm, ff->func_ptr, ff->name, args,
                                           actual_nargs, double_arg_mask,
                                           float_arg_mask, ff->returns_double,
                                           ff->returns_float, ff->is_variadic,
                                           ff->num_fixed_args);
        } else {
            rc = cccc_call_native_function(vm, sym->func_ptr, sym->name, args,
                                           actual_nargs, double_arg_mask,
                                           float_arg_mask, returns_double,
                                           returns_float, callsite_is_variadic,
                                           callsite_is_variadic
                                               ? callsite_fixed_param_count
                                               : actual_nargs);
        }
        free(heap_args);
        return rc;
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

    // JMPI (computed goto: `goto *expr`) carries a single operand word --
    // no per-callsite nargs/masks the way CALL/CALLN do -- so there is no
    // sound way to dispatch a foreign function from here (#874). A target
    // landing on a dlopen'd symbol or an FFI token means the guest took
    // the address of a foreign function and jumped to it directly, which
    // is not a supported use of computed goto; report it instead of
    // silently calling with zero (and possibly wrong-ABI) arguments.
    if (cccc_find_dynamic_symbol(vm, target_value) ||
        target_value <= CCCC_FFI_TOKEN_BASE) {
        printf("error: indirect jump target is a foreign function pointer\n");
        return -1;
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

    vm->heap_ptr = vm->heap_ptr + total_size;
    void *user_ptr = (void *)(header + 1);

    // Track base address -> header for O(log n) interior-pointer lookups.
    sorted_allocs_insert(vm, user_ptr, header);

    // Fresh allocation: no effective type established yet. Nothing to do
    // here for the shadow itself (#653/#753) -- the bump allocator never
    // reuses bytes, and with the page-chunked shadow a page that was never
    // allocated already reads back as all TY_VOID ("no info"), so there's
    // no growth to force and eagerly touching a page here would just
    // undermine #753's goal of costing memory only for what's actually
    // stamped.

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

// Shared by op_MALC_fn (direct `malloc(...)` calls, routed here by codegen's
// is_extern_func_name special-casing) and cccc_ffi_malloc (src/stdlib/
// stdlib.c, #865) -- a bare `malloc` value escaping into a function pointer
// and called indirectly bypasses codegen's syntactic routing entirely, so it
// needs the identical VM-heap-aware implementation available as a plain
// callable function rather than only as register-based opcode glue.
void *cccc_vm_heap_malloc(VirtualMachine *vm, long long requested_size) {
    void *user_ptr = vm_heap_bump_alloc(vm, requested_size, 8);
    if (vm->debug_vm && user_ptr) {
        printf("MALC: allocated %zu bytes at 0x%llx\n", (size_t)((requested_size + 7) & ~7),
               (long long)user_ptr);
    }
    return user_ptr;
}

static inline int op_MALC_fn(VirtualMachine *vm) {
    // malloc: size in REG_A0, return pointer in REG_A0
    long long requested_size = vm->regs[REG_A0];
    vm->regs[REG_A0] = (long long)cccc_vm_heap_malloc(vm, requested_size);
    return 0;
}

// Shared by op_MALCA_fn (direct `aligned_alloc(...)` calls) and
// cccc_ffi_aligned_alloc (stdlib.c, #865) -- see cccc_vm_heap_malloc above.
void *cccc_vm_heap_malloc_aligned(VirtualMachine *vm, long long requested_size, size_t alignment) {
    // aligned_alloc requires a power-of-two alignment; reject anything else
    // (and anything smaller than the default 8-byte alignment just uses 8).
    if (alignment == 0 || (alignment & (alignment - 1)) != 0)
        return NULL;
    if (alignment < 8)
        alignment = 8;

    void *user_ptr = vm_heap_bump_alloc(vm, requested_size, alignment);
    if (vm->debug_vm && user_ptr) {
        printf("MALCA: allocated %zu bytes aligned to %zu at 0x%llx\n", (size_t)requested_size, alignment,
               (long long)user_ptr);
    }
    return user_ptr;
}

static inline int op_MALCA_fn(VirtualMachine *vm) {
    // aligned_alloc: size in REG_A0, alignment in REG_A1, return pointer in REG_A0
    long long requested_size = vm->regs[REG_A0];
    size_t alignment = (size_t)vm->regs[REG_A1];
    vm->regs[REG_A0] = (long long)cccc_vm_heap_malloc_aligned(vm, requested_size, alignment);
    return 0;
}

// Shared by op_PMEMA_fn (direct `posix_memalign(...)` calls) and
// cccc_ffi_posix_memalign (stdlib.c, #865) -- see cccc_vm_heap_malloc above.
// Returns 0/EINVAL/ENOMEM, matching posix_memalign's own return convention.
int cccc_vm_heap_posix_memalign(VirtualMachine *vm, void **memptr, size_t alignment,
                                long long requested_size) {
    // POSIX: alignment must be a power of two and a multiple of sizeof(void*).
    if (alignment == 0 || (alignment & (alignment - 1)) != 0 || alignment % sizeof(void *) != 0)
        return EINVAL;

    if (requested_size == 0) {
        // Implementation-defined: either NULL or a unique freeable pointer.
        // Return a minimal unique allocation, matching glibc's behaviour.
        requested_size = 1;
    }

    void *user_ptr = vm_heap_bump_alloc(vm, requested_size, alignment);
    if (!user_ptr)
        return ENOMEM;

    *memptr = user_ptr;
    if (vm->debug_vm) {
        printf("PMEMA: allocated %zu bytes aligned to %zu at 0x%llx\n", (size_t)requested_size, alignment,
               (long long)user_ptr);
    }
    return 0;
}

static inline int op_PMEMA_fn(VirtualMachine *vm) {
    // posix_memalign: memptr in REG_A0, alignment in REG_A1, size in REG_A2,
    // return status (0/EINVAL/ENOMEM) in REG_A0.
    void **memptr = (void **)vm->regs[REG_A0];
    size_t alignment = (size_t)vm->regs[REG_A1];
    long long requested_size = vm->regs[REG_A2];
    vm->regs[REG_A0] = cccc_vm_heap_posix_memalign(vm, memptr, alignment, requested_size);
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

// Shared by op_MFRE_fn (direct `free(...)`/`free_sized(...)`/
// `free_aligned_sized(...)` calls) and cccc_ffi_free (stdlib.c, #865) -- a
// bare `free` value escaping into a function pointer and called indirectly
// bypasses codegen's syntactic routing entirely and previously fell through
// to the raw host free(), which aborts when handed a VM-heap pointer (the
// bytes preceding it are cccc's own AllocHeader, not a real libmalloc
// chunk). Returns 0 on success, -1 if a fatal heap-safety violation
// (double-free, canary corruption) was detected and already reported via
// printf above -- op_MFRE_fn propagates that through the opcode dispatch's
// normal VM_TRAP_OR_RETURN/auto-debug-on-crash path; cccc_ffi_free (which
// has no such path available several C frames deep in a native call) instead
// hard-exits via error() using the same already-printed diagnostic.
int cccc_vm_heap_free(VirtualMachine *vm, void *ptr) {
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

    // Type shadow: a freed allocation has no effective type (#653);
    // clearing also stops a later, unrelated CHKT3 through a stale
    // interior pointer into this range from reporting a mismatch instead
    // of leaving detection to CHKP3's UAF check.
    type_shadow_clear(vm, ptr, header->size);

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

static inline int op_MFRE_fn(VirtualMachine *vm) {
    // free: pointer in REG_A0
    void *ptr = (void *)vm->regs[REG_A0];
    return cccc_vm_heap_free(vm, ptr);
}

static inline int op_MCPY_fn(VirtualMachine *vm) {
    // memcpy: dest in REG_A0, src in REG_A1, count in REG_A2
    void *dest = (void *)vm->regs[REG_A0];
    void *src = (void *)vm->regs[REG_A1];
    size_t count = (size_t)vm->regs[REG_A2];
    memcpy(dest, src, count);
    // Propagate effective type along with the bytes (C11 6.5p6): backs
    // struct/union assignment and the restrict memcpy-loop lowering, so
    // e.g. `struct S a = b;` keeps both members' shadow entries correct
    // (#653). type_shadow_copy clears dest when src isn't a tracked heap
    // range, rather than leaving it stale.
    type_shadow_copy(vm, dest, src, count);
    return 0;
}

static inline int op_MSET_fn(VirtualMachine *vm) {
    // Zero-fill: dest in REG_A0, count in REG_A2 (REG_A1 unused, value is always 0)
    // Backs ND_MEMZERO (the pre-zero step emitted for partial aggregate initialisers).
    void *dest = (void *)vm->regs[REG_A0];
    size_t count = (size_t)vm->regs[REG_A2];
    memset(dest, 0, count);
    // dest may be a stack or heap address; type_shadow_clear is a no-op
    // outside the tracked heap range, so this is safe either way (#653).
    type_shadow_clear(vm, dest, count);
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

// ========== C23 _Decimal32/64/128 (#402) ==========
// Thin dispatch into src/stdlib/decimal.c's BID-backed shim. Same
// zero-operand, fixed-A-register shape as WIDE_* above -- see the OPS_X
// comment in cccc.h for why. Guest-pointer validation (CHKP3) for the
// address operands is emitted by codegen before these ops, exactly like
// the WIDE_* arithmetic ops; the handlers here just dereference directly.
static inline int op_DADD_fn(VirtualMachine *vm) {
    cccc_dec_binop('+', (int)vm->regs[REG_A3], (void *)vm->regs[REG_A0],
                   (const void *)vm->regs[REG_A1], (const void *)vm->regs[REG_A2],
                   CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DSUB_fn(VirtualMachine *vm) {
    cccc_dec_binop('-', (int)vm->regs[REG_A3], (void *)vm->regs[REG_A0],
                   (const void *)vm->regs[REG_A1], (const void *)vm->regs[REG_A2],
                   CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DMUL_fn(VirtualMachine *vm) {
    cccc_dec_binop('*', (int)vm->regs[REG_A3], (void *)vm->regs[REG_A0],
                   (const void *)vm->regs[REG_A1], (const void *)vm->regs[REG_A2],
                   CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DDIV_fn(VirtualMachine *vm) {
    cccc_dec_binop('/', (int)vm->regs[REG_A3], (void *)vm->regs[REG_A0],
                   (const void *)vm->regs[REG_A1], (const void *)vm->regs[REG_A2],
                   CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DNEG_fn(VirtualMachine *vm) {
    cccc_dec_neg((int)vm->regs[REG_A2], (void *)vm->regs[REG_A0],
                 (const void *)vm->regs[REG_A1]);
    return 0;
}

static inline int op_DCMP_fn(VirtualMachine *vm) {
    int w = (int)vm->regs[REG_A2];
    int result = cccc_dec_cmp(w, (const void *)vm->regs[REG_A0],
                              (const void *)vm->regs[REG_A1]);
    vm->regs[REG_A0] = result;
    return 0;
}

static inline int op_DFROMI_fn(VirtualMachine *vm) {
    cccc_dec_from_int((int)vm->regs[REG_A2], (void *)vm->regs[REG_A0],
                      (long long)vm->regs[REG_A1], vm->regs[REG_A3] != 0,
                      CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DTOI_fn(VirtualMachine *vm) {
    long long out = 0;
    cccc_dec_to_int((int)vm->regs[REG_A1], (const void *)vm->regs[REG_A0],
                    &out, vm->regs[REG_A2] != 0, CCCC_DEC_ENV_DYNAMIC);
    vm->regs[REG_A0] = out;
    return 0;
}

static inline int op_DFROMBITS_fn(VirtualMachine *vm) {
    cccc_dec_from_bin((int)vm->regs[REG_A2], (void *)vm->regs[REG_A0],
                      (uint64_t)vm->regs[REG_A1], vm->regs[REG_A3] != 0,
                      CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DTOBITS_fn(VirtualMachine *vm) {
    uint64_t bits = 0;
    cccc_dec_to_bin((int)vm->regs[REG_A1], (const void *)vm->regs[REG_A0],
                    vm->regs[REG_A2] != 0, &bits, CCCC_DEC_ENV_DYNAMIC);
    vm->regs[REG_A0] = (long long)bits;
    return 0;
}

static inline int op_DCVT_fn(VirtualMachine *vm) {
    cccc_dec_convert((int)vm->regs[REG_A2], (int)vm->regs[REG_A3],
                     (void *)vm->regs[REG_A0], (const void *)vm->regs[REG_A1],
                     CCCC_DEC_ENV_DYNAMIC);
    return 0;
}

static inline int op_DFMT_fn(VirtualMachine *vm) {
    char *buf = (char *)vm->regs[REG_A0];
    size_t n = (size_t)vm->regs[REG_A1];
    const void *val = (const void *)vm->regs[REG_A2];
    int w = (int)vm->regs[REG_A3];
    int written = cccc_dec_format(buf, n, val, w);
    vm->regs[REG_A0] = written;
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
    // realloc is always a fresh bump allocation here (never in-place), so
    // carry the old block's effective type across the same way MCPY does
    // (#653) -- must run before op_MFRE_fn below clears the old range.
    type_shadow_copy(vm, new_ptr, ptr, copy_size);

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

// #865: cccc_ffi_calloc/realloc/reallocarray (stdlib.c) back a bare
// calloc/realloc/reallocarray value escaping into a function pointer and
// called indirectly -- unlike op_CALC_fn/op_REALC_fn/op_REALCA_fn above,
// these are NOT register-based opcode glue but fresh, small orchestrations
// built from the same shared primitives (cccc_vm_heap_malloc/
// cccc_vm_heap_free), so the opcode functions above are untouched and their
// direct-call behavior carries zero risk from this addition.
void *cccc_vm_heap_calloc(VirtualMachine *vm, long long nmemb, long long size) {
    long long total = nmemb * size;
    void *ptr = cccc_vm_heap_malloc(vm, total);
    if (ptr)
        memset(ptr, 0, total);
    return ptr;
}

void *cccc_vm_heap_realloc(VirtualMachine *vm, void *ptr, long long new_size) {
    if (!ptr)
        return cccc_vm_heap_malloc(vm, new_size); // realloc(NULL, size) == malloc(size)

    if (new_size <= 0) {
        // realloc(ptr, 0) == free(ptr); a fatal free-path violation here is
        // already reported by cccc_vm_heap_free's own diagnostic printf --
        // nothing further to add before returning NULL.
        cccc_vm_heap_free(vm, ptr);
        return NULL;
    }

    // Pointer isn't inside the VM heap arena, or has no valid AllocHeader
    // (e.g. Block_copy's malloc, strdup, aligned_alloc under -V) -- fall
    // back to system realloc directly, same fallback op_REALC_fn applies.
    if (!is_vm_heap_ptr(vm, ptr))
        return realloc(ptr, (size_t)new_size);
    AllocHeader *old_header = ((AllocHeader *)ptr) - 1;
    if (old_header->magic != 0xDEADBEEF)
        return realloc(ptr, (size_t)new_size);

    size_t old_size = old_header->size;
    void *new_ptr = cccc_vm_heap_malloc(vm, new_size);
    if (!new_ptr)
        return NULL;

    size_t copy_size = old_size < (size_t)new_size ? old_size : (size_t)new_size;
    memcpy(new_ptr, ptr, copy_size);
    // realloc is always a fresh bump allocation here (never in-place), so
    // carry the old block's effective type across the same way MCPY/op_REALC_fn
    // do (#653) -- must run before cccc_vm_heap_free below clears the old range.
    type_shadow_copy(vm, new_ptr, ptr, copy_size);
    cccc_vm_heap_free(vm, ptr);
    return new_ptr;
}

void *cccc_vm_heap_reallocarray(VirtualMachine *vm, void *ptr, long long nmemb, long long size) {
    if (nmemb < 0 || size < 0 || (size != 0 && nmemb > (INT64_MAX / size)))
        return NULL; // overflow/negative -- original ptr untouched, matches op_REALCA_fn
    return cccc_vm_heap_realloc(vm, ptr, nmemb * size);
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

// ========== SIMD / Vector Operations (#72/#463, widened to 256/512-bit by
// #722) ==========
//
// Vector registers (vregs[], see VReg in cccc.h) hold up to 64 raw bytes; the
// opcode name carries the lane TYPE (mirroring the FADD3 vs FADD3_F32 scalar
// split), and the operand's "scale" byte carries the active WIDTH (VLDR/VSTR:
// byte count) or LANE COUNT (every other op) -- see ENCODE_RRRS/DECODE_RRRS
// in internal.h. Two-register ops (VLDR/VSTR/VSPLAT/VNEG/VNOT/VCVT) decode
// with DECODE_RRRS and simply leave rs2 unread, exactly like the existing
// VEC_EXTRACT_*/VEC_INSERT_* handlers already do. Binary/unary arithmetic
// reads its operand(s) into locals before writing vregs[rd], so `rd == rs1`
// (or `rd == rs2`) aliasing is safe.
//
// The op families below are near-identical across lane types, so they are
// generated with local macros (undef'd immediately after use) rather than
// hand-duplicated ~40 times; each expansion is a small, ordinary op_NAME_fn
// like every other handler in this file.

static inline void ld_vN(const void *p, VReg *out, int bytes) { __builtin_memcpy(out, p, (size_t)bytes); }
static inline void st_vN(void *p, const VReg *v, int bytes) { __builtin_memcpy(p, v, (size_t)bytes); }

// The compiler only ever emits width == 16/32/64 (parse.c's vector_size
// gate), but the width rides in an unauthenticated 8-bit bytecode operand
// (0-255) -- unlike pre-#722, where VLDR/VSTR always moved a fixed
// sizeof(VReg) with no attacker/corruption-controlled length at all. A
// malformed or hand-crafted .c4 file could otherwise drive ld_vN/st_vN to
// memcpy up to 255 bytes into/out of a 64-byte vregs[] slot, corrupting
// adjacent VM state. Reject anything else outright, same trapping style as
// VEC_IDIV/VEC_IMOD's zero-divisor check below.
static inline bool vreg_width_ok(int width) { return width == 16 || width == 32 || width == 64; }

static inline int op_VLDR_fn(VirtualMachine *vm) {
    // vregs[rd] = <width> raw bytes at regs[rs] (unaligned-safe)
    // Format: [VLDR] [rd:8|rs:8|unused:8|width:8]
    long long operands = cc_read_word(vm);
    int rd, rs, rs2, width;
    DECODE_RRRS(operands, rd, rs, rs2, width);
    (void)rs2;
    if (!vreg_width_ok(width)) {
        printf("\n========== CORRUPT BYTECODE ==========\n");
        printf("Invalid vector load width %d (expected 16, 32, or 64)\n", width);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc, (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], width, WATCH_READ);
    ld_vN((void *)vm->regs[rs], &vm->vregs[rd], width);
    return 0;
}

static inline int op_VSTR_fn(VirtualMachine *vm) {
    // <width> raw bytes at regs[rs] = vregs[rd]
    // Format: [VSTR] [rd:8|rs:8|unused:8|width:8]
    long long operands = cc_read_word(vm);
    int rd, rs, rs2, width;
    DECODE_RRRS(operands, rd, rs, rs2, width);
    (void)rs2;
    if (!vreg_width_ok(width)) {
        printf("\n========== CORRUPT BYTECODE ==========\n");
        printf("Invalid vector store width %d (expected 16, 32, or 64)\n", width);
        printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc, (long long)vm->pc);
        printf("======================================\n");
        return -1;
    }

    st_vN((void *)vm->regs[rs], &vm->vregs[rd], width);
    WATCHPOINT_CHECK(vm, (void *)vm->regs[rs], width, WATCH_WRITE);
    return 0;
}

static inline int op_VMOV3_fn(VirtualMachine *vm) {
    // vregs[rd] = vregs[rs1] (full-register copy, all 64 bytes -- the
    // uncopied tail beyond the value's real width is simply don't-care)
    // Format: [VMOV3] [rd:8|rs1:8|unused:48]
    long long operands = cc_read_word(vm);
    int rd, rs1;
    DECODE_RR(operands, rd, rs1);

    vm->vregs[rd] = vm->vregs[rs1];
    return 0;
}

// ---- Splat: vregs[rd].FIELD[0..count-1] = (scalar src, broadcast) ----

#define VEC_SPLAT_FROM_FREG(NAME, FIELD, CTYPE)                              \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, count;                                              \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                           \
        (void)rs2;                                                            \
        CTYPE v = (CTYPE)cccc_freg_get_f64(vm, rs1);                          \
        for (int i = 0; i < count; i++) vm->vregs[rd].FIELD[i] = v;           \
        return 0;                                                             \
    }

#define VEC_SPLAT_FROM_REG(NAME, FIELD, CTYPE)                               \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, count;                                              \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                           \
        (void)rs2;                                                            \
        CTYPE v = (CTYPE)vm->regs[rs1];                                       \
        for (int i = 0; i < count; i++) vm->vregs[rd].FIELD[i] = v;           \
        return 0;                                                             \
    }

VEC_SPLAT_FROM_FREG(VSPLAT_F64, f64, double)
VEC_SPLAT_FROM_FREG(VSPLAT_F32, f32, float)
VEC_SPLAT_FROM_REG(VSPLAT_I64, i64, int64_t)
VEC_SPLAT_FROM_REG(VSPLAT_I32, i32, int32_t)
VEC_SPLAT_FROM_REG(VSPLAT_I16, i16, int16_t)
VEC_SPLAT_FROM_REG(VSPLAT_I8, i8, int8_t)

#undef VEC_SPLAT_FROM_FREG
#undef VEC_SPLAT_FROM_REG

// ---- Extract/Insert: scalar <-> single lane. RRRS-encoded; the lane index
// rides in the "scale" field (see ENCODE_RRRS/DECODE_RRRS). ----

#define VEC_EXTRACT_TO_FREG(NAME, FIELD)                                      \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, lane;                                               \
        DECODE_RRRS(operands, rd, rs1, rs2, lane);                            \
        (void)rs2;                                                            \
        cccc_freg_set_f64(vm, rd, (double)vm->vregs[rs1].FIELD[lane]);        \
        return 0;                                                             \
    }

#define VEC_EXTRACT_TO_REG(NAME, FIELD)                                       \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, lane;                                               \
        DECODE_RRRS(operands, rd, rs1, rs2, lane);                            \
        (void)rs2;                                                            \
        vm->regs[rd] = (long long)vm->vregs[rs1].FIELD[lane];                 \
        return 0;                                                             \
    }

VEC_EXTRACT_TO_FREG(VEXTRACT_F64, f64)
VEC_EXTRACT_TO_FREG(VEXTRACT_F32, f32)
VEC_EXTRACT_TO_REG(VEXTRACT_I64, i64)
VEC_EXTRACT_TO_REG(VEXTRACT_I32, i32)
VEC_EXTRACT_TO_REG(VEXTRACT_I16, i16)
VEC_EXTRACT_TO_REG(VEXTRACT_I8, i8)

#undef VEC_EXTRACT_TO_FREG
#undef VEC_EXTRACT_TO_REG

#define VEC_INSERT_FROM_FREG(NAME, FIELD, CTYPE)                              \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, lane;                                               \
        DECODE_RRRS(operands, rd, rs1, rs2, lane);                            \
        (void)rs2;                                                            \
        vm->vregs[rd].FIELD[lane] = (CTYPE)cccc_freg_get_f64(vm, rs1);        \
        return 0;                                                             \
    }

#define VEC_INSERT_FROM_REG(NAME, FIELD, CTYPE)                               \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, lane;                                               \
        DECODE_RRRS(operands, rd, rs1, rs2, lane);                            \
        (void)rs2;                                                            \
        vm->vregs[rd].FIELD[lane] = (CTYPE)vm->regs[rs1];                     \
        return 0;                                                             \
    }

VEC_INSERT_FROM_FREG(VINSERT_F64, f64, double)
VEC_INSERT_FROM_FREG(VINSERT_F32, f32, float)
VEC_INSERT_FROM_REG(VINSERT_I64, i64, int64_t)
VEC_INSERT_FROM_REG(VINSERT_I32, i32, int32_t)
VEC_INSERT_FROM_REG(VINSERT_I16, i16, int16_t)
VEC_INSERT_FROM_REG(VINSERT_I8, i8, int8_t)

#undef VEC_INSERT_FROM_FREG
#undef VEC_INSERT_FROM_REG

// ---- Per-lane arithmetic: vregs[rd].FIELD[i] = vregs[rs1].FIELD[i] OP vregs[rs2].FIELD[i] ----

#define VEC_BINOP(NAME, FIELD, OP)                                            \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, count;                                              \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                           \
        VReg a = vm->vregs[rs1], b = vm->vregs[rs2], r = {0};                       \
        for (int i = 0; i < count; i++) r.FIELD[i] = a.FIELD[i] OP b.FIELD[i]; \
        vm->vregs[rd] = r;                                                    \
        return 0;                                                             \
    }

#define VEC_NEG(NAME, FIELD)                                                  \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                    \
        long long operands = cc_read_word(vm);                                \
        int rd, rs1, rs2, count;                                              \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                           \
        (void)rs2;                                                            \
        VReg a = vm->vregs[rs1], r = {0};                                           \
        for (int i = 0; i < count; i++) r.FIELD[i] = -a.FIELD[i];             \
        vm->vregs[rd] = r;                                                    \
        return 0;                                                             \
    }

VEC_BINOP(VADD_F64, f64, +)
VEC_BINOP(VSUB_F64, f64, -)
VEC_BINOP(VMUL_F64, f64, *)
VEC_BINOP(VDIV_F64, f64, /)
VEC_NEG(VNEG_F64, f64)

VEC_BINOP(VADD_F32, f32, +)
VEC_BINOP(VSUB_F32, f32, -)
VEC_BINOP(VMUL_F32, f32, *)
VEC_BINOP(VDIV_F32, f32, /)
VEC_NEG(VNEG_F32, f32)

// Integer lane division is intentionally not provided as a plain VEC_BINOP
// (needs div-by-zero handling per lane); see VEC_IDIV below.
VEC_BINOP(VADD_I64, i64, +)
VEC_BINOP(VSUB_I64, i64, -)
VEC_BINOP(VMUL_I64, i64, *)
VEC_NEG(VNEG_I64, i64)

VEC_BINOP(VADD_I32, i32, +)
VEC_BINOP(VSUB_I32, i32, -)
VEC_BINOP(VMUL_I32, i32, *)
VEC_NEG(VNEG_I32, i32)

VEC_BINOP(VADD_I16, i16, +)
VEC_BINOP(VSUB_I16, i16, -)
VEC_BINOP(VMUL_I16, i16, *)
VEC_NEG(VNEG_I16, i16)

VEC_BINOP(VADD_I8, i8, +)
VEC_BINOP(VSUB_I8, i8, -)
VEC_BINOP(VMUL_I8, i8, *)
VEC_NEG(VNEG_I8, i8)

#undef VEC_BINOP
#undef VEC_NEG

// ---- Bitwise (tracker #715): width-agnostic over i64[0..words-1], where
// `words` (operand-carried, like VLDR/VSTR's byte width) is the value's
// byte width / 8. ----

#define VEC_BITBINOP(NAME, OP)                                               \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                   \
        long long operands = cc_read_word(vm);                               \
        int rd, rs1, rs2, words;                                             \
        DECODE_RRRS(operands, rd, rs1, rs2, words);                          \
        VReg a = vm->vregs[rs1], b = vm->vregs[rs2], r = {0};                      \
        for (int i = 0; i < words; i++) r.i64[i] = a.i64[i] OP b.i64[i];     \
        vm->vregs[rd] = r;                                                   \
        return 0;                                                            \
    }

VEC_BITBINOP(VAND, &)
VEC_BITBINOP(VOR,  |)
VEC_BITBINOP(VXOR, ^)

#undef VEC_BITBINOP

static inline int op_VNOT_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, words;
    DECODE_RRRS(operands, rd, rs1, rs2, words);
    (void)rs2;
    VReg a = vm->vregs[rs1], r = {0};
    for (int i = 0; i < words; i++) r.i64[i] = ~a.i64[i];
    vm->vregs[rd] = r;
    return 0;
}

// ---- Integer lane division/modulo (tracker #715): traps on a zero divisor
// or on MIN/-1 overflow, mirroring op_DIVC_fn/op_MODC_fn's scalar policy
// (src/ops.c op_DIVC_fn) rather than op_DIV3_fn's non-trapping LLONG_MIN
// return -- vector integer division is deliberately stricter. ----

#define VEC_IDIV(NAME, FIELD, CTYPE, MINVAL)                                 \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                   \
        long long operands = cc_read_word(vm);                               \
        int rd, rs1, rs2, count;                                             \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                          \
        VReg a = vm->vregs[rs1], b = vm->vregs[rs2], r = {0};                      \
        for (int i = 0; i < count; i++) {                                    \
            CTYPE bv = b.FIELD[i];                                           \
            if (bv == 0) {                                                   \
                printf("\n========== DIVISION BY ZERO ==========\n");        \
                printf("Attempted vector lane division by zero (lane %d)\n", i); \
                printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,   \
                       (long long)vm->pc);                                   \
                printf("======================================\n");         \
                return -1;                                                   \
            }                                                                \
            if (a.FIELD[i] == (MINVAL) && bv == (CTYPE)-1) {                 \
                printf("\n========== INTEGER OVERFLOW ==========\n");        \
                printf("Vector lane division overflow (lane %d)\n", i);      \
                printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,   \
                       (long long)vm->pc);                                   \
                printf("======================================\n");         \
                return -1;                                                   \
            }                                                                \
            r.FIELD[i] = a.FIELD[i] / bv;                                    \
        }                                                                    \
        vm->vregs[rd] = r;                                                   \
        return 0;                                                            \
    }

#define VEC_IMOD(NAME, FIELD, CTYPE, MINVAL)                                 \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                   \
        long long operands = cc_read_word(vm);                               \
        int rd, rs1, rs2, count;                                             \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                          \
        VReg a = vm->vregs[rs1], b = vm->vregs[rs2], r = {0};                      \
        for (int i = 0; i < count; i++) {                                    \
            CTYPE bv = b.FIELD[i];                                           \
            if (bv == 0) {                                                   \
                printf("\n========== DIVISION BY ZERO ==========\n");        \
                printf("Attempted vector lane modulo by zero (lane %d)\n", i);   \
                printf("PC:       0x%llx (offset: %lld)\n", (long long)vm->pc,   \
                       (long long)vm->pc);                                   \
                printf("======================================\n");         \
                return -1;                                                   \
            }                                                                \
            if (a.FIELD[i] == (MINVAL) && bv == (CTYPE)-1) {                 \
                r.FIELD[i] = 0;                                              \
            } else {                                                         \
                r.FIELD[i] = a.FIELD[i] % bv;                                \
            }                                                                \
        }                                                                    \
        vm->vregs[rd] = r;                                                   \
        return 0;                                                            \
    }

VEC_IDIV(VDIV_I64, i64, int64_t, INT64_MIN)
VEC_IDIV(VDIV_I32, i32, int32_t, INT32_MIN)
VEC_IDIV(VDIV_I16, i16, int16_t, INT16_MIN)
VEC_IDIV(VDIV_I8, i8, int8_t, INT8_MIN)

VEC_IMOD(VMOD_I64, i64, int64_t, INT64_MIN)
VEC_IMOD(VMOD_I32, i32, int32_t, INT32_MIN)
VEC_IMOD(VMOD_I16, i16, int16_t, INT16_MIN)
VEC_IMOD(VMOD_I8, i8, int8_t, INT8_MIN)

#undef VEC_IDIV
#undef VEC_IMOD

// ---- Comparisons (tracker #715): GCC semantics -- per-lane all-ones (-1) if
// true, 0 if false, written into a same-width signed integer lane. Unsigned
// variants (VCLTU/VCLEU) compare the unsigned view for int lanes. ----

#define VEC_FCMP(NAME, FIELD, ICAST, OP)                                     \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                   \
        long long operands = cc_read_word(vm);                               \
        int rd, rs1, rs2, count;                                             \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                          \
        VReg a = vm->vregs[rs1], b = vm->vregs[rs2], r = {0};                      \
        for (int i = 0; i < count; i++)                                      \
            r.ICAST[i] = (a.FIELD[i] OP b.FIELD[i]) ? -1 : 0;                \
        vm->vregs[rd] = r;                                                   \
        return 0;                                                            \
    }

#define VEC_ICMP(NAME, FIELD, CTYPE, OP)                                    \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                   \
        long long operands = cc_read_word(vm);                               \
        int rd, rs1, rs2, count;                                             \
        DECODE_RRRS(operands, rd, rs1, rs2, count);                          \
        VReg a = vm->vregs[rs1], b = vm->vregs[rs2], r = {0};                      \
        for (int i = 0; i < count; i++)                                      \
            r.FIELD[i] = ((CTYPE)a.FIELD[i] OP (CTYPE)b.FIELD[i]) ? -1 : 0;   \
        vm->vregs[rd] = r;                                                   \
        return 0;                                                            \
    }

VEC_FCMP(VCEQ_F64, f64, i64, ==)
VEC_FCMP(VCNE_F64, f64, i64, !=)
VEC_FCMP(VCLT_F64, f64, i64, <)
VEC_FCMP(VCLE_F64, f64, i64, <=)

VEC_FCMP(VCEQ_F32, f32, i32, ==)
VEC_FCMP(VCNE_F32, f32, i32, !=)
VEC_FCMP(VCLT_F32, f32, i32, <)
VEC_FCMP(VCLE_F32, f32, i32, <=)

VEC_ICMP(VCEQ_I64, i64, int64_t, ==)
VEC_ICMP(VCNE_I64, i64, int64_t, !=)
VEC_ICMP(VCLT_I64, i64, int64_t, <)
VEC_ICMP(VCLE_I64, i64, int64_t, <=)
VEC_ICMP(VCLTU_I64, i64, uint64_t, <)
VEC_ICMP(VCLEU_I64, i64, uint64_t, <=)

VEC_ICMP(VCEQ_I32, i32, int32_t, ==)
VEC_ICMP(VCNE_I32, i32, int32_t, !=)
VEC_ICMP(VCLT_I32, i32, int32_t, <)
VEC_ICMP(VCLE_I32, i32, int32_t, <=)
VEC_ICMP(VCLTU_I32, i32, uint32_t, <)
VEC_ICMP(VCLEU_I32, i32, uint32_t, <=)

VEC_ICMP(VCEQ_I16, i16, int16_t, ==)
VEC_ICMP(VCNE_I16, i16, int16_t, !=)
VEC_ICMP(VCLT_I16, i16, int16_t, <)
VEC_ICMP(VCLE_I16, i16, int16_t, <=)
VEC_ICMP(VCLTU_I16, i16, uint16_t, <)
VEC_ICMP(VCLEU_I16, i16, uint16_t, <=)

VEC_ICMP(VCEQ_I8, i8, int8_t, ==)
VEC_ICMP(VCNE_I8, i8, int8_t, !=)
VEC_ICMP(VCLT_I8, i8, int8_t, <)
VEC_ICMP(VCLE_I8, i8, int8_t, <=)
VEC_ICMP(VCLTU_I8, i8, uint8_t, <)
VEC_ICMP(VCLEU_I8, i8, uint8_t, <=)

#undef VEC_FCMP
#undef VEC_ICMP

// ---- Select (tracker #715): GCC vector ?:, nonzero-per-lane condition.
// rd is pre-loaded by codegen with the else-arm; VSEL overwrites only the
// lanes where cond is nonzero -- a read-modify-write on rd, like VINSERT_*
// (see op_has_vector_operand()'s comment in optimize.c for why this is safe
// under the optimizer's fully-opaque treatment of vector opcodes). ----

#define VEC_SEL(NAME, FIELD)                                                 \
    static inline int op_##NAME##_fn(VirtualMachine *vm) {                   \
        long long operands = cc_read_word(vm);                               \
        int rd, rcond, rthen, count;                                         \
        DECODE_RRRS(operands, rd, rcond, rthen, count);                      \
        VReg cond = vm->vregs[rcond], then_ = vm->vregs[rthen];              \
        VReg r = vm->vregs[rd];                                              \
        for (int i = 0; i < count; i++)                                      \
            if (cond.FIELD[i]) r.FIELD[i] = then_.FIELD[i];                  \
        vm->vregs[rd] = r;                                                   \
        return 0;                                                            \
    }

VEC_SEL(VSEL_8,  i8)
VEC_SEL(VSEL_16, i16)
VEC_SEL(VSEL_32, i32)
VEC_SEL(VSEL_64, i64)

#undef VEC_SEL

// ---- __builtin_convertvector (tracker #715): same lane count on both
// sides, rides in the operand like every other op; integer conversion
// truncates toward zero (C cast semantics, matches GCC). ----

static inline int op_VCVT_I32_F32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, count;
    DECODE_RRRS(operands, rd, rs1, rs2, count);
    (void)rs2;
    VReg a = vm->vregs[rs1], r = {0};
    for (int i = 0; i < count; i++) r.i32[i] = (int32_t)a.f32[i];
    vm->vregs[rd] = r;
    return 0;
}

static inline int op_VCVT_F32_I32_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, count;
    DECODE_RRRS(operands, rd, rs1, rs2, count);
    (void)rs2;
    VReg a = vm->vregs[rs1], r = {0};
    for (int i = 0; i < count; i++) r.f32[i] = (float)a.i32[i];
    vm->vregs[rd] = r;
    return 0;
}

static inline int op_VCVT_I64_F64_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, count;
    DECODE_RRRS(operands, rd, rs1, rs2, count);
    (void)rs2;
    VReg a = vm->vregs[rs1], r = {0};
    for (int i = 0; i < count; i++) r.i64[i] = (int64_t)a.f64[i];
    vm->vregs[rd] = r;
    return 0;
}

static inline int op_VCVT_F64_I64_fn(VirtualMachine *vm) {
    long long operands = cc_read_word(vm);
    int rd, rs1, rs2, count;
    DECODE_RRRS(operands, rd, rs1, rs2, count);
    (void)rs2;
    VReg a = vm->vregs[rs1], r = {0};
    for (int i = 0; i < count; i++) r.f64[i] = (double)a.i64[i];
    vm->vregs[rd] = r;
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

static inline int op_CHKR_fn(VirtualMachine *vm) {
    // Checked-pointer range check (Checked C-style spatial safety, #770/#482-484).
    // Format: [CHKR] [rs_addr:8|rs_lo:8|rs_hi:8|unused:8] (RRR operand word)
    //         [access_size:i64]
    // Unlike CHKB/CHKP3, lo/hi are NOT derived from sorted_allocs or any
    // other allocation-time side table -- they are the caller-computed
    // declared bounds of a checked pointer (from its count()/byte_count()/
    // bounds() attribute), passed in fresh at every checked access by
    // codegen. That is what lets this opcode work uniformly across heap,
    // stack and global storage, which CHKB cannot do for non-heap bases.
    long long operands = cc_read_word(vm);
    int rs_addr, rs_lo, rs_hi;
    DECODE_RRR(operands, rs_addr, rs_lo, rs_hi);
    long long access_size = cc_read_i64(vm);

    if (!(vm->flags & CCCC_CHECKED_BOUNDS))
        return 0;

    long long addr = vm->regs[rs_addr];
    long long lo   = vm->regs[rs_lo];
    long long hi   = vm->regs[rs_hi];

    // NULL is always out of range for any declared bound: this is what gives
    // a [[cccc::single]] pointer its "null-checked on deref" semantics for
    // free, since its lowering is lo=p, hi=p+sizeof(T) and a NULL p would
    // otherwise need a separate check.
    if (addr == 0 || addr < lo || addr + access_size > hi) {
        printf("\n========== CHECKED BOUNDS VIOLATION ==========\n");
        printf("Checked-pointer access out of declared bounds\n");
        printf("Address:       0x%llx\n", addr);
        printf("Access size:   %lld bytes\n", access_size);
        printf("Declared bounds: [0x%llx, 0x%llx)\n", lo, hi);
        printf("PC: 0x%llx (offset: %lld)\n",
               (long long)vm->pc, (long long)vm->pc);
        printf("================================================\n");
        return -1;
    }

    return 0;
}

static inline int op_CHKNT_fn(VirtualMachine *vm) {
    // Checked-pointer null-terminator guard (#923). Format:
    // [CHKNT] [rs_addr:8|rs_hi:8|rs_val:8|unused:8] (RRR operand word)
    //         [elem_size:i64]
    // Enforces only the store half of the [[cccc::ntarray]] invariant: the
    // widened terminator slot (the +1 element CHKR's bounds already cover)
    // must stay null. addr is the address just stored to; hi is the checked
    // pointer's own already-widened upper bound (the same hi CHKR just range-
    // checked addr against, so no range check is repeated here); val is the
    // value that was stored. This does NOT verify a null terminator is
    // actually present anywhere -- that is unsound to check from the
    // declaration alone (see man/SAFETY.md's Checked Pointers section and
    // the CHKNT comment in src/cccc.h).
    long long operands = cc_read_word(vm);
    int rs_addr, rs_hi, rs_val;
    DECODE_RRR(operands, rs_addr, rs_hi, rs_val);
    long long elem_size = cc_read_i64(vm);

    if (!(vm->flags & CCCC_CHECKED_BOUNDS))
        return 0;

    if (elem_size <= 0)
        return 0;

    long long addr = vm->regs[rs_addr];
    long long hi   = vm->regs[rs_hi];
    long long val  = vm->regs[rs_val];

    if (hi < elem_size)
        return 0; // no room for a terminator slot at all -- nothing to guard

    long long term_slot = hi - elem_size;
    if (addr == term_slot && val != 0) {
        printf("\n========== CHECKED TERMINATOR VIOLATION ==========\n");
        printf("Non-null store into a [[cccc::ntarray]] terminator slot\n");
        printf("Address:         0x%llx\n", addr);
        printf("Terminator slot: [0x%llx, 0x%llx)\n", term_slot, hi);
        printf("Value stored:    %lld\n", val);
        printf("PC: 0x%llx (offset: %lld)\n",
               (long long)vm->pc, (long long)vm->pc);
        printf("====================================================\n");
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

// ---- #751: host-function classification for the CALLF shadow backstop ----
//
// op_CALLF_fn's generic backstop (below) must clear the shadow of any heap
// allocation reachable through an unshimmed host call, since the VM can't
// observe what the call actually wrote. That's sound (no false positives)
// but lossy: for a read-only function (strlen, memcmp, ...) nothing was
// written at all, and for a function with a statically-known write extent
// (fread, snprintf, ...) only a sub-range of the allocation was. This
// table recovers that coverage for a handful of common libc/POSIX names
// without weakening the backstop's guarantee for anything else -- an
// unclassified name keeps today's whole-allocation clear exactly as
// before, so the no-false-positive property can only ever be preserved or
// improved by adding an entry here, never regressed.
typedef enum {
    FFI_SHADOW_DEFAULT = 0, // unclassified: today's whole-allocation clear
    FFI_SHADOW_HANDLED,     // memcpy/memmove: the shim already propagated; skip entirely
    FFI_SHADOW_READONLY,    // never writes through any pointer arg: no clear at all
    FFI_SHADOW_BOUNDED,     // writes only [args[out_arg], args[out_arg]+len): narrow the clear to that
    FFI_SHADOW_PRINTF,      // printf family: writes only out_arg (if any), UNLESS the format
                            // string (fmt_arg) contains %n -- then every pointer-shaped
                            // argument is unknown-write and falls back to FFI_SHADOW_DEFAULT.
                            // See #768.
} FfiShadowClass;

typedef struct FfiShadowRule {
    const char *name;
    FfiShadowClass class_;
    int out_arg;      // BOUNDED/PRINTF: index of the pointer arg written, or -1 if none
    int len_arg;      // BOUNDED/PRINTF: index of the integer arg giving the length, or -1 for fixed_len
    int len_arg2;     // BOUNDED: second length arg to multiply in (fread's nmemb), or -1
    size_t fixed_len; // BOUNDED: used when len_arg == -1 (a statically-known length)
    int fmt_arg;      // PRINTF: index of the format-string argument, else -1
} FfiShadowRule;

static const FfiShadowRule ffi_shadow_rules[] = {
    // Folds in what used to be two ad hoc strcmp(ff->name, ...) checks.
    {"memcpy",  FFI_SHADOW_HANDLED, 0, -1, -1, 0, -1},
    {"memmove", FFI_SHADOW_HANDLED, 0, -1, -1, 0, -1},
    // #769: wrap_qsort (src/stdlib/stdlib.c) does its own shadow bookkeeping
    // around the host qsort() call -- preserves the shadow across the sort
    // when the array's elements carry a uniform shadow pattern (checked
    // both before and after, to catch a comparator that writes through its
    // arguments), otherwise clears [base, base+nmemb*size) -- narrower than
    // this backstop's whole-allocation default, since that's the exact
    // range a host qsort() call can touch. Nothing left for this backstop
    // to do for qsort's other arguments (nmemb, size, the comparator's
    // code address) since none of them are heap pointers.
    {"qsort",   FFI_SHADOW_HANDLED, 0, -1, -1, 0, -1},

    // Never write through a pointer argument.
    {"strlen",  FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"strnlen", FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"strcmp",  FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"strncmp", FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"memcmp",  FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"strchr",  FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"strrchr", FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"strstr",  FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"fwrite",  FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"puts",    FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"fputs",   FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"atoi",    FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"atol",    FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    {"atof",    FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},
    // #769: the host bsearch() writes through no argument at all -- only
    // reads the array to find a match. Its comparator runs guest code via
    // the same #738 trampoline as qsort's, and that code is fully
    // shadow-tracked like any other guest execution, so there's no
    // unobserved write to account for here.
    {"bsearch", FFI_SHADOW_READONLY, 0, -1, -1, 0, -1},

    // Write only a statically-known extent through one designated arg.
    // Every *other* pointer-shaped argument to these calls still falls
    // through to the default whole-allocation clear below -- narrowing is
    // per-argument, not per-call.
    {"memset",   FFI_SHADOW_BOUNDED, 0, 2, -1, 0, -1},
    {"fread",    FFI_SHADOW_BOUNDED, 0, 1, 2, 0, -1}, // len = size(arg1) * nmemb(arg2)
    {"fgets",    FFI_SHADOW_BOUNDED, 0, 1, -1, 0, -1},
    {"strncpy",  FFI_SHADOW_BOUNDED, 0, 2, -1, 0, -1},
    {"read",     FFI_SHADOW_BOUNDED, 1, 2, -1, 0, -1},
    {"recv",     FFI_SHADOW_BOUNDED, 1, 2, -1, 0, -1},
    {"recvfrom", FFI_SHADOW_BOUNDED, 1, 2, -1, 0, -1},
    // strtol/strtod write a single pointer (*endptr) through arg 1, a
    // fixed sizeof(char*) bytes -- there's no argument to read a length
    // from.
    {"strtol",   FFI_SHADOW_BOUNDED, 1, -1, -1, sizeof(char *), -1},
    {"strtod",   FFI_SHADOW_BOUNDED, 1, -1, -1, sizeof(char *), -1},
    // __cccc_dec_strtod(w, dst, s, endp) -- #832's strtod32/64/128 shim
    // (src/stdlib/stdlib.c). `endp` is arg index 3 here, not 1, since `dst`
    // (the decimal out-param) occupies index 1. `dst`'s own write still
    // falls through to the default whole-allocation clear below -- its
    // width varies with `w` (4/8/16 bytes), which this table has no way to
    // express, so it's conservative rather than unsafe.
    {"__cccc_dec_strtod", FFI_SHADOW_BOUNDED, 3, -1, -1, sizeof(char *), -1},

    // #768: printf family. Every argument other than the designated output
    // buffer (if any) is read-only UNLESS the format string contains a %n
    // conversion, which can write through any pointer-shaped argument
    // (including a variadic one) -- ffi_shadow_backstop checks the format
    // string at the call site and demotes to FFI_SHADOW_DEFAULT whenever it
    // can't prove %n is absent (unreadable pointer, non-literal content it
    // can't scan confidently, or an actual %n), so this can only preserve
    // or improve the no-false-positive property, never regress it.
    {"printf",   FFI_SHADOW_PRINTF, -1, -1, -1, 0, 0},
    {"fprintf",  FFI_SHADOW_PRINTF, -1, -1, -1, 0, 1},
    {"dprintf",  FFI_SHADOW_PRINTF, -1, -1, -1, 0, 1},
    {"sprintf",  FFI_SHADOW_PRINTF,  0, -1, -1, 0, 1}, // unbounded write extent: out_arg still
                                                        // falls through to the whole-allocation clear
    {"snprintf", FFI_SHADOW_PRINTF,  0,  1, -1, 0, 2}, // bounded write: narrows like FFI_SHADOW_BOUNDED

    // Deliberately left unclassified (default whole-allocation clear):
    // scanf/sscanf/fscanf (write through *every* pointer argument by
    // design, so the PRINTF tier doesn't apply); vprintf/vsprintf/
    // vsnprintf/vfprintf (the va_list argument is itself written through as
    // iteration state, and its pointees aren't reachable from args[] --
    // possible follow-up); struct out-params like stat/gettimeofday/
    // localtime_r/getaddrinfo/sigaction (safe by omission, which is the
    // point of this table: absence keeps today's sound-but-lossy default).
    // qsort/bsearch (#769) are handled/classified above instead of falling
    // here: qsort restores shadow coverage across the sort in the common
    // uniform-element case via wrap_qsort's own bookkeeping rather than
    // this table's generic per-argument narrowing, and bsearch's host half
    // never writes at all.
};

// Returns the classification rule for `name`, or NULL if unclassified
// (caller should treat that as FFI_SHADOW_DEFAULT).
static const FfiShadowRule *ffi_shadow_classify(const char *name) {
    for (size_t i = 0; i < sizeof(ffi_shadow_rules) / sizeof(ffi_shadow_rules[0]); i++) {
        if (strcmp(ffi_shadow_rules[i].name, name) == 0)
            return &ffi_shadow_rules[i];
    }
    return NULL;
}

// #768: how many bytes are safe to read starting at `p`, or 0 if `p` isn't
// inside any segment the VM knows the extent of. CCCC has no guest/host
// address translation -- a guest pointer already *is* a host pointer (see
// heap_alloc_for_ptr above) -- so this is a plain range check, mirroring
// is_valid_vm_address (src/debugger.c) and type_shadow_bounds's segment
// checks. Checked in likely-hit order: data segment (string literals),
// heap, then stack (a %s/vararg buffer could technically be a local, though
// a *format string* living on the stack is rare).
static size_t fmt_readable_bound(VirtualMachine *vm, const char *p) {
    if (p >= vm->data_seg && p < vm->data_ptr)
        return (size_t)(vm->data_ptr - p);
    if (p >= vm->heap_seg && p < vm->heap_ptr)
        return (size_t)(vm->heap_ptr - p);
    char *stack_lo = (char *)vm->stack_seg;
    char *stack_hi = (char *)vm->stack_seg + vm->poolsize;
    if (p >= stack_lo && p < stack_hi)
        return (size_t)(stack_hi - p);
    return 0;
}

// #768: conservative %n scan -- true if `fmt` may contain a %n conversion,
// or if it can't be scanned with confidence at all (unreadable pointer, no
// NUL within the readable bound, an unrecognised conversion character).
// Only ever returns false when the format string is provably %n-free, which
// is what lets ffi_shadow_backstop treat every non-%n argument as read-only
// without risking a missed write. Deliberately not shared with parse.c's
// validate_format_call -- that operates on compile-time AST nodes for
// -Wformat diagnostics, this operates on a raw guest pointer at runtime.
static bool fmt_may_write_via_percent_n(VirtualMachine *vm, const char *fmt) {
    size_t bound = fmt_readable_bound(vm, fmt);
    if (bound == 0)
        return true; // can't even confirm the pointer is valid: bail conservatively
    for (size_t i = 0; i < bound; i++) {
        char c = fmt[i];
        if (c == '\0')
            return false; // scanned the whole string, no %n found
        if (c != '%')
            continue;
        if (++i >= bound)
            return true; // truncated conversion: bail conservatively
        if (fmt[i] == '%')
            continue; // %% literal
        // Flags
        while (i < bound && strchr("-+ #0'", fmt[i]))
            i++;
        // Width (digits or '*')
        if (i < bound && fmt[i] == '*')
            i++;
        else
            while (i < bound && fmt[i] >= '0' && fmt[i] <= '9')
                i++;
        // Precision
        if (i < bound && fmt[i] == '.') {
            i++;
            if (i < bound && fmt[i] == '*')
                i++;
            else
                while (i < bound && fmt[i] >= '0' && fmt[i] <= '9')
                    i++;
        }
        // Length modifiers
        while (i < bound && strchr("hlLqjzt", fmt[i]))
            i++;
        if (i >= bound)
            return true; // truncated conversion: bail conservatively
        char conv = fmt[i];
        if (conv == 'n')
            return true;
        if (!strchr("diouxXeEfFgGaAcspm", conv))
            return true; // unrecognised conversion: bail conservatively
        // recognised, non-%n conversion: continue scanning
    }
    return true; // ran off the readable bound without a NUL: bail conservatively
}

// #914: resolves the clear extent for a pointer-shaped FFI argument across
// either tracked segment. Heap addresses go through heap_alloc_for_ptr as
// before -- base = the allocation's own base (so an interior pointer still
// clears the bytes before it, unchanged from pre-#914 behavior), len =
// header->size, freed allocations excluded. A data-segment (global)
// address carries no AllocHeader, so there is no exact object extent to
// recover; the only sound bound is the rest of the emitted data segment --
// [ptr, vm->data_ptr), not vm->data_committed, which is a coarse upfront
// reservation (starts at a whole poolsize chunk) many times larger than
// any real global. This is deliberately not extended to heap addresses
// that resolve to no tracked allocation -- those still get no clear, same
// as before #914.
static bool ffi_shadow_clear_extent(VirtualMachine *vm, long long ptr,
                                    void **out_base, size_t *out_len) {
    size_t off;
    AllocHeader *header = heap_alloc_for_ptr(vm, ptr, &off);
    if (header) {
        if (header->freed)
            return false;
        *out_base = (char *)ptr - off;
        *out_len = header->size;
        return true;
    }
    if ((char *)ptr >= vm->data_seg && (char *)ptr < vm->data_ptr) {
        *out_base = (void *)ptr;
        *out_len = (size_t)(vm->data_ptr - (char *)ptr);
        return true;
    }
    return false;
}

// #768/#751: shared FFI shadow backstop, called from both op_CALLF_fn and
// op_CALLN_fn (the latter previously had no backstop at all -- an
// unclassified/unshimmed host function reached through a function pointer
// or dlsym'd symbol could leave a stale shadow stamp with no clear at all).
// See ffi_shadow_classify above for the per-name classification this drives.
static void ffi_shadow_backstop(VirtualMachine *vm, const char *name,
                                const long long *args, int actual_nargs,
                                uint64_t double_arg_mask, uint64_t float_arg_mask) {
    if (!(vm->flags & CCCC_TYPE_CHECKS) || !(vm->heap_shadow.pages || vm->data_shadow.pages))
        return;

    const FfiShadowRule *rule = ffi_shadow_classify(name);
    FfiShadowClass cls = rule ? rule->class_ : FFI_SHADOW_DEFAULT;

    if (cls == FFI_SHADOW_PRINTF) {
        const char *fmt = (rule->fmt_arg >= 0 && rule->fmt_arg < actual_nargs)
                               ? (const char *)args[rule->fmt_arg] : NULL;
        if (!fmt || fmt_may_write_via_percent_n(vm, fmt))
            cls = FFI_SHADOW_DEFAULT; // can't prove %n-free: today's whole-allocation clear
    }

    if (cls == FFI_SHADOW_HANDLED)
        return;

    for (int i = 0; i < actual_nargs && i < 64; i++) {
        if ((float_arg_mask | double_arg_mask) & (1ULL << i))
            continue; // not a pointer-shaped argument

        if (cls == FFI_SHADOW_READONLY)
            continue; // never writes: no clear needed for any arg

        if ((cls == FFI_SHADOW_BOUNDED || cls == FFI_SHADOW_PRINTF) && i == rule->out_arg) {
            // Narrow the clear to the statically-known extent this call
            // writes through this specific argument (or fall through to the
            // default whole-allocation clear below, for a PRINTF rule with
            // no len_arg -- e.g. sprintf, whose write extent isn't
            // statically bounded). Every *other* pointer-shaped argument
            // (checked on the next loop iteration) still gets the default
            // whole-allocation clear -- narrowing is per-argument.
            if (rule->len_arg >= 0 || rule->fixed_len > 0) {
                size_t len = rule->fixed_len;
                if (rule->len_arg >= 0 && rule->len_arg < actual_nargs) {
                    long long l = args[rule->len_arg];
                    len = (l > 0) ? (size_t)l : 0;
                    if (rule->len_arg2 >= 0 && rule->len_arg2 < actual_nargs) {
                        long long l2 = args[rule->len_arg2]; // e.g. fread's nmemb
                        if (l2 <= 0 || len == 0) {
                            len = 0;
                        } else if (len > SIZE_MAX / (size_t)l2) {
                            len = SIZE_MAX; // overflow: clamp; the allocation-size clamp below still applies
                        } else {
                            len *= (size_t)l2;
                        }
                    }
                }
                void *base;
                size_t obj_len;
                if (ffi_shadow_clear_extent(vm, args[i], &base, &obj_len)) {
                    size_t off = (size_t)((char *)args[i] - (char *)base);
                    size_t remaining = obj_len - off;
                    if (len > remaining)
                        len = remaining; // a short/clamped read cleared a superset: still sound
                    type_shadow_clear(vm, (void *)args[i], len);
                }
                continue;
            }
            // else: fall through to the default whole-allocation clear.
        }

        if (cls == FFI_SHADOW_PRINTF)
            continue; // a %n-free printf-family call reads every argument
                      // other than its designated out_arg (if any) -- e.g.
                      // %d/%s/%p values -- never writes through them.

        // Default: unclassified name, or a BOUNDED call's non-designated
        // pointer argument -- whole-object clear, heap or global
        // (ffi_shadow_clear_extent, #914).
        void *base;
        size_t obj_len;
        if (ffi_shadow_clear_extent(vm, args[i], &base, &obj_len))
            type_shadow_clear(vm, base, obj_len);
    }
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

    // FFI-clear backstop (#653), classified per host function (#751/#768):
    // guest memcpy/memmove get shadow-aware shims (cccc_shim_memcpy/
    // cccc_shim_memmove in src/stdlib/string.c) that propagate effective
    // type from src to dst; every other host function may write heap bytes
    // with no VM hook at all (fread, read, recv, scanf, sprintf, any
    // third-party FFI library, ...), so this backstop clears shadow state
    // before the call runs rather than after -- see ffi_shadow_classify
    // above for how much of a given allocation gets cleared. This is what
    // makes byte-granular CHKT3 sound against host writes the VM can't
    // observe: a real type-confusion bug that happens to route through an
    // unclassified/unshimmed host function is missed (accepted for
    // unclassified names, tracked as a follow-up), but no host write can
    // ever leave a stale stamp that later false-positives.
    ffi_shadow_backstop(vm, ff->name, args, actual_nargs, double_arg_mask, float_arg_mask);

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
    // return_buffer_pool entries live in data_seg and are reused by every
    // struct-returning call (#752): without clearing, a buffer would carry
    // a stale stamp from whichever struct type was returned through it
    // last, and false-positive against a later call that returns a
    // different type through the same rotating slot. A freshly handed-out
    // return buffer has no effective type, same as any other fresh
    // allocation.
    type_shadow_clear(vm, vm->compiler.return_buffer_pool[idx],
                       (size_t)vm->compiler.return_buffer_size);
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

    /* #787: POSIX signal() implies an empty mask and no flags -- without
       this, a signal() call after a sigaction(SA_SIGINFO|SA_RESETHAND, ...)
       on the same slot would inherit stale flags (delivering a 1-arg
       handler with 3-arg registers set, or resetting on first delivery). */
    slot->sa_mask  = 0;
    slot->sa_flags = 0;

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

    /* #787: raise() of a currently-blocked signal generates it (marks it
       pending) rather than delivering it synchronously -- POSIX semantics,
       matching the dispatch loop's pending-signal poll. Delivered once the
       signal is unblocked (sa_mask/SA_NODEFER expire on handler return). */
    if (vm->sig_blocked & (1u << (unsigned)(sig - 1))) {
        _cccc_pending[sig] = 1;
        _cccc_any_pending = 1;
        vm->regs[REG_A0] = 0;
        return 0;
    }

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
        int flags = cccc_signal_prepare_delivery(vm, sig, slot, /*async=*/false);
        if (flags & SA_SIGINFO) {
            /* #745: raise() never goes through the host signal mechanism,
               so synthesize real POSIX raise() semantics instead of real
               captured data (si_code = SI_USER, si_pid/si_uid = self). */
            vm->regs[REG_A1] = cccc_guest_siginfo_for(sig, 1);
            vm->regs[REG_A2] = 0; /* ucontext: not modelled */
        }
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
