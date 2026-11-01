// EXPECT_RUNTIME_ERROR CCCC_FLAGS: --checked-pointers
// A constant index into a member array reached through a NULL pointer keeps
// its range check, so the first member traps instead of faulting.

struct S {
    int m _Checked[8];
};

int main(void) {
    struct S *sp = 0;
    sp->m[0]     = 1;
    return 42;
}
