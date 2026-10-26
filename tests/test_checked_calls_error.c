// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -Werror=checked-calls
// CCCC_EXPECT_STDERR: error: checked pointer passed to unchecked parameter 1
// Checked regions: -Werror=checked-calls makes the call-boundary warning fatal.

int raw(int *p) {
    return p[0];
}

[[cccc::checked]]
int f(void) {
    int *[[cccc::array, cccc::count(1)]] a = (int _Checked[1]){42};
    return raw(a);
}

int main(void) {
    return f();
}
