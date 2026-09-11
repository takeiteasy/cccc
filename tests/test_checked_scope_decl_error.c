// EXPECT_COMPILE_ERROR
// Checked regions (#485), v1 ban list item 1: an unchecked pointer local
// declared inside a [[cccc::checked]] function body is a compile error --
// always on, deliberately not passed --checked-pointers here.

[[cccc::checked]]
void f(void) {
    int *p = 0;
    (void)p;
}

int main(void) {
    f();
    return 42;
}
