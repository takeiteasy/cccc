// Regression test for #780 (follow-up to #775): unsigned int -> float
// conversion in the *other* direction from test_float_to_int_conversion.c.
//
// I2F3/I2F3_F32 (the int->float opcodes) always treat the source register
// as a *signed* 64-bit value: "fregs[rd] = (double)regs[rs]" with regs[rs]
// declared as a signed integer type. For an unsigned 64-bit source whose
// value is >= 2^63, that reinterprets the bit pattern as negative, so
// e.g. "(double)18446744073709551615ULL" (ULLONG_MAX) produced -1.0
// instead of the correct ~1.8446744073709552e19.
//
// Fixed with a dedicated U2F3/U2F3_F32 opcode pair, selected by codegen
// whenever the cast *source* is an unsigned 64-bit integer, plus the
// matching fix in parse.c's eval_double() (which had the identical bug in
// its ND_CAST arm: "return eval(vm, node->lhs)" implicitly sign-converts
// the int64_t result to double). Narrower unsigned types (e.g. unsigned
// int) are unaffected -- they're already zero-extended in the register,
// so plain I2F3 has always been correct for them.
//
// ULLONG_MAX (2^64 - 1) is not exactly representable in either double or
// float, so round-to-nearest-even rounds it up to exactly 2^64
// (18446744073709551616.0) in both precisions -- that's the expected
// value below, not a rounded display like "1.8446744073709552e19".

// Global initializer: exercises the compile-time constant-fold path
// (eval_double()) rather than the runtime U2F3 opcode.
double g_double_from_u64 = (double)18446744073709551615ULL;

int main(void) {
    if (g_double_from_u64 != 18446744073709551616.0)
        return 1;

    // In-function constant expression.
    double local_const = (double)18446744073709551615ULL;
    if (local_const != 18446744073709551616.0)
        return 2;

    // Runtime path (U2F3): ULLONG_MAX must convert to its correct positive
    // double value, not -1.0.
    volatile unsigned long long umax = 18446744073709551615ULL;
    double                      d    = (double)umax;
    if (d != 18446744073709551616.0)
        return 3;
    if (d < 0)
        return 4; // the historical bug: sign-reinterpreted as -1.0

    // 2^63 is the smallest value where the signed/unsigned interpretations
    // diverge -- exactly the boundary the old I2F3-based lowering got
    // wrong.
    volatile unsigned long long two_p63 = 9223372036854775808ULL;
    d                                   = (double)two_p63;
    if (d != 9223372036854775808.0)
        return 5;

    // Values below 2^63 are unaffected -- signed and unsigned
    // interpretations agree, sanity-checking that the new path didn't
    // regress the common case.
    volatile unsigned long long small = 42;
    d                                 = (double)small;
    if (d != 42.0)
        return 6;

    // float32 path (U2F3_F32): ULLONG_MAX rounds to 2^64 in float
    // precision too (see header comment).
    volatile unsigned long long umax_f = 18446744073709551615ULL;
    float                       f      = (float)umax_f;
    if ((double)f != 18446744073709551616.0)
        return 7;
    if (f < 0)
        return 8;

    // Narrower unsigned source (unsigned int, zero-extended in the
    // register) must remain unaffected by the new gating -- regression
    // guard that I2F3 is still selected for non-64-bit unsigned sources.
    volatile unsigned int u32 = 4000000000u;
    d                         = (double)u32;
    if (d != 4000000000.0)
        return 9;

    // Signed 64-bit source must remain unaffected too -- regression guard
    // that I2F3 is still selected for signed sources.
    volatile long long neg = -1;
    d                      = (double)neg;
    if (d != -1.0)
        return 10;

    return 42;
}
