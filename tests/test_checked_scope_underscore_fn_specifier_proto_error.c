// EXPECT_COMPILE_ERROR
// A prototype has no body for the region to apply to, same as the attribute
// form.

_Checked int f(void);

int main(void) {
    return 42;
}
