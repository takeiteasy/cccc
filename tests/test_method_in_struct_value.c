// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: 'f' is a method
struct S {
    int x;
    int f(struct S *s) {
        return s->x;
    }
};
int main(void) {
    struct S s = {1};
    auto     g = s.f;
    return 0;
}
