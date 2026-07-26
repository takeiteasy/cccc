// #815: duplicate case values in a switch must be a compile error, not
// silently-last-one-wins.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate case value '1'
int printf(const char *, ...);
int main(void) {
    switch (1) {
    case 1: printf("first\n"); break;
    case 1: printf("second\n"); break;
    }
    return 42;
}
