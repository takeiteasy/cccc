// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A constant index one past a checked array's extent keeps its range check.

int main(void) {
    int a _Checked[5];
    a[5] = 1;
    return 42;
}
