// a capturing block literal inside a MakeFunction()+FunctionSetBody(fn,
// Quote(...)) body built without WithFn(fn) round-trips VM 42 -> native 42 -- a
// -m shape assertion alone can't see the execution-level failure this ticket
// was about (every local in the generated body aliased frame offset 0, so the
// block's own descriptor got clobbered and CALLI jumped to a raw host address)
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
