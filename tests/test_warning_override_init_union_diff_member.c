// CCCC_FLAGS: -Woverride-init
// CCCC_EXPECT_STDERR: initializer overrides prior initialization of
// 'i'.*\[-Woverride-init\] #961: union members alias, so a designator to a
// *different* member overrides whichever member was previously live, not just a
// re-designator of the same member -- gcc/clang both warn on this shape too.
// cccc had no union override check at all before this fix. The message names
// the previously-live member ('i'), the one actually being overridden, not the
// incoming one ('f').

struct S {
    union {
        int   i;
        float f;
    };
    int tag;
};

int main(void) {
    struct S s = {.i = 1, .f = 2, .tag = 3};
    (void)s;
    return 42;
}
