// Ticket #235: $serialize/$deserialize round-trip for a flat struct with a
// nested struct field.

#include <string.h>

struct Inner {
    int a;
    int b;
};

struct Outer {
    int x;
    struct Inner inner;
    double y;
};

[[cccc::comptime]]
void generate_outer_serdes(void) {
    $type_t *ty = $get_type("Outer");

    $obj_t *pack = $function("outer_pack", $get_type("int"));
    $function_add_param(pack, "self", $make_pointer(ty));
    $function_add_param(pack, "buf", $make_pointer($get_type("void")));
    $with_fn(pack) {
        $node_t *self = $unary(nk_deref, $param_ref(pack, "self"));
        $node_t *buf = $param_ref(pack, "buf");
        $node_t *block = $serialize(ty, self, buf);
        $block_add_stmt(block, $return($int_literal($type_size(ty))));
        $function_set_body(pack, block);
    }

    $obj_t *unpack = $function("outer_unpack", ty);
    $function_add_param(unpack, "buf", $make_pointer($get_type("void")));
    $with_fn(unpack) {
        $function_set_body(unpack, $return($deserialize(ty, $param_ref(unpack, "buf"))));
    }
}

generate_outer_serdes();

int main(void) {
    struct Outer o = {1, {2, 3}, 4.5};
    char buf[sizeof(struct Outer)];

    // $serialize writes each member individually and never touches struct
    // padding, so the deserialized copy's padding bytes would otherwise be
    // indeterminate. Zero buf up front so the final memcmp (which spans
    // padding) compares like with like.
    memset(buf, 0, sizeof buf);

    int n = outer_pack(&o, buf);
    if (n != (int)sizeof(struct Outer))
        return 1;

    struct Outer o2 = outer_unpack(buf);
    if (memcmp(&o, &o2, sizeof(struct Outer)) != 0)
        return 2;

    return 42;
}
