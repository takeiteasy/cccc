// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_EXPECT_STDERR: error: '__builtin_reg_class' is not supported by clang \[-Wtarget-compiler\]
int k = __builtin_reg_class(1);
int main(void) {
    return 42;
}
