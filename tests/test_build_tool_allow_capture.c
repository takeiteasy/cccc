// CCCC_FLAGS: --build --build-tool-allow=HaveTool
//
// #568: CaptureCommand is gated by --build-tool-allow.
// With an allowlist that does NOT include "CaptureCommand", it must return NULL.
// The entry never calls Build* so its return value is the exit code directly.

[[cccc::build]]
int build_main(Builder *ctx) {
    // "CaptureCommand" is NOT in the allowlist (only "HaveTool" is).
    const char *out = CaptureCommand(ctx, "echo should_not_run");
    return out == (void*)0 ? 42 : 1;
}
