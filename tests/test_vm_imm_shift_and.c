// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : SHLI3 +[0-9]+ 61 0
// CCCC_EXPECT_STDOUT: : SHRI3 +[0-9]+ 4 0
// CCCC_EXPECT_STDOUT: : USHRI3 +[0-9]+ 3 0
// CCCC_EXPECT_STDOUT: : ANDI3 +[0-9]+ 255 0
// CCCC_EXPECT_STDOUT: : MULI3 +[0-9]+ 1284865837 1481765933
// CCCC_EXPECT_STDOUT: : MULADDI3 +[0-9]+ 1284865837 1481765933
// CCCC_REJECT_STDOUT: : (SHL3|SHR3|USHR3|AND3) 
// CCCC_REJECT_STDOUT: : LI3 +[0-9]+ 1284865837
// A shift or AND by a constant, and a multiply by a 64-bit constant, take the
// constant as an immediate. A signed right shift stays arithmetic and an
// unsigned one logical.
unsigned long rotate(unsigned long a) {
    return (a >> 3) | (a << 61);
}

unsigned long lcg(unsigned long m) {
    return m * 6364136223846793005UL;
}

long scale(long s, unsigned long m) {
    return ((s >> 4) & 0xff) + m * 6364136223846793005UL;
}

// A variable whose address is taken is updated in its frame slot.
int framed(int n) {
    int c  = n;
    int *p = &c;
    c <<= 3;
    c &= 0xff;
    return c + *p;
}

int main(void) {
    return rotate(8) == 1 && lcg(0) == 0 && scale(-32, 0) == 0xfe && framed(33) == 16 ? 42 : 1;
}
