// Ticket #955: an unbraced multi-statement Quote() template used to silently
// drop every statement after the first (the trailing "return" here would
// have been discarded, leaving the generated function falling through with
// no return value). Verifies both statements survive and the generated
// function returns the intended value.

[[cccc::comptime]]
void gen_pick_or_default(void) {
    Type *int_ty = GetType("int");
    Obj *fn = MakeFunction("pick_or_default", int_ty);
    FunctionAddParam(fn, "a", int_ty);
    FunctionAddParam(fn, "b", int_ty);
    Node *a = MakeParamRef(fn, "a");
    Node *b = MakeParamRef(fn, "b");
    WithFn(fn) {
        FunctionSetBody(fn, Quote("if (!$1) $1 = $2; return $1;", a, b));
    }
}
gen_pick_or_default();

int main(void) {
    if (pick_or_default(5, 9) != 5)
        return 1;
    if (pick_or_default(0, 9) != 9)
        return 2;
    return 42;
}
