// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a pack in a pattern is written alone as the last item
@template(Ts...) struct A {
    Ts... v;
};
@template(Ts...) @specialize(Ts..., int) struct A {
    int q;
};
int main(void) {
    return 42;
}
