// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A read-modify-write that skips its second range check keeps the
// null-terminator guard on the store.

int main(void) {
    char s _Nt_checked[4]  = "abc";
    int    n               = 3;
    s[n]                  += 1;
    return 42;
}
