// Ticket #235: $foreach_member host-side iteration over struct members.

struct Point3D {
    int x;
    int y;
    int z;
};

[[cccc::comptime]]
void generate_point3d_offset_sum(void) {
    $type_t *ty = $get_type("Point3D");

    int total_offset = 0;
    int count = 0;
    $foreach_member(ty, m, {
        total_offset += $member_offset(m);
        count++;
    });

    $obj_t *sum_fn = $function("point3d_offset_sum", $get_type("int"));
    $with_fn(sum_fn) {
        $function_set_body(sum_fn, $return($int_literal(total_offset)));
    }

    $obj_t *count_fn = $function("point3d_member_count", $get_type("int"));
    $with_fn(count_fn) {
        $function_set_body(count_fn, $return($int_literal(count)));
    }
}

generate_point3d_offset_sum();

int main(void) {
    // offsets: x=0, y=4, z=8 -> sum = 12
    if (point3d_offset_sum() != 12)
        return 1;
    if (point3d_member_count() != 3)
        return 2;
    return 42;
}
