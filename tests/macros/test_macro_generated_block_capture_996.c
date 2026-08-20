// Ticket #996: a capturing block literal inside a macro-generated function
// body, built without WithFn(fn). Exercises the block_fn->parent_fn repair
// in __builtin_ast_function_set_body (src/reflection.c) -- without WithFn,
// block_literal() sees vm->compiler.current_fn == NULL at parse time and
// leaves the lifted block's parent_fn unset, which belongs_to_outer_function()
// (src/codegen.c) needs to resolve the captured 'n' through the static
// chain.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("use_capturing_block", GetType("int"));
    FunctionSetBody(
        fn,
        Quote("{ int n = 42; int (^b)(void) = ^{ return n; }; return b(); }"));
    PublishNode(fn);
}
gen();

int use_capturing_block(void);

int main(void) {
    return use_capturing_block();
}
