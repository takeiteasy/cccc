// EXPECT_COMPILE_ERROR
// `_Checked` as a declaration specifier applies only to a function
// definition: on a variable it is rejected rather than silently ignored.

_Checked int x = 3;

int main(void) {
    return 42;
}
