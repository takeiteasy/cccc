// #1015: independently declares a differently-tagged enum (E1015DTB, not
// E1015DTA) that nonetheless reuses AA1015DT as an enumerator name --
// rename_colliding_type_tags() (#1014) has nothing to do here, since the
// tags E1015DTA/E1015DTB never collide; only the enumerator collides.
enum E1015DTB { AA1015DT = 5, CC1015DT };
int b_use_1015dt(void) {
    return AA1015DT + CC1015DT;
}
