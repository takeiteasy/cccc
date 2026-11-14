// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: ambiguous partial specializations of 'A'
@template(T, Ts...) struct A {
    char kind;
};
@template(Ts...)
@specialize(Ts *...)
struct A {
    char every;
};
@template(T, Us...)
@specialize(T *, T *, Us...)
struct A {
    char twice;
};
int main(void) {
    @inst(int *, int *, int *) A a;
    return 42;
}
