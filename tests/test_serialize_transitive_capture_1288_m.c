// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: #include "fixtures/transitive_capture_1288_a\.h"
// CCCC_REJECT_STDOUT: \(memset\)\(
// CCCC_REJECT_STDOUT: \(memcpy\)\(
// CCCC_REJECT_STDOUT: \(strlen\)\(
// -m output replays the first header and no bodiless libc fallback prototypes.
#include "fixtures/transitive_capture_1288_a.h"
static volatile int g_never_1288 = 0;
int main(void) {
    char buf[8];
    memset(buf, 0, sizeof(buf));
    if (g_never_1288) {
        memcpy(buf, buf, 0);
        (void)strlen(buf);
    }
    return buf[0] == 0 ? 42 : 1;
}
