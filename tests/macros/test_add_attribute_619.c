// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: add_attr_test_a
// CCCC_EXPECT_STDOUT: add_attr_test_b
// AddAttribute is the generic programmatic attribute API (ticket #619).
// MarkAsTest/MarkAsBuild/MarkAsBuildTarget are convenience shorthands over it.

[[cccc::comptime]]
void gen_add_attr_fns(void) {
    // Direct AddAttribute call with a bare mode attribute
    Obj *a = MakeFunction("add_attr_test_a", GetType("void"));
    WithFn(a) {
        FunctionSetBody(a, Quote("return;"));
    }
    PublishNode(a);
    AddAttribute(a, "cccc::test");

    // AddAttribute with test options expressed inline in the attribute string
    Obj *b = MakeFunction("add_attr_test_b", GetType("void"));
    WithFn(b) {
        FunctionSetBody(b, Quote("return;"));
    }
    PublishNode(b);
    AddAttribute(b, "cccc::test(suite=\"gen_suite\", timeout=5000)");
}
gen_add_attr_fns();
