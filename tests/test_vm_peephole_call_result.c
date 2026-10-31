// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: CALL
// CCCC_REJECT_STDOUT: CALL[^:]*: SX4
// A direct call to a function returning int needs no sign extension of its
// result: the callee already returns it normalised.
int sum_to(int n) {
    int s = 0;
    for (int i = 1; i <= n; i++)
        s += i;
    return s;
}

int twice(int n) {
    int p = sum_to(n);
    return p + p;
}

int main(void) {
    return twice(3) == 12 ? 42 : 1;
}
