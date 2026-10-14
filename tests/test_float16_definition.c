// EXPECT_COMPILE_ERROR
// A function cannot be defined with a _Float16 parameter or result.
_Float16 identity(_Float16 x) {
    return x;
}

int main(void) {
    return 42;
}
