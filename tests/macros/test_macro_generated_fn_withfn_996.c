// Ticket #996: positive control. The WithFn(fn) form already worked before
// this fix (WithFn's push_fn/pop_fn flushes vm->compiler.locals into
// fn->locals itself) and must keep working identically afterward --
// __builtin_ast_function_set_body's new adoption is gated on
// vm->compiler.current_fn == NULL specifically so it never double-owns a
// local that WithFn already attached (which would otherwise mean two
// separate assign_stack_offsets passes touching the same Obj).

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("gen_add_withfn", GetType("int"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("{ int x = 40; int y = 2; return x + y; }"));
    }
    PublishNode(fn);
}
gen();

int gen_add_withfn(void);

int main(void) {
    return gen_add_withfn();
}
