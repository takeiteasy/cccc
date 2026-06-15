// Ticket #235: $offsetof_chain for (possibly nested) member chains.

#include <stddef.h>

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    struct Inner inner;
};

[[cccc::comptime]]
void generate_outer_offsets(void) {
    $type_t *outer_ty = $get_type("Outer");

    $obj_t *x_fn = $function("outer_x_offset", $get_type("int"));
    $with_fn(x_fn) {
        $function_set_body(x_fn, $return($offsetof_chain(outer_ty, "x")));
    }

    $obj_t *inner_b_fn = $function("outer_inner_b_offset", $get_type("int"));
    $with_fn(inner_b_fn) {
        $function_set_body(inner_b_fn,
            $return($offsetof_chain(outer_ty, "inner", "b")));
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
