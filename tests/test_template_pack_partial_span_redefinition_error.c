// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: redefinition of partial specialization of 'D'
@template(T, U = T, Us...) struct D {
    char kind[1];
};
@template(Ts...)
@specialize(int, Ts...)
struct D {
    char kind[2];
};
@template(U, Us...)
@specialize(int, U, Us...)
struct D {
    char kind[3];
};
int main(void) {
    return 42;
}
