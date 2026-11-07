// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'f'

int f(long);
int f(long long);

int main(void) {
    return 42;
}
