// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: B(LT|GE)I3
// CCCC_REJECT_STDOUT: CALL[^=]*CALL[^=]*CALL
// A test inside `a < 5` that the range of `a` decides is folded, and the arm
// that cannot run -- with its call -- is gone.
int heavy(int x) {
    return x * 3;
}

int run(int a, int n) {
    int s = 0;
    for (int k = 0; k < n; k++) {
        if (a < 5) {
            if (a < 10)
                s += k;
            else
                s += heavy(a) + heavy(s) + heavy(k);
        }
    }
    return s;
}

int main(void) {
    return run(1, 4) == 6 ? 42 : 1;
}
