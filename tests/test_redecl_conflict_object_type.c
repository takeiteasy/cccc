// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'x'

extern int x;
extern double x;

int main(void) {
    return 42;
}
