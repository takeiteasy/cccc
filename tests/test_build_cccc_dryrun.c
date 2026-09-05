// CCCC_FLAGS: --build --build-dry-run
// CCCC_EXPECT_STDOUT: (?=[\s\S]*--compile=native)(?=[\s\S]*build succeeded \(1 target, 0 errors\))
// CCCC_REJECT_STDOUT: -MMD
//
// #1133: a CcccExecutable target lowers to a single `cccc --compile=native`
// invocation — the long form (not bare `-c`, which takes an optional
// argument and would eat the next source path). It is one whole-program
// step, so the step counter reads [1/1], and it never emits `-MMD`/`-MF`
// (not cccc flags).

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_dryrun_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddInclude(app, "examples/build_demo/include");
    AddDefine(app, "GREET_DEFAULT", "\"x\"");
    return BuildDefault(ctx);
}
