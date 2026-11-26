// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDERR: error: 'overloadable' is not supported by gcc and is dropped \[-Wtarget-compiler\]
__attribute__((overloadable)) int f(int);
int main(void) {
    return 42;
}
