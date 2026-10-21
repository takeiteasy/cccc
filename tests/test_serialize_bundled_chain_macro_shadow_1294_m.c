// CCCC_FLAGS: -m
// CCCC_EXPECT_STDOUT: int \(getchar_unlocked\)\(void\)
// -m output parenthesizes getchar_unlocked's fallback prototype.
#include <assert.h>
static volatile int g_never_1297d = 0;
int main(void) {
    if (g_never_1297d) {
        getchar_unlocked();
    }
    return 42;
}
