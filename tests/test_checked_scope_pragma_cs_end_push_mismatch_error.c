// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: checked end does not match
// checked end cannot close a CHECKED_SCOPE push.

#pragma CHECKED_SCOPE push
#pragma cccc checked end

int main(void) {
    return 42;
}
