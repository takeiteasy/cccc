// CCCC_FLAGS: -c=generated -o /dev/stdout
// CCCC_EXPECT_STDOUT: #define CT_EMIT_VALUE 99
#pragma cccc comptime begin
@define CT_EMIT_VALUE 99
#pragma cccc comptime end

int main(void) { return 0; }
