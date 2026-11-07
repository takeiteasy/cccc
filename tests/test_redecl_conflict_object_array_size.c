// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'a'

extern int a[3];
extern int a[5];

int main(void) {
    return 42;
}
