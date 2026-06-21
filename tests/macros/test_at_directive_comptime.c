// CCCC_FLAGS: -m -G
// CCCC_EXPECT_STDOUT: #define CT_EMIT_VALUE 99
#pragma cccc comptime begin
@define CT_EMIT_VALUE 99
#pragma cccc comptime end

int main(void) { return 0; }
