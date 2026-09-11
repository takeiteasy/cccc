// EXPECT_COMPILE_ERROR
// Checked regions (#485), v1 ban list item 2 (second half): a cast that
// changes an already-checked pointer's checked kind (array -> single) is
// also a compile error, not just a cast to a wholly unchecked type.

[[cccc::checked]]
void f(void) {
    int *[[cccc::array, cccc::count(1)]] a = 0;
    int *[[cccc::single]] s                = (int *[[cccc::single]])a;
    (void)s;
}

int main(void) {
    f();
    return 42;
}
