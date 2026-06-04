// EXPECT_COMPILE_ERROR
// JCC_EXPECT_STDERR: no such member 'bar'
struct Foo { int x; };
int main(void) {
    struct Foo f;
    return f.bar;
}
