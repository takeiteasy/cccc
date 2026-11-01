// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : USHRI3 +[0-9]+ 1 0
// CCCC_REJECT_STDOUT: : (ADDI3|ORI3|XORI3|SHLI3|SHRI3|USHRI3) +[0-9]+ 0 0$
// CCCC_REJECT_STDOUT: : ANDI3 +[0-9]+ 4294967295 4294967295$
// CCCC_REJECT_STDOUT: : MULI3 +[0-9]+ [01] 0$
// An immediate op that leaves its operand unchanged, or always gives 0, is a
// move. Values arrive through noinline parameters so nothing is folded.
struct bits {
    unsigned lo : 5;
    unsigned hi : 3;
};

[[gnu::noinline]] unsigned long shr0(unsigned long x) {
    return x >> 0;
}
[[gnu::noinline]] unsigned long shr1(unsigned long x) {
    return x >> 1;
}
[[gnu::noinline]] long sar0(long x) {
    return x >> 0;
}
[[gnu::noinline]] long shl0(long x) {
    return x << 0;
}
[[gnu::noinline]] long add0(long x) {
    return x + 0;
}
[[gnu::noinline]] long or0(long x) {
    return x | 0;
}
[[gnu::noinline]] long xor0(long x) {
    return x ^ 0;
}
[[gnu::noinline]] long and_all(long x) {
    return x & -1;
}
[[gnu::noinline]] long and0(long x) {
    return x & 0;
}
[[gnu::noinline]] long mul1(long x) {
    return x * 1;
}
[[gnu::noinline]] long mul0(long x) {
    return x * 0;
}
[[gnu::noinline]] unsigned low_field(struct bits *b) {
    return b->lo;
}

int main(void) {
    struct bits b = {.lo = 21, .hi = 5};
    if (shr1(2) != 1 || shr0(0x8000000000000001ul) != 0x8000000000000001ul ||
        sar0(-7) != -7 || shl0(-7) != -7 || add0(-7) != -7 || or0(-7) != -7 ||
        xor0(-7) != -7)
        return 1;
    if (and_all(-7) != -7 || and0(-7) != 0 || mul1(-7) != -7 || mul0(-7) != 0)
        return 2;
    if (low_field(&b) != 21)
        return 3;
    return 42;
}
