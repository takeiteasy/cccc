// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: a partial specialization must be more specific than the primary template
@template(Ts...) struct A { Ts... v; };
@template(Us...) @specialize(Us...) struct A {
    int q;
};
int main(void) {
    return 42;
}
