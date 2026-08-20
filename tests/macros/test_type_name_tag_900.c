// Ticket #900: TypeName()/TypeCName()/EnumName() must report a tagged
// aggregate's real tag, not the last declarator name that happened to
// reuse the shared Type object.
//
// Root cause: struct_union_decl/enum_specifier (src/parse.c) set a fresh
// tag Type's ->name to the tag token, but a *reference* to an existing tag
// (e.g. a second function parameter of the same struct type) returns the
// canonical, shared Type found by find_tag -- and declarator() then
// overwrites that shared type's ->name with the declarator's own
// identifier (the parameter/variable name), not the tag. #892 already
// added Type.struct_tag/Type.enum_tag to survive this exact hazard for the
// serializer (see src/cccc.h); __builtin_ast_type_name/type_c_name
// (src/reflection.c) now prefer the same fields.
//
// Each aggregate below is referenced by a function parameter and a local
// variable *before* the comptime function below queries its name -- this
// is exactly the sequence that clobbers ty->name in the pre-fix code.

#include <string.h>

struct Point {
    int x;
    int y;
};
int use_point(struct Point q) {
    return q.x + q.y;
}

union U {
    int   a;
    float b;
};
int use_union(union U q) {
    return q.a;
}

enum Color { RED, GREEN, BLUE };
int use_color(enum Color c) {
    return (int)c;
}

[[cccc::comptime]]
Node *check_900(void) {
    if (strcmp(TypeName(GetType("Point")), "Point") != 0)
        return MakeIntLiteral(1);
    if (strcmp(TypeCName(GetType("Point")), "Point") != 0)
        return MakeIntLiteral(1);
    if (strcmp(TypeName(GetType("U")), "U") != 0)
        return MakeIntLiteral(1);
    if (strcmp(EnumName(GetType("Color")), "Color") != 0)
        return MakeIntLiteral(1);
    return MakeIntLiteral(0);
}

int main(void) {
    struct Point p = {1, 2};
    union U      u;
    u.a            = 3;
    enum Color c   = RED;
    int        sum = use_point(p) + use_union(u) + use_color(c);
    (void)sum;
    return 42 + check_900();
}
