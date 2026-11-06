// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: conflicting types for 'q'
void k(void) {
    int q(int);
}
int q(double);

int main(void) {
    return 42;
}
