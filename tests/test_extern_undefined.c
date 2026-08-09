// EXPECT_COMPILE_ERROR
// CCCC_C4_SKIP: in -c mode (used by C4 round-trip), compile_only allows text
// relocations for undefined symbols (library semantics), so the error is
// intentionally deferred -- same reasoning as test_bytecode_link_unresolved.c
// (#565). This test covers exe-mode-only behaviour.
// CCCC_EXPECT_STDERR: undefined function: undefined_func
//
// Test for an undefined extern function - should fail at link time. The
// extern-global case has its own dedicated test,
// test_extern_global_undefined.c (#957) -- this file used to also declare
// an unused `extern int undefined_var;`, but nothing here ever asserted
// which symbol produced the error, so that half was silently vacuous (it
// passed purely because the function error fires first) and would have
// kept passing even if #957 were never fixed.
extern int undefined_func(int x);

int main() {
    // Try to use an undefined symbol
    int result = undefined_func(10);
    return result;
}
