// CCCC_FLAGS: --type-checks
// Ticket #653 (was #651, before per-offset tracking landed): CHKT3 now
// stamps and checks the effective type of each struct member's own byte
// range independently, via a byte-granular heap type shadow rather than
// one type_kind per allocation. Two same-size-but-different-type members
// (an int then a float) at different offsets must each keep their own
// effective type -- this is legal C and must never false-positive, since
// each member's bytes are only ever checked against a store through that
// same member.
#include <stdlib.h>

struct S {
    int   a;
    float b;
};

int main(void) {
    struct S *s = malloc(sizeof(struct S));
    s->a        = 20;    // stamps s->a's own byte range (offset 0) as int
    s->b        = 22.0f; // stamps s->b's own byte range (offset 4) as float
    int result  = s->a + (int)s->b;
    free(s);
    return result;
}
