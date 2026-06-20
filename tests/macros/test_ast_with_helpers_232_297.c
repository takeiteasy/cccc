// Tickets #232 and #297: scoped AST builder helpers and generated switch cases.

[[cccc::comptime]]
Node *make_scoped_switch(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("scoped_switch", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        WithSwitch(sw) {
            SwitchAddCase(MakeIntLiteral(0), MakeReturn(MakeIntLiteral(10)));
            SwitchAddCase(MakeIntLiteral(1), MakeReturn(MakeIntLiteral(20)));
            SwitchSetDefault(MakeReturn(MakeIntLiteral(-1)));
        }
        FunctionSetBody(fn, sw);
    }

    return MakeIntLiteral(0);
}
make_scoped_switch();

[[cccc::comptime]]
Node *make_explicit_switch(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("explicit_switch", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        SwitchAddCase(sw, MakeIntLiteral(0), MakeReturn(MakeIntLiteral(30)));
        SwitchSetDefault(sw, MakeReturn(MakeIntLiteral(-3)));
        FunctionSetBody(fn, sw);
    }

    return MakeIntLiteral(0);
}
make_explicit_switch();

[[cccc::comptime]]
Node *make_scoped_struct(void) {
    Type *int_ty = GetType("int");
    Type *point = MakeStruct("ScopedPoint");
    WithStruct(point) {
        StructAddField("x", int_ty);
        StructAddField("y", int_ty);
    }

    Obj *fn = MakeFunction("scoped_point_sum", int_ty);
    FunctionAddParam(fn, "p", MakePointer(point));
    Node *p = MakeParamRef(fn, "p");
    Node *x = MakeMember(MakeUnary(NK_DEREF, p), "x");
    Node *y = MakeMember(MakeUnary(NK_DEREF, MakeParamRef(fn, "p")), "y");
    FunctionSetBody(fn, MakeReturn(MakeBinary(NK_ADD, x, y)));

    return MakeIntLiteral(0);
}
make_scoped_struct();

[[cccc::comptime]]
Node *make_scoped_enum(void) {
    Type *tag = MakeEnum("ScopedTag");
    WithEnum(tag) {
        EnumAddConstant("SCOPED_A", 4);
        EnumAddConstant("SCOPED_B", 8);
    }
    return MakeIntLiteral(0);
}
make_scoped_enum();

[[cccc::comptime]]
Node *make_scoped_block(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("scoped_block_value", int_ty);
    Node *block = MakeBlock((Node*[]){0}, 0);

    WithBlock(block) {
        BlockAddStmt(MakeReturn(MakeIntLiteral(6)));
    }
    FunctionSetBody(fn, block);

    return MakeIntLiteral(0);
}
make_scoped_block();

int main(void) {
    if (scoped_switch(0) != 10) return 1;
    if (scoped_switch(1) != 20) return 2;
    if (scoped_switch(7) != -1) return 3;

    if (explicit_switch(0) != 30) return 4;
    if (explicit_switch(7) != -3) return 5;

    int point[2] = {3, 5};
    if (scoped_point_sum((void *)point) != 8) return 6;

    if (SCOPED_A != 4) return 7;
    if (SCOPED_B != 8) return 8;

    if (scoped_block_value() != 6) return 9;

    return 42;
}
