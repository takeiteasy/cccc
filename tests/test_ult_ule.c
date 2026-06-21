// CCCC_FLAGS: --optimize=2
// Tests ULT3/ULE3 unsigned 64-bit comparison opcodes (ticket #556).
// The opt level exercises both the runtime VM path (volatile variables)
// and the constant-folding path (literal expressions optimised at compile time).
#include <stdint.h>

int main(void) {
    // -----------------------------------------------------------------------
    // Runtime path — volatile prevents constant-folding; hits ULT3/ULE3 in VM
    // -----------------------------------------------------------------------

    // The original bug: 18446744073709551614ULL interpreted as signed -2,
    // so SLT3 incorrectly returned 1 for (-2 < 0).
    volatile uint64_t large = 18446744073709551614ULL; // ULLONG_MAX - 1
    volatile uint64_t zero  = 0ULL;
    volatile uint64_t one   = 1ULL;
    volatile uint64_t two   = 2ULL;

    // ULT3: large < zero must be false (was bug: true with XOR trick removed)
    if (large < zero)  return 1;
    // ULT3: true case
    if (!(one < two))  return 2;
    // ULT3: equal not strictly less
    if (two < two)     return 3;

    // ULE3: large <= zero must be false
    if (large <= zero) return 4;
    // ULE3: equal case must be true
    if (!(two <= two)) return 5;
    // ULE3: true strict case
    if (!(one <= two)) return 6;

    // Edge: max value less than anything → false
    volatile uint64_t max = 18446744073709551615ULL;
    if (max < large)   return 7;   // max > large, so must be false
    if (!(large < max)) return 8;  // large < max, must be true

    // -----------------------------------------------------------------------
    // Constant-fold path — literal expressions; optimizer evaluates at O2
    // -----------------------------------------------------------------------

    // These are compiled as constant comparisons; at -O2 the optimizer runs
    // eval_binary_const with the new ULT3/ULE3 cases.
    if (18446744073709551614ULL < 0ULL)   return 11;  // must fold to 0
    if (!(1ULL < 2ULL))                   return 12;  // must fold to 1
    if (2ULL < 2ULL)                      return 13;
    if (18446744073709551614ULL <= 0ULL)  return 14;
    if (!(2ULL <= 2ULL))                  return 15;
    if (!(1ULL <= 2ULL))                  return 16;
    if (18446744073709551615ULL < 18446744073709551614ULL) return 17;
    if (!(18446744073709551614ULL < 18446744073709551615ULL)) return 18;

    return 42;
}
