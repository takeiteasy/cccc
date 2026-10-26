// EXPECT_COMPILE_ERROR
// count() belongs to a flexible array member, not a complete checked array.

struct S {
    int n;
    int d _Checked[4] [[cccc::count(n)]];
};

int main(void) {
    return 42;
}
