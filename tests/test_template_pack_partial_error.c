// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: partial specialization of variadic template 'A' is not supported
@template(Ts...) struct A { Ts... v; };
@template(T) @specialize(T *) struct A { int q; };
int main(void) {
    return 42;
}
