// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot name an instance of variadic template 'P'
@template(Ts...) struct P { Ts... v; };
@template(T) struct A { T x; };
@template(T) @specialize(P@(T)) struct A { int q; };
int main(void) {
    return 42;
}
