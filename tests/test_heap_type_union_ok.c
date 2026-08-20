// CCCC_FLAGS: --type-checks
// Ticket #653: legal union member punning on a heap allocation must never
// false-positive under byte-granular CHKT3. Writing through one member and
// reading through another (the entire point of a union) stores through
// emit_store_ex/emit_load_ex with vm->compiler.in_union_member_access set,
// which clears rather than stamps the shadow on write and skips the check
// entirely on read.
#include <stdlib.h>

union U {
    int   i;
    float f;
};

int main(void) {
    union U *u = malloc(sizeof(union U));
    u->i       = 42;
    int a      = u->i; // same member: must read back cleanly
    u->f       = 1.0f; // legal punning: write a different member
    float b    = u->f; // read that member back
    u->i       = 42;   // punning back the other way
    int c      = u->i;
    free(u);
    return (a == 42 && b == 1.0f && c == 42) ? 42 : 1;
}
