// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: a global checked array is bounds-checked the same as a local one --
// this is also the case --bounds-checks' CHKB structurally cannot catch,
// since a global has no AllocHeader.size to derive a limit from.

int a _Checked[10];

int main(void) {
    volatile int i = 10;
    a[i]           = 1;
    return 42;
}
