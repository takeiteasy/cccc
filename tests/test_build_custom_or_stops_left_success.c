// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #842: `||`'s left side succeeding must short-circuit the right side. Using
// `false` on the right (rather than checking output text, which would
// spuriously match the verbatim command banner CCCC always prints) proves
// the point via exit code: if the right side ran unconditionally, the build
// would report failure instead of success here.

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *step = RunCustom(ctx, "ortest", "true || false");
    return BuildDefault(ctx);
}
