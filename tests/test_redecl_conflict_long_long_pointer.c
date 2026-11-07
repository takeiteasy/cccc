// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'g'

int g(long *);
int g(long long *);

int main(void) {
    return 42;
}
