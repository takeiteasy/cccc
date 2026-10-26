// CCCC_FLAGS: -Wno-error=checked-regions
// CCCC_EXPECT_STDERR: unchecked array local variable
// Demoted to a warning, a plain array in a region still compiles and runs.

[[cccc::checked]]
int f(void) {
    int a[3] = {42, 2, 3};
    return a[0];
}

int main(void) {
    return f();
}
