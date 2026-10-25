// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: unchecked end does not match '#pragma cccc checked begin'
// The innermost begin is the one an end is matched against.

#pragma cccc checked begin
#pragma cccc checked begin
#pragma cccc unchecked end
#pragma cccc checked end

int main(void) {
    return 42;
}
