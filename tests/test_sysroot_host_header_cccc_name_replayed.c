// CCCC_FLAGS: --sysroot tests/fixtures/fake_sysroot -m
// CCCC_EXPECT_STDOUT: #include <foo.h>
// CCCC_REJECT_STDOUT: SYSROOT_AVAILABILITY
// A sysroot header named like a cccc-supplied header (Availability.h) is a
// host header: its includer is replayed, not flattened.
#include <foo.h>

int main(void) {
    return 42;
}
