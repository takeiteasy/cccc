// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: suite_test_fn
// CCCC_EXPECT_STDOUT: custom display name
// AddAttribute with test args expresses suite, display name, and timeout in the
// attribute string — equivalent to the old TestSetSuite/TestSetDisplayName/TestSetTimeout.

[[cccc::comptime]]
void gen_suite_test(void) {
    Obj *fn = MakeFunction("suite_test_fn", GetType("void"));
    WithFn(fn) { FunctionSetBody(fn, Quote("return;")); }
    PublishNode(fn);
    AddAttribute(fn, "cccc::test(suite=\"generated_suite\", name=\"custom display name\", timeout=5000)");
}
gen_suite_test();
