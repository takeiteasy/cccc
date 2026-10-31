// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 3200 0
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 25 0
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 8 0
// CCCC_EXPECT_STDOUT: LI3 +[0-9]+ 4294967271 4294967295
// CCCC_REJECT_STDOUT: : (SHLI3|SHRI3|USHRI3|ANDI3) +[0-9]+ [0-9]+ 0
// A shift or AND by an immediate on a constant held in a register is replaced
// by its value.
long shl(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x << 4;
    return r;
}

long shr(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x >> 3;
    return r;
}

long sar(long n) {
    long x = -200, r = 0;
    for (int k = 0; k < n; k++)
        r = x >> 3;
    return r;
}

long and_(long n) {
    long x = 200, r = 0;
    for (int k = 0; k < n; k++)
        r = x & 0x3c;
    return r;
}

int main(void) {
    return shl(2) + shr(2) + and_(2) + sar(2) == 3208 ? 42 : 1;
}
