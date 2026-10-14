// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: --link-arg=-framework --link-arg=CoreFoundation
// CCCC_EXPECT_STDOUT: --link-arg=-lm
//
// AddLdFlag and AddFramework on a CcccExecutable forward to the host link line
// as --link-arg, in declaration order.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_link_arg");
    AddSource(app, "examples/build_demo/src/main.c");
    AddFramework(app, "CoreFoundation");
    AddLdFlag(app, "-lm");
    return BuildDefault(ctx);
}
