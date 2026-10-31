// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: B(LT|GT|LE|GE)I3
// CCCC_REJECT_STDOUT: LI3[^:]*: (S|B)(LT|GT|LE|GE|EQ|NE)3
// A switch's comparisons against case values are immediate compares: the
// constant is not loaded into a register first.
int classify(int c) {
    switch (c) {
        case 'e':
            return 1;
        case 'l':
            return 2;
        case 'W':
            return 3;
        case 'o':
            return 4;
        default:
            return 0;
    }
}

int main(void) {
    if (classify('e') != 1 || classify('l') != 2 || classify('W') != 3 ||
        classify('o') != 4 || classify('x') != 0)
        return 1;
    return 42;
}
