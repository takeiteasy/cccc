// A bundled pointer typedef alias round-trips under -c=native.
#include <xlocale.h>
static volatile int g_never_1297c = 0;
int main(void) {
    if (g_never_1297c) {
        locale_t l = duplocale((locale_t)0);
        (void)l;
    }
    return 42;
}
