// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : SUBSX3
// CCCC_EXPECT_STDOUT: : SUBZX3
// CCCC_REJECT_STDOUT: (NOT3|DIV3|BNOT3|NEG3) [^:]*: (SX4|ZX4)
// A logical not is 0 or 1, a divide by a positive constant and a bitwise not
// stay in 32 bits, and a negation and its wrap are one subtraction from zero.
[[gnu::noinline]] int use(int v) {
    return v;
}
[[gnu::noinline]] int not_arg(int x) {
    return use(!x);
}
[[gnu::noinline]] int div4(int *p) {
    return *p / 4;
}
[[gnu::noinline]] long bnot(int *p) {
    return ~*p;
}
[[gnu::noinline]] int neg_s(int x) {
    return -x;
}
[[gnu::noinline]] unsigned neg_z(unsigned x) {
    return -x;
}

int main(void) {
    int x = 6;
    if (not_arg(0) != 1 || not_arg(x) != 0 || div4(&x) != 1 || bnot(&x) != -7 ||
        neg_s(x) != -6 || neg_z(1) != 0xFFFFFFFFu)
        return 1;
    return 42;
}
