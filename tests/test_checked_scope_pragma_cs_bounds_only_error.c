// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: _Bounds_only is not supported
// The Checked C bounds-only variant is not supported.

#pragma CHECKED_SCOPE _Bounds_only

int main(void) {
    return 42;
}
