// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --output-compiler=gcc
// CCCC_EXPECT_STDERR: error: blocks are not supported by gcc \[-Wtarget-compiler\]
void run(void (^cb)(void));
int main(void) {
    return 42;
}
