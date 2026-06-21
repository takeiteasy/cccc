// CCCC_FLAGS: --build
//
// #543: HaveTool(ctx, name) returns 1 when the named tool is found in PATH and
// not blocked by the tool allowlist.  Since we call no Build* function, the
// build entry's return value is used directly as the process exit code.
//
// The test verifies that `cc` (always available in the CI environment that can
// run the build tests at all) is found.

[[cccc::build]]
int build_main(Builder *ctx) {
    int found = HaveTool(ctx, "cc");
    // Return 42 (test passes) when cc is found; 1 otherwise.
    return found ? 42 : 1;
}
