// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: custom step 'bgstatus' failed
//
// Only the left side of `&` is a background job: the command after it runs in
// the foreground and its exit status is the step's.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "bgstatus", "sleep 0 & false");
    return BuildDefault(ctx);
}
