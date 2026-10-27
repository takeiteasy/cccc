// EXPECT_COMPILE_ERROR
// byte_count() belongs to a flexible array member, not a complete checked array.

struct S {
    int n;
    int d _Checked[4] [[cccc::byte_count(n)]];
};

int main(void) {
    return 42;
}
