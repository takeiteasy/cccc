// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: `_Checked[N]` gives a local array's declared extent the same
// enforceable bounds a checked pointer's count(n) has -- CHKR traps a stack
// array access one past its declared end exactly like it would for
// `int * [[cccc::array, cccc::count(10)]]`.

int main(void) {
    int a        _Checked[10];
    volatile int i = 10;
    a[i]           = 1;
    return 42;
}
