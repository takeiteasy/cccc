// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// #487: a struct member declared as a checked array is enforced through the
// same member-root path as a checked pointer member (#921) -- s.m[i] resolves
// against the containing instance, same as a pointer member's count(n) would.

struct S {
    int m _Checked[4];
};

int main(void) {
    struct S     s;
    volatile int i = 4;
    s.m[i]         = 1;
    return 42;
}
