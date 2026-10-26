// EXPECT_COMPILE_ERROR
// A block-scope variable declared with the specifier is rejected too.

int main(void) {
    _Checked int x = 42;
    return x;
}
