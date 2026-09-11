// EXPECT_COMPILE_ERROR
// Checked regions (#485): a #pragma cccc checked begin with no matching end
// is a compile error at end-of-file, mirroring the existing #pragma cccc
// suite/comptime "unclosed begin" diagnostics.

#pragma cccc checked begin

int main(void) {
    return 42;
}
