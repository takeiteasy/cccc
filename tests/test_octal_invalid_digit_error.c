// EXPECT_COMPILE_ERROR
// Regression test for #776: an invalid digit for the constant's base
// (here, '8' in an octal constant -- the leading 0 selects base 8, and 8
// is not a valid octal digit) used to be silently accepted, parsing as
// the wrong value (0) instead of being rejected. Now a hard compile
// error, matching gcc/clang.
int main(void) {
    int x = 08;
    return x;
}
