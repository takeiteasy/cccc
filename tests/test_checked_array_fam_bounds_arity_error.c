// EXPECT_COMPILE_ERROR
// bounds() takes two arguments or `unknown`.

struct S {
    int n;
    int d _Checked[] [[cccc::bounds(n)]];
};

int main(void) {
    return 42;
}
