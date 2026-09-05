// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDERR: AddCFlag is not supported on a CcccExecutable target
//
// #1133: `cccc --compile=native` takes only -I/-D/-U/-L/-l/--std= and has no
// host-cc flag pass-through, so silently dropping AddCFlag("-Wall") would be
// a quiet correctness change. It must hard-error naming the call.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_reject_cflag");
    AddSource(app, "examples/build_demo/src/main.c");
    AddCFlag(app, "-Wall");
    return BuildDefault(ctx);
}
