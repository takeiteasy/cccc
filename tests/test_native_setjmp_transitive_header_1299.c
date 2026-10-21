// setjmp reached through a transitive header round-trips under -c=native.
#include "fixtures/setjmp_transitive_1299_smoke.h"
static jmp_buf env;
static void unwind(void) {
    longjmp(env, 42);
}
int main(void) {
    int rv = setjmp(env);
    if (rv == 0) {
        unwind();
        return 1;
    }
    return rv;
}
