// Ticket #235: OffsetofChain for (possibly nested) member chains.

#include <stddef.h>

struct Inner {
    int a;
    int b;
};

struct Outer {
    int          x;
    struct Inner inner;
};

[[cccc::comptime]]
void generate_outer_offsets(void) {
    Type *outer_ty = GetType("Outer");

    Obj  *x_fn     = MakeFunction("outer_x_offset", GetType("int"));
    WithFn(x_fn) {
        FunctionSetBody(x_fn, MakeReturn(OffsetofChain(outer_ty, "x")));
    }

    Obj *inner_b_fn = MakeFunction("outer_inner_b_offset", GetType("int"));
    WithFn(inner_b_fn) {
        FunctionSetBody(inner_b_fn,
                        MakeReturn(OffsetofChain(outer_ty, "inner", "b")));
    }
}

generate_outer_offsets();

int main(void) {
    if (outer_x_offset() != (int)offsetof(struct Outer, x))
        return 1;
    if (outer_inner_b_offset() != (int)offsetof(struct Outer, inner.b))
        return 2;
    return 42;
}
