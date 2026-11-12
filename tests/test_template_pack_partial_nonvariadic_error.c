// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: cannot expand pack 'Ts' into template 'Box', which is not variadic
@template(T) struct Box {
    T x;
};
@template(Ts...) @specialize(Ts...) struct Box {
    int q;
};
int main(void) {
    return 42;
}
