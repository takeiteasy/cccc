// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -d
// CCCC_EXPECT_STDOUT: : LDR_H
// CCCC_EXPECT_STDOUT: : ZX2
// CCCC_REJECT_STDOUT: : SX4
// A halfword load is already sign-extended, so widening it needs no extension;
// converting it to unsigned short still does.
short gs[3] = {-1, 2, -3};

[[gnu::noinline]] int add_one(const short *p) {
    return *p + 1;
}
[[gnu::noinline]] long widen(const short *p) {
    return (int)*p;
}
[[gnu::noinline]] unsigned to_ushort(const short *p) {
    return (unsigned short)*p;
}

int main(void) {
    if (add_one(&gs[0]) != 0 || widen(&gs[2]) != -3 ||
        to_ushort(&gs[0]) != 65535u)
        return 1;
    return 42;
}
