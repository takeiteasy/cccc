// #1015: second-listed TU independently declaring a same-named tag AND a
// same-named enumerator with a different value/shape than
// dup_enum_1015_a.c. same_type_or_origin() (src/serialize.c) correctly
// treats the enum itself as a distinct type (enumerator list differs), and
// #1014's rename_colliding_type_tags() already renames the tag apart
// (`enum E1015__cccc_dupN`) -- but AA1015 itself still collided with the
// enumerator declared by dup_enum_1015_a.c until #1015 fixed it.
enum E1015 { AA1015 = 5, CC1015 };
int b_use_1015(void) {
    return AA1015 + CC1015;
}
