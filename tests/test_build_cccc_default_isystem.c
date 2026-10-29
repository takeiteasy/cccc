// CCCC_FLAGS: --build --build-dry-run -i build/gen/default_sys
// CCCC_EXPECT_STDOUT: --isystem build/gen/default_sys
// CCCC_REJECT_STDOUT: [ ]-isystem build/gen/default_sys
//
// A CLI `-i DIR` default reaches a CcccExecutable as --isystem: the single-dash
// spelling would be parsed by cccc's getopt as `-i system`.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_default_isystem");
    AddSource(app, "examples/build_demo/src/main.c");
    return BuildDefault(ctx);
}
