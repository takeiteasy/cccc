// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: stray #pragma cccc comptime end without matching begin

#pragma cccc comptime

int comptime_helper(void) { return 1; }

#pragma cccc comptime end

int main(void) {
    return 42;
}
