// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 1601 0
// CCCC_REJECT_STDOUT: MULADDI3
// CCCC_REJECT_STDOUT: MULI3
// An arithmetic on a constant held in a register is replaced by its value.
long scaled(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x * 8 + 1;
    return r;
}

int main(void) {
    return scaled(2) == 1601 ? 42 : 1;
}
