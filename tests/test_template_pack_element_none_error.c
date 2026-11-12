// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: the pattern before '...' names no pack variable
@template(Ts...) struct A {
    Ts... v;
};
@template(Ts...) @specialize(int *...) struct A {
    int q;
};
int main(void) {
    return 42;
}
