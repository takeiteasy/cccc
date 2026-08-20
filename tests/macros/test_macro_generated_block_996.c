// Ticket #996: block literal inside a macro-generated function body.
// This is the ticket's own repro -- FunctionSetBody(fn, Quote(...)) with a
// non-capturing block literal, built without WithFn(fn). Previously crashed
// the VM with "invalid indirect call target: <huge magnitude>": the block
// descriptor and the local pointer 'b' both landed at frame offset 0 (see
// test_macro_generated_fn_locals_996.c for the root cause), so storing 'b'
// clobbered the descriptor's invoke-pointer slot with a raw host stack
// address, which CALLI then tried to jump to.

[[cccc::comptime]]
void gen(void) {
    Obj *fn = MakeFunction("use_block", GetType("int"));
    FunctionSetBody(fn,
                    Quote("{ int (^b)(void) = ^{ return 42; }; return b(); }"));
    PublishNode(fn);
}
gen();

int use_block(void);

int main(void) {
    return use_block();
}
