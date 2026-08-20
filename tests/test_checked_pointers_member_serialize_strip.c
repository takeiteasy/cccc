// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: struct S \{\s*int n;\s*int \*p;\s*\};
// CCCC_REJECT_STDOUT: cccc::
//
// #921's struct-member equivalent of test_checked_pointers_serialize_strip.c
// -- a struct member's checked-pointer attributes (Member.checked_kind, via
// mem->ty) must strip identically to a variable's under -m/-c=native, same
// ABI-transparency guarantee (#482/#488). The [[cccc::array, cccc::count]]
// attributes on `p` must not survive into -m's emitted C, and the plain
// `int *p` member must.

struct S {
    int n;
    int *[[cccc::array, cccc::count(n)]] p;
};

int main(void) {
    struct S s = {3, (int[3]){1, 2, 3}};
    return s.p[0] + s.p[1] + s.p[2] + 39;
}
