// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: MULADD3
// CCCC_REJECT_STDOUT: MOV3
// An index, a base, a multiply-add operand and a compare operand that are
// promoted locals are read from their registers: no move copies one into a
// temporary first.
long data[8];

long work(long n) {
    long s = 0;
    for (long i = 0; i < n; i++)
        s += data[i] * i;
    return s * 3;
}

int main(void) {
    for (long i = 0; i < 8; i++)
        data[i] = i + 1;
    return work(8) == 3 * 168 ? 42 : 1;
}
