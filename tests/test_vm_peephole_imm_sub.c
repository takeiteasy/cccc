// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: ADDI3
// CCCC_REJECT_STDOUT: LI3[^:]*: SUB3
// A pointer stepped back by a constant after an index is added subtracts an
// immediate: the scaled constant is not loaded into a register first.
int *last3(int *p, int n) {
    return p + n - 3;
}

long long *last1(long long *p, int n) {
    return p + n - 1;
}

int main(void) {
    int       a[8]  = {0, 1, 2, 3, 4, 5, 6, 7};
    long long b[4]  = {10, 11, 12, 13};
    if (*last3(a, 6) != 3 || *last1(b, 4) != 13)
        return 1;
    return 42;
}
