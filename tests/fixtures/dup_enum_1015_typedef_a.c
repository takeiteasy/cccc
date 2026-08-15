// #1015: a tagless typedef'd enum (`typedef enum { ... } T1015A;`) never
// forms a group in rename_colliding_type_tags() (#1014, which only ever
// walks ctx->tags -- src/serialize.c) since it has no tag record at all;
// rename_colliding_enum_constants() has to collect groups from
// ctx->typedefs too, or this shape's colliding enumerator is never even
// detected. See tests/test_serialize_dup_enum_1015_typedef.c.
typedef enum { AA1015TD = 1, BB1015TD } T1015A;
int a_use_1015td(void) {
    T1015A t = AA1015TD;
    return t + BB1015TD;
}
