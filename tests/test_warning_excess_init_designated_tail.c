// CCCC_FLAGS: -Wexcess-init
// CCCC_EXPECT_STDERR: excess elements in struct initializer.*\[-Wexcess-init\]
//
// A positional initializer past the last designator-reached member is
// excess, matching gcc/clang: `.b = 1` leaves no member for the trailing
// `2` to target.

struct S {
    int a;
    int b;
};

int main(void) {
    struct S s = {.b = 1, 2};
    return s.b + 41;
}
