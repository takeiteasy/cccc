// CCCC_FLAGS: -Wmaybe-nonnull
// CCCC_REJECT_STDERR: nonnull
// #688's key constraint: an unknown/external callee (no visible body in
// this translation unit) must NOT be assumed may-return-null -- otherwise
// every f(g()) call through an unannotated pointer-returning function
// would warn. malloc() is only declared (via stdlib.h), never defined in
// this TU, so its summary stays "no evidence" and the result reads as
// NN_UNKNOWN, not NN_MAYBE, even though malloc can genuinely return NULL.
#include <stdlib.h>
void handle(void *p) __attribute__((nonnull));
void handle(void *p) {}
int main(void) {
    void *p = malloc(16);
    handle(p);
    free(p);
    return 42;
}
