// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'p'

extern int *p;
extern const int *p;

int main(void) {
    return 42;
}
