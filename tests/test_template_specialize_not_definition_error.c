// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a specialization must be a definition
@template(T) T f(T a) {
    return a;
}
@specialize(int) int f(int a);
