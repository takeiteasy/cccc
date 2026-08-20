// EXPECT_COMPILE_ERROR
// CCCC_EXPECT_STDERR: error: FunctionSetBody: local 'a' was declared for
// function 'helper' while building it from inside function 'main'; wrap the
// call in WithFn\(helper\)
//
// Ticket #997: follow-up from #996. #996's fix adopts locals declared while
// building a function's body onto that function, but only when
// vm->compiler.current_fn == NULL -- the guarantee that no other function
// already owns vm->compiler.locals. Here gen() is itself a comptime macro
// invoked from *inside* main(), so current_fn is main (not NULL) while gen()
// calls MakeFunction()+FunctionSetBody(fn, Quote(...)) on a completely
// different function (helper) without WithFn(helper). Before this fix, the
// Quote()-declared locals 'a'/'b' stayed misattached to main's own locals
// list, silently producing broken -m/-c=native output (both declared inside
// main(), referenced from helper()) and, one frame-layout change away, the
// same offset-aliasing hazard #996 fixed for the current_fn == NULL case.
// Verified this exact program previously exited 42 (by luck, in the VM,
// which doesn't care which function "owns" a frame slot the way the
// serializer does) -- so an exit-code-only assertion would be vacuous; this
// must be a compile-error test that checks the diagnostic fires instead.

[[cccc::comptime]]
Node *gen(void) {
    Obj *fn = MakeFunction("helper", GetType("int"));
    FunctionSetBody(fn, Quote("{ int a = 40; int b = 2; return a + b; }"));
    PublishNode(fn);
    return Quote("0");
}

int helper(void);

int main(void) {
    int x = 1;
    int y = gen();
    return helper() + x + y - 1;
}
