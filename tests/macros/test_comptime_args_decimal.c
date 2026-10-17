// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDOUT: decimal: .* is outside VM memory
// CCCC_EXPECT_STDERR: compile-time macro 'probe' aborted
[[cccc::comptime]]
Node *probe(void) {
    _Decimal64 *p = (_Decimal64 *)8;
    _Decimal64 v  = *p + *p;
    return __builtin_ast_int_literal((int)v);
}

int main(void) {
    return probe();
}
