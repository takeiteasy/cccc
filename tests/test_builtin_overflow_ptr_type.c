// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: __builtin_\*_overflow: third argument must be a pointer
// to a non-const integer type
//
// #964: __builtin_*_overflow's third-argument check used to only require
// TY_PTR, so a `float *` (or `const int *`) third argument was accepted by
// the parser but would produce C the host compiler rejects once the
// serializer emits the call verbatim under -m/-c=native (clang/gcc both
// require a pointer to a non-const integer here). Tightened to match, so
// the mismatch is caught at parse time instead.

int main(void) {
    float r;
    int   a = 1;
    return __builtin_add_overflow(a, a, &r);
}
