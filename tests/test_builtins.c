// Test GNU-style builtins implemented in the parser (ticket #220)

#include <math.h>

int main() {
    // Math constants
    double h = HUGE_VAL;
    float hf = HUGE_VALF;
    double inf = INFINITY;
    float inff = __builtin_inff();
    double nan = NAN;
    float nanf = __builtin_nanf("");

    // Verify HUGE_VAL is positive infinity
    if (!(h > 0)) return 1;
    if (!(inf > 0)) return 2;
    if (!(hf > 0)) return 3;
    if (!(inff > 0)) return 4;

    // Verify NaN
    if (nan == nan) return 5;     // NaN != NaN
    if (nanf == nanf) return 6;

    // Math predicates
    if (!__builtin_isnan(nan)) return 10;
    if (!__builtin_isnan(nanf)) return 11;
    if (__builtin_isnan(1.0)) return 12;

    if (!__builtin_isinf(h)) return 13;
    if (!__builtin_isinf(inf)) return 14;
    if (__builtin_isinf(1.0)) return 15;

    if (!__builtin_isfinite(1.0)) return 16;
    if (__builtin_isfinite(h)) return 17;
    if (__builtin_isfinite(nan)) return 18;

    if (!__builtin_signbit(-1.0)) return 19;
    if (__builtin_signbit(1.0)) return 20;

    // __builtin_expect (ignored hint)
    if (__builtin_expect(42, 0) != 42) return 21;
    if (__builtin_expect(0, 1) != 0) return 22;

    // __builtin_constant_p
    if (!__builtin_constant_p(1 + 2)) return 23;
    int x = 5;
    if (__builtin_constant_p(x)) return 24;

    // __builtin_alloca
    void *p = __builtin_alloca(64);
    if (!p) return 25;

    return 42;
}
