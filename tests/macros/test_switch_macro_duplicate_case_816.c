// #816: __builtin_ast_switch_add_case (the explicit "SwitchAddCase(sw, ...)"
// form) bypasses the parser entirely, so it must reject a duplicate case
// value itself instead of silently overwriting the earlier one -- same bug
// class as #815, just reachable from comptime macros.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate case value '1'

[[cccc::comptime]]
Node *make_dup_case_switch(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("dup_case_switch", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        SwitchAddCase(sw, MakeIntLiteral(1), MakeReturn(MakeIntLiteral(10)));
        SwitchAddCase(sw, MakeIntLiteral(1), MakeReturn(MakeIntLiteral(20)));
        FunctionSetBody(fn, sw);
    }

    return MakeIntLiteral(0);
}
make_dup_case_switch();

int main(void) {
    return 42;
}
