// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: must have an integer type
@template(double... Ds) int f(void) { return 0; }
int g(void) { return f@(1.0)(); }
int main(void) {
    return 42;
}
