// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: ANDI3
// CCCC_EXPECT_STDOUT: SLTI3
// CCCC_REJECT_STDOUT: LI3[^:]*: (AND3|SLT3|SEQ3|ADD3|SHL3)
// A constant expression operand (not only a literal) is an immediate: no
// register is loaded with it first.
int mask(int x) {
    return x & (1 + 2);
}

int below(int x) {
    return x < -(1 << 4);
}

int same(int x) {
    return x == 3 * 7;
}

int bump(int x) {
    return x + (100 - 1);
}

int main(void) {
    if (mask(7) != 3 || below(-17) != 1 || below(-16) != 0 || same(21) != 1 ||
        bump(1) != 100)
        return 1;
    return 42;
}
