// #1300 (item 3): found via #1132's round-13 self-hosting spike. A struct
// field whose declared type is a pointer to an *anonymous* struct/union
// (src/cccc.h's `vm->compiler.call_patches` is the real shape) is assigned
// realloc()'s `void *` result. The type checker inserts an implicit
// ND_CAST(anon-struct *, void *); serialize_expr.c's ND_CAST case spelled
// it by calling serialize_type(), which re-synthesizes a fresh
// `struct {...} *` body. C's nominal typing makes that re-synthesized
// anonymous struct a DISTINCT type from the field's own (identically
// shaped) one, so every assignment site drew -Wincompatible-pointer-types
// -- a warning on Apple clang, a hard error on newer GCC or under
// -Werror.
//
// Since every such cast is an implicit conversion from `void *` (which
// converts to any object pointer with no cast at all), the ND_CAST case
// now emits the operand bare when the destination is a pointer to an
// anonymous aggregate. tools/comptime_native_smoke.py's
// case_anon_struct_ptr_cast_1300 is the load-bearing check: it asserts the
// -m output carries no synthesized `(struct {` cast and compiles clean
// under -Werror=incompatible-pointer-types.

#include <stdlib.h>

struct Holder {
    struct {
        long location;
        int  function;
    }  *patches;
    int count;
    int cap;
};

static struct Holder h;

static int grow_and_fill(void) {
    h.cap = 8;
    // realloc returns void *; the assignment to an anon-struct-pointer
    // field is where the spurious cast used to be synthesized.
    h.patches = realloc(h.patches, (size_t)h.cap * sizeof(*h.patches));
    if (!h.patches)
        return -1;
    h.patches[0].location = 40;
    h.patches[0].function = 2;
    h.count               = 1;
    return (int)h.patches[0].location + h.patches[0].function;
}

int main(void) {
    return grow_and_fill();
}
