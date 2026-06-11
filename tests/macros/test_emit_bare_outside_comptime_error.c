// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: #pragma cccc emit requires an active comptime context

#pragma cccc emit

int main(void) {
    return 42;
}
