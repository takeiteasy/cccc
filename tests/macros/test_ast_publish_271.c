// Test ticket #271: unified $publish API.

[[cccc::comptime]]
void publish_proto(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *proto = $function_prototype("published_add_one", int_ty);
    $function_add_param(proto, "x", int_ty);
    $publish(proto);
}
publish_proto();

[[cccc::comptime]]
void define_published_fn(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("published_add_one", int_ty);
    $function_set_body(fn,
        $return($binary(nk_add, $param_ref(fn, "x"),
                                $int_literal(1))));
}
define_published_fn();

[[cccc::comptime]]
void publish_global_and_types(void) {
    $type_t *char_ty = $get_type("char");
    $type_t *arr_ty = $make_array(char_ty, 4);
    $obj_t *g = $global_var("published_bytes", arr_ty);
    $global_var_set_init_data(g, "CCCC\0", 4);
    $publish_at(g, $synthetic_token("published global"));

    $type_t *int_ty = $get_type("int");
    $type_t *point = $make_struct("PublishedPoint");
    $struct_add_field(point, "x", int_ty);
    $struct_add_field(point, "y", int_ty);
    $publish(point);

    $type_t *tagged = $make_enum("PublishedTag");
    $enum_add_constant(tagged, "PUBLISHED_TAG_OK", 7);
    $publish(tagged);

    $publish($make_typedef("PublishedLong", $get_type("long")));
}
publish_global_and_types();

[[cccc::comptime]]
void forward_alias_still_works(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *proto = $function_prototype("published_alias_fn", int_ty);
    $publish(proto);
}
forward_alias_still_works();

[[cccc::comptime]]
void define_alias_fn(void) {
    $type_t *int_ty = $get_type("int");
    $obj_t *fn = $function("published_alias_fn", int_ty);
    $function_set_body(fn, $return($int_literal(5)));
}
define_alias_fn();

int main(void) {
    if (published_add_one(41) != 42) return 1;
    if (published_bytes[0] != 'C') return 2;
    if (published_bytes[1] != 'C') return 3;
    if (published_bytes[2] != 'C') return 4;

    struct PublishedPoint p;
    p.x = 20;
    p.y = 22;
    if (p.x + p.y != 42) return 5;

    PublishedLong n = 42;
    if (n != 42) return 6;
    if (PUBLISHED_TAG_OK != 7) return 7;
    if (published_alias_fn() != 5) return 8;
    return 42;
}
