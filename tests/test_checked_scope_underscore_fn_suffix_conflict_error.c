// EXPECT_COMPILE_ERROR
// An attribute and a suffix keyword disagreeing on one function definition
// is a compile error.

[[cccc::unchecked]] int f(void) _Checked {
    return 42;
}

int main(void) {
    return f();
}
