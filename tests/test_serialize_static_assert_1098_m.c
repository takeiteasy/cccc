// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Static_assert\(\(unsigned long\)8 <= sizeof\(struct statfs\), "struct statfs too small"\)
// CCCC_EXPECT_STDOUT: _Static_assert\(\(unsigned long\)8 <= sizeof\(struct statfs\), "struct statfs too small \(block\)"\)
// CCCC_REJECT_STDOUT: int must be 4 bytes
// -m output emits the host-dependent _Static_assert and not the portable one.

#include <sys/mount.h>

// #1098: the file-scope form. Deliberately `>=`, not `==` -- true against
// BOTH CCCC's own guest projection of struct statfs and the real host's
// (much larger) one, so this exercises the RE-EMISSION itself rather than
// a projection-vs-real-layout mismatch (see test_serialize_static_assert_
// 1098.c for that half, and dbg5-style manual verification in the ticket
// for a deliberately-failing `==` case).
_Static_assert(sizeof(struct statfs) >= 8, "struct statfs too small");

int main(void) {
    // Block-scope form, same gate.
    static_assert(sizeof(struct statfs) >= 8,
                  "struct statfs too small (block)");

    // Negative half: an ordinary compile-time-only assert (no from_include
    // type anywhere in its condition) must NOT be re-emitted -- confirms
    // the host-owned-layout gate (expr_has_host_owned_layout(),
    // serialize.c) doesn't fire indiscriminately on every static_assert in
    // a TU that merely includes a header with from_include types in scope.
    _Static_assert(sizeof(int) == 4, "int must be 4 bytes");

    return 42;
}
