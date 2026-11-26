// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDERR: error: 'address_space' is not supported by gcc and is dropped \[-Wtarget-compiler\]
int __attribute__((address_space(1))) a;
int main(void) {
    return 42;
}
