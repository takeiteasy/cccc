// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: LI3
// CCCC_REJECT_STDOUT: S(EQ|NE|LT|GT|LE|GE)3
// A switch on a constant compares constants: each comparison is folded into the
// load of its constant.
int main(void) {
    int r = 0;
    switch (10) {
        case 10:
            r = 10;
            break;
        case 11:
            r = 11;
            break;
        default:
            r = 0;
            break;
    }
    return r == 10 ? 42 : 1;
}
