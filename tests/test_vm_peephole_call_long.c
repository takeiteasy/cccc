// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: CALL[^:]*: SX4
// A call returning long promises nothing about the low 32 bits being the whole
// value, so narrowing it to int keeps the sign extension when the whole
// register is read (a store of the int would not need it).
long sum_to(long n) {
    long s = 0;
    for (long i = 1; i <= n; i++)
        s += i;
    return s;
}

long narrow(long n) {
    return (int)sum_to(n);
}

int main(void) {
    return narrow(3) == 6 && narrow(70000) == -1844932296 ? 42 : 1;
}
