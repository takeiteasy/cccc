// EXPECT_COMPILE_ERROR
// Checked regions (#485): [[cccc::checked]]/[[cccc::unchecked]] applies to a
// function's body -- a bodyless declaration has nothing for it to take
// effect over, so it is a compile error rather than a silent no-op.

[[cccc::checked]]
void f(void);

int main(void) {
    return 42;
}
