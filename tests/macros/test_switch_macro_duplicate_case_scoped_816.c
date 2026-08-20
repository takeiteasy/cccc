// #816: the scoped "WithSwitch(sw) { SwitchAddCase(...) }" form delegates to
// __builtin_ast_switch_add_current_case, which in turn calls
// __builtin_ast_switch_add_case -- confirm the duplicate-case check applies
// through that delegation too, not just the explicit form.
// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: duplicate case value '1'

[[cccc::comptime]]
Node *make_dup_case_scoped_switch(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("dup_case_scoped_switch", int_ty);
    FunctionAddParam(fn, "x", int_ty);

    WithFn(fn) {
        Node *sw = MakeSwitch(MakeParamRef(fn, "x"));
        WithSwitch(sw) {
            SwitchAddCase(MakeIntLiteral(1), MakeReturn(MakeIntLiteral(10)));
            SwitchAddCase(MakeIntLiteral(1), MakeReturn(MakeIntLiteral(20)));
        }
        FunctionSetBody(fn, sw);
    }

    return MakeIntLiteral(0);
}
make_dup_case_scoped_switch();

int main(void) {
    return 42;
}
