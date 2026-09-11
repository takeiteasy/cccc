// EXPECT_COMPILE_ERROR
// Checked regions (#485): an unchecked pointer struct member declared
// inside a checked region is a compile error -- the struct_members() member
// loop (src/parse_types.c) is one of the four declaration-ban call sites.

[[cccc::checked]] void f(void) {
    struct S {
        int  n;
        int *p; // unchecked member -- must be rejected
    };
    struct S s = {0, 0};
    (void)s;
}

int main(void) {
    f();
    return 42;
}
