// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : SLT3 +[0-9]+
// CCCC_REJECT_STDOUT: : NOT3
// CCCC_REJECT_STDOUT: : LI3 +[0-9]+ 1 0[^:]*: JMP
// CCCC_REJECT_STDOUT: : LI3 +[0-9]+ 0 0[^:]*: JZ3
// A condition made of &&, || and ! jumps straight to the branch targets: no
// 0/1 is built and tested again, and ! flips the jump instead of a NOT3.
int outside(int x, int lo, int hi) {
    if (x < lo || x > hi)
        return 1;
    return 0;
}

int both(int x, int lo, int hi) {
    int n = 0;
    while (x > lo && x < hi) {
        x++;
        n++;
    }
    return n;
}

int zero(int x) {
    if (!x)
        return 1;
    return 0;
}

int main(void) {
    return outside(3, 1, 2) + both(1, 0, 5) + zero(0) == 7 ? 42 : 1;
}
