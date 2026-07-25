// CCCC_FLAGS: --type-checks
// Ticket #653: legal member punning through an *anonymous* union embedded
// in a struct must not false-positive either. struct_ref's member-lookup
// loop (src/parse.c) synthesizes an intermediate ND_MEMBER node for the
// anonymous union subobject itself (type TY_UNION), so the terminal
// ND_MEMBER for `s->i`/`s->f` still has an immediate lhs whose type is
// TY_UNION -- is_union_member_access (src/codegen.c) sees through the
// anonymous wrapper the same way it does a named union.
#include <stdlib.h>

struct S {
    int tag;
    union {
        int i;
        float f;
    };
};

int main(void) {
    struct S *s = malloc(sizeof(struct S));
    s->tag = 7;      // regular struct member: stamped/checked normally
    s->i = 42;        // anonymous union member: must clear, not stamp
    float g = s->f;    // legal punning through the other anon union member
    (void)g;
    int h = s->tag;   // regular member: still checked, must still match
    free(s);
    return (h == 7 && s->i == 42) ? 42 : 1;
}
