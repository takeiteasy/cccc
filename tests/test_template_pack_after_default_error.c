// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot have default arguments
@template(T = int, Ts...) struct A { int x; };
int main(void) {
    return 42;
}
