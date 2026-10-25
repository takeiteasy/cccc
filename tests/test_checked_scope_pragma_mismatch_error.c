// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: checked end does not match '#pragma cccc unchecked begin'
// A #pragma cccc end must name the same keyword as its begin.

#pragma cccc unchecked begin
#pragma cccc checked end

int main(void) {
    return 42;
}
