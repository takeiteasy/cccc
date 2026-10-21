// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include "fixtures/transitive_bundled_capture_1297\.h"
// CCCC_REJECT_STDOUT: nl_catd \(catopen\)
// -m output replays the user header and no bundled prototype it supplies.
#include "fixtures/transitive_bundled_capture_1297.h"
static volatile int g_never_1297 = 0;
int main(void) {
    if (g_never_1297) {
        nl_catd c = catopen("x", 0);
        catclose(c);
    }
    return 42;
}
