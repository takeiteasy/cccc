// Ticket #235: ForeachMember host-side iteration over struct members.

struct Point3D {
    int x;
    int y;
    int z;
};

[[cccc::comptime]]
void generate_point3d_offset_sum(void) {
    Type *ty = GetType("Point3D");

    int total_offset = 0;
    int count = 0;
    ForeachMember(ty, m, {
        total_offset += MemberOffset(m);
        count++;
    });

    Obj *sum_fn = MakeFunction("point3d_offset_sum", GetType("int"));
    WithFn(sum_fn) {
        FunctionSetBody(sum_fn, MakeReturn(MakeIntLiteral(total_offset)));
    }

    Obj *count_fn = MakeFunction("point3d_member_count", GetType("int"));
    WithFn(count_fn) {
        FunctionSetBody(count_fn, MakeReturn(MakeIntLiteral(count)));
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
