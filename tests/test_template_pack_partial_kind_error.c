// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack 'Ms' is a constant pack, but 'A' takes types there
@template(Ts...) struct A {
    Ts... v;
};
@template(int... Ms) @specialize(Ms...) struct A {
    int q;
};
int main(void) {
    return 42;
}
