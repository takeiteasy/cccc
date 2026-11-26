// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=clang
// CCCC_EXPECT_STDERR: error: 'optimize' is not supported by clang and is dropped \[-Wtarget-compiler\]
__attribute__((optimize("O2"))) void k(void);
int main(void) {
    return 42;
}
