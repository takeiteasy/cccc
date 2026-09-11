// EXPECT_COMPILE_ERROR
// Checked regions (#485): a #pragma cccc checked end with no matching begin
// is a compile error.

#pragma cccc checked end

int main(void) {
    return 42;
}
