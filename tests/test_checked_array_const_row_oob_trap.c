// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A constant row index past a multidimensional checked array's outer extent
// keeps its range check.

int main(void) {
    int g _Checked[3][5];
    g[3][0] = 1;
    return 42;
}
