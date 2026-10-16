// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: @generate_getters expects a string literal prefix

@generate_getters(42) struct S {
    int x;
};

int main(void) {
    return 42;
}
