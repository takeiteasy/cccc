/*
 * stdarg.h - Variable Argument Lists
 * CCCC C Compiler - Standard C Library Header
 * 
 * Implements C99/C11 variable argument list support for the CCCC VM.
 * 
 * Stack Layout for Variadic Functions (after ENT3):
 *   Stack Args      (call args 8+, caller's frame)
 *   [arg9]          ← bp[+3]
 *   [arg8]          ← bp[+2]   (first stack-passed arg)
 *   [ret_addr]      ← bp[+1]
 *   [old_bp]        ← bp[+0]   (base pointer points here)
 *   ---- callee frame below ----
 *   [param0]        ← bp[-1]   (spilled from REG_A0)
 *   [param1]        ← bp[-2]   (spilled from REG_A1)
 *   ...
 *   [param7]        ← bp[-8]   (spilled from REG_A7, last register arg)
 *   [locals...]     ← bp[-9] and below
 * 
 * Register-based calling: first 8 args in REG_A0-A7, spilled by ENT3.
 * Args 9+ are pushed to stack before CALL. ENT3 copies fixed stack-passed
 * parameters into callee local slots; va_arg reads the remaining variadic tail
 * from the caller's stack area.
 * 
 * va_list is a struct that tracks position in both regions and knows
 * when to switch from the register-spill area to the stack args area.
 */

#ifndef _STDARG_H
#define _STDARG_H

/* 
 * va_list type: struct tracking position in two memory regions
 * - reg_ptr: current position in spilled register area (bp[-1...-8])
 * - stack_ptr: current position in stack args area (bp[+2...])
 * - reg_count: number of register-spill slots remaining before switching
 */
typedef struct {
    char *reg_ptr;      /* Current position in register spill area */
    char *stack_ptr;    /* Current position in stack overflow area */
    int reg_count;      /* Remaining slots in register spill area */
} va_list;

/*
 * va_start(ap, last) - Initialize va_list
 * @ap:   va_list to initialize
 * @last: name of the last fixed parameter before '...'
 * 
 * Computes:
 * - reg_ptr: address of first variadic arg = &last - 8 (one slot before last)
 * - stack_ptr: address of first stack variadic arg
 * - reg_count: how many varargs fit in remaining register spill slots
 * 
 * Stack geometry: last is at bp[-(param_idx+1)], bp[-8] is last reg slot.
 * reg_count = 8 - (param_idx + 1) = 7 - param_idx
 * 
 * We compute this by measuring distance from last to bp[-8]:
 * bp[-8] = bp - 64, and &last = bp - (param_idx+1)*8
 * Distance in slots = (bp - (param_idx+1)*8) - (bp - 64) / 8 = (64 - (param_idx+1)*8) / 8
 *                   = 8 - param_idx - 1 = 7 - param_idx
 *
 * Simpler: reg_count = (reg_end - &last) / 8 - 1 where reg_end is at a known offset.
 * 
 * The trick: bp can be recovered as ((long long *)&last) + (param_offset / 8)
 * where param_offset = &last's position below bp. But param_offset is encoded
 * in the stack layout.
 *
 * Practical approach: From &last, we walk back to find bp. ENT3 stores old_bp
 * at bp[-0], but we have a simpler method: assume fixed params start at bp[-1].
 * The number of fixed params = (&(bp[-1]) - &last) / 8 + 1.
 *
 * Since we can't easily find bp from &last alone, we use
 * __builtin_frame_address. CCCC compiles this to LEA 0 which gives bp. When the
 * last fixed parameter itself was stack-passed, ENT3 has copied it into a local
 * slot, so the first stack variadic argument is after those stack-passed fixed
 * arguments in the caller's frame.
 */
#define va_start(ap, last) do { \
    long long *__bp = (long long *)__builtin_frame_address(0); \
    (ap).reg_ptr = (char *)((long long *)&(last) - 1); \
    /* Count remaining reg slots: last is at __bp[-(N+1)], reg area ends at __bp[-8] */ \
    /* Remaining = 8 - (N+1) = number of slots from (last-1) down to __bp[-8] */ \
    int __param_slot = (int)(__bp - (long long *)&(last)); \
    /* Under --stack-canaries, ENT3 reserves bp[-1] for the canary and shifts \
     * params one slot lower, inflating this bp-to-&last distance by one. The \
     * relative reg_ptr above is unaffected; only the slot count needs the fix \
     * (#445). __CCCC_STACK_CANARIES__ is 1 when canaries are on, else 0. */ \
    __param_slot -= __CCCC_STACK_CANARIES__; \
    (ap).reg_count = 8 - __param_slot; \
    if ((ap).reg_count < 0) (ap).reg_count = 0; \
    int __stack_fixed = __param_slot > 8 ? __param_slot - 8 : 0; \
    (ap).stack_ptr = (char *)(__bp + 2 + __stack_fixed); \
} while(0)

/*
 * va_arg(ap, type) - Retrieve next argument
 * @ap:   va_list to read from
 * @type: type of the argument to retrieve
 *
 * If reg_count > 0: read from reg_ptr, decrement both reg_ptr and reg_count
 * Else: read from stack_ptr, increment stack_ptr
 *
 * For floating-point types, we cast to double* to get correct IEEE 754 value.
 *
 * The fp vs non-fp split uses __builtin_choose_expr (not a runtime "?:") so
 * that each arm has a single, consistent element type.  A plain conditional
 * would fuse the double and 'type' arms via the usual arithmetic conversions
 * and mistype the result (e.g. typing va_arg(ap,int*) as double), which then
 * breaks a dereference or member access on the result.
 *
 * A GNU vector_size vector (ticket #721) is a third case, checked via
 * __builtin_classify_type(*(type *)0) == 99 (CCCC_VECTOR_TYPE_CLASS, see
 * src/parse.c). The operand is never evaluated -- like sizeof(expr), only
 * its type is inspected -- so the null dereference is compile-time only.
 * Unlike a scalar, a variadic vector argument does NOT live directly in its
 * 8-byte slot: the call site (gen_vector_arg_ptr, #714) always passes it BY
 * POINTER to a caller-frame scratch copy, regardless of the vector's width,
 * so it always occupies exactly one slot no matter how large the vector is
 * (16/32/64 bytes, #722) and never straddles the register/stack boundary.
 * The vector arm therefore reads the slot as a pointer and derefs it, one
 * extra level of indirection versus the scalar arms.
 *
 * _Decimal32/64/128 (#829) shares that same by-pointer arm: a decimal
 * variadic argument is likewise never in its own 8-byte slot, so its
 * discriminant (== 98, CCCC_DECIMAL_TYPE_CLASS) is folded into the same
 * vector-or-decimal check via `||` rather than adding a fourth
 * __builtin_choose_expr level. See gen_decimal_arg_ptr (src/codegen.c) for
 * the call-site half of this convention -- it always copies into a fresh
 * 16-byte scratch slot regardless of the argument's actual 4/8/16-byte
 * width, since the reading side's width comes from the %Hf/%Df/%DDf
 * modifier in a format string the compiler generally can't correlate with
 * the argument's declared type.
 */
#define va_arg(ap, type) \
    __builtin_choose_expr( \
        __builtin_types_compatible_p(type, double) || __builtin_types_compatible_p(type, float), \
        (((ap).reg_count > 0) \
            ? (--((ap).reg_count), (*(double *)(((ap).reg_ptr) -= 8, ((ap).reg_ptr) + 8))) \
            : (*(double *)(((ap).stack_ptr) += 8, ((ap).stack_ptr) - 8))), \
        __builtin_choose_expr( \
            __builtin_classify_type(*(type *)0) == 99 || \
            __builtin_classify_type(*(type *)0) == 98, \
            (((ap).reg_count > 0) \
                ? (--((ap).reg_count), (*(type *)(*(void **)(((ap).reg_ptr) -= 8, ((ap).reg_ptr) + 8)))) \
                : (*(type *)(*(void **)(((ap).stack_ptr) += 8, ((ap).stack_ptr) - 8)))), \
            (((ap).reg_count > 0) \
                ? (--((ap).reg_count), (*(type *)(((ap).reg_ptr) -= 8, ((ap).reg_ptr) + 8))) \
                : (*(type *)(((ap).stack_ptr) += 8, ((ap).stack_ptr) - 8)))))

/*
 * va_end(ap) - Cleanup va_list
 * @ap: va_list to cleanup
 * 
 * No-op in this implementation. Included for C standard compliance.
 */
#define va_end(ap) ((void)0)

/*
 * va_copy(dest, src) - Copy va_list (C99)
 * @dest: destination va_list
 * @src:  source va_list
 *
 * Struct assignment copies all fields.
 */
#define va_copy(dest, src) ((dest) = (src))

/*
 * __builtin_va_* aliases
 *
 * GCC exposes these alternative names for the va_arg family.  They are aliased
 * to the corresponding va_* macros here so that code using either form works
 * identically when <stdarg.h> is included.
 *
 * Note: __builtin_va_list is defined as 'char*' by the preprocessor
 * (preprocess.c) for macOS system-header compatibility and is intentionally
 * NOT the struct va_list used by this header.  These macros therefore require
 * that the ap argument has type 'va_list' (the struct) rather than
 * '__builtin_va_list'.
 */
#define __builtin_va_start(ap, last) va_start(ap, last)
#define __builtin_va_end(ap)         va_end(ap)
#define __builtin_va_copy(d, s)      va_copy(d, s)
#define __builtin_va_arg(ap, type)   va_arg(ap, type)

#endif /* _STDARG_H */
