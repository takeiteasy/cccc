// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #if !defined\(__DEC64_MAX__\).*#error "cccc: _Decimal32/64/128 has no native/-m lowering
//
// Requires CCCC_HAS_DECIMAL=1 to reach the check (the literal itself needs
// the library) -- without it, this is a compile-time skip, not a pass, same
// caveat as tests/test_decimal_variadic_error.c.
//
// #1113(a): clang implements no GNU decimal extension at all and rejects
// _Decimal32/64/128 outright ("GNU decimal type extension not supported"),
// but gcc does implement it (confirmed on both macOS gcc-16 and Linux gcc
// 15.2, both predefining __DEC64_MAX__) -- test_suite_decimal.c already
// passes -c=native under CCCC_NATIVE_CC=gcc (NATIVE_SKIP_TESTS_CLANG,
// tools/testing/__init__.py, #1186). So the serializer can't hard-refuse
// _Decimal unconditionally the way __builtin_decimal_to_chars/
// <decimal_math.h> do (serialize_expr.c, serialize_program.c) -- that would
// regress the working gcc configuration. Instead it emits a guarded
// #error preamble (serialize_decimal_native_guard(), serialize_program.c)
// that only fires under a host compiler lacking __DEC64_MAX__ (clang):
// deferred to whichever compiler actually reads the -m output, since -m
// itself doesn't know who that will be. This test only asserts the guard
// text is present in -m output -- it does not itself assert gcc accepts /
// clang rejects the surrounding decimal declarations, which is exercised
// by test_suite_decimal.c's own native round-trip instead.

int main(void) {
    _Decimal64 d = 1.5dd;
    return (int)d == 1 ? 42 : 1;
}
