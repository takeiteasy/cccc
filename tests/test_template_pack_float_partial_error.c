// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: pack 'Ns' holds integer constants, but 'f' takes floating ones
@template(double... Ds) int f(void) {
    return 0;
}
@template(int... Ns) @specialize(1.0, Ns...) int f(void) {
    return 1;
}
int main(void) {
    return 42;
}
