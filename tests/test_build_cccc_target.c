// CCCC_FLAGS: --build --build-out-dir=build/test_cccc_target_out
// CCCC_EXPECT_STDOUT: (?=[\s\S]*--compile=native)(?=[\s\S]*cccc_target_ran: sum = 10)(?=[\s\S]*build succeeded)
//
// #1133: a CcccExecutable target is compiled by the running cccc itself with
// one whole-program `cccc --compile=native` invocation (no host cc -c, no
// per-source .o). Builds a real binary from the build_demo sources and runs
// it via a RunCustom step to prove the output executable actually works.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *app = CcccExecutable(ctx, "cccc_target_app");
    AddSource(app, "examples/build_demo/src/lib/sum.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddSource(app, "examples/build_demo/src/main.c");
    AddInclude(app, "examples/build_demo/include");

    char cmd[512];
    snprintf(cmd, sizeof(cmd), "%s | tail -n1 | sed 's/^/cccc_target_ran: /'",
             TargetOutput(app));
    BuildTarget *run = RunCustom(ctx, "run-cccc-target", cmd);
    DependsOn(run, app);

    return BuildDefault(ctx);
}
