// EXPECT_COMPILE_ERROR
// CCCC_C4_SKIP: in -c mode (used by C4 round-trip), compile_only allows text
// relocations for undefined symbols (library semantics), so the error is
// intentionally deferred -- same reasoning as test_bytecode_link_unresolved.c
// (#565). This test covers exe-mode-only behaviour.
//
// Test for undefined extern symbols - should fail at link time
extern int undefined_var;
extern int undefined_func(int x);

int main() {
    // Try to use undefined symbols
    int result = undefined_var + undefined_func(10);
    return result;
}
