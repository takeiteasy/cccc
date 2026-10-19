// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -3 -d
// CCCC_EXPECT_STDOUT: LEA3 +26[0-9]
// CCCC_REJECT_STDOUT: LEA3 +([0-9]{1,2}|1[0-9]{2}|2[0-4][0-9]|25[0-5])[^0-9]
// CCCC_REJECT_STDOUT: MARKP
// CCCC_REJECT_STDOUT: STKTAG
// ++, -- and op= on a local, or on a member of one, only use the address
// transiently: it is neither recorded as escaping nor given a provenance entry.
struct S {
    int x;
};

int main(void) {
    struct S s = {0};
    int      v = 0;
    for (int i = 0; i < 6; i++) {
        v++;
        v += 2;
        s.x += 1;
        s.x--;
    }
    return v == 18 && s.x == 0 ? 42 : 1;
}
