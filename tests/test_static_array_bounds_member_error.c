// EXPECT_RUNTIME_ERROR CCCC_FLAGS: -3
// An out-of-range index into a struct member array, and into the row of a
// two-dimensional array, is reported.
struct S {
    int arr[4];
    int tail;
};

int main(int argc, char **argv) {
    (void)argv;
    struct S s;
    int      m[3][3];
    s.tail      = 7;
    int k       = 4 + argc - 1; // always past the end of arr
    m[0][0]     = 0;
    m[1][k - 2] = 1;            // column past the 3-wide row
    s.arr[k]    = 1;
    return s.tail == 7 ? 42 : 1;
}
