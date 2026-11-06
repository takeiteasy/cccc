// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --std=c17
// CCCC_EXPECT_STDERR: redeclared as a different kind of symbol
void k(void) {
    extern int q;
}
int q(void);

int main(void) {
    return 42;
}
