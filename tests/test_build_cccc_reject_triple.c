// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDERR: SetTargetTriple is not supported on a CcccExecutable target
//
// #1133: companion to test_build_cccc_reject_cflag — the same declaration-time
// rejection covers SetTargetTriple (and AddLdFlag / AddFramework / SetProfile),
// none of which `cccc --compile=native` can honour.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_reject_triple");
    AddSource(app, "examples/build_demo/src/main.c");
    SetTargetTriple(app, "aarch64-linux-gnu");
    return BuildDefault(ctx);
}
