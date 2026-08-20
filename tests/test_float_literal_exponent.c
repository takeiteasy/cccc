// Regression test for #776: bare-exponent floating literals of the form
// INTEGER `e`/`E` INTEGER -- no decimal point, no explicit sign after the
// exponent letter -- were silently misparsed by convert_pp_int()
// (src/tokenize.c) as a garbage integer value instead of a compile error
// or a correctly-computed float.
//
// Root cause: the digit-separator-cleaning loop treats 'e'/'E' (and the
// exponent digits) as ordinary alnum characters and swallows the whole
// exponent into `cleaned`, e.g. "1e10" -> cleaned="1e10". strtoul() then
// only consumes the leading "1", but the old code trusted the *collected*
// length (not where strtoul() actually stopped) as bytes-consumed, so
// "1e10" silently "succeeded" as the integer 1 and reinterpreted its bit
// pattern as a double (printing 4.94066e-324) instead of falling through
// to convert_pp_number's strtold()-based float parse. The same root cause
// broke hex floats ("0x1p3" misparsed the same way).
//
// Fixed with a whole-token prescan for '.', or an exponent letter
// appropriate to the base ('e'/'E' for octal/decimal, 'p'/'P' for hex),
// done *before* any digit-run collection -- so a case like "08.5" (an
// invalid octal digit '8' followed by a decimal point) is recognized as
// a float before the invalid-digit check ever sees the '8'. Genuinely
// invalid digits for a base (not float-shaped) are now a hard compile
// error instead of a silent truncation to a wrong value.
int main(void) {
    // The exact reproductions from the ticket.
    if (1e1 != 10.0)
        return 1;
    if (2e3 != 2000.0)
        return 2;
    if (123e2 != 12300.0)
        return 3;
    if (1E10 != 1e+10)
        return 4;

    // Controls that already worked before the fix -- must keep working.
    if (1e+10 != 1e+10)
        return 5;
    if (1e-3 != 0.001)
        return 6;
    if (1.0e10 != 1e+10)
        return 7;

    // Hex floats: broken by the identical root cause ('p'/'P' + exponent
    // digits swallowed into the integer digit-run the same way 'e' was).
    if (0x1p3 != 8.0)
        return 8;
    if (0x1.8p1 != 3.0)
        return 9;

    // Leading-zero decimal floats: these take the octal `base = 8` code
    // path up front (leading '0'), so the fix must recognize '.'/'e'/'E'
    // as float indicators for base 8 too, not just base 10 -- otherwise
    // valid floats like these would be wrongly rejected as octal
    // constants.
    if (0e5 != 0.0)
        return 10;
    if (012e3 != 12000.0)
        return 11; // digits after '0' read as decimal, not octal
    if (08.5 != 8.5)
        return 12;

    // float suffix on a bare-exponent literal.
    if (1e2f != 100.0f)
        return 13;

    return 42;
}
