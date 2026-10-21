// A user header that includes a bundled header round-trips under -c=native.
#include "fixtures/transitive_bundled_capture_1297.h"
static volatile int g_never_1297 = 0;
int main(void) {
    if (g_never_1297) {
        nl_catd c = catopen("x", 0);
        catclose(c);
    }
    return 42;
}
