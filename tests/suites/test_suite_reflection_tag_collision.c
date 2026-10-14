// CCCC_FLAGS: --testing
// User tags named like reflection.h's opaque handles (Node, Type, Obj, Token,
// ...) coexist with the implicit comptime pass.

#pragma cccc suite begin "reflection_tag_collision"

union Node {
    int  x;
    char pad[16];
};
enum Type { TYPE_A, TYPE_B };
struct Obj {
    int id;
};
struct Token {
    int kind;
    int value;
};

__attribute__((visibility("hidden"))) extern int hidden_decl(void);

[[cccc::comptime]]
int comptime_marker(void) {
    return 1;
}

[[cccc::test]]
void test_union_node_survives(void) {
    union Node n = {.x = 7};
    AssertEq(n.x, 7);
    AssertEq((int)sizeof(union Node), 16);
}

[[cccc::test]]
void test_enum_type_survives(void) {
    enum Type t = TYPE_B;
    AssertEq((int)t, 1);
}

[[cccc::test]]
void test_struct_obj_and_token_survive(void) {
    struct Obj   o = {.id = 3};
    struct Token k = {.kind = 1, .value = 41};
    AssertEq(o.id, 3);
    AssertEq(k.value + k.kind, 42);
}

#pragma cccc suite end
