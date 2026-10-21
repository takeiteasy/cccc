// setjmp/longjmp round-trip under -c=native.
#include <setjmp.h>

struct canary_layout {
    jmp_buf       env;
    unsigned long canary;
};

static struct canary_layout g;

static void unwind(void) {
    longjmp(g.env, 42);
}

int main(void) {
    g.canary = 0xC0FFEE1054UL;
    int rv   = setjmp(g.env);
    if (rv == 0) {
        unwind();
        return 1;
    }
    if (g.canary != 0xC0FFEE1054UL)
        return 2;
    return rv;
}
