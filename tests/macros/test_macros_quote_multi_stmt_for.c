// Ticket #955: an unbraced multi-statement template containing a `for (a; b;
// c)` header must only treat the top-level ';' as a statement separator --
// the ones inside the for-header's parens must not trip multi-statement
// detection on their own, and the real top-level ';'s must still be kept.

[[cccc::comptime]]
void gen_sum_upto(void) {
    Type *int_ty = GetType("int");
    Obj  *fn     = MakeFunction("sum_upto", int_ty);
    FunctionAddParam(fn, "n", int_ty);
    Node *n = MakeParamRef(fn, "n");
    WithFn(fn) {
        FunctionSetBody(
            fn,
            Quote("int s = 0; for (int i = 0; i < $1; i++) s += i; return s;",
                  n));
    }
}
gen_sum_upto();

int main(void) {
    // 0 + 1 + 2 + 3 + 4 = 10
    if (sum_upto(5) != 10)
        return 1;
    if (sum_upto(0) != 0)
        return 2;
    return 42;
}
