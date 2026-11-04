// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A macro in a counted loop's condition may write the index, so the access
// keeps its check.

[[cccc::comptime]]
Node *bump(Node *unused) { return Quote("(i = i + 100)"); }

int main(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    int sum           = 0;
    for (int i = 0; i < 5 && bump(0) >= 0; i++)
        sum += a[i];
    return sum;
}
