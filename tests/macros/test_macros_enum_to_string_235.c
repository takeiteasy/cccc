// Ticket #235: EnumToString/EnumFromString thin AST wrappers.
//
// The function signatures use `int` instead of `enum Color` for the
// enum-valued parameter/return to avoid a pre-existing forward-declaration
// gap (`<inline-macro-fwd>` synthesizes "enum Color foo(...)" forward decls
// for [[cccc::comptime]]-generated functions, which C23 rejects without an
// underlying type). EnumToString/EnumFromString only need the
// expression's value, not its declared type, so this is harmless.

#include <string.h>

enum Color {
    RED,
    GREEN,
    BLUE,
};

[[cccc::comptime]]
void generate_color_funcs(void) {
    Type *ty     = GetType("Color");

    Obj  *to_str = MakeFunction("color_to_string",
                                MakePointer(MakeConst(GetType("char"))));
    FunctionAddParam(to_str, "v", GetType("int"));
    WithFn(to_str) {
        Node *v = MakeParamRef(to_str, "v");
        FunctionSetBody(to_str, EnumToString(ty, v));
    }

    Obj *from_str = MakeFunction("color_from_string", GetType("int"));
    FunctionAddParam(from_str, "s", MakePointer(MakeConst(GetType("char"))));
    WithFn(from_str) {
        Node *s = MakeParamRef(from_str, "s");
        FunctionSetBody(from_str, EnumFromString(ty, s));
    }
}

generate_color_funcs();

int main(void) {
    if (strcmp(color_to_string(RED), "RED") != 0)
        return 1;
    if (strcmp(color_to_string(GREEN), "GREEN") != 0)
        return 2;
    if (strcmp(color_to_string(BLUE), "BLUE") != 0)
        return 3;
    if (strcmp(color_to_string(99), "") != 0)
        return 4;

    if (color_from_string("RED") != RED)
        return 5;
    if (color_from_string("GREEN") != GREEN)
        return 6;
    if (color_from_string("BLUE") != BLUE)
        return 7;
    if (color_from_string("nope") != -1)
        return 8;

    return 42;
}
