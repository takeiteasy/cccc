// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'r'

long long r(void);
long r(void);

int main(void) {
    return 42;
}
