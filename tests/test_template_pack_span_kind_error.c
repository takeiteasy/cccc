// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack variable 'Ts' cannot stand for 'N', a constant parameter
@template(T, int N = 1, Ts...) struct A { int x; };
@template(T, Ts...) @specialize(T, Ts...) struct A { int y; };
int main(void) {
    return 42;
}
