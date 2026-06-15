// Ticket #235: $enum_to_string/$enum_from_string thin AST wrappers.
//
// The function signatures use `int` instead of `enum Color` for the
// enum-valued parameter/return to avoid a pre-existing forward-declaration
// gap (`<inline-macro-fwd>` synthesizes "enum Color foo(...)" forward decls
// for [[cccc::comptime]]-generated functions, which C23 rejects without an
// underlying type). $enum_to_string/$enum_from_string only need the
// expression's value, not its declared type, so this is harmless.

#include <string.h>

enum Color {
    RED,
    GREEN,
    BLUE,
};

[[cccc::comptime]]
void generate_color_funcs(void) {
    $type_t *ty = $get_type("Color");

    $obj_t *to_str = $function("color_to_string", $make_pointer($make_const($get_type("char"))));
    $function_add_param(to_str, "v", $get_type("int"));
    $with_fn(to_str) {
        $node_t *v = $param_ref(to_str, "v");
        $function_set_body(to_str, $enum_to_string(ty, v));
    }

    $obj_t *from_str = $function("color_from_string", $get_type("int"));
    $function_add_param(from_str, "s", $make_pointer($make_const($get_type("char"))));
    $with_fn(from_str) {
        $node_t *s = $param_ref(from_str, "s");
        $function_set_body(from_str, $enum_from_string(ty, s));
    }
}

generate_color_funcs();

int main(void) {
    if (strcmp(color_to_string(RED), "RED") != 0) return 1;
    if (strcmp(color_to_string(GREEN), "GREEN") != 0) return 2;
    if (strcmp(color_to_string(BLUE), "BLUE") != 0) return 3;
    if (strcmp(color_to_string(99), "") != 0) return 4;

    if (color_from_string("RED") != RED) return 5;
    if (color_from_string("GREEN") != GREEN) return 6;
    if (color_from_string("BLUE") != BLUE) return 7;
    if (color_from_string("nope") != -1) return 8;

    return 42;
}
