// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: not to this declaration
// __attribute__((checked)) on a variable opens no region, so it is an error
// rather than silently ignored.

__attribute__((checked)) int x;

int main(void) {
    return 42;
}
