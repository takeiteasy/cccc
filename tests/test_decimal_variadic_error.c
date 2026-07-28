// EXPECT_COMPILE_ERROR
// C23 (#402, revised by #829): a _Decimal value through the *variadic tail*
// of a call is now supported (#829 wired it up: passed by pointer to a
// caller-frame scratch copy, see gen_decimal_arg_ptr in src/codegen.c and
// docs/VM.md's Decimal Floating-Point section) -- that's what this test used
// to check was rejected. What's still rejected is a decimal value as a
// *fixed* parameter through a native FFI call: libffi has no decimal
// ffi_type, so there's no sound by-value marshalling convention for it yet
// (tracked as #830). `abs` is registered as an ordinary non-variadic FFI
// function (src/stdlib/stdlib.c); declaring it here with a mismatched
// _Decimal64 fixed parameter reaches that check. This test requires
// CCCC_HAS_DECIMAL=1 to reach the check (the literal itself needs the
// library); it is a compile-time skip, not a pass, without it.

extern int abs(_Decimal64 x);

int main(void) {
    _Decimal64 x = 1.dd;
    abs(x);
    return 0;
}
