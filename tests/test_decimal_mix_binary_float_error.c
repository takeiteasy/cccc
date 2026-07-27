// EXPECT_COMPILE_ERROR
// C23 (#402): mixing a _Decimal type with a standard binary floating type in
// an operation is a constraint violation, not an implicit conversion -- there
// is no common representation to convert through (unlike int<->float). An
// explicit cast is required. Runs identically in both build configurations:
// the TypeKind distinction and usual_arith_conv's rejection are unconditional.

int main(void) {
    _Decimal64 x;
    double y;
    _Decimal64 z = x + y;
    return (int)z;
}
