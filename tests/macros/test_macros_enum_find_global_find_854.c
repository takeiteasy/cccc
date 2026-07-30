// Ticket #854: exercises EnumFind/EnumName/EnumValueCount/EnumValueName/
// EnumValue and FindGlobal -- all six are registered as FFI now, so their
// convenience macros are reachable from comptime code.

typedef enum { COLOR_RED, COLOR_GREEN = 5, COLOR_BLUE } Color;

int the_global = 123;

[[cccc::comptime]]
Node *check_enum(void) {
    Type *color_ty = GetType("Color");

    if (EnumValueCount(color_ty) != 3)
        return MakeIntLiteral(1);

    EnumConstant *green = EnumFind(color_ty, "COLOR_GREEN");
    if (!green || EnumConstantValue(green) != 5)
        return MakeIntLiteral(2);

    const char *name0 = EnumValueName(color_ty, 0);
    if (!name0 || name0[0] != 'C')
        return MakeIntLiteral(3);

    if (EnumValue(color_ty, 1) != 5)
        return MakeIntLiteral(4);

    const char *ename = EnumName(color_ty);
    if (!ename)
        return MakeIntLiteral(5);

    Obj *global = FindGlobal("the_global");
    if (!global)
        return MakeIntLiteral(6);

    return MakeIntLiteral(42);
}

int main(void) {
    return check_enum();
}
