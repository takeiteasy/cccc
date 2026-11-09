// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: --asm-passthru
// CCCC_EXPECT_STDERR: not addressable
//
// A member of a call result has no storage, so a memory constraint cannot name it.

struct S { struct { int a; int b; } in; };

static struct S make(void) {
    struct S s = {{1, 2}};
    return s;
}

int main(void) {
    __asm__ volatile("" : : "m"(make().in));
    return 42;
}
