// CCCC_FLAGS: --build --build-out-dir=build/test_out
// CCCC_EXPECT_STDERR: target 'core' failed
//
// Regression test (#825): a target whose source file doesn't exist must
// fail the build cleanly (non-zero exit, diagnostic on stderr), not crash.
//
// Root cause was a double-free in build_target() (src/build.c): when
// compile_sources() failed, the rc != 0 branch freed eff_cc and jumped to
// the done: label, which unconditionally freed eff_cc again -- SIGABRT /
// "double free detected in tcache" on Linux, ASan double-free abort on
// macOS. Reproduced with -fsanitize=address,undefined before the fix.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *core = StaticLib(ctx, "core");
    AddSource(core, "tests/test_build_missing_source_no_crash_NONEXISTENT.c");
    return BuildDefault(ctx);
}
