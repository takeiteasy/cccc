// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: __CCCC_CTOR_P\(101\) void early\(void\)
// CCCC_EXPECT_STDOUT: __CCCC_CTOR void plain\(void\)
// CCCC_EXPECT_STDOUT: __CCCC_DTOR_P\(200\) void fini\(void\)
// CCCC_EXPECT_STDOUT: __attribute__\(\(constructor\)\) static void __cccc_init_dispatch
// CCCC_EXPECT_STDOUT: __attribute__\(\(destructor\)\) static void __cccc_fini_dispatch
// CCCC_REJECT_STDOUT: __attribute__\(\(constructor\(101
// A program with a constructor priority spells its constructors and destructors
// through macros and dispatches them where the host cc has no priorities.
int seen;

__attribute__((constructor)) void plain(void) {
    seen |= 2;
}
__attribute__((constructor(101))) void early(void) {
    seen |= 1;
}
__attribute__((destructor(200))) void fini(void) {
    seen = 0;
}

int main(void) {
    return seen == 3 ? 42 : 1;
}
