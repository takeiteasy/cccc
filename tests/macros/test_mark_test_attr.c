// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: mark_test_generated
// MarkAsTest registers a programmatically generated function as a
// [[cccc::test]] entry.

[[cccc::comptime]]
void gen_marked_test(void) {
    Obj *fn = MakeFunction("mark_test_generated", GetType("void"));
    WithFn(fn) {
        FunctionSetBody(fn, Quote("return;"));
    }
    PublishNode(fn);
    MarkAsTest(fn);
}
gen_marked_test();
