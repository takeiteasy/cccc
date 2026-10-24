// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: member size
// A failing static assertion in a struct body is a compile error.
struct S {
    int a;
    _Static_assert(sizeof(int) == 3, "member size");
};
int main(void) { return 42; }
