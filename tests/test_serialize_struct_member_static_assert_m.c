// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: _Static_assert\(\(unsigned long\)8 <= sizeof\(struct statfs\), "statfs too small"\)
// CCCC_REJECT_STDOUT: int must be 4 bytes
// -m output keeps a host-dependent struct-body assertion and drops a portable one.
#include <sys/mount.h>

union U {
    char c;
    _Static_assert(sizeof(struct statfs) >= 8, "statfs too small");
    _Static_assert(sizeof(int) == 4, "int must be 4 bytes");
    int  i;
};

int main(void) {
    union U u;
    u.i = 42;
    return u.i;
}
