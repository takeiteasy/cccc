// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack variable 'Ms' cannot stand for 'D', which has floating type
@template(double D, int... Ms) struct A {
    char kind;
};
@template(int... Ms) @specialize(Ms...) struct A {
    char kind;
};
int main(void) {
    return 42;
}
