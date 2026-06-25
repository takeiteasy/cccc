// CCCC_FLAGS: --testing
// CCCC_EXPECT_STDOUT: suite_test_fn
// CCCC_EXPECT_STDOUT: custom display name
// TestSetSuite, TestSetDisplayName, and TestSetTimeout configure the
// TestFnRecord returned by MarkAsTest.

[[cccc::comptime]]
void gen_suite_test(void) {
    Obj *fn = MakeFunction("suite_test_fn", GetType("void"));
    WithFn(fn) { FunctionSetBody(fn, Quote("return;")); }
    PublishNode(fn);
    TestFnRecord *rec = MarkAsTest(fn);
    TestSetSuite(rec, "generated_suite");
    TestSetDisplayName(rec, "custom display name");
    TestSetTimeout(rec, 5000);
}
gen_suite_test();
