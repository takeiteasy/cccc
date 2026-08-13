// Ticket #997: positive control. Same shape as
// test_macro_nested_setbody_locals_error.c (a comptime macro invoked from
// inside main() builds a *different* function's body), but wrapped in
// WithFn(fn) -- push_fn/pop_fn (src/reflection.c) flushes
// vm->compiler.locals into fn->locals itself before FunctionSetBody ever
// runs, so no locals are left stranded on main's list and the new #997
// diagnostic must not fire.

[[cccc::comptime]]
Node *gen(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("{ int a = 40; int b = 2; return a + b; }"));
    }
    PublishNode(fn);
    return Quote("0");
}

int helper(void);

int main(void) {
    int x = 1;
    int y = gen();
    return helper() + x + y - 1;
}
