// Ticket #995: VM-side control for a block literal that *captures* a
// generated-function local, inside a macro-generated function body built
// without WithFn(fn). #996 fixed the VM crash for this shape (the
// capturing variant of #996's own non-capturing repro,
// test_macro_generated_block_996.c); this file exists purely so the #995
// -c=generated fix below has a VM-side sibling proving 42 on this exact
// program before the -c=generated assertion runs the same source.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("use_block", GetType("int"));
    FunctionSetBody(
        fn,
        Quote("{ int n = 42; int (^b)(void) = ^{ return n; }; return b(); }"));
    PublishNode(fn);
}
gen();

int use_block(void);

int main(void) {
    return use_block();
}
