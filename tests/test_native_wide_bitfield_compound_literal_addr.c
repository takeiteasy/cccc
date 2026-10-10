// EXPECT_COMPILE_ERROR
// CCCC_FLAGS: -m
// CCCC_EXPECT_STDERR: address of a member of a compound literal
//
// A block-scope compound literal lowers to a comma chain that the address-of
// rewrite would spell with the member's name, which an opaque-storage
// aggregate doesn't have.
struct WithTag {
    int tag;
    _BitInt(256) f : 100;
};

int main(void) {
    int *p = &((struct WithTag){0}).tag;
    return *p;
}
