// CCCC_FLAGS: --build --build-out-dir=build/test_cccc_object_exe_out
// CCCC_EXPECT_STDOUT: --link-arg=.*cccc_object_exe_sum\.o
// CCCC_EXPECT_STDOUT: cccc_object_exe_ran: sum = 10
// CCCC_EXPECT_STDOUT: build succeeded
//
// A CcccObject linked into a CcccExecutable reaches the host link line as
// --link-arg.

#include <stdio.h>

[[cccc::build]]
int build_main(Builder *ctx) {
    BuildTarget *obj = CcccObject(ctx, "cccc_object_exe_sum");
    AddSource(obj, "examples/build_demo/src/lib/sum.c");
    AddInclude(obj, "examples/build_demo/include");

    BuildTarget *app = CcccExecutable(ctx, "cccc_object_exe_app");
    AddSource(app, "examples/build_demo/src/main.c");
    AddSource(app, "examples/build_demo/src/greet.c");
    AddInclude(app, "examples/build_demo/include");
    LinkWith(app, obj);

    char cmd[512];
    snprintf(cmd, sizeof(cmd),
             "%s | tail -n1 | sed 's/^/cccc_object_exe_ran: /'",
             TargetOutput(app));
    BuildTarget *run = RunCustom(ctx, "run-cccc-object-exe", cmd);
    DependsOn(run, app);

    return BuildDefault(ctx);
}
