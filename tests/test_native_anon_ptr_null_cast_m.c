// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: p\.items = \(\(void \*\)0\)
// CCCC_REJECT_STDOUT: \(struct \{
// A `{0}` initializer's null store to a pointer-to-anonymous-struct member is
// not cast to a fresh anonymous struct type.
#include "fixtures/anon_member_ptr.h"

int main(void) {
    struct Patches p = {0};
    return p.items ? 1 : 42;
}
