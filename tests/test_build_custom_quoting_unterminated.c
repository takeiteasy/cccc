// CCCC_FLAGS: --build
// CCCC_EXPECT_STDERR: custom step 'badquote' failed
//
// An unterminated quote in a RunCustom command string must still fail the
// step (companion to test_build_custom_quoting.c, which exercises the
// successful decoding paths).

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "badquote", "echo 'unterminated");
    return BuildDefault(ctx);
}
