// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_REJECT_STDOUT: CHKR
// CCCC_EXPECT_STDOUT: LEV3
//
// A constant offset inside a checked array's declared extent emits no range
// check: local, global, member, _Nt_checked and multidimensional arrays.

struct S {
    int m _Checked[8];
};

int g _Checked[4] = {1, 2, 3, 4};

int main(void) {
    int a    _Checked[5]    = {1, 2, 3, 4, 5};
    char s   _Nt_checked[4] = "abc";
    int grid _Checked[3][5];
    struct S st = {0};

    grid[2][4]  = 7;
    st.m[7]     = 3;
    int sum     = a[0] + a[4] + g[3] + grid[2][4] + st.m[7] + (s[2] == 'c');
    return sum == 21 ? 42 : 1;
}
