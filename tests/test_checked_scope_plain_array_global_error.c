// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked array global variable
// A plain global array under a pragma region is not allowed.

#pragma cccc checked begin
int g[2];
#pragma cccc checked end

int main(void) {
    return 42;
}
