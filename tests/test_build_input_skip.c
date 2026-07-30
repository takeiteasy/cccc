// CCCC_FLAGS: --build
// CCCC_EXPECT_STDOUT: \(custom\) cp build/test_input_skip_src/in\.txt
// CCCC_EXPECT_STDOUT: \(up to date\) gen-input-skip
//
// #851: AddInput + DeclareOutput give build_target() a real "up to date"
// skip check for RunCustom targets, instead of always re-running the
// command. The first Build() call must actually run it (the declared output
// does not exist yet); an immediately following second Build() call on the
// same target must be skipped.

[[cccc::build]]
int build_main(Builder *ctx) {
    WriteFile(ctx, "build/test_input_skip_src/in.txt", "hello\n");

    BuildTarget *gen = RunCustom(ctx, "gen-input-skip",
        "cp build/test_input_skip_src/in.txt build/test_input_skip_src/out.txt");
    AddInput(gen, "build/test_input_skip_src/in.txt");
    DeclareOutput(gen, "build/test_input_skip_src/out.txt");

    if (Build(ctx, gen) != 0) return 1;
    if (Build(ctx, gen) != 0) return 1;

    return 0;
}
