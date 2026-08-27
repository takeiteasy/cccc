// EXPECT_COMPILE_ERROR
// A plain (non -c) run hard-errors on an undefined function immediately;
// only -c/--compile defers it (compile_only lets the host C linker resolve
// or reject it instead). This test covers the immediate-error, non -c case.
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
