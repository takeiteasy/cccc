// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack variable 'Ts' cannot stand for 'N', a constant parameter
@template(int N, Ts...) struct A {
    char kind;
};
@template(Ts...) @specialize(Ts *...) struct A {
    char kind;
};
int main(void) {
    return 42;
}
