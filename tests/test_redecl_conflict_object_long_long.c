// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'x'

extern long x;
extern long long x;

int main(void) {
    return 42;
}
