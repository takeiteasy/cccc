// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: #pragma cccc emit: blocks cannot be nested

#pragma cccc comptime begin
#pragma cccc emit begin
#pragma cccc emit begin
#pragma cccc emit end
#pragma cccc emit end
#pragma cccc comptime end

int main(void) {
    return 42;
}
