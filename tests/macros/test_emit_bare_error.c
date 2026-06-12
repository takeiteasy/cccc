// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: expected 'begin' or 'end' after '#pragma cccc emit'

#pragma cccc comptime begin

#pragma cccc emit

#pragma cccc comptime end

int main(void) {
    return 42;
}
