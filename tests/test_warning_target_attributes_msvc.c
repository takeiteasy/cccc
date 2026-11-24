// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -E --attr-target=msvc --output-compiler=gcc
// CCCC_EXPECT_STDERR: --output-compiler=gcc conflicts with --attr-target=msvc
int main(void) {
    return 42;
}
