// #832: a decimal constant expression folded at compile time (src/parse.c's
// eval_decimal, running inside the *compiler* process) must never leave the
// guest program observing dirty host FP exception flags at startup. This is
// a standalone, deliberately non-suite test: inside tests/suites/
// test_suite_decimal.c's ~30 tests it would be order-dependent on tests
// that divide or printf a decimal value (both can legitimately set FP
// flags), and would silently false-pass if run after one of those.
//
// static_decimal_x below is 1.0dd/3.0dd -- inexact at _Decimal64's 16
// significant digits, so it *would* raise FE_INEXACT if the fold ever
// touched the real host FP environment instead of discarding flags
// internally (CCCC_DEC_ENV_STATIC, see src/stdlib/decimal.c). The check
// below must be the very first statement in main(), before anything else
// (including printf of a decimal value, or a runtime decimal division) has
// a chance to legitimately set a flag of its own.
//
// This also guards a related, independently-verified pre-existing issue
// this ticket's fix papers over: tokenize.c's convert_pp_number scans every
// floating/decimal literal's extent via a host strtold() call whose value
// is discarded but whose side effect isn't (strtold("1.1", NULL) alone sets
// FE_UNDERFLOW on at least one verified platform) -- see cc_run()'s
// pre-execution fenv reset in src/vm.c.

#include <fenv.h>

#ifdef __STDC_IEC_60559_DFP__
static _Decimal64 static_decimal_x = 1.0dd / 3.0dd;
#endif

int main(void) {
#ifdef __STDC_IEC_60559_DFP__
    if (fetestexcept(FE_ALL_EXCEPT) != 0)
        return 1;
    if (static_decimal_x == 0.dd) // use the variable, avoid unused-warning DCE
        return 2;
#endif
    return 42;
}
