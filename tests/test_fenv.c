// Regression test for #771: include/fenv.h hardcoded one platform's FE_*
// bit values, which did not match the real host <fenv.h> that
// src/stdlib/fenv.c's wrap_fe*() functions were compiled against, so every
// guest call into <fenv.h> silently passed the wrong bit pattern through to
// the real host function. FE_* is now injected from the actual host
// <fenv.h> at compile time (see init_fenv_macros() in src/preprocess.c),
// so it is correct on whatever platform this test runs on.
#include <fenv.h>
#include <float.h>

int main(void) {
    // Round-trip fesetround()/fegetround() for every rounding mode.
    if (fesetround(FE_UPWARD) != 0) return 1;
    if (fegetround() != FE_UPWARD) return 2;
    if (FLT_ROUNDS != 2) return 3; // toward +inf

    if (fesetround(FE_DOWNWARD) != 0) return 4;
    if (fegetround() != FE_DOWNWARD) return 5;
    if (FLT_ROUNDS != 3) return 6; // toward -inf

    if (fesetround(FE_TOWARDZERO) != 0) return 7;
    if (fegetround() != FE_TOWARDZERO) return 8;
    if (FLT_ROUNDS != 0) return 9; // toward zero

    if (fesetround(FE_TONEAREST) != 0) return 10;
    if (fegetround() != FE_TONEAREST) return 11;
    if (FLT_ROUNDS != 1) return 12; // to-nearest

    // Exception flags: raise/test/clear must actually observe host state.
    feclearexcept(FE_ALL_EXCEPT);
    if (fetestexcept(FE_ALL_EXCEPT) != 0) return 13;

    feraiseexcept(FE_INEXACT);
    if (!(fetestexcept(FE_INEXACT))) return 14;
    if (!(fetestexcept(FE_ALL_EXCEPT) & FE_INEXACT)) return 15;

    feclearexcept(FE_INEXACT);
    if (fetestexcept(FE_INEXACT) != 0) return 16;

    feraiseexcept(FE_DIVBYZERO);
    if (!(fetestexcept(FE_DIVBYZERO))) return 17;
    feclearexcept(FE_ALL_EXCEPT);

    // fegetexceptflag()/fesetexceptflag() round-trip through fexcept_t,
    // which is now correctly sized (was hardcoded `unsigned int`; is 2
    // bytes on macOS/arm64).
    feraiseexcept(FE_OVERFLOW);
    fexcept_t saved;
    if (fegetexceptflag(&saved, FE_OVERFLOW) != 0) return 18;
    feclearexcept(FE_ALL_EXCEPT);
    if (fetestexcept(FE_OVERFLOW) != 0) return 19;
    if (fesetexceptflag(&saved, FE_OVERFLOW) != 0) return 20;
    if (!(fetestexcept(FE_OVERFLOW))) return 21;
    feclearexcept(FE_ALL_EXCEPT);

    // fesetenv(FE_DFL_ENV): the guest's FE_DFL_ENV is a -1 sentinel, not a
    // real host pointer; wrap_fesetenv() must substitute the real host
    // FE_DFL_ENV rather than dereferencing -1.
    if (fesetenv(FE_DFL_ENV) != 0) return 22;
    if (feupdateenv(FE_DFL_ENV) != 0) return 23;

    // fegetenv()/fesetenv() round-trip through fenv_t.
    fenv_t env;
    if (fegetenv(&env) != 0) return 24;
    if (fesetenv(&env) != 0) return 25;

    return 42;
}
