// CCCC_NATIVE_SKIP: -c=native rejects VM bytecode options (-d)
// CCCC_FLAGS: --checked-pointers -d
// CCCC_EXPECT_STDOUT: (CHKR .*){1}
// CCCC_REJECT_STDOUT: (CHKR .*){3}
//
// A counted loop holding a macro call keeps its range check even when the
// macro writes nothing; the same loop without one is proven.

[[cccc::comptime]]
Node *nop(Node *unused) { return Quote("(void)0;"); }

int plain(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    int sum           = 0;
    for (int i = 0; i < 5; i++)
        sum += a[i];
    return sum;
}

int with_macro(void) {
    int a _Checked[5] = {1, 2, 3, 4, 5};
    int sum           = 0;
    for (int i = 0; i < 5; i++) {
        nop(0);
        sum += a[i];
    }
    return sum;
}

int main(void) {
    return plain() + with_macro() == 30 ? 42 : 1;
}
