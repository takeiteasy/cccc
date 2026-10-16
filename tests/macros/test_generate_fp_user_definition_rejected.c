// EXPECT_COMPILE_ERROR
// A user-written sum_int is not silently replaced by the generator.
// CCCC_EXPECT_STDERR: redefinition of sum_int

int sum_int(int *a, int n) {
    return n;
}

[[cccc::comptime]]
void setup(void) {
    GenerateSum(GetType("int"));
}

setup();

int main(void) {
    return 42;
}
