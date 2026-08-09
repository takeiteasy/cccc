// Ticket #955: an unbraced multi-statement template that starts with a
// declaration ("int t = $1; ...") must still keep every statement -- stmt()
// alone does not parse declarations, only compound_stmt() does, so this
// exercises the fact that the fix routes unbraced multi-statement templates
// through the same braced/compound_stmt path as an explicit "{ ... }".

[[cccc::comptime]]
void gen_next(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("next_after", int_ty);
    FunctionAddParam(fn, "n", int_ty);
    Node *n = MakeParamRef(fn, "n");
    WithFn(fn) {
        FunctionSetBody(fn, Quote("int t = $1; return t + 1;", n));
    }
}
gen_next();

int main(void) {
    if (next_after(41) != 42)
        return 1;
    return 42;
}
