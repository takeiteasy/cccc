// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_REJECT_STDOUT: CHKR
// CCCC_EXPECT_STDOUT: LEV3
//
// A loop variable proven to stay inside a checked array's extent needs no
// range check on the accesses it indexes.

struct S {
    int m _Checked[8];
};

int g _Checked[4];

int main(void) {
    int a    _Checked[5]    = {1, 2, 3, 4, 5};
    char s   _Nt_checked[4] = "abc";
    int grid _Checked[3][5];
    struct S st  = {0};
    int      sum = 0;

    for (int i = 0; i < 5; i++)
        sum += a[i];
    for (int i = 0; i < 5; i++)
        a[i] += 1;
    for (int i = 0; i < 5; ++i)
        a[i]++;
    for (int i = 0; i < 4; i++)
        g[i] = i;
    for (int i = 0; i < 8; i++)
        st.m[i] = i;
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 5; j++)
            grid[i][j] = i + j;
    for (int i = 0; i < 3; i++)
        sum += s[i] == 'a' + i;
    for (int i = 0; i < 4; i++)
        sum += a[i + 1];
    for (unsigned long u = 1; u <= 4; u += 1)
        sum += a[u];
    return sum == 62 ? 42 : 1;
}
