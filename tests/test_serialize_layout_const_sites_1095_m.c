// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: buf\[sizeof\(struct statfs\)\]
// CCCC_EXPECT_STDOUT: case sizeof\(struct statfs\)
// CCCC_EXPECT_STDOUT: N = sizeof\(struct statfs\)
// CCCC_REJECT_STDOUT: g_init\[sizeof\(struct statfs\)\]
// -m output keeps the host-owned sizes symbolic in those contexts and folds the
// initializer array.

#include <sys/mount.h>

// A tagged enum with a variable actually declared of that type forces its
// own body to be serialized -- a tagless enum used only for its constant's
// VALUE (N below would otherwise never need its type's own definition
// emitted at all) wouldn't exercise serialize_enum_def()'s own
// re-materialization, only the propagated-to-every-USE half.
enum Layout1095 { N = sizeof(struct statfs) };
static enum Layout1095 g_enum_dummy;

static char            g_init[sizeof(struct statfs)] = {1};

int main(void) {
    char buf[sizeof(struct statfs)];
    (void)buf;

    unsigned long n = sizeof(struct statfs);
    switch (n) {
        case sizeof(struct statfs):
            break;
        default:
            return 1;
    }

    if (N != (int)sizeof(struct statfs))
        return 2;

    (void)g_init;
    (void)g_enum_dummy;
    return 42;
}
