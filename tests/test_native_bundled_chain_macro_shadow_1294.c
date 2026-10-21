// A bundled function shadowed by a host macro round-trips under -c=native.
#include <assert.h>
static volatile int g_never_1297d = 0;
int main(void) {
    if (g_never_1297d) {
        getchar_unlocked();
    }
    return 42;
}
