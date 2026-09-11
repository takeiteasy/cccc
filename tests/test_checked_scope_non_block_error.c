// EXPECT_COMPILE_ERROR
// Checked regions (#485): '[[cccc::checked]]'/'[[cccc::unchecked]]' before a
// statement that is not a compound statement is a compile error -- the
// attribute must be followed by '{ ... }'.

int main(void) {
    [[cccc::checked]] return 42;
}
