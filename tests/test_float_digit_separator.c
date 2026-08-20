// Regression test for #782: a C23 digit separator (single quote) placed
// inside a floating-point literal's mantissa or exponent broke parsing,
// e.g. `1'000e5` (should be 1000e5 = 1.0e8).
//
// Root cause: convert_pp_number() falls through to strtold(tok->loc, ...)
// for anything that isn't an integer constant, but strtold() has no
// knowledge of C23 digit separators -- it simply stops at the first
// single-quote character, so it parsed only the leading digits and left
// the rest of the token unconsumed, which the trailing-length check then
// reported as "invalid numeric constant".
//
// Fixed by stripping digit separators from the float literal's text (the
// same way convert_pp_int already does for integers) into a clean buffer
// before calling strtold() on it, following #776's fix.
int main(void) {
    // Separator in the integer part of the mantissa, before an exponent.
    if (1'000e5 != 1000e5)
        return 1;

    // Separator in the fractional part.
    if (1.0'00 != 1.000)
        return 2;

    // Separator inside the exponent digits themselves.
    if (1e1'0 != 1e10)
        return 3;

    // Separators in integer part, fractional part, and exponent all at once.
    if (1'2.3'4e1'0 != 12.34e10)
        return 4;

    // Hex float with a separator in the mantissa.
    if (0x1'0p3 != 0x10p3)
        return 5;

    // Separator plus an explicit float suffix.
    if (1'0e1f != 100.0f)
        return 6;

    // Controls that already worked before the fix -- must keep working.
    if (1000e5 != 1000e5)
        return 7;
    if (1.000 != 1.000)
        return 8;

    return 42;
}
