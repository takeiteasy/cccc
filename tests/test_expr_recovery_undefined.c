// EXPECT_COMPILE_ERROR
// Test expression recovery for undefined variables
// This should report 3 separate undefined variable errors

int main() {
    int x = undefined_var1 + 5;
    int y = undefined_var2 * 2;
    int z = undefined_var3 - 10;
    return 42;
}
