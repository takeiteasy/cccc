// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: -3 -d
// CCCC_EXPECT_STDOUT: LEA3 +26[0-9]
// CCCC_REJECT_STDOUT: LEA3 +([0-9]{1,2}|1[0-9]{2}|2[0-4][0-9]|25[0-5])[^0-9]
// CCCC_REJECT_STDOUT: MARKP
// CCCC_REJECT_STDOUT: STKTAG
// ++, -- and op= on an element of a local array, or of an array member of a
// local struct, only use the element address transiently: it is neither
// recorded as escaping nor given a provenance entry.
struct S {
    int a[4];
};

int main(void) {
    int      arr[4]   = {0};
    int      m[2][2]  = {{0}};
    struct S s        = {{0}};
    for (int i = 0; i < 4; i++) {
        arr[i]++;
        arr[i] += 2;
        m[i & 1][i >> 1]--;
        s.a[i]++;
    }
    return arr[0] == 3 && m[0][0] == -1 && s.a[0] == 1 ? 42 : 1;
}
