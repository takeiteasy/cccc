// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: CALL[^:]*: SX4
// A call returning long promises nothing about the low 32 bits being the whole
// value, so narrowing it to int keeps the sign extension.
long sum_to(long n) {
    long s = 0;
    for (long i = 1; i <= n; i++)
        s += i;
    return s;
}

int narrow(long n) {
    int p = (int)sum_to(n);
    return p + p;
}

int main(void) {
    return narrow(3) == 12 ? 42 : 1;
}
