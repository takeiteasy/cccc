// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: body_error\.c:[0-9]+: +return a \+ missing
// CCCC_EXPECT_STDERR: in instantiation of template 'f'
@template(T) T f(T a) {
    return a;
}
@specialize(int) int f(int a) {
    return a + missing;
}
int main(void) {
    return @inst(int) f(1);
}
