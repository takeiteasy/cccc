// EXPECT_COMPILE_ERROR
// #pragma comptime helpers are not callable from runtime program code.

#pragma comptime
int comptime_helper(void) {
    return 42;
}

int main(void) {
    return comptime_helper();
}
