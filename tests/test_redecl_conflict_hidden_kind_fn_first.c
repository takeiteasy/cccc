// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: redeclared as a different kind of symbol
void k(void) {
    int q(void);
}
int q;

int main(void) {
    return 42;
}
