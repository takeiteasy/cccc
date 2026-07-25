// CCCC_FLAGS: --type-checks
// Ticket #653: memcpy between two heap allocations must propagate the
// source's per-member effective type onto the destination (the shadow-
// aware cccc_shim_memcpy in src/stdlib/string.c, backed by
// cc_type_shadow_copy), so a plain struct copy followed by ordinary member
// reads never false-positives.
#include <stdlib.h>
#include <string.h>

struct S {
    int a;
    float b;
};

int main(void) {
    struct S *src = malloc(sizeof(struct S));
    src->a = 20;
    src->b = 22.0f;

    struct S *dst = malloc(sizeof(struct S));
    memcpy(dst, src, sizeof(struct S));

    int result = dst->a + (int)dst->b;
    free(src);
    free(dst);
    return result;
}
