// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : ORI3 +[0-9]+ 240 0
// CCCC_EXPECT_STDOUT: : XORI3 +[0-9]+ 255 0
// CCCC_EXPECT_STDOUT: : ORI3 +[0-9]+ 1284865837 1481765933
// CCCC_EXPECT_STDOUT: : XORI3 +[0-9]+ 4294967295 0
// CCCC_REJECT_STDOUT: : (OR3|XOR3) 
// An OR or XOR with a constant takes the constant as an immediate, whichever
// side it is on, and keeps C's results for negative and 64-bit constants.
long set_bits(long a) {
    return a | 0xf0;
}

long flip_low(long a) {
    return 0xff ^ a;
}

unsigned long set_wide(unsigned long a) {
    return a | 6364136223846793005UL;
}

unsigned flip_all(unsigned a) {
    return a ^ 0xffffffffu;
}

int main(void) {
    if (set_bits(0x0f) != 0xff || set_bits(-256) != -16)
        return 1;
    if (flip_low(0xf0) != 0x0f || flip_low(-1) != -256)
        return 2;
    if (set_wide(1) != 6364136223846793005UL)
        return 3;
    if (flip_all(1u) != 0xfffffffeu || flip_all(0xffffffffu) != 0)
        return 4;
    return 42;
}
