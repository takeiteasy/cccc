// CCCC_FLAGS: --testing --uninitialized-detection
// CCCC_EXPECT_STDERR: UNINITIALIZED VARIABLE READ
// CCCC_EXPECT_STDOUT: not ok 1 -
// CCCC_C4_SKIP
//
// #1013: a runtime safety violation inside a [[cccc::test]] body silently
// aborted the test -- cc_run_at's return value was discarded entirely at
// the in-process call site (testing.c), so execution simply stopped
// mid-body (the AssertEq below never ran) but TAP still reported "ok".
// Fixed by a persistent vm->runtime_fault marker set at the one dispatch
// choke point (cccc_vm_eval_dispatch's per-op epilogue, vm.c) and checked
// immediately after the in-process cc_run_at call, before a return-value
// capture or teardown hooks can clobber it.
//
// The trapping uninitialized read happens before an AssertEq that would
// itself fail if reached -- both prove non-completion (the diagnostic
// banner appears, but so would the assertion failure if execution somehow
// continued past the trap).
[[cccc::test]]
void test_safety_violation_aborts(void) {
    int x;
    int y = x + 1;
    (void)y;
    AssertEq(1, 2);
}
