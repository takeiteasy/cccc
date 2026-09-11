// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: build succeeded
//
// #1326: two same-direction redirects on one command (`cmd > o1 > o2`) must
// give the *last* one priority -- o2 gets the output, o1 is created but left
// empty -- matching real `sh`/`bash`. Previously command()'s outermost-wraps
// nesting made the first-written redirect (o1) win.

[[cccc::build]]
int build_main(Builder *ctx) {
    RunCustom(ctx, "seed", "printf 'payload\\n' > c1326_in.txt");
    RunCustom(ctx, "lastwins",
              "cat c1326_in.txt > c1326_o1.txt > c1326_o2.txt");
    RunCustom(ctx, "check_o2", "test \"$(cat c1326_o2.txt)\" = payload");
    RunCustom(ctx, "check_o1_empty", "test ! -s c1326_o1.txt");
    RunCustom(ctx, "cleanup", "rm -f c1326_in.txt c1326_o1.txt c1326_o2.txt");
    return BuildDefault(ctx);
}
