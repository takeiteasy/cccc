// CCCC_FLAGS: --format-string-checks
// CCCC_EXPECT_STDERR: expected type '_Decimal64'
// CCCC_EXPECT_STDERR: does not accept a decimal length modifier
/*
 * Format string validation for the #829 decimal length modifiers
 * (%Hf/%Df/%DDf). Both calls below use ordinary (non-decimal) arguments, so
 * this test runs identically whether or not the binary was built with
 * CCCC_HAS_DECIMAL=1 -- the format checker itself only inspects declared
 * types, which always exist (see man/COVERAGE.md: "declarations/sizeof/
 * struct layout always work" regardless of the flag).
 *
 * A %Df/%Hf/%DDf argument is passed *by pointer*, unlike every other
 * printf conversion this project's -Wformat already covers (all int-vs-
 * long-vs-double-style mismatches, which just misread a same-category
 * register/slot). Actually calling printf with the wrong argument type
 * here would dereference the mismatched value's bit pattern as a pointer
 * and crash -- the same real UB a %s-given-a-double mismatch already has
 * today, not something specific to decimal. validate_format_call's whole
 * purpose is to catch this at compile time via -Wformat, so the calls
 * below are compiled (and therefore format-checked) but deliberately never
 * executed.
 */

#include "stdio.h"

int main() {
    if (0) {
        // %Df expects a _Decimal64, but a plain double is provided.
        printf("%Df\n", 3.14);

        // A decimal length modifier on a hex-float conversion has no
        // meaning (no GCC/TR 24732 equivalent) and is diagnosed directly,
        // independent of the argument's type.
        printf("%Da\n", 3.14);
    }

    return 42;
}
