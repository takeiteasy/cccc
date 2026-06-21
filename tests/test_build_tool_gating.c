// CCCC_FLAGS: --build --build-tool-allow=make
//
// #543: --build-tool-allow restricts which tools may be probed via HaveTool.
// With only "make" in the allowlist, probing for "cc" must return 0 (blocked).
//
// The entry never calls Build*, so its return value is used directly as the
// exit code.  Returning 42 when cc is correctly blocked → test passes.

[[cccc::build]]
int build_main(Builder *ctx) {
    // "cc" is NOT in the allowlist (only "make" is) → must return 0
    int found = HaveTool(ctx, "cc");
    return found == 0 ? 42 : 1;
}
