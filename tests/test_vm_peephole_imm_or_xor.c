// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 207 0
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 165 0
// CCCC_REJECT_STDOUT: : (OR3|XOR3|ORI3|XORI3)
// An OR or XOR of a constant held in a register is replaced by its value.
long or_(long n) {
    long x = 0xc0, r = 0;
    for (int k = 0; k < n; k++)
        r = x | 0x0f;
    return r;
}

long xor_(long n) {
    long x = 0xff, r = 0;
    for (int k = 0; k < n; k++)
        r = x ^ 0x5a;
    return r;
}

int main(void) {
    return or_(2) + xor_(2) == 372 ? 42 : 1;
}
