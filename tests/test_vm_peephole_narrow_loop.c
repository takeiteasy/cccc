// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: B(LT|GE)I3
// CCCC_REJECT_STDOUT: CALL[^=]*CALL
// A test inside `i < 5` that the bounds of the counter decide is folded, and
// the arm that cannot run -- with its call -- is gone.
int heavy(int x) {
    return x * 3;
}

int run(void) {
    int s = 0;
    for (int i = 0; i < 5; i++) {
        if (i < 10)
            s += i;
        else
            s += heavy(i) + heavy(s);
    }
    return s;
}

int main(void) {
    return run() == 10 ? 42 : 1;
}
