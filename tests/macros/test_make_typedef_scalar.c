// MakeTypedef() names an alias of a scalar without renaming the builtin type
// it aliases.
#include <string.h>

@comptime Node *check(void) {
    Type *alias = MakeTypedef("MyInt", GetType("int"));
    if (strcmp(TypeName(alias), "MyInt") != 0)
        return MakeIntLiteral(1);
    if (strcmp(TypeName(GetType("MyInt")), "MyInt") != 0)
        return MakeIntLiteral(2);
    if (strcmp(TypeName(GetType("int")), "int") != 0)
        return MakeIntLiteral(3);
    if (TypeSize(alias) != 4)
        return MakeIntLiteral(4);
    return MakeIntLiteral(0);
}

int main(void) {
    return 42 + check();
}
