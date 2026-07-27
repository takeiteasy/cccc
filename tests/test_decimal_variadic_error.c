// EXPECT_COMPILE_ERROR
// C23 (#402): passing a _Decimal value through a variadic tail argument is
// rejected with a diagnostic in phase 1 -- <stdarg.h>'s va_arg has no
// __builtin_classify_type case for it yet (see the #402 follow-up ticket),
// so silently threading the by-address ABI convention through would let a
// va_arg reader misinterpret the value. This test requires
// CCCC_HAS_DECIMAL=1 to reach the check (the literal itself needs the
// library); it is a compile-time skip, not a pass, without it.

void variadic(int n, ...);

int main(void) {
    _Decimal64 x = 1.dd;
    variadic(1, x);
    return 0;
}
