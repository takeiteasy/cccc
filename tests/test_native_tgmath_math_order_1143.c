// #1143 regression (the same fix as test_native_bundled_include_1143.c):
// forcing CCCC's own math.h/float.h via an absolute-path #include
// substitution (find_cccc_bundled_header_path(), src/preprocess.c) is only
// safe when nothing else in the TU already activated the real host's own
// <tgmath.h> type-generic macros first. Real <tgmath.h> pulls in the real
// host's own <math.h> internally (sharing that header's own include guard)
// to build macros like `remquo(x, y, &n)`; CCCC's bundled math.h has a
// *different* guard macro name, so a later captured `#include <math.h>`
// forced to CCCC's own copy is not skipped as the natural no-op a
// guard-matching real header would produce -- CCCC's plain `double
// remquo(double, double, int *);` declaration gets corrupted mid-parse by
// tgmath.h's already-active `remquo` macro ("a type specifier is required
// for all declarations"). Confirmed via tests/suites/test_suite_floats.c,
// which includes <tgmath.h> before <math.h> exactly like this file.
//
// Fixed by tracking whether <tgmath.h> was already replayed
// (seen_tgmath_h, src/serialize_program.c) and only substituting CCCC's
// own math.h/float.h when it wasn't -- source order after tgmath.h falls
// back to the ordinary replay (the real host's math.h, already correctly
// active via tgmath.h's own internal include), matching the safe
// pre-#1143 behaviour for that ordering.
#include <tgmath.h>
#include <math.h>

int main(void) {
    double x = 10.0, y = 3.0;
    int    n = 0;
    // remquo is exactly the declaration the corruption bug mangled.
    double r = remquo(x, y, &n);
    if (r != 1.0 || n != 3)
        return 1;
    if (sqrt(4.0) != 2.0)
        return 2;
    return 42;
}
