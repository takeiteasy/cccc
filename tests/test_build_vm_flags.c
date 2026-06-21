// CCCC_FLAGS: --build -c=native
// CCCC_EXPECT_STDERR: cannot be combined
//
// --build cannot be combined with VM/output options like -c=native.

[[cccc::build]]
int build_main(Builder *ctx) {
    return BuildAll(ctx);
}
