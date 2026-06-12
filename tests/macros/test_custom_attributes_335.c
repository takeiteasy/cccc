// Ticket #335: custom file-scope attributes backed by comptime macros.

@macro(attribute("serialize"))
void define_serializer($attr_target_t *target) {
    if ($attr_target_kind(target) != attr_target_type)
        $macro_error_at(0, "serialize expected a type target");
    if (!$attr_target_name(target))
        $macro_error_at(0, "serialize target has no name");

    $type_t *ty = $attr_target_type(target);
    $obj_t *fn = $function("serialize_Point", $get_type("int"));
    $function_add_param(fn, "p", $make_pointer(ty));
    $with_fn(fn) {
        $function_set_body(fn, $quote("return sizeof(struct Point);"));
    }
    $publish(fn);
}

@serialize
struct Point {
    int x;
    int y;
};

@macro(attribute("answer"))
void define_answer($attr_target_t *target, $node_t *value) {
    if ($attr_target_kind(target) != attr_target_global)
        $macro_error_at(0, "answer expected a global target");

    $obj_t *fn = $function("custom_attr_answer", $get_type("int"));
    $with_fn(fn) {
        $function_set_body(fn, $return(value));
    }
    $publish(fn);
}

@answer(123)
int configured_value;

@macro(attribute("typedef_size"))
void define_typedef_size($attr_target_t *target) {
    if ($attr_target_kind(target) != attr_target_typedef)
        $macro_error_at(0, "typedef_size expected a typedef target");

    $obj_t *fn = $function("custom_attr_typedef_size", $get_type("int"));
    $with_fn(fn) {
        $function_set_body(fn, $quote("return sizeof(AliasPoint);"));
    }
    $publish(fn);
}

@typedef_size
typedef struct Point AliasPoint;

int main(void) {
    struct Point p = {1, 2};
    if (serialize_Point(&p) != (int)sizeof(struct Point))
        return 1;
    if (custom_attr_answer() != 123)
        return 2;
    if (custom_attr_typedef_size() != (int)sizeof(AliasPoint))
        return 3;
    return 42;
}
