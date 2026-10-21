// A header reached through two user headers round-trips under -c=native.
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
