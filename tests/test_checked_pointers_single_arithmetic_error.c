// EXPECT_COMPILE_ERROR
// Checked-pointer arithmetic rule (#770/#482): [[cccc::single]] represents
// exactly one object and rejects all pointer arithmetic, matching Checked
// C's _Ptr<T>. This is a compile-time diagnostic, always on regardless of
// --checked-pointers (deliberately not passed here) -- see new_add()'s
// comment in src/parse.c.

int main(void) {
    int x                   = 5;
    int *[[cccc::single]] p = &x;
    int *[[cccc::single]] q = p + 1;
    return *q;
}
